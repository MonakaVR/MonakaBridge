using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using System.Threading;
using System.Web.Script.Serialization;
using System.Windows;
using System.Windows.Controls;
namespace MonakaBridge {
 public static partial class SteamVrControlWpf {
  private sealed partial class ControlWindow {
   DiscoveryPanel discoveryPanel;Dictionary<string,object> discoveryHealth;
   DiscoveryDraft discoverySaved;bool discoveryRendering;string discoveryOutcome;
   void DiscoveryConfigurationLoaded(){if(discoveryPanel!=null){discoveryPanel.Candidates.SelectedIndex=-1;SelectDiscovery();}}
   void DiscoveryTargetChanged(){discoveryHealth=null;discoverySaved=null;discoveryOutcome=null;DiscoveryConfigurationLoaded();}
   void InitializeDiscovery(Panel panel){
    discoveryPanel=new DiscoveryPanel();panel.Children.Add(discoveryPanel);
    discoveryPanel.Candidates.SelectionChanged+=delegate{if(!discoveryRendering)SelectDiscovery();};
    discoveryPanel.Create.Click+=delegate{Safe(delegate{SaveDiscovery(DiscoveryAction.Create);});};
    discoveryPanel.Replace.Click+=delegate{Safe(delegate{SaveDiscovery(DiscoveryAction.Replace);});};
    discoveryPanel.Revision.Click+=delegate{Safe(delegate{SaveDiscovery(DiscoveryAction.UpdateRevision);});};
    discoveryPanel.Ignore.Click+=delegate{discoveryPanel.Candidates.SelectedIndex=-1;discoveryPanel.Details.Text="Ignored locally. Configuration bytes and revision unchanged.";};
   }
   DiscoverySnapshot DiscoveryView(){return DiscoveryModel.Read(config,healthAvailable?discoveryHealth:null,DateTime.UtcNow);}
   bool DiscoveryBusy(){return busy||needsReload||closing||switching||calibration!=null&&calibration.Active;}
   void RefreshDiscovery(){
    if(discoveryPanel==null)return;var view=DiscoveryView();var selected=discoveryPanel.Candidates.SelectedItem as DiscoveryObservation;
    discoveryRendering=true;try{
     discoveryPanel.Candidates.Items.Clear();foreach(var row in view.Rows)if(!row.Configured||row.Reason!="CONFIGURED"){
      discoveryPanel.Candidates.Items.Add(row);if(row.Same(selected))discoveryPanel.Candidates.SelectedItem=row;
     }
    }finally{discoveryRendering=false;}
    if(selected!=null&&discoveryPanel.Candidates.SelectedItem==null)SelectDiscovery();
    string reason=options.ReadOnly?"Read-only installation":DiscoveryBusy()?"Finish calibration/save/shutdown and reload/review first":!view.Ready?view.Error:null;
    discoveryPanel.Enable(reason==null,reason);
    string outcome=discoveryOutcome??"No discovery configuration saved.";
    if(discoverySaved==null&&needsReload&&discoveryOutcome!=null)outcome="Save rejected or outcome unconfirmed. Reload/review required; no automatic retry or rollback.";
    if(healthAvailable&&discoverySaved!=null&&DiscoveryModel.Applied(discoverySaved,discoveryHealth,DateTime.UtcNow))outcome="SAVED / APPLIED (observed matching runtime health). Downstream output and physical tracking unverified.";
    discoveryPanel.State.Text=(reason??"Current matching-instance discovery; select one device, contract and profile explicitly.")+"\n"+outcome;
   }
   void SelectDiscovery(){
    var row=discoveryPanel.Candidates.SelectedItem as DiscoveryObservation;
    discoveryPanel.Contracts.Items.Clear();discoveryPanel.Profiles.Items.Clear();discoveryPanel.Slots.Items.Clear();discoveryPanel.Logical.Text="";
    if(row==null||config==null){discoveryPanel.Details.Text="Select an unmapped claim or a known input revision mismatch.";return;}
    foreach(var c in DiscoveryModel.Contracts(config,row.Source))discoveryPanel.Contracts.Items.Add(c);
    foreach(var name in ((Dictionary<string,object>)config["profiles"]).Keys){var p=MappingEditor.RequireProfile(config,name);if(DiscoveryModel.Flag(p,"approved")&&DiscoveryModel.Text(p,"evidence")!=""&&DiscoveryModel.Text(p,"convention")==row.Convention)discoveryPanel.Profiles.Items.Add(name);}
    for(int n=0;n<map.Count;++n){var b=(Dictionary<string,object>)map[n];if(DiscoveryModel.Text(b,"source_id")==row.Source)discoveryPanel.Slots.Items.Add(new DiscoverySlot{Index=n,Label=DiscoveryModel.Text(b,"tracker_id")+" | "+DiscoveryModel.Text(b,"device_id")+" | "+DiscoveryModel.Text(b,"input_space")+" @"+DiscoveryModel.Text(b,"input_revision")});}
    discoveryPanel.Details.Text=(row.Configured?"KNOWN INPUT FRAME/REVISION NOTICE":"UNMAPPED (not admitted)")+"\nSource: "+row.Source+"\nDevice: "+row.Device+"\nSession: "+row.Session+" | clock: "+row.Clock+"\nInput: "+row.Space+" @"+row.Revision+" | "+row.Convention+"\nAdvertised claims: "+string.Join(", ",row.Claims)+"\nNo support inferred from pose validity/presence. A compatible explicit source contract is required. Otherwise NEEDS_CONTRACT_REVIEW.";
   }
   sealed class DiscoverySlot {internal int Index;internal string Label;public override string ToString(){return Label;}}
   DiscoveryDraft BuildDiscoveryDraft(DiscoveryAction action){
    RequireSave();var slot=discoveryPanel.Slots.SelectedItem as DiscoverySlot;
    return DiscoveryModel.Prepare(config,DiscoveryView(),discoveryPanel.Candidates.SelectedItem as DiscoveryObservation,action,action==DiscoveryAction.Create?-1:slot==null?-1:slot.Index,discoveryPanel.Logical.Text,discoveryPanel.Profiles.SelectedItem as string,discoveryPanel.Contracts.SelectedItem as DiscoveryContract,options.ReadOnly,DiscoveryBusy());
   }
   void SaveDiscovery(DiscoveryAction action){
    var draft=BuildDiscoveryDraft(action);discoveryPanel.Details.Text=draft.Confirmation;
    if(MessageBox.Show(this,draft.Confirmation,"Review explicit discovery mapping",MessageBoxButton.OKCancel,MessageBoxImage.Warning)!=MessageBoxResult.OK)return;
    // The modal review may have taken time. Recheck now, then again immediately
    // before the native commit, after helper validation and temporary snapshots.
    DiscoveryModel.Recheck(config,HealthReader.ReadOnce(options.HealthFile).Value,draft,options.ReadOnly,DiscoveryBusy(),DateTime.UtcNow);
    var target=options;var expected=(byte[])configBytes.Clone();var loaded=MappingEditor.Clone(config);
    discoverySaved=null;discoveryOutcome="Saving explicit draft; result pending. Runtime application unconfirmed.";
    RunSave(delegate(CancellationToken cancel){
     string candidate=Path.Combine(Path.GetTempPath(),"monaka-discovery-"+Guid.NewGuid().ToString("N")+".json");
     try{
      File.WriteAllText(candidate,new JavaScriptSerializer().Serialize(draft.Document),new UTF8Encoding(false));
      new ConfigHelperClient(target).Commit(expected,candidate,cancel,delegate{
       // Last UI ownership check is on its Dispatcher; native CAS owns the
       // subsequent exact-byte configuration check. No retry or live write here.
       Dispatcher.Invoke((Action)delegate{if(!object.ReferenceEquals(target,options)||closed||closing||switching||needsReload||calibration.Active)throw new InvalidOperationException("Discovery ownership changed before commit");target.RequireEdit();});
       var health=HealthReader.ReadOnce(target.HealthFile);DiscoveryModel.Recheck(loaded,health.Value,draft,target.ReadOnly,false,DateTime.UtcNow);
      });
     }finally{if(File.Exists(candidate))File.Delete(candidate);}
    },Tuple.Create(draft.Observation.Source,draft.Observation.Device),delegate{
     discoverySaved=draft;discoveryOutcome=draft.Action==DiscoveryAction.Replace?"SAVED / Bridge restart required / runtime unconfirmed. Restart manually after review.":"SAVED / runtime unconfirmed. Observe matching-instance health; restart manually if required.";
     RefreshDiscovery();
    });
   }
  }
 }
}
