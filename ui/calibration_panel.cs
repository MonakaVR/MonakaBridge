using System;
using System.Collections.Generic;
using System.Collections;
using System.IO;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Threading;
using System.Windows.Automation;

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
   readonly Button capturePoint,openApproval,reviewOutput,prepareOutput,approveOutput;
   readonly ComboBox mode=new ComboBox{ItemsSource=new[]{BridgeText.T("Trajectory"),BridgeText.T("Static")},SelectedIndex=0,MinWidth=160};
   readonly TextBox points=new TextBox{Text="3",Width=70},pairs=new TextBox{Text="60",Width=70};
   readonly StackPanel members=new StackPanel();readonly List<CheckBox> memberChoices=new List<CheckBox>();
   bool approvalSession;string shownApproval;
  readonly CheckBox reviewed=new CheckBox{Content=BridgeText.T("I reviewed this exact native candidate and its SHA-256")};
  readonly DispatcherTimer timer=new DispatcherTimer();
  bool pending,disposed,reportedActive;int generation;string shownResult,shownCandidate,detail;
  internal CalibrationPanel(GuiLaunchOptions selected,Func<bool> available,Action<bool,bool> owned) {
   target=selected;hostAvailable=available;ownership=owned;
    Children.Add(new TextBlock{Text=BridgeText.T("World calibration and separate output approval"),FontSize=20,Margin=new Thickness(0,18,0,3)});
    Children.Add(new TextBlock{Text=BridgeText.T("Open is resource-free. Discover explicitly starts OpenVR Background and the selected loopback mirror. Choose measurement mode before Open. Static captures paired fresh points; it does not verify physical stillness or accuracy. Saving W requires separate output reapproval. Output approval requires an adopted external criteria policy and matching physical evidence."),TextWrapping=TextWrapping.Wrap});
    Children.Add(new Label{Content=BridgeText.T("_Measurement mode"),Target=mode});Children.Add(mode);Children.Add(new Label{Content=BridgeText.T("Static _points (3..16)"),Target=points});Children.Add(points);Children.Add(new Label{Content=BridgeText.T("Fresh _pairs per point (1..4096; total <=16384)"),Target=pairs});Children.Add(pairs);
    AutomationProperties.SetName(mode,"Measurement mode");AutomationProperties.SetName(points,"Static point count");AutomationProperties.SetName(pairs,"Fresh pairs per static point");AutomationProperties.SetName(state,"Calibration status and refusal reason");AutomationProperties.SetName(evidence,"Native witness, result and output approval criteria");AutomationProperties.SetName(preview,"Exact native candidate config and SHA-256");AutomationProperties.SetName(reviewed,"Confirm review of this exact candidate");
   var actions=new WrapPanel();Children.Add(actions);
   open=Button(actions,"Open session",delegate{return Open();});
   discover=Button(actions,"Discover target",delegate{return Command(delegate{return client.DiscoverAsync();});});
   confirm=Button(actions,"Confirm displayed witness",delegate{return Command(delegate{return client.ConfirmAsync();});});
   begin=Button(actions,"Begin trajectory",delegate{return Command(delegate{return client.BeginAsync();});});
    capturePoint=Button(actions,"_Capture static point",delegate{return Command(delegate{return client.CapturePointAsync();});});
   prepare=Button(actions,"Prepare same result",Prepare);
   save=Button(actions,"Save exact candidate",delegate{if(reviewed.IsChecked!=true)throw new InvalidOperationException("Review the exact candidate first");return Command(delegate{return client.CommitAsync();});});
   cancel=Button(actions,"Cancel / close",StopAsync,true);
   fresh=Button(actions,"New measurement",NewMeasurement);
    openApproval=Button(actions,"Open separate _output approval review",OpenOutputApproval);
    reviewOutput=Button(actions,"Review output criteria and _evidence",delegate{return Command(delegate{return client.ReviewOutputAsync();});});
    prepareOutput=Button(actions,"Prepare selected output approval _diff",PrepareOutput);
    approveOutput=Button(actions,"_Approve Output (separate transaction)",delegate{if(reviewed.IsChecked!=true)throw new InvalidOperationException("Review the separate exact approval candidate first");return Command(delegate{return client.CommitAsync();});});
    Children.Add(new TextBlock{Text=BridgeText.T("Output approval members: no default selection. Missing physical criteria/evidence is a native refusal. A holding measurement never approves sibling mounts or body fit."),TextWrapping=TextWrapping.Wrap});Children.Add(members);
   Children.Add(state);Children.Add(evidence);Children.Add(new TextBlock{Text=BridgeText.T("Candidate config preview (bounded). Native commit rechecks exact bytes, identity, witness, lock and original config snapshot."),TextWrapping=TextWrapping.Wrap});Children.Add(preview);Children.Add(reviewed);
   reviewed.Checked+=delegate{Refresh();};reviewed.Unchecked+=delegate{Refresh();};
   timer.Interval=TimeSpan.FromMilliseconds(200);timer.Tick+=delegate{Refresh();};timer.Start();Refresh();
  }
  Button Button(Panel panel,string text,Func<Task> operation,bool independent=false) {
   var button=new Button{Content=BridgeText.T(text),MinHeight=28,Margin=new Thickness(3),Padding=new Thickness(8,2,8,2)};
   button.Click+=async delegate{if(disposed||(!independent&&pending))return;int token=generation;if(!independent)pending=true;
    try{await operation();}catch(Exception e){if(!disposed&&token==generation)detail=BridgeText.Diagnostic(e.GetBaseException().Message);}
    finally{if(!disposed&&token==generation){if(!independent)pending=false;Refresh();}}
    };AutomationProperties.SetName(button,BridgeText.T(text).Replace("_",""));panel.Children.Add(button);return button;
  }
  internal bool Active {get{return client!=null&&!client.Snapshot().Stopped;}}
   internal void SetTarget(GuiLaunchOptions selected){if(Active)throw new InvalidOperationException("Owned calibration shutdown must finish before changing target");if(client!=null)client.Dispose();client=null;target=selected;generation++;pending=false;detail=null;shownResult=shownCandidate=shownApproval=null;approvalSession=false;memberChoices.Clear();members.Children.Clear();evidence.Text=preview.Text="";reviewed.IsChecked=false;Refresh();}
   async Task Open(){if(!hostAvailable())throw new InvalidOperationException("Reload/review configuration and finish the current save first");if(target.Calibration==null)throw new InvalidOperationException("Create or explicitly open a v2 calibration installation profile first");if(client!=null)throw new InvalidOperationException("Use New measurement after the previous worker has stopped");var next=new CalibrationProcessClient(target.Calibration,target.ReadOnly);if(mode.SelectedIndex==1){int count,samples;if(!Int32.TryParse(points.Text,out count)||!Int32.TryParse(pairs.Text,out samples))throw new InvalidOperationException("Enter whole static point/pair counts");next.SelectStatic(count,samples);}client=next;approvalSession=false;ReportOwnership();await Command(delegate{return client.OpenAsync();});}
   async Task OpenOutputApproval(){await StopAsync();if(!hostAvailable())throw new InvalidOperationException("Reload/review current saved configuration, then open separate output approval review");SetTarget(target);if(target.Calibration==null)throw new InvalidOperationException("Explicit v2 profile required");client=new CalibrationProcessClient(target.Calibration,target.ReadOnly);client.SelectOutputApproval();approvalSession=true;ReportOwnership();await Command(delegate{return client.OpenAsync();});detail="Fresh output review opened. Discover and confirm current witness, then review criteria/evidence. Historic drafts do not grant approval.";}
  async Task Command(Func<Task<Dictionary<string,object>>> command){int token=generation;var value=await command();if(disposed||token!=generation)return;detail=CalibrationJson.Utf8.GetString(CalibrationJson.Encode(value,CalibrationContract.LineBytes));if(value.ContainsKey("witness"))evidence.Text=detail;}
   async Task Prepare(){await Command(delegate{return client.PrepareAsync();});await ShowPrepared();}
   async Task PrepareOutput(){var selected=new List<string>();foreach(var member in memberChoices)if(member.IsEnabled&&member.IsChecked==true)selected.Add((string)member.Tag);await Command(delegate{return client.PrepareOutputAsync(selected.ToArray());});await ShowPrepared();}
   async Task ShowPrepared(){int token=generation;reviewed.IsChecked=false;preview.Text="";var candidate=client.Snapshot().Prepared;if(candidate==null)throw new InvalidOperationException("Native prepared candidate unavailable");string text=await Task.Run(delegate{bool truncated;return candidate.ReadPreview(out truncated);});if(disposed||token!=generation)return;if(client.Snapshot().Prepared!=candidate)throw new InvalidOperationException("Native review expired during preview");shownCandidate=candidate.CandidateSha256;preview.Text=(candidate.OutputApproval?"SEPARATE OUTPUT APPROVAL: W/E/M unchanged\n":"WORLD ALIGNMENT SAVE: OUTPUT REAPPROVAL REQUIRED\n")+"SHA-256: "+candidate.CandidateSha256+"\nBytes: "+candidate.CandidateBytes+"\nPath: "+candidate.CandidatePath+"\n"+text;}
  async Task NewMeasurement(){await StopAsync();if(!hostAvailable())throw new InvalidOperationException("Reload/review saved or unknown configuration before a new measurement");SetTarget(target);}
  internal async Task StopAsync(){var owned=client;if(owned==null)return;try{await owned.CancelAsync();if(!owned.Snapshot().Stopped)throw new InvalidOperationException("Owned worker shutdown is unconfirmed; a new worker cannot start");}finally{if(!disposed){ReportOwnership();Refresh();}}}
  void ReportOwnership(){bool active=Active;if(active!=reportedActive){reportedActive=active;ownership(active,client!=null&&client.Snapshot().CommitMayHaveStarted);}}
  void Refresh(){if(disposed)return;ReportOwnership();var s=client==null?null:client.Snapshot();bool ready=!pending&&!disposed;
    mode.IsEnabled=points.IsEnabled=pairs.IsEnabled=client==null&&!pending;
    open.IsEnabled=ready&&client==null&&target.Calibration!=null&&hostAvailable();discover.IsEnabled=ready&&s!=null&&!s.Stopping&&s.Phase=="OPEN";confirm.IsEnabled=ready&&s!=null&&!s.Stopping&&s.Phase=="DISCOVERED";begin.IsEnabled=ready&&!approvalSession&&s!=null&&!s.Stopping&&s.Phase=="CONFIRMED";begin.Content=BridgeText.T(mode.SelectedIndex==1?"Begin static plan":"Begin trajectory");
    capturePoint.IsEnabled=ready&&s!=null&&!s.Stopping&&s.Phase=="WAITING_POINT"&&s.PointToken!=null;
    prepare.IsEnabled=ready&&s!=null&&s.CanPrepare;save.IsEnabled=ready&&s!=null&&s.CanCommit&&!s.Prepared.OutputApproval&&shownCandidate==s.Prepared.CandidateSha256&&reviewed.IsChecked==true;
    openApproval.IsEnabled=ready&&target.Calibration!=null&&(s==null||s.Stopped||s.ActualOutcome=="SAVED"||s.ActualOutcome=="SAVED_CONFIG_EVIDENCE_STALE");reviewOutput.IsEnabled=ready&&approvalSession&&s!=null&&!s.Stopping&&(s.Phase=="CONFIRMED"||s.Phase=="APPROVAL_REVIEW");
    bool selectedMember=false;foreach(var member in memberChoices)if(member.IsEnabled&&member.IsChecked==true)selectedMember=true;
    prepareOutput.IsEnabled=ready&&approvalSession&&!target.ReadOnly&&s!=null&&s.Phase=="APPROVAL_REVIEW"&&selectedMember;approveOutput.IsEnabled=ready&&s!=null&&s.CanCommit&&s.Prepared.OutputApproval&&shownCandidate==s.Prepared.CandidateSha256&&reviewed.IsChecked==true;
   cancel.IsEnabled=s!=null&&!s.Stopped;fresh.IsEnabled=ready&&s!=null&&s.Stopped;
   if(s!=null&&s.Prepared==null&&shownCandidate!=null){shownCandidate=null;reviewed.IsChecked=false;}
    if(s!=null&&s.Result!=null){shownResult=s.Result.ResultId;string resultStage=s.ActualOutcome=="UNKNOWN"?"UNKNOWN":s.ActualOutcome!=null&&s.ActualOutcome.StartsWith("SAVED",StringComparison.Ordinal)?s.ActualOutcome:s.Prepared!=null?"PREPARED (NOT SAVED)":"MEASURED (NOT SAVED)";evidence.Text=resultStage+". W: measured world alignment. "+(s.Result.Static?"Static E and orientation residual: NOT ESTIMATED / NOT APPLICABLE.":"E: holding extrinsic, NOT mount.")+" M: existing mount, unchanged.\nSaving this result resets group output approval and increments mapping revision once. Runtime application and downstream output remain unconfirmed.\n"+s.Result.NativeJson;}
    if(s!=null&&s.Approval!=null&&shownApproval!=s.Approval.ApprovalId){shownApproval=s.Approval.ApprovalId;evidence.Text=s.Approval.NativeJson;members.Children.Clear();memberChoices.Clear();var value=s.Approval.DisplayCopy();foreach(Dictionary<string,object> item in (IList)value["members"]){var choice=new CheckBox{Content=item["device"]+" / "+item["tracker"]+" | eligible: "+item["eligible"]+" | "+item["reason"],Tag=(string)item["device"],IsChecked=false,IsEnabled=Convert.ToBoolean(item["eligible"])&&!target.ReadOnly};AutomationProperties.SetName(choice,Convert.ToString(choice.Content));choice.Checked+=delegate{Refresh();};choice.Unchecked+=delegate{Refresh();};members.Children.Add(choice);memberChoices.Add(choice);}}
   string phase=s==null?BridgeText.T("No native worker"):s.Phase+(s.Stopped?" | owned worker stopped":"")+(s.ActualOutcome==null?"":" | actual native outcome: "+s.ActualOutcome);
    state.Text=(target.Calibration==null?"v2 profile required. Use Create calibration profile (Save As).\n":"Profile: "+target.Calibration.ProfilePath+"\nSnapshot SHA-256: "+target.Calibration.ProfileSha256+"\n")+phase+(target.ReadOnly?" | READ ONLY (measurement allowed)":"")+"\n"+(s==null?"":s.Fault??"")+"\n"+(s!=null&&s.PointToken!=null?s.PointInstruction:"")+"\n"+(detail??"")+"\n"+(s==null?"":s.Progress??"");
   if(s!=null){string[] notices=client.DrainNotifications();if(notices.Length!=0)detail=notices[notices.Length-1];}
  }
  public void Dispose(){if(disposed)return;if(Active)throw new InvalidOperationException("Wait for owned worker shutdown before disposing calibration view");disposed=true;generation++;timer.Stop();if(client!=null)client.Dispose();}
 }
}
