using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Forms = System.Windows.Forms;

namespace MonakaBridge {
 internal sealed class SteamVrControlTray : IDisposable {
  readonly GuiLaunchOptions options;
  readonly Forms.ApplicationContext context=new Forms.ApplicationContext();
  readonly Forms.NotifyIcon icon;
  System.Drawing.Icon trayImage;bool lightTaskbar;
  readonly Forms.ToolStripMenuItem statusItem=new Forms.ToolStripMenuItem{Enabled=false};
  readonly Dictionary<string,Forms.ToolStripMenuItem> policies=new Dictionary<string,Forms.ToolStripMenuItem>();
  readonly HealthReader reader=new HealthReader();
  readonly Forms.Timer timer=new Forms.Timer{Interval=1000};
  readonly Forms.Control dispatcher=new Forms.Control();
  HealthReadResult health;
  Process controlProcess;
  bool disposed,busy,needsReload;string helperIssue;
  readonly PcServiceLifecycle.Control serviceControl;
  readonly EventWaitHandle openRequest;
  readonly Forms.ToolStripMenuItem exitItem;
  readonly bool showWindow;
  readonly string serviceArguments;
  readonly BridgeOwnerChannel ownerChannel;
  bool exitPending;string lifecycleIssue;
  internal SteamVrControlTray(GuiLaunchOptions target,string serviceExecutable,bool openWindow)
   :this(target,serviceExecutable,openWindow,@"Local\MonakaBridge_OpenControl_29810"){}
  internal SteamVrControlTray(GuiLaunchOptions target,string serviceExecutable,bool openWindow,string openRequestName)
   :this(target,serviceExecutable,openWindow,openRequestName,null,null){}
  internal SteamVrControlTray(GuiLaunchOptions target,string serviceExecutable,bool openWindow,string openRequestName,string ports,string logs){
   showWindow=openWindow;
   var ingress=ports==null?"29810":ports.Split(' ')[0];
   serviceArguments=GuiLaunchOptions.Quote(target.ConfigFile)+(ports==null?"":" "+ports);
   serviceControl=new PcServiceLifecycle.Control(@"Local\MonakaBridge_ServiceLifecycle_"+ingress,serviceExecutable,
    logs??Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"MonakaBridge","service-logs"),ports!=null);
   if(ports!=null){ownerChannel=new BridgeOwnerChannel(target,delegate(string op){if(exitPending)throw new IOException("Tray exit pending");return serviceControl.Command(op,serviceArguments,5000);});}
   openRequest=new EventWaitHandle(false,EventResetMode.AutoReset,openRequestName);
   options=target;dispatcher.CreateControl();var menu=new Forms.ContextMenuStrip();
   menu.Items.Add(new Forms.ToolStripMenuItem(options.InstallationName){Enabled=false});menu.Items.Add(statusItem);
   var output=new Forms.ToolStripMenuItem("Save output policy");
   foreach(string value in new[]{"steamvr","monaka","disabled"}){
    string selected=value;var item=new Forms.ToolStripMenuItem(value);
    item.Click+=delegate{SetRoute(selected);};policies.Add(value,item);output.DropDownItems.Add(item);
   }
   menu.Items.Add(output);
   menu.Items.Add("Open Bridge Control...",null,delegate{OpenControlWindow();});
   menu.Items.Add("Reload target / review status",null,delegate{needsReload=false;RefreshTarget();});
   exitItem=new Forms.ToolStripMenuItem("Exit");exitItem.Click+=delegate{Exit();};menu.Items.Add(exitItem);
   lightTaskbar=BridgeIcons.LightTaskbar();trayImage=BridgeIcons.TrayIcon();
   icon=new Forms.NotifyIcon{Icon=trayImage,Text="Monaka Bridge",ContextMenuStrip=menu,Visible=true};
   icon.DoubleClick+=delegate{OpenControlWindow();};menu.Opening+=delegate{RefreshTarget();};
   timer.Tick+=delegate{RefreshIcon();ReadHealth();if(openRequest.WaitOne(0))OpenControlWindow();};timer.Start();RefreshTarget();ReadHealth();
  }
  internal void Run(){
   ThreadPool.QueueUserWorkItem(delegate{
    string error=null;try{serviceControl.EnsureRunning(serviceArguments);}catch(Exception e){error=e.Message;}
    try{dispatcher.BeginInvoke((Action)delegate{
     lifecycleIssue=error;RefreshRouteUi();
     if(error!=null)icon.ShowBalloonTip(5000,"Monaka Bridge",error,Forms.ToolTipIcon.Error);
     if(showWindow)OpenControlWindow();
    });}catch(InvalidOperationException){}
   });
   Forms.Application.Run(context);
  }
  void RefreshIcon(){
   bool light=BridgeIcons.LightTaskbar();if(light==lightTaskbar)return;
   var next=BridgeIcons.TrayIcon();var previous=trayImage;
   icon.Icon=next;trayImage=next;lightTaskbar=light;previous.Dispose();
  }
  void RefreshTarget(){helperIssue=options.ValidateEditing();RefreshRouteUi();}
  void ReadHealth(){reader.Request(options.HealthFile,delegate(Action apply){dispatcher.BeginInvoke(apply);},delegate(HealthReadResult result){health=result;RefreshRouteUi();});}
  void RefreshRouteUi(){
   if(disposed)return;
   if(exitPending){statusItem.Text="Stopping Bridge; tray retained until process exit is confirmed";return;}
   if(lifecycleIssue!=null){statusItem.Text="Service control: "+lifecycleIssue;return;}
   try{RefreshRoutePresentation();}catch(Exception e){
    // Diagnostic/presentation failures never escape a WinForms callback and
    // cannot establish runtime application or write permission.
    foreach(var item in policies)item.Value.Enabled=false;
    statusItem.Text="Saved: unavailable | health: unavailable / historical | runtime ownership unverified";
    statusItem.ToolTipText="Presentation unavailable: "+e.Message.Substring(0,Math.Min(e.Message.Length,256));
   }
  }
  void RefreshRoutePresentation(){
   string requested="unavailable",publisher=null;
   try{var config=SteamVrControlWpf.Read(options.ConfigFile);requested=Convert.ToString(config["policy"],CultureInfo.InvariantCulture);publisher=Convert.ToString(config["bridge_id"],CultureInfo.InvariantCulture);}catch{}
   string reason=ControlAlive()?"Owned Bridge Control window is open; save there or close it first":busy?"Save in progress":needsReload?"Outcome requires reload/review":options.EditBlockReason()??helperIssue;
   foreach(var item in policies){item.Value.Checked=item.Key==requested;item.Value.Enabled=reason==null;item.Value.ToolTipText=reason??"Save through the selected native helper; runtime application remains unconfirmed";}
   string diagnostic="unavailable / historical";
   string observedPolicy;
   if(health!=null&&health.Error==null&&HealthPresentation.TryPolicy(health.Value,out observedPolicy)){
    object id;bool same=health.Value.TryGetValue("publisher_id",out id)&&id is string&&publisher!=null&&publisher==(string)id;
    diagnostic=!same?"INSTANCE MISMATCH":!HealthPresentation.Current(health.SnapshotUtc,DateTime.UtcNow)?"stale / historical":"policy="+observedPolicy+" (diagnostic only)";
   }
   statusItem.Text="Saved: "+requested+(requested=="both"?" (legacy; select one destination to migrate)":"")+" | health: "+diagnostic+" | runtime ownership unverified";
   statusItem.ToolTipText=options.ConfigFile+"\n"+options.HealthFile+"\n"+(reason??"Writable; native commit checks directory permission/lock/CAS");
  }
  void SetRoute(string policy){
   try{
    RequireRouteChange();
    if(!policies.ContainsKey(policy))throw new ArgumentException("Unsupported policy");
    byte[] expected=File.ReadAllBytes(options.ConfigFile);busy=true;RefreshRouteUi();
    Task.Run(delegate{new ConfigHelperClient(options).TransformAndCommit(expected,"policy "+policy,false,CancellationToken.None);}).ContinueWith(delegate(Task task){
     if(disposed)return;
     dispatcher.BeginInvoke((Action)delegate{
      if(disposed)return;busy=false;needsReload=task.IsFaulted;
      string result=task.IsFaulted?"Save rejected or outcome unconfirmed: "+task.Exception.GetBaseException().Message:"SAVED. Runtime application unconfirmed; Core/IK/SteamVR output unverified.";
      RefreshRouteUi();icon.ShowBalloonTip(4000,"Monaka Bridge",result,task.IsFaulted?Forms.ToolTipIcon.Error:Forms.ToolTipIcon.Info);
     });
    });
   }catch(Exception e){Forms.MessageBox.Show(e.Message,"Output policy",Forms.MessageBoxButtons.OK,Forms.MessageBoxIcon.Error);}
  }
  void RequireRouteChange(){options.RequireEdit();if(busy||needsReload||ControlAlive())throw new InvalidOperationException("Reload and review before saving");}
  bool ControlAlive(){return controlProcess!=null&&!controlProcess.HasExited;}
  void OpenControlWindow(){
   if(exitPending)return;
   try{
    if(busy)throw new InvalidOperationException("Wait for the pending tray save");
    if(ControlAlive()){
     if(!OwnedControlWindow.TryActivate(controlProcess))icon.ShowBalloonTip(1500,"Monaka Bridge","Bridge Control could not be brought forward. Select its window from the taskbar.",Forms.ToolTipIcon.Info);
     return;
    }
    if(controlProcess!=null)controlProcess.Dispose();
    controlProcess=Process.Start(new ProcessStartInfo(Process.GetCurrentProcess().MainModule.FileName,options.WindowArguments()){UseShellExecute=false,CreateNoWindow=true});RefreshRouteUi();
   }catch(Exception e){Forms.MessageBox.Show(e.Message,"Bridge Control",Forms.MessageBoxButtons.OK,Forms.MessageBoxIcon.Error);}
  }
  void Exit(){
   if(exitPending)return;
   if(busy){icon.ShowBalloonTip(4000,"Monaka Bridge","Wait for the pending save before Exit",Forms.ToolTipIcon.Warning);return;}
   exitPending=true;exitItem.Enabled=false;RefreshRouteUi();
   ThreadPool.QueueUserWorkItem(delegate{
    string error;
    new PcServiceLifecycle.ExitGate().TryExit(delegate{
     if(ControlAlive()){
      controlProcess.CloseMainWindow();if(!controlProcess.WaitForExit(1000))throw new IOException("Close Bridge Control first; service and tray retained");
     }
     serviceControl.StopAndWait(5000);
    },delegate{
     dispatcher.BeginInvoke((Action)delegate{context.ExitThread();});
    },out error);
    if(error!=null)try{dispatcher.BeginInvoke((Action)delegate{ExitFailed(error);});}catch(InvalidOperationException){}
   });
  }
  void ExitFailed(string error){exitPending=false;exitItem.Enabled=true;lifecycleIssue=error;RefreshRouteUi();icon.ShowBalloonTip(5000,"Bridge stop failed",error,Forms.ToolTipIcon.Error);}
  public void Dispose(){if(disposed)return;disposed=true;if(ownerChannel!=null)ownerChannel.Dispose();timer.Stop();timer.Dispose();reader.Dispose();openRequest.Dispose();serviceControl.Dispose();icon.Visible=false;icon.Dispose();trayImage.Dispose();dispatcher.Dispose();context.Dispose();if(controlProcess!=null)controlProcess.Dispose();}
 }
 internal static class OwnedControlWindow {
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window,out uint processId);
  [DllImport("user32.dll")] static extern bool IsIconic(IntPtr window);
  [DllImport("user32.dll")] static extern bool ShowWindowAsync(IntPtr window,int command);
  [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr window);
  static bool Matches(Process retainedProcess,IntPtr window){
   uint id;return !retainedProcess.HasExited&&GetWindowThreadProcessId(window,out id)!=0&&id==(uint)retainedProcess.Id;
  }
  internal static bool TryActivate(Process retainedProcess){
   if(retainedProcess==null)return false;
   try{
    if(retainedProcess.HasExited)return false;
    retainedProcess.Refresh();var window=retainedProcess.MainWindowHandle;
    if(window==IntPtr.Zero||!Matches(retainedProcess,window))return false;
    if(IsIconic(window)&&Matches(retainedProcess,window))ShowWindowAsync(window,9);
    return Matches(retainedProcess,window)&&SetForegroundWindow(window);
   }catch(InvalidOperationException){return false;}
    catch(System.ComponentModel.Win32Exception){return false;}
  }
 }
}
