using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using Forms = System.Windows.Forms;

namespace MonakaBridge {
 internal sealed class SteamVrControlTray : IDisposable {
  readonly GuiLaunchOptions options;
  readonly Forms.ApplicationContext context=new Forms.ApplicationContext();
  readonly Forms.NotifyIcon icon;
  readonly Forms.ToolStripMenuItem statusItem=new Forms.ToolStripMenuItem{Enabled=false};
  readonly Dictionary<string,Forms.ToolStripMenuItem> policies=new Dictionary<string,Forms.ToolStripMenuItem>();
  readonly HealthReader reader=new HealthReader();
  readonly Forms.Timer timer=new Forms.Timer{Interval=1000};
  readonly Forms.Control dispatcher=new Forms.Control();
  HealthReadResult health;
  Process controlProcess;
  bool disposed,busy,needsReload;string helperIssue;
  internal SteamVrControlTray(string root):this(GuiLaunchOptions.FromRoot(root)){}
  internal SteamVrControlTray(GuiLaunchOptions target){
   options=target;dispatcher.CreateControl();var menu=new Forms.ContextMenuStrip();
   menu.Items.Add(new Forms.ToolStripMenuItem(options.InstallationName){Enabled=false});menu.Items.Add(statusItem);
   var output=new Forms.ToolStripMenuItem("Save output policy");
   foreach(string value in new[]{"steamvr","monaka","both","disabled"}){
    string selected=value;var item=new Forms.ToolStripMenuItem(value);
    item.Click+=delegate{SetRoute(selected);};policies.Add(value,item);output.DropDownItems.Add(item);
   }
   menu.Items.Add(output);
   foreach(string operation in new[]{"Start","Stop","Restart"})menu.Items.Add(new Forms.ToolStripMenuItem(operation+" Bridge (unsupported)"){Enabled=false,ToolTipText=options.ServiceBlockReason()});
   menu.Items.Add("Open Bridge Control...",null,delegate{OpenControlWindow();});
   menu.Items.Add("Reload target / review status",null,delegate{needsReload=false;RefreshTarget();});
   menu.Items.Add("Exit",null,delegate{Exit();});
   icon=new Forms.NotifyIcon{Icon=System.Drawing.SystemIcons.Application,Text="Monaka Bridge",ContextMenuStrip=menu,Visible=true};
   icon.DoubleClick+=delegate{OpenControlWindow();};menu.Opening+=delegate{RefreshTarget();};
   timer.Tick+=delegate{ReadHealth();};timer.Start();RefreshTarget();ReadHealth();
  }
  internal void Run(){OpenControlWindow();Forms.Application.Run(context);}
  void RefreshTarget(){helperIssue=options.ValidateEditing();RefreshRouteUi();}
  void ReadHealth(){reader.Request(options.HealthFile,delegate(Action apply){dispatcher.BeginInvoke(apply);},delegate(HealthReadResult result){health=result;RefreshRouteUi();});}
  void RefreshRouteUi(){
   if(disposed)return;
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
   statusItem.Text="Saved: "+requested+" | health: "+diagnostic+" | runtime ownership unverified";
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
   try{
    if(busy)throw new InvalidOperationException("Wait for the pending tray save");
    if(ControlAlive()){icon.ShowBalloonTip(1500,"Monaka Bridge","Bridge Control is already open.",Forms.ToolTipIcon.Info);return;}
    if(controlProcess!=null)controlProcess.Dispose();
    controlProcess=Process.Start(new ProcessStartInfo(Process.GetCurrentProcess().MainModule.FileName,options.WindowArguments()){UseShellExecute=false,CreateNoWindow=true});RefreshRouteUi();
   }catch(Exception e){Forms.MessageBox.Show(e.Message,"Bridge Control",Forms.MessageBoxButtons.OK,Forms.MessageBoxIcon.Error);}
  }
  void Exit(){context.ExitThread();}
  public void Dispose(){if(disposed)return;disposed=true;timer.Stop();timer.Dispose();reader.Dispose();icon.Visible=false;icon.Dispose();dispatcher.Dispose();context.Dispose();if(controlProcess!=null)controlProcess.Dispose();}
 }
}
