using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Controls;
namespace MonakaBridge {
 // All new user-visible Discovery/Rebind strings are isolated in discovery_*.cs.
 // Existing control/calibration wording is owned by the parallel localization.
 internal sealed class DiscoveryPanel:StackPanel {
  internal readonly ListBox Candidates=new ListBox{Height=100},Slots=new ListBox{Height=70};
  internal readonly ComboBox Profiles=new ComboBox(),Contracts=new ComboBox();
  internal readonly TextBox Logical=new TextBox(),Details=new TextBox{IsReadOnly=true,TextWrapping=TextWrapping.Wrap,AcceptsReturn=true,MaxHeight=150,VerticalScrollBarVisibility=ScrollBarVisibility.Auto};
  internal readonly TextBlock State=new TextBlock{TextWrapping=TextWrapping.Wrap};
  internal readonly Button Create=new Button{Content=BridgeText.T("Create new logical tracker")},Replace=new Button{Content=BridgeText.T("Replace selected offline slot")},Revision=new Button{Content=BridgeText.T("Update this known input revision")},Ignore=new Button{Content=BridgeText.T("Ignore (no configuration write)")};
  internal DiscoveryPanel(){
   Children.Add(new TextBlock{Text=BridgeText.T("Unmapped discovery / explicit rebind"),FontSize=18});
   Children.Add(new TextBlock{Text=BridgeText.T("Canonical decoded claims only; not admitted tracking or verified device support. Known input-frame changes are listed separately."),TextWrapping=TextWrapping.Wrap});
   Children.Add(State);Children.Add(Candidates);Field("Defined coordinate profile",Profiles);Field("Declared static capabilities / saved contract",Contracts);Field("New logical tracker ID (Create only)",Logical);Field("Persisted slot (Replace / revision update)",Slots);
   var actions=new WrapPanel();foreach(var b in new[]{Create,Replace,Revision,Ignore}){b.Margin=new Thickness(3);b.MinHeight=28;actions.Children.Add(b);}Children.Add(actions);Children.Add(Details);
  }
  static void Field(Panel p,string label,Control control){p.Children.Add(new TextBlock{Text=BridgeText.T(label)});p.Children.Add(control);}
  void Field(string label,Control control){Field(this,label,control);}
  internal void Enable(bool writable,string reason){foreach(Control c in new Control[]{Create,Replace,Revision,Profiles,Contracts,Slots,Logical}){c.IsEnabled=writable;c.ToolTip=reason;}Ignore.IsEnabled=true;}
 }
}
