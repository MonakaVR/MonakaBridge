using System;
using System.Collections.Generic;
using System.Threading.Tasks;

namespace MonakaBridge {
 // One Start owns one worker and at most one native candidate. Native remains the
 // authority for identity, freshness, result validity, CAS and actual save.
 internal sealed class CalibrationWorkflow:IDisposable {
  readonly object gate=new object();
  readonly Func<CalibrationInstallation,bool,CalibrationProcessClient> factory;
  CalibrationProcessClient client;Task running;bool active,cancelled,disposed,readOnlyRun;string workflowFault,workflowOutcome,candidateSha256;
  internal CalibrationWorkflow(Func<CalibrationInstallation,bool,CalibrationProcessClient> create=null){factory=create??delegate(CalibrationInstallation selected,bool readOnly){return new CalibrationProcessClient(selected,readOnly);};}
  internal bool Active {get{lock(gate){return active;}}}
  internal string CandidateSha256 {get{lock(gate){return candidateSha256;}}}
  internal CalibrationClientSnapshot Snapshot(){lock(gate){
   var value=client==null?new CalibrationClientSnapshot{Phase="NEW",Stopped=true}:client.Snapshot();
   if(workflowFault!=null)value.Fault=workflowFault;
   if(workflowOutcome!=null&&value.ActualOutcome==null)value.ActualOutcome=workflowOutcome;
   // Prepared state is an internal handoff, never a separate operator action.
   value.CanPrepare=false;value.CanCommit=false;return value;
  }}
  internal Task StartAsync(CalibrationInstallation selected,bool readOnly){lock(gate){
   if(disposed)throw new ObjectDisposedException("CalibrationWorkflow");
   if(active)throw new InvalidOperationException("Calibration worker is still active");
   if(selected==null)throw new ArgumentNullException("selected");
   readOnlyRun=readOnly||selected.ReadOnly;
   if(client!=null)client.Dispose();client=factory(selected,readOnlyRun);
   if(client==null)throw new InvalidOperationException("Calibration client required");
   cancelled=false;workflowFault=null;workflowOutcome=null;candidateSha256=null;active=true;
   // Even bootstrap/config reads execute away from the UI thread.
   running=Task.Run((Func<Task>)RunAsync);return running;
  }}
  Task<Dictionary<string,object>> Step(Func<Task<Dictionary<string,object>>> action){lock(gate){
   if(cancelled)throw new OperationCanceledException("Calibration stopped");
   return action();
  }}
  static void Require(Dictionary<string,object> response,string status){if(CalibrationJson.Text(response,"status",128)!=status)throw new InvalidOperationException("Calibration expected "+status+": "+CalibrationJson.Utf8.GetString(CalibrationJson.Encode(response,CalibrationContract.LineBytes)));}
  async Task RunAsync(){
   try{
    Require(await Step(client.OpenAsync).ConfigureAwait(false),"OPEN");
    Require(await Step(client.DiscoverAsync).ConfigureAwait(false),"DISCOVERED");
    Require(await Step(client.ConfirmAsync).ConfigureAwait(false),"CONFIRMED");
    Require(await Step(client.BeginAsync).ConfigureAwait(false),"RESULT_READY");
    if(readOnlyRun){lock(gate){if(cancelled)throw new OperationCanceledException("Calibration stopped");workflowOutcome="MEASURED_READ_ONLY";}}
    else{
     Require(await Step(client.PrepareAsync).ConfigureAwait(false),"PREPARED");
     var candidate=client.Snapshot().Prepared;if(candidate==null)throw new InvalidOperationException("Native prepared candidate missing");
     bool truncated;candidate.ReadPreview(out truncated); // exact byte count + SHA; never reconstruct config
     lock(gate){candidateSha256=candidate.CandidateSha256;}
     var saved=await Step(client.CommitAsync).ConfigureAwait(false);
     string outcome=CalibrationJson.Text(saved,"status",128);
     if(outcome!="SAVED"&&outcome!="SAVED_CONFIG_EVIDENCE_STALE"&&outcome!="UNKNOWN")throw new InvalidOperationException("Calibration save refused: "+CalibrationJson.Utf8.GetString(CalibrationJson.Encode(saved,CalibrationContract.LineBytes)));
    }
   }catch(Exception e){lock(gate){if(!cancelled)workflowFault=e.Message;}}
   // Kept after catch for the repository's Framework C# compiler, which does
   // not support await in finally. Every operation error above reaches here.
   try{Task cleanup;lock(gate){cleanup=cancelled?client.CancelAsync():client.CloseAsync();}await cleanup.ConfigureAwait(false);}
   catch(Exception e){lock(gate){workflowFault=workflowFault??("Worker cleanup unconfirmed: "+e.Message);}}
   lock(gate){active=!client.Snapshot().Stopped;}
  }
  internal Task StopAsync(){lock(gate){
   cancelled=true;
   if(client==null)return Task.FromResult(true);
   // The same lock guards step dispatch, so cancellation wins every subsequent
   // dispatch, including Commit. Already-started commit retains native UNKNOWN.
   Task stopped=client.CancelAsync();return running==null?stopped:WaitStopped(stopped,running);
  }}
  async Task WaitStopped(Task stopped,Task run){await stopped.ConfigureAwait(false);await run.ConfigureAwait(false);}
  public void Dispose(){lock(gate){
   if(disposed)return;
   if(active||client!=null&&!client.Snapshot().Stopped)throw new InvalidOperationException("Wait for owned calibration worker shutdown before disposing workflow");
   disposed=true;if(client!=null)client.Dispose();
  }}
 }
}
