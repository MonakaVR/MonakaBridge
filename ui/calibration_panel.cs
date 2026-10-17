using System;
using System.Collections.Generic;
using System.IO;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;

namespace MonakaBridge {
 // Native owns capture, solver, result and transaction. The view only sends
 // control requests and displays verbatim evidence from its one owned worker.
 internal sealed class CalibrationPanel:StackPanel,IDisposable {
  GuiLaunchOptions target;CalibrationProcessClient client;
  readonly Func<bool> hostAvailable;readonly Action<bool,bool> ownership;
  readonly TextBlock state=new TextBlock{TextWrapping=TextWrapping.Wrap};
  readonly TextBox evidence=new TextBox{IsReadOnly=true,TextWrapping=TextWrapping.Wrap,VerticalScrollBarVisibility=ScrollBarVisibility.Auto,Height=230,FontFamily=new FontFamily("Consolas")};
  readonly TextBox preview=new TextBox{IsReadOnly=true,TextWrapping=TextWrapping.NoWrap,VerticalScrollBarVisibility=ScrollBarVisibility.Auto,HorizontalScrollBarVisibility=ScrollBarVisibility.Auto,Height=180,FontFamily=new FontFamily("Consolas")};
  readonly Button open,discover,confirm,begin,prepare,save,cancel,fresh;
  readonly CheckBox reviewed=new CheckBox{Content="I reviewed this exact native candidate and its SHA-256"};
  readonly DispatcherTimer timer=new DispatcherTimer();
  bool pending,disposed,reportedActive;int generation;string shownResult,shownCandidate,detail;
  internal CalibrationPanel(GuiLaunchOptions selected,Func<bool> available,Action<bool,bool> owned) {
   target=selected;hostAvailable=available;ownership=owned;
   Children.Add(new TextBlock{Text="Trajectory calibration",FontSize=20,Margin=new Thickness(0,18,0,3)});
   Children.Add(new TextBlock{Text="Open is resource-free. Discover explicitly starts OpenVR Background and the selected loopback mirror. Hardware validation and downstream output are unverified. Static calibration and output approval are separate.",TextWrapping=TextWrapping.Wrap});
   var actions=new WrapPanel();Children.Add(actions);
   open=Button(actions,"Open session",delegate{return Open();});
   discover=Button(actions,"Discover target",delegate{return Command(delegate{return client.DiscoverAsync();});});
   confirm=Button(actions,"Confirm displayed witness",delegate{return Command(delegate{return client.ConfirmAsync();});});
   begin=Button(actions,"Begin trajectory",delegate{return Command(delegate{return client.BeginAsync();});});
   prepare=Button(actions,"Prepare same result",Prepare);
   save=Button(actions,"Save exact candidate",delegate{if(reviewed.IsChecked!=true)throw new InvalidOperationException("Review the exact candidate first");return Command(delegate{return client.CommitAsync();});});
   cancel=Button(actions,"Cancel / close",StopAsync,true);
   fresh=Button(actions,"New measurement",NewMeasurement);
   Children.Add(state);Children.Add(evidence);Children.Add(new TextBlock{Text="Candidate config preview (bounded). Native commit rechecks exact bytes, identity, witness, lock and original config snapshot.",TextWrapping=TextWrapping.Wrap});Children.Add(preview);Children.Add(reviewed);
   reviewed.Checked+=delegate{Refresh();};reviewed.Unchecked+=delegate{Refresh();};
   timer.Interval=TimeSpan.FromMilliseconds(200);timer.Tick+=delegate{Refresh();};timer.Start();Refresh();
  }
  Button Button(Panel panel,string text,Func<Task> operation,bool independent=false) {
   var button=new Button{Content=text,MinHeight=28,Margin=new Thickness(3),Padding=new Thickness(8,2,8,2)};
   button.Click+=async delegate{if(disposed||(!independent&&pending))return;int token=generation;if(!independent)pending=true;
    try{await operation();}catch(Exception e){if(!disposed&&token==generation)detail=e.GetBaseException().Message;}
    finally{if(!disposed&&token==generation){if(!independent)pending=false;Refresh();}}
   };panel.Children.Add(button);return button;
  }
  internal bool Active {get{return client!=null&&!client.Snapshot().Stopped;}}
  internal void SetTarget(GuiLaunchOptions selected){if(Active)throw new InvalidOperationException("Owned calibration shutdown must finish before changing target");if(client!=null)client.Dispose();client=null;target=selected;generation++;pending=false;detail=null;shownResult=shownCandidate=null;evidence.Text=preview.Text="";reviewed.IsChecked=false;Refresh();}
  async Task Open(){if(!hostAvailable())throw new InvalidOperationException("Reload/review configuration and finish the current save first");if(target.Calibration==null)throw new InvalidOperationException("Create or explicitly open a v2 calibration installation profile first");if(client!=null)throw new InvalidOperationException("Use New measurement after the previous worker has stopped");client=new CalibrationProcessClient(target.Calibration,target.ReadOnly);ReportOwnership();await Command(delegate{return client.OpenAsync();});}
  async Task Command(Func<Task<Dictionary<string,object>>> command){int token=generation;var value=await command();if(disposed||token!=generation)return;detail=CalibrationJson.Utf8.GetString(CalibrationJson.Encode(value,CalibrationContract.LineBytes));if(value.ContainsKey("witness"))evidence.Text=detail;}
  async Task Prepare(){int token=generation;reviewed.IsChecked=false;preview.Text="";await Command(delegate{return client.PrepareAsync();});if(disposed||token!=generation)return;var candidate=client.Snapshot().Prepared;if(candidate==null)throw new InvalidOperationException("Native prepared candidate unavailable");string text=await Task.Run(delegate{bool truncated;return candidate.ReadPreview(out truncated);});if(disposed||token!=generation)return;if(client.Snapshot().Prepared!=candidate)throw new InvalidOperationException("Native review expired during preview");shownCandidate=candidate.CandidateSha256;preview.Text="SHA-256: "+candidate.CandidateSha256+"\nBytes: "+candidate.CandidateBytes+"\nPath: "+candidate.CandidatePath+"\n"+text;}
  async Task NewMeasurement(){await StopAsync();if(!hostAvailable())throw new InvalidOperationException("Reload/review saved or unknown configuration before a new measurement");SetTarget(target);}
  internal async Task StopAsync(){var owned=client;if(owned==null)return;try{await owned.CancelAsync();if(!owned.Snapshot().Stopped)throw new InvalidOperationException("Owned worker shutdown is unconfirmed; a new worker cannot start");}finally{if(!disposed){ReportOwnership();Refresh();}}}
  void ReportOwnership(){bool active=Active;if(active!=reportedActive){reportedActive=active;ownership(active,client!=null&&client.Snapshot().CommitMayHaveStarted);}}
  void Refresh(){if(disposed)return;ReportOwnership();var s=client==null?null:client.Snapshot();bool ready=!pending&&!disposed;
   open.IsEnabled=ready&&client==null&&target.Calibration!=null&&hostAvailable();discover.IsEnabled=ready&&s!=null&&!s.Stopping&&s.Phase=="OPEN";confirm.IsEnabled=ready&&s!=null&&!s.Stopping&&s.Phase=="DISCOVERED";begin.IsEnabled=ready&&s!=null&&!s.Stopping&&s.Phase=="CONFIRMED";
   prepare.IsEnabled=ready&&s!=null&&s.CanPrepare;save.IsEnabled=ready&&s!=null&&s.CanCommit&&shownCandidate==s.Prepared.CandidateSha256&&reviewed.IsChecked==true;
   cancel.IsEnabled=s!=null&&!s.Stopped;fresh.IsEnabled=ready&&s!=null&&s.Stopped;
   if(s!=null&&s.Prepared==null&&shownCandidate!=null){shownCandidate=null;reviewed.IsChecked=false;}
   if(s!=null&&s.Result!=null&&shownResult!=s.Result.ResultId){shownResult=s.Result.ResultId;evidence.Text="W: saved world alignment. E: holding extrinsic, NOT mount. M: existing mount, unchanged.\nGroup output approval is reset; mapping revision increments once. Runtime application and downstream output remain unconfirmed.\n"+s.Result.NativeJson;}
   string phase=s==null?"No native worker":s.Phase+(s.Stopped?" | owned worker stopped":"")+(s.ActualOutcome==null?"":" | actual native outcome: "+s.ActualOutcome);
   state.Text=(target.Calibration==null?"v2 profile required. Use Create calibration profile (Save As).\n":"Profile: "+target.Calibration.ProfilePath+"\nSnapshot SHA-256: "+target.Calibration.ProfileSha256+"\n")+phase+(target.ReadOnly?" | READ ONLY (measurement allowed)":"")+"\n"+(s==null?"":s.Fault??"")+"\n"+(detail??"")+"\n"+(s==null?"":s.Progress??"");
   if(s!=null){string[] notices=client.DrainNotifications();if(notices.Length!=0)detail=notices[notices.Length-1];}
  }
  public void Dispose(){if(disposed)return;if(Active)throw new InvalidOperationException("Wait for owned worker shutdown before disposing calibration view");disposed=true;generation++;timer.Stop();if(client!=null)client.Dispose();}
 }
}
