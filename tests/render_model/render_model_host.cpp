// Reuse D1's actual-DLL loader, strict SDK Host, fixture/codec and lifecycle.
// No product Provider or TrackerDevice source is compiled into this executable.
#define main inherited_d1_main
#include "../provider_host/provider_host.cpp"
#undef main

const std::string Model="{monaka_bridge}pico_motion_tracker";
mb::c1::MtpPose pico(){auto p=full();p.input.device_id="physical-pico";p.input.session_id=S1;p.coordinate_space={"world","rh_y_up_neg_z_forward",1};return p;}

int main(int argc,char** argv){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOOPENFILEERRORBOX|SEM_NOGPFAULTERRORBOX);
 if(argc!=5)return 2;
 const fs::path dll=fs::absolute(argv[1]),out=fs::absolute(argv[2]);const std::string mode=argv[3],mutation=argv[4];
 require(!fs::exists(out),"fresh D2 output required");fs::create_directories(out);pt::Events events((out/"host-events.jsonl").string());events.scenario="D2-05/06/07/08/09";
 Json result={{"result","FAIL"},{"mode",mode},{"mutation",mutation},{"driver_sha256",sha256(dll)},{"physical_hil","NOT RUN"}};
 try{
  try{PortReservation preflight;}
  catch(const std::exception& e){result["result"]="BLOCKED";result["reason"]=e.what();std::ofstream(out/"result.json")<<result.dump(2);return 77;}
  {
   Module module(dll,events);Session run(module);require(run.error==vr::VRInitError_None,"D2 Init");
   const bool enabled=mode=="pico"||mode=="revoke"||mode=="mixed";
   if(enabled)run.host.expectedRenderModels[Serial]=Model;
   mb::Udp sender;auto p=pico();
   if(mutation=="publisher")p.publisher_id="different-publisher";
   if(mutation=="source")p.source_id=p.input.source_id="different-source";
   if(mutation=="device")p.input.device_id="ALTRA-device";
   if(mutation=="tracker")p.tracker_id="different-tracker";
   if(mutation=="revision")p.mapping_revision=2;
   if(mutation=="session")p.session_id=p.clock_id=S2;
   if(mutation=="input-session")p.input.session_id=S2;
   send(sender,events,p);run.until([&]{return run.host.totalPoses>0;});validFull(run.pose(),p);
   const auto& props=run.host.devices.at(1).properties;
   if(enabled)require(props.at(vr::Prop_RenderModelName_String)==Model,"PICO property missing");
   else require(!props.count(vr::Prop_RenderModelName_String),"generic/unknown received PICO model");
   if(mode=="mixed"){
    auto other=p;other.source_id=other.input.source_id="altra";other.input.device_id="ALTRA-device";
    send(sender,events,other);run.until([&]{return run.host.devices.size()==2&&run.host.devices.at(2).poses>0;});
    require(!run.host.devices.at(2).properties.count(vr::Prop_RenderModelName_String),"PICO model leaked to Altra");
    validFull(run.host.devices.at(2).pose,other);
   }
   if(mode=="revoke"){
    p.sequence++;
    if(mutation=="rebind")p.input.device_id="ALTRA-replacement";
    else if(mutation=="generation")p.mapping_revision=2;
    else if(mutation=="input-restart")p.input.session_id=S2;
    else if(mutation=="expiry")run.pump(510);
    else if(mutation=="backend-restart"){run.pump(510);p.session_id=p.clock_id=S2;p.sequence=0;}
    if(mutation!="expiry")send(sender,events,p);
    run.pump(10);
    require(props.at(vr::Prop_RenderModelName_String)=="","old PICO model was inherited");
    if(mutation!="expiry")validFull(run.pose(),p);
    // Restoring an old matching tuple must not silently regrant appearance.
    auto old=pico();old.sequence=100;send(sender,events,old);run.pump(10);
    require(props.at(vr::Prop_RenderModelName_String)=="","revoked appearance was reapplied");
    require(run.host.devices.size()==1,"presentation changed registration count");
   }
   result["pose_updates"]=run.host.totalPoses;result["registered_devices"]=run.host.devices.size();
   result["properties"]=Json::object();for(const auto& [id,value]:props)result["properties"][std::to_string(int(id))]=value;
   run.finish();module.unload();events.write("FreeLibrary",{{"returned",true}});
  }
  {PortReservation released;}
  result["result"]="PASS";result["cleanup"]={{"exclusive_rebind",true},{"contexts_retired",true},{"child_exit",true}};
 }catch(const std::exception& e){result["error"]=e.what();events.write("Failure",{{"message",e.what()}});}
 std::ofstream(out/"result.json")<<result.dump(2);std::cout<<result.dump()<<'\n';return result["result"]=="PASS"?0:1;
}
