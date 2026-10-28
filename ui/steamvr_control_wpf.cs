using System;
using System.Collections.Generic;
using System.Collections;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
namespace MonakaBridge {
 public static partial class SteamVrControlWpf {
  public static void Run(string root){Run(GuiLaunchOptions.FromRoot(root));}
  internal static void Run(GuiLaunchOptions target){new Application().Run(new ControlWindow(target));}
  internal static string ConfigPath(string root){return Path.Combine(root,"config","bridge.json");}
  internal static string Native(string root,string name){return Path.Combine(root,"build","Release",name+".exe");}
  internal static string Quote(string s){return GuiLaunchOptions.Quote(s);}
  internal static void Command(string root,string args){
   new ConfigHelperClient(GuiLaunchOptions.FromRoot(root)).Command(args);
  }
  internal static void CommitFile(string root,byte[] expected,string candidate){
   new ConfigHelperClient(GuiLaunchOptions.FromRoot(root)).Commit(expected,candidate,CancellationToken.None);
  }
  internal static void Commit(string root,byte[] expected,Dictionary<string,object> candidate){
   Commit(GuiLaunchOptions.FromRoot(root),expected,candidate,CancellationToken.None);
  }
  internal static void Commit(GuiLaunchOptions target,byte[] expected,Dictionary<string,object> candidate,CancellationToken cancel){
   target.RequireEdit();cancel.ThrowIfCancellationRequested();string file=Path.Combine(Path.GetTempPath(),"monaka-config-draft-"+Guid.NewGuid().ToString("N")+".json");
   try {File.WriteAllText(file,new JavaScriptSerializer().Serialize(candidate),new UTF8Encoding(false));new ConfigHelperClient(target).Commit(expected,file,cancel);}
   finally {if(File.Exists(file))File.Delete(file);}
  }
  internal static Dictionary<string,object> Read(string path){return new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(File.ReadAllText(path,Encoding.UTF8));}
  private sealed partial class ControlWindow:Window {
   GuiLaunchOptions options;readonly TextBlock status=new TextBlock{TextWrapping=TextWrapping.Wrap};
   readonly TextBlock accessStatus=new TextBlock{TextWrapping=TextWrapping.Wrap};
   readonly List<Control> editControls=new List<Control>();readonly List<Button> serviceControls=new List<Button>();
   Button openInstallation,createCalibration,reloadConfiguration,cancelSave;bool busy,closed,needsReload,closing,closingReady,switching;CancellationTokenSource cancellation;Task pendingSave,pendingService;readonly CalibrationPanel calibration;
   readonly TextBlock livePose=new TextBlock{TextWrapping=TextWrapping.Wrap,Margin=new Thickness(2,4,2,8)};
   readonly TextBlock healthStatus=new TextBlock{TextWrapping=TextWrapping.Wrap};readonly HealthReader healthReader=new HealthReader();
   DateTime healthSnapshotUtc=DateTime.MinValue;bool healthAvailable;string currentPolicy="unknown",healthPublisher;object healthRevision;double poseTimeoutMs=500;
   readonly TextBlock mappingStatus=new TextBlock{TextWrapping=TextWrapping.Wrap};readonly MappingEditorState editor=new MappingEditorState();
   readonly ListBox devices=new ListBox{Height=160};readonly ListBox mappings=new ListBox{Height=130};
   readonly TextBox source=new TextBox(),device=new TextBox(),tracker=new TextBox(),space=new TextBox(),revision=new TextBox(),world=new TextBox();
   readonly ComboBox profile=new ComboBox{IsEditable=false};
   readonly TextBox x=new TextBox{Text="0"},y=new TextBox{Text="0"},z=new TextBox{Text="0"};readonly CheckBox approved=new CheckBox{Content="Existing space approval (new approval requires separate native review)"};
   readonly ComboBox routes=new ComboBox{ItemsSource=new[]{"steamvr","monaka","disabled"}};
   byte[] configBytes;Dictionary<string,object> config;ArrayList map;ArrayList observations=new ArrayList();readonly DispatcherTimer timer=new DispatcherTimer();
   public ControlWindow(string root):this(GuiLaunchOptions.FromRoot(root)){}
   public ControlWindow(GuiLaunchOptions target){options=target;Title="Monaka Bridge";Icon=BridgeIcons.WindowIcon();Width=900;Height=820;
    var panel=new StackPanel{Margin=new Thickness(16)};Content=new ScrollViewer{Content=panel};
    openInstallation=Add(panel,"Open installation profile...",OpenInstallation);createCalibration=Add(panel,"Create calibration profile (Save As)...",CreateCalibrationProfile);panel.Children.Add(accessStatus);
    panel.Children.Add(new TextBlock{Text="Sources and devices",FontSize=20});panel.Children.Add(healthStatus);panel.Children.Add(devices);panel.Children.Add(livePose);
    devices.SelectionChanged+=delegate{UpdateLivePose();};
    InitializeDiscovery(panel);
    reloadConfiguration=Add(panel,"Refresh / load configuration",delegate{if(busy||calibration.Active)throw new InvalidOperationException("Finish the pending save or close calibration first");Reload();});AddEdit(panel,"Use selected observation",SelectObservation);
    panel.Children.Add(new TextBlock{Text="Persistent tracker mapping and profile",FontSize=18});panel.Children.Add(mappingStatus);panel.Children.Add(mappings);mappings.SelectionChanged+=delegate{if(!editor.Updating)SelectMapping();};
    AddEdit(panel,"New mapping / clear selection",delegate{editor.Select(map,-1,RenderMapping);status.Text="No mapping selected; select a profile explicitly. Save updates the exact source/device or creates a new mapping.";});
    Field(panel,"Source",source);Field(panel,"Device",device);Field(panel,"Logical tracker",tracker);Field(panel,"Profile",profile);Field(panel,"Input map",space);Field(panel,"Input map revision",revision);Field(panel,"World map",world);panel.Children.Add(approved);
    foreach(var identityField in new[]{source,device,space,revision})identityField.TextChanged+=delegate{if(editor.IdentityEdited())approved.IsChecked=false;};
    approved.Checked+=delegate{if(!editor.Updating)editor.Approved=true;};approved.Unchecked+=delegate{if(!editor.Updating)editor.Approved=false;};
    profile.SelectionChanged+=delegate{if(editor.IdentityEdited())approved.IsChecked=false;};world.TextChanged+=delegate{if(editor.IdentityEdited())approved.IsChecked=false;};
    AddEdit(panel,"Clear existing space approval",delegate{editor.Approved=false;approved.IsChecked=false;});
    AddEdit(panel,"Save mapping",SaveMapping);Add(panel,"Show profile approval",delegate{var p=MappingEditor.RequireProfile(config,profile.SelectedItem as string);status.Text="Profile approved: "+p["approved"]+"\nEvidence: "+p["evidence"]+"\nInput space approval is separate.";});
    AddEdit(panel,"Import legacy route / alignment",Migrate);
    panel.Children.Add(new TextBlock{Text="Shared alignment for the selected source map (metres)",FontSize=18});Field(panel,"X",x);Field(panel,"Y",y);Field(panel,"Z",z);
    AddEdit(panel,"Apply shared translation",delegate{double.Parse(x.Text,CultureInfo.InvariantCulture);double.Parse(y.Text,CultureInfo.InvariantCulture);double.Parse(z.Text,CultureInfo.InvariantCulture);SaveNativeDraft("align "+Quote(tracker.Text)+" "+Quote(x.Text)+" "+Quote(y.Text)+" "+Quote(z.Text),false);});
    panel.Children.Add(routes);AddEdit(panel,"Apply output policy",delegate{if(routes.SelectedItem==null)throw new InvalidOperationException("Select an output policy.");SaveNativeDraft("policy "+routes.SelectedItem,false);});
    foreach(string operation in new[]{"Start","Stop","Restart"}){string selected=operation;serviceControls.Add(Add(panel,operation+" Bridge",delegate{ControlService(selected);}));}
    editControls.AddRange(new Control[]{source,device,tracker,space,revision,world,profile,x,y,z,approved,routes});
    cancelSave=Add(panel,"Cancel pending save",delegate{if(cancellation!=null)cancellation.Cancel();status.Text="Cancellation requested. A native commit already started may finish; wait for its result.";});
    calibration=new CalibrationPanel(options,delegate{return !busy&&!closing&&!switching&&!needsReload&&config!=null;},delegate(bool active,bool mayCommit){if(!active&&mayCommit)needsReload=true;RefreshAvailability();});panel.Children.Add(calibration);panel.Children.Add(status);RefreshAvailability();timer.Interval=TimeSpan.FromSeconds(1);timer.Tick+=delegate{Health();};timer.Start();Closing+=CloseSafely;Closed+=delegate{closed=true;calibration.Dispose();if(cancellation!=null)cancellation.Cancel();timer.Stop();healthReader.Dispose();};Loaded+=delegate{Safe(Reload);};
   }
   async void ControlService(string operation){
    bool entered=false;
    try{
     if(busy||closing||switching||calibration.Active)throw new InvalidOperationException("Finish the pending operation or calibration first");
     options.RequireServiceControl();var selected=options;busy=true;entered=true;status.Text=operation+" requested; awaiting owned process confirmation";RefreshAvailability();
     var work=Task.Run(delegate{return selected.ServiceCommand(operation);});pendingService=work;status.Text=await work;
    }
    catch(Exception e){status.Text="UNCONFIRMED: "+e.Message;}
    finally{if(entered)busy=false;if(!closed)RefreshAvailability();}
   }
   // WPF button/error pattern migrated from Task2 SteamVrControlWpf.
   static Button MakeButton(string text,double width){return new Button{Content=text,Width=width,MinHeight=28,Margin=new Thickness(3)};}
   Button Add(Panel p,string text,Action action){var b=MakeButton(text,260);b.Click+=delegate{Safe(action);};p.Children.Add(b);return b;}
   void AddEdit(Panel p,string text,Action action){editControls.Add(Add(p,text,delegate{RequireSave();action();}));}
   void RequireSave(){options.RequireEdit();if(busy||needsReload||closing||switching||calibration.Active)throw new InvalidOperationException("Finish calibration/shutdown and reload/review before another save");}
   void OpenInstallation(){
    if(busy||closing||switching)throw new InvalidOperationException("Wait for the pending operation");
    var dialog=new Microsoft.Win32.OpenFileDialog{Title="Select Bridge installation profile",Filter="JSON profile|*.json|All files|*.*"};if(dialog.ShowDialog(this)!=true)return;
    var next=GuiLaunchOptions.Parse(new[]{"--installation",dialog.FileName},null);
    if(config!=null&&MessageBox.Show(this,"Discard unsaved form edits and open the selected installation? The owned calibration worker will be closed first.","Monaka Bridge",MessageBoxButton.OKCancel)!=MessageBoxResult.OK)return;
    SwitchInstallation(next);
   }
   void CreateCalibrationProfile(){
    if(busy||closing||switching||calibration.Active||needsReload)throw new InvalidOperationException("Close calibration and reload/review first");
    if(config==null||map==null||editor.SelectedIndex<0||editor.SelectedIndex>=map.Count)throw new InvalidOperationException("Select an existing persisted mapping first");
    var dialog=new CalibrationProfileDialog(options,config,(Dictionary<string,object>)map[editor.SelectedIndex]){Owner=this};
    if(dialog.ShowDialog()!=true)return;
    // Explicit Save As followed by explicit selection; the legacy profile is
    // untouched and no new profile is auto-selected on later startup.
    SwitchInstallation(GuiLaunchOptions.Parse(new[]{"--installation",dialog.Created.ProfilePath,"--profile-sha256",dialog.Created.ProfileSha256},null));
   }
   async void SwitchInstallation(GuiLaunchOptions next){
    next=options.RetainMatchingOwner(next);
    if(switching||closing)return;switching=true;RefreshAvailability();
    try{await calibration.StopAsync();if(closed||closing)return;calibration.SetTarget(next);options=next;config=null;configBytes=null;map=null;healthAvailable=false;DiscoveryTargetChanged();observations.Clear();devices.Items.Clear();editor.Select(null,-1,RenderMapping);Reload();}
    catch(Exception e){if(!closed){needsReload=true;status.Text="Target change stopped: "+e.Message;}}
    finally{switching=false;if(!closed)RefreshAvailability();}
   }
   async void CloseSafely(object sender,System.ComponentModel.CancelEventArgs e){
    if(closingReady)return;e.Cancel=true;if(closing)return;closing=true;RefreshAvailability();if(cancellation!=null)cancellation.Cancel();
    try{var work=pendingSave;if(work!=null)try{await work;}catch{}work=pendingService;if(work!=null)try{await work;}catch{}await calibration.StopAsync();closingReady=true;await Dispatcher.InvokeAsync(new Action(Close));}
    catch(Exception error){closing=false;needsReload=true;status.Text="Owned shutdown unconfirmed; window retained: "+error.Message;RefreshAvailability();}
   }
   void RefreshAvailability(){
    string reason=closing||switching?"Owned shutdown / target change in progress":busy?"Save in progress":calibration.Active?"Calibration session owns this window; close it before config edits":needsReload?"Reload/review required":options.ValidateEditing();
    foreach(Control control in editControls){control.IsEnabled=reason==null;control.ToolTip=reason;}
    approved.IsEnabled=false;
    foreach(Button button in serviceControls){button.IsEnabled=!busy&&!closing&&!switching&&!calibration.Active&&options.ServiceBlockReason()==null;button.ToolTip=options.ServiceBlockReason();}
    cancelSave.IsEnabled=busy&&!closing;openInstallation.IsEnabled=!busy&&!closing&&!switching;createCalibration.IsEnabled=!busy&&!closing&&!switching&&!calibration.Active&&!needsReload;reloadConfiguration.IsEnabled=!busy&&!closing&&!switching&&!calibration.Active;
    Title="Monaka Bridge | "+options.InstallationName+(options.ReadOnly?" | READ ONLY":"");
    accessStatus.Text="Installation: "+options.InstallationName+"\nConfig: "+options.ConfigFile+"\nHealth: "+options.HealthFile+"\nHelper: "+options.ConfigHelper+"\n"+(reason??"Writable; compatible transaction helper. Native commit checks directory permission, lock and exact snapshot.")+"\n"+options.ServiceBlockReason();
    RefreshDiscovery();
   }
   void Safe(Action a){try{a();}catch(Exception e){status.Text=e.Message;MessageBox.Show(this,e.Message,"Monaka Bridge",MessageBoxButton.OK,MessageBoxImage.Error);}}
   static void Field(Panel p,string label,Control field){var row=new DockPanel();var text=new TextBlock{Text=label,Width=140};row.Children.Add(text);field.Margin=new Thickness(2);row.Children.Add(field);p.Children.Add(row);}
   void Reload(){Reload(editor.SelectedKey);}
   void Reload(Tuple<string,string> restoreKey){
    if(calibration.Active)throw new InvalidOperationException("Close calibration before loading another config snapshot");needsReload=true;RefreshAvailability();if(!File.Exists(options.ConfigFile)||new FileInfo(options.ConfigFile).Length>2*1024*1024)throw new InvalidOperationException("Configuration unavailable or exceeds size bound: "+options.ConfigFile);
    var loadedBytes=File.ReadAllBytes(options.ConfigFile);var persisted=new JavaScriptSerializer().Deserialize<Dictionary<string,object>>(Encoding.UTF8.GetString(loadedBytes).TrimStart('\uFEFF'));var nextMap=new ArrayList((ICollection)persisted["mappings"]);
    configBytes=loadedBytes;config=persisted;map=nextMap;DiscoveryConfigurationLoaded();needsReload=false;RefreshAvailability();
    status.Text="Loaded revision "+config["mapping_revision"]+"; profile approval is required independently of space approval.";
    editor.Programmatic(delegate{
     mappings.Items.Clear();foreach(Dictionary<string,object> b in map)mappings.Items.Add(b["tracker_id"]+" | "+b["source_id"]+" / "+b["device_id"]);
     var names=new List<string>(((Dictionary<string,object>)config["profiles"]).Keys);names.Sort(StringComparer.Ordinal);profile.ItemsSource=names;
     routes.SelectedItem=Convert.ToString(config["policy"],CultureInfo.InvariantCulture);
     if(Convert.ToString(config["policy"],CultureInfo.InvariantCulture)=="both")status.Text+=" Legacy both is saved; select one destination explicitly to migrate. No automatic change.";
     editor.Restore(map,restoreKey,RenderMapping);
    });
    Health();
   }
   void Health(){
    UpdateLivePose();
    var target=options;healthReader.Request(target.HealthFile,delegate(Action apply){Dispatcher.BeginInvoke(apply);},delegate(HealthReadResult result){if(!closed&&object.ReferenceEquals(target,options))ApplyHealth(result);});
   }
   void ApplyHealth(HealthReadResult result){try{
    if(result.Error!=null){healthAvailable=false;healthStatus.Text="Bridge health unavailable; previous values retained. "+result.Error;UpdateLivePose();return;}
    string selectedSource=null,selectedDevice=null,selectedPublisher=null;
    if(devices.SelectedIndex>=0&&devices.SelectedIndex<observations.Count){var selected=(Dictionary<string,object>)observations[devices.SelectedIndex];selectedSource=(string)selected["source"];selectedDevice=(string)selected["device"];selectedPublisher=selected.ContainsKey("publisher_id")?(string)selected["publisher_id"]:null;}
    var h=result.Value;discoveryHealth=h;var next=new ArrayList((ICollection)h["devices"]);devices.Items.Clear();observations=next;int restore=-1;int index=0;
    healthSnapshotUtc=result.SnapshotUtc;healthAvailable=true;currentPolicy=Convert.ToString(h["policy"],CultureInfo.InvariantCulture);
    healthRevision=h.ContainsKey("mapping_revision")?h["mapping_revision"]:null;healthPublisher=h.ContainsKey("publisher_id")?Convert.ToString(h["publisher_id"],CultureInfo.InvariantCulture):null;
    poseTimeoutMs=h.ContainsKey("pose_timeout_ms")?Convert.ToDouble(h["pose_timeout_ms"],CultureInfo.InvariantCulture):500;
    foreach(Dictionary<string,object> d in observations){
     var tracking=d.ContainsKey("tracking_state")?Convert.ToString(d["tracking_state"],CultureInfo.InvariantCulture):"unknown";
     var pos=d.ContainsKey("position_valid")&&Convert.ToBoolean(d["position_valid"],CultureInfo.InvariantCulture);
     var rot=d.ContainsKey("orientation_valid")&&Convert.ToBoolean(d["orientation_valid"],CultureInfo.InvariantCulture);
     devices.Items.Add(d["source"]+" / "+d["device"]+" | "+d["tracker"]+" | tracking="+tracking+" | pos="+pos+" rot="+rot+" | snapshot fresh="+d["fresh"]);
     var publisher=d.ContainsKey("publisher_id")?(string)d["publisher_id"]:null;
     if(selectedPublisher==publisher&&selectedSource==(string)d["source"]&&selectedDevice==(string)d["device"])restore=index;++index;
    }
    if(restore>=0)devices.SelectedIndex=restore;
    UpdateLivePose();
   }catch(Exception e){healthAvailable=false;healthStatus.Text="Bridge health unavailable: "+e.Message;UpdateLivePose();}}
   void UpdateLivePose(){
    RefreshDiscovery();
    bool same=config!=null&&healthPublisher==Convert.ToString(config["bridge_id"],CultureInfo.InvariantCulture);
    bool current=healthAvailable&&same&&HealthPresentation.Current(healthSnapshotUtc,DateTime.UtcNow);
    if(config!=null)mappingStatus.Text=HealthPresentation.MappingRevision(config["mapping_revision"],healthRevision,current,same);
    if(healthAvailable)healthStatus.Text="Health="+(!same?"INSTANCE MISMATCH (publisher)":current?"current diagnostic snapshot":"stale / unavailable / historical")+" | policy="+(current?currentPolicy:"unknown")+" | runtime process ownership unverified; downstream output unverified";
    if(devices.SelectedIndex<0||devices.SelectedIndex>=observations.Count){livePose.Text="Select a source/device to inspect live pose.";return;}
    var d=(Dictionary<string,object>)observations[devices.SelectedIndex];
    livePose.Text=HealthPresentation.Pose(d,current,(DateTime.UtcNow-healthSnapshotUtc).TotalMilliseconds,poseTimeoutMs);
   }
   void Migrate(){
    RequireSave();
    var route=new Microsoft.Win32.OpenFileDialog{Title="Select the legacy output route file"};if(route.ShowDialog(this)!=true)return;
    var alignment=new Microsoft.Win32.OpenFileDialog{Title="Select the legacy alignment JSON"};if(alignment.ShowDialog(this)!=true)return;
    SaveNativeDraft("migrate "+Quote(route.FileName)+" "+Quote(alignment.FileName),true);
   }
   void SelectObservation(){RequireSave();if(!healthAvailable||!HealthPresentation.Current(healthSnapshotUtc,DateTime.UtcNow)||config==null||healthPublisher!=Convert.ToString(config["bridge_id"],CultureInfo.InvariantCulture))throw new InvalidOperationException("Current matching-instance health is required to adopt an observation");if(devices.SelectedIndex<0)return;var d=(Dictionary<string,object>)observations[devices.SelectedIndex];source.Text=(string)d["source"];device.Text=(string)d["device"];space.Text=(string)d["space"];revision.Text=Convert.ToString(d["revision"],CultureInfo.InvariantCulture);editor.Approved=false;approved.IsChecked=false;status.Text=(editor.SelectedIndex>=0?"Selected mapping will be rebound to this observation on Save mapping. ":"")+"Observed convention: "+d["convention"]+". Select a verified profile before approving output.";}
   void SelectMapping(){editor.Select(map,mappings.SelectedIndex,RenderMapping);}
   void RenderMapping(Dictionary<string,object> b){
    mappings.SelectedIndex=editor.SelectedIndex;
    if(b==null){foreach(var field in new[]{source,device,tracker,space,revision,world})field.Text="";profile.SelectedIndex=-1;approved.IsChecked=false;x.Text=y.Text=z.Text="0";return;}
    source.Text=(string)b["source_id"];device.Text=(string)b["device_id"];tracker.Text=(string)b["tracker_id"];space.Text=(string)b["input_space"];revision.Text=Convert.ToString(b["input_revision"],CultureInfo.InvariantCulture);world.Text=(string)b["world_space"];
    profile.SelectedIndex=-1;if(profile.Items.Contains((string)b["profile"]))profile.SelectedItem=(string)b["profile"];
    if(profile.SelectedItem==null)status.Text="Unknown profile: "+b["profile"]+". Select an existing profile before saving or viewing approval.";
    approved.IsChecked=editor.Approved;var t=(IList)((Dictionary<string,object>)b["world"])["translation"];x.Text=Convert.ToString(t[0],CultureInfo.InvariantCulture);y.Text=Convert.ToString(t[1],CultureInfo.InvariantCulture);z.Text=Convert.ToString(t[2],CultureInfo.InvariantCulture);
   }
   void SaveMapping(){
    RequireSave();if(config==null||configBytes==null)throw new InvalidOperationException("Load the configuration first");
    MappingEditor.RequireProfile(config,profile.SelectedItem as string);
    var edit=new Dictionary<string,object>{{"source_id",source.Text},{"device_id",device.Text},{"tracker_id",tracker.Text},{"profile",profile.SelectedItem as string},{"input_space",space.Text},{"input_revision",UInt32.Parse(revision.Text)},{"world_space",world.Text},{"space_approved",approved.IsChecked==true}};
    var updated=MappingEditor.Candidate(config,editor.SelectedIndex,edit);
    var target=options;var expected=(byte[])configBytes.Clone();RunSave(delegate(CancellationToken cancel){Commit(target,expected,updated,cancel);},MappingEditorState.Key(edit));
   }
   void SaveNativeDraft(string arguments,bool migration){
    RequireSave();if(configBytes==null)throw new InvalidOperationException("Load the configuration first");
    var target=options;var expected=(byte[])configBytes.Clone();RunSave(delegate(CancellationToken cancel){new ConfigHelperClient(target).TransformAndCommit(expected,arguments,migration,cancel);},editor.SelectedKey);
   }
   void RunSave(Action<CancellationToken> operation,Tuple<string,string> selection,Action onSaved=null){
    RequireSave();busy=true;cancellation=new CancellationTokenSource();var pending=cancellation;RefreshAvailability();status.Text="Saving; cancellation is checked before native commit.";
    pendingSave=Task.Run(delegate{operation(pending.Token);});pendingSave.ContinueWith(delegate(Task task){
     if(closed){pending.Dispose();return;}
     Dispatcher.BeginInvoke((Action)delegate{
      pending.Dispose();cancellation=null;if(closed)return;busy=false;
      if(task.IsFaulted){needsReload=true;RefreshAvailability();var error=task.Exception.GetBaseException();status.Text=error is OperationCanceledException?"Cancelled before native commit. Reload and review.":"Save rejected or outcome unconfirmed: "+error.Message+" Reload/review before retry. No automatic rollback.";}
      else {try{if(onSaved!=null)onSaved();Reload(selection);status.Text="SAVED. Runtime application unconfirmed; Core/IK/SteamVR output unverified. Review matching-instance health separately. Rebinding may require an owned runtime restart (unsupported here).";}catch(Exception e){needsReload=true;RefreshAvailability();status.Text="SAVED; reload failed: "+e.Message+". Runtime application unconfirmed.";}}
     });
    });
   }
  }
 }
}
