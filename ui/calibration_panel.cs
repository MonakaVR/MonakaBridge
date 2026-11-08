using System;
using System.Collections;
using System.Collections.Generic;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Threading;

namespace MonakaBridge {
 // Native owns capture, solving and the atomic World transaction.
 internal sealed class CalibrationPanel:StackPanel,IDisposable {
  sealed class TrackerChoice {
   internal Dictionary<string,object> Mapping;internal string Source,Device;
   public override string ToString(){return Convert.ToString(Mapping["tracker_id"])+" | "+Source+" / "+Device;}
  }
  GuiLaunchOptions target;Dictionary<string,object> configuration;
  CalibrationWorkflow workflow=new CalibrationWorkflow();readonly object dispatchGate=new object();
  readonly Func<bool> hostAvailable;readonly Action<bool,bool> ownership;
  readonly ComboBox reference=new ComboBox{ItemsSource=new[]{BridgeText.T("Left controller"),BridgeText.T("Right controller"),BridgeText.T("HMD")},SelectedIndex=0};
  readonly ComboBox tracker=new ComboBox();
  readonly ComboBox method=new ComboBox{ItemsSource=new[]{BridgeText.T("Figure-eight")},SelectedIndex=0};
  readonly Button start,cancel;
  readonly TextBlock instructions=new TextBlock{TextWrapping=TextWrapping.Wrap};
  readonly TextBlock state=new TextBlock{TextWrapping=TextWrapping.Wrap,Margin=new Thickness(0,6,0,0)};
  readonly ProgressBar progress=new ProgressBar{Height=8,Margin=new Thickness(0,8,0,3)};
  readonly DispatcherTimer timer=new DispatcherTimer();
  bool pending,stopping,disposed,reportedActive;volatile bool workflowStarted;int generation;string detail;Task running;
  internal CalibrationPanel(GuiLaunchOptions selected,Func<bool> available,Action<bool,bool> owned){
   target=selected;hostAvailable=available;ownership=owned;
   Children.Add(new TextBlock{Text=BridgeText.T("Calibration"),FontSize=22,Margin=new Thickness(0,8,0,4)});
   Children.Add(instructions);
   Field("Reference",reference);Field("Tracker",tracker);Field("Method",method);
   var actions=new WrapPanel();Children.Add(actions);
   start=new Button{Content=BridgeText.T("Start"),MinWidth=100,MinHeight=32,Margin=new Thickness(0,8,6,3)};
   cancel=new Button{Content=BridgeText.T("Cancel"),MinWidth=100,MinHeight=32,Margin=new Thickness(0,8,0,3)};
   actions.Children.Add(start);actions.Children.Add(cancel);Children.Add(progress);Children.Add(state);
   AutomationProperties.SetName(reference,BridgeText.T("Calibration reference"));AutomationProperties.SetName(tracker,BridgeText.T("Calibration tracker"));AutomationProperties.SetName(method,BridgeText.T("Calibration method"));
   AutomationProperties.SetName(start,BridgeText.T("Start calibration"));AutomationProperties.SetName(cancel,BridgeText.T("Cancel calibration"));AutomationProperties.SetName(state,BridgeText.T("Calibration progress and result"));AutomationProperties.SetName(progress,BridgeText.T("Calibration progress"));
   start.Click+=async delegate{if(Active||disposed)return;running=StartAsync();await running;};
   cancel.Click+=async delegate{try{await StopAsync();}catch(Exception e){detail=BridgeText.Diagnostic(e.GetBaseException().Message);Refresh();}};
   tracker.SelectionChanged+=delegate{Refresh();};reference.SelectionChanged+=delegate{Refresh();};
   timer.Interval=TimeSpan.FromMilliseconds(200);timer.Tick+=delegate{Refresh();};timer.Start();Refresh();
  }
  void Field(string title,Control control){Children.Add(new Label{Content=BridgeText.T(title),Target=control,Padding=new Thickness(0,5,0,2)});Children.Add(control);}
  internal bool Active {get{return pending||workflow.Active;}}
  internal CalibrationClientSnapshot Snapshot(){return workflowStarted?workflow.Snapshot():new CalibrationClientSnapshot{Phase="NEW",Stopped=!pending,Fault=detail};}
  internal void SetConfiguration(Dictionary<string,object> config){
   if(Active)throw new InvalidOperationException("Finish calibration before loading configuration");
   var previous=tracker.SelectedItem as TrackerChoice;configuration=config;tracker.Items.Clear();
   if(config!=null){object rows;if(config.TryGetValue("mappings",out rows)&&rows is IEnumerable)foreach(Dictionary<string,object> mapping in (IEnumerable)rows){
    var choice=new TrackerChoice{Mapping=mapping,Source=Convert.ToString(mapping["source_id"]),Device=Convert.ToString(mapping["device_id"])};tracker.Items.Add(choice);
    if(previous!=null&&choice.Source==previous.Source&&choice.Device==previous.Device)tracker.SelectedItem=choice;
   }}
   if(tracker.SelectedIndex<0&&tracker.Items.Count>0)tracker.SelectedIndex=0;Refresh();
  }
  internal void SetTarget(GuiLaunchOptions selected){
   if(Active)throw new InvalidOperationException("Owned calibration shutdown must finish before changing target");
   target=selected;configuration=null;tracker.Items.Clear();workflow.Dispose();workflow=new CalibrationWorkflow();generation++;detail=null;stopping=false;workflowStarted=false;Refresh();
  }
  async Task StartAsync(){
   if(!hostAvailable()||configuration==null||tracker.SelectedItem==null)return;
   var selected=target;var config=configuration;var mapping=((TrackerChoice)tracker.SelectedItem).Mapping;
   string role=reference.SelectedIndex==0?"left":reference.SelectedIndex==1?"right":"hmd";int token=generation;
   pending=true;stopping=false;detail=null;workflowStarted=false;Refresh();
   try{
    await Task.Run(async delegate{
     var installation=CalibrationInstallation.ForSelection(selected,config,mapping,role);
     Task operation;lock(dispatchGate){if(disposed||token!=generation||stopping)return;operation=workflow.StartAsync(installation,selected.ReadOnly);workflowStarted=true;}
     await operation.ConfigureAwait(false);
    });
   }catch(Exception e){if(!disposed&&token==generation)detail=BridgeText.Diagnostic(e.GetBaseException().Message);}
   finally{if(!disposed&&token==generation){pending=false;Refresh();}}
  }
  internal async Task StopAsync(){
   Task stop;lock(dispatchGate){stopping=true;stop=workflow.StopAsync();}await stop;var work=running;if(work!=null)await work;
   if(Active)throw new InvalidOperationException("Owned calibration shutdown is unconfirmed");if(!disposed)Refresh();
  }
  void Refresh(){
   if(disposed)return;var s=Snapshot();bool active=Active;
   reference.IsEnabled=tracker.IsEnabled=method.IsEnabled=!active;
   start.IsEnabled=!active&&configuration!=null&&tracker.SelectedItem!=null&&hostAvailable();cancel.IsEnabled=active&&!stopping;
   progress.IsIndeterminate=active;progress.Value=!active&&s!=null&&s.ActualOutcome!=null&&s.ActualOutcome.StartsWith("SAVED",StringComparison.Ordinal)?100:0;
   instructions.Text=BridgeText.T(target.ReadOnly?"Select a reference and tracker, then Start. Hold them rigidly together and move in a figure-eight. Read-only measurement will not save configuration.":"Select a reference and tracker, then Start. Hold them rigidly together and move in a figure-eight until calibration finishes and saves automatically.");
   string message;
   if(active)message=stopping?"Cancelling calibration…":s!=null&&(s.Phase=="RECORDING"||s.Phase=="SOLVING")?"Move the reference and tracker rigidly together in a figure-eight…":"Starting calibration…";
   else if(s!=null&&s.ActualOutcome=="UNKNOWN")message="Save outcome unknown. Reload configuration before starting again.";
   else if(s!=null&&s.ActualOutcome=="SAVED_CONFIG_EVIDENCE_STALE")message="Done — calibration saved. Configuration evidence is stale; runtime application is unconfirmed.";
   else if(s!=null&&s.ActualOutcome=="SAVED")message="Done — calibration saved. Runtime application is unconfirmed.";
   else if(s!=null&&s.ActualOutcome=="MEASURED_READ_ONLY")message="Measurement complete (read-only, not saved).";
   else if(s!=null&&(s.Phase=="CANCELLED"||s.ActualOutcome=="CANCELLED")||stopping)message="Calibration cancelled.";
   else if(detail!=null||s!=null&&s.Fault!=null)message=BridgeText.T("Calibration did not finish. ")+(detail??s.Fault);
   else if(target.ReadOnly)message="Read-only installation. Measurement is available; configuration will not be saved.";
   else if(tracker.Items.Count==0)message="No persisted tracker mappings. Add a tracker in Advanced settings first.";
   else message="Ready. Select the reference and tracker, then Start.";
   state.Text=BridgeText.T(message);
   if(!active&&s!=null&&s.ActualOutcome!=null&&(s.ActualOutcome.StartsWith("SAVED",StringComparison.Ordinal)||s.ActualOutcome=="MEASURED_READ_ONLY")&&s.Fault!=null)state.Text+="\n"+BridgeText.Diagnostic(s.Fault);
   if(active&&s!=null&&!String.IsNullOrEmpty(s.Progress))state.Text+="\n"+BridgeText.T("Measurement in progress");
   // The host may reload synchronously after this callback. Keep the done state.
   if(active!=reportedActive){reportedActive=active;ownership(active,s!=null&&s.CommitMayHaveStarted);}
  }
  public void Dispose(){if(disposed)return;if(Active)throw new InvalidOperationException("Wait for owned worker shutdown before disposing calibration view");disposed=true;generation++;timer.Stop();workflow.Dispose();}
 }
}
