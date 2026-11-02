// Reuse D1's actual-DLL loader, strict SDK Host, fixture/codec and lifecycle.
// No product Provider or TrackerDevice source is compiled into this executable.
#define main inherited_d1_main
#include "../provider_host/provider_host.cpp"
#undef main

std::string Model="{monaka_bridge}pico_motion_tracker";
mb::c1::MtpPose pico(){auto p=full();p.input.device_id="physical-pico";p.input.session_id=S1;p.coordinate_space={"world","rh_y_up_neg_z_forward",1};return p;}
mb::c1::MtpPose invalidPose(mb::c1::MtpPose p,bool diagnostic=false){
 p.modality="none";p.validity={false,false};p.tracking_state="lost";
 if(!diagnostic){p.position.reset();p.orientation.reset();}
 p.linear_velocity.reset();p.angular_velocity.reset();p.linear_acceleration.reset();p.confidence.position=p.confidence.orientation=0;
 return p;
}
void reacquireSuite(Session& run,mb::Udp& sender,mb::c1::MtpPose& p,Json& result){
 auto& device=run.host.devices.at(1);auto* const pointer=device.driver;const auto prop=vr::Prop_RenderModelName_String;
 auto visible=[&]{require(device.properties.at(prop)==Model,"recovery did not restore model");validFull(run.pose(),p);};
 auto stable=[&]{const auto writes=device.propertyWrites[prop];run.pump(20);require(device.propertyWrites[prop]==writes,"redundant model property write");};
 auto recover=[&]{++p.sequence;p.timestamp_ns+=50000000;p.sent_at_ns=p.timestamp_ns;p.position=mb::Vec{.1*p.sequence,.4,.5};p.orientation=mb::Quat{0,0,0,1};send(sender,run.module.events,p);run.pump(10);visible();stable();};
 Json cases=Json::object();visible();stable();cases["continuous-valid-no-rewrite"]="PASS";
 for(int repeat=0;repeat<3;++repeat)for(const std::string kind:{"lost","expiry","invalid-pose","rotation-only","disconnect"}){
  const auto writes=device.propertyWrites[prop];
  if(kind=="expiry")run.pump(520);
  else if(kind=="disconnect"){
   auto state=fixture<mb::c1::MtpTrackerState>("tracker-state.json");state.publisher_id=p.publisher_id;state.source_id=p.source_id;state.tracker_id=p.tracker_id;
   state.session_id=p.session_id;state.clock_id=p.clock_id;state.mapping_revision=p.mapping_revision;state.sequence=repeat;
   state.timestamp_ns=p.timestamp_ns;state.sent_at_ns=state.timestamp_ns;state.modality="none";state.presence="absent";state.tracking_state="disconnected";
   send(sender,run.module.events,state);run.pump(10);
  }else{
   auto lost=invalidPose(p,kind=="invalid-pose");lost.sequence=++p.sequence;
   if(kind=="rotation-only"){
    lost.modality="rotation_only";lost.validity.orientation=true;lost.orientation=p.orientation;lost.confidence.orientation=p.confidence.orientation;lost.tracking_state="degraded";
   }
   send(sender,run.module.events,lost);run.pump(10);
  }
  require(!run.pose().poseIsValid&&device.properties.at(prop)=="","loss did not hide model");
  require(device.propertyWrites[prop]==writes+1,"hidden edge not written once");stable();recover();
  require(device.propertyWrites[prop]==writes+2,"visible edge not written once");
  require(run.host.devices.size()==1&&run.host.attempts.at(Serial)==1&&device.driver==pointer,"recovery recreated logical device");
  cases[kind+"-recovery-"+std::to_string(repeat+1)]="PASS";
 }
 // A failed property write must remain pending and must not stop pose output.
 for(bool failHide:{false,true}){
  auto lost=invalidPose(p);lost.sequence=++p.sequence;
  if(failHide)run.host.rejectRenderModelWrites=10000;
  send(sender,run.module.events,lost);run.pump(10);require(!run.pose().poseIsValid,"model failure suppressed invalid Pose");
  if(failHide){require(device.properties.at(prop)==Model,"failed hide was marked applied");run.host.rejectRenderModelWrites=0;run.pump(10);}
  require(device.properties.at(prop)=="","hide retry failed");
  run.host.rejectRenderModelWrites=10000;++p.sequence;p.timestamp_ns+=50000000;p.sent_at_ns=p.timestamp_ns;
  const auto attempts=device.propertyAttempts[prop],poses=device.poses;send(sender,run.module.events,p);run.pump(10);validFull(run.pose(),p);
  require(device.poses>poses&&device.propertyAttempts[prop]>attempts&&device.properties.at(prop)=="","property failure affected pose or applied cache");
  run.host.rejectRenderModelWrites=0;run.pump(10);visible();stable();
 }
 cases["property-failure-isolated-retry"]="PASS";
 // SteamVR may reuse a registered object across Deactivate/Activate.
 device.driver->Deactivate();device.stage=pt::StrictHost::Stage::Deactivated;
 require(!device.driver->GetPose().poseIsValid,"deactivate retained valid Pose");
 run.host.expectedActivationRenderModels[Serial]="";device.stage=pt::StrictHost::Stage::Pending;run.host.activatePending();
 require(device.properties.at(prop)=="","reactivation reused stale applied state");run.pump(10);visible();stable();
 require(device.driver==pointer&&run.host.attempts.at(Serial)==1,"reactivation registered another device");
 cases["deactivate-reactivate"]="PASS";result["reacquire_cases"]=cases;
}

int main(int argc,char** argv){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOOPENFILEERRORBOX|SEM_NOGPFAULTERRORBOX);
 if(argc!=5&&argc!=6)return 2;
 if(argc==6)Model=argv[5];
 const fs::path dll=fs::absolute(argv[1]),out=fs::absolute(argv[2]);const std::string mode=argv[3],mutation=argv[4];
 require(!fs::exists(out),"fresh D2 output required");fs::create_directories(out);pt::Events events((out/"host-events.jsonl").string());events.scenario="D2-05/06/07/08/09";
 Json result={{"result","FAIL"},{"mode",mode},{"mutation",mutation},{"driver_sha256",sha256(dll)},{"physical_hil","NOT RUN"}};
 try{
  try{PortReservation preflight;}
  catch(const std::exception& e){result["result"]="BLOCKED";result["reason"]=e.what();std::ofstream(out/"result.json")<<result.dump(2);return 77;}
  {
   Module module(dll,events);Session run(module);require(run.error==vr::VRInitError_None,"D2 Init");
   const bool enabled=mode=="two-assigned"||mode=="pico"||mode=="revoke"||mode=="mixed"||mode=="reacquire"||mode=="initial-hidden";
   if(enabled)run.host.expectedRenderModels[Serial]=Model;
   run.host.activateSynchronously=mutation=="activate-inline";
   if(mode=="initial-hidden")run.host.expectedActivationRenderModels[Serial]="";
   mb::Udp sender;auto p=pico();
   if(mutation=="publisher")p.publisher_id="different-publisher";
   if(mutation=="source")p.source_id=p.input.source_id="different-source";
   if(mutation=="device")p.input.device_id="ALTRA-device";
   if(mutation=="tracker")p.tracker_id="different-tracker";
   if(mutation=="revision")p.mapping_revision=2;
   if(mutation=="session")p.session_id=p.clock_id=S2;
   if(mutation=="input-session")p.input.session_id=S2;
   if(mutation=="session-reapproved")p.session_id=p.clock_id=S2;
   auto initial=p;
   if(mode=="initial-hidden"){
    if(mutation=="expiry")initial.sent_at_ns+=700000000;
    else initial=invalidPose(p);
   }
   send(sender,events,initial);run.until([&]{return run.host.totalPoses>0;});
   if(mode!="initial-hidden")validFull(run.pose(),p);
   const auto& props=run.host.devices.at(1).properties;
   if(mode=="initial-hidden"){
    require(!run.pose().poseIsValid&&props.at(vr::Prop_RenderModelName_String)=="","initial invalid sample displayed model");
    ++p.sequence;send(sender,events,p);run.pump(10);validFull(run.pose(),p);
    require(props.at(vr::Prop_RenderModelName_String)==Model,"first recovery did not restore model");
   }
   else if(enabled)require(props.at(vr::Prop_RenderModelName_String)==Model,"PICO property missing");
   else require(!props.count(vr::Prop_RenderModelName_String),"generic/unknown received PICO model");
   if(mode=="reacquire"){
    auto lost=p;lost.sequence++;lost.modality="none";lost.validity={false,false};lost.tracking_state="lost";
    lost.position.reset();lost.orientation.reset();lost.linear_velocity.reset();lost.angular_velocity.reset();lost.linear_acceleration.reset();
    lost.confidence.position=lost.confidence.orientation=0;
    send(sender,events,lost);run.pump(10);
    require(!run.pose().poseIsValid&&props.at(vr::Prop_RenderModelName_String)=="","loss did not hide model");
    p.sequence+=2;p.position=mb::Vec{.3,.4,.5};send(sender,events,p);run.pump(10);validFull(run.pose(),p);
    require(props.at(vr::Prop_RenderModelName_String)==Model,"recovery did not restore model");
    require(run.host.devices.size()==1&&run.host.attempts.at(Serial)==1,"recovery recreated logical device");
    reacquireSuite(run,sender,p,result);
   }
   if(mode=="two-assigned"){
    auto other=p;other.source_id=other.input.source_id="unknown";other.input.device_id="anonymous-device";
    const std::string otherSerial="monaka-direct:313a70756e6b6e6f776e:74";
    run.host.expectedRenderModels[otherSerial]=Model;
    send(sender,events,other);run.until([&]{return run.host.devices.size()==2&&run.host.devices.at(2).poses>0;});
    require(run.host.devices.at(2).properties.at(vr::Prop_RenderModelName_String)==Model,"explicit generic identity did not receive same resource");
    validFull(run.host.devices.at(2).pose,other);
    auto lost=invalidPose(other);++lost.sequence;send(sender,events,lost);run.pump(10);
    require(!run.host.devices.at(2).pose.poseIsValid&&run.host.devices.at(2).properties.at(vr::Prop_RenderModelName_String)=="","second identity loss failed");
    other.sequence+=2;send(sender,events,other);run.pump(10);validFull(run.host.devices.at(2).pose,other);
    require(run.host.devices.at(2).properties.at(vr::Prop_RenderModelName_String)==Model,"second identity recovery failed");
    result["equivalent_synthetic_identities"]="PASS";
   }
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
    const bool retained=mutation=="generation"||mutation=="input-restart"||mutation=="backend-restart";
    require(props.at(vr::Prop_RenderModelName_String)==(retained?Model:""),"presentation identity/reconnect state");
    if(mutation!="expiry")validFull(run.pose(),p);
    // Restoring an old matching tuple must not silently regrant appearance.
    auto old=pico();old.sequence=100;send(sender,events,old);run.pump(10);
    require(props.at(vr::Prop_RenderModelName_String)==(retained?Model:""),"old sample changed presentation state");
    require(run.host.devices.size()==1,"presentation changed registration count");
   }
   result["pose_updates"]=run.host.totalPoses;result["registered_devices"]=run.host.devices.size();
   result["properties"]=Json::object();for(const auto& [id,value]:props)result["properties"][std::to_string(int(id))]=value;
   run.finish();
   if(mode=="reacquire"){
    Session restarted(module);require(restarted.error==vr::VRInitError_None,"fixture restart Init");restarted.host.expectedRenderModels[Serial]=Model;
    auto fresh=pico();send(sender,events,fresh);restarted.until([&]{return restarted.host.totalPoses>0;});validFull(restarted.pose(),fresh);
    require(restarted.host.devices.at(1).properties.at(vr::Prop_RenderModelName_String)==Model,"fixture restart model");
    restarted.finish();result["reacquire_cases"]["driver-fixture-restart"]="PASS";
   }
   module.unload();events.write("FreeLibrary",{{"returned",true}});
  }
  {PortReservation released;}
  result["result"]="PASS";result["cleanup"]={{"exclusive_rebind",true},{"contexts_retired",true},{"child_exit",true}};
 }catch(const std::exception& e){result["error"]=e.what();events.write("Failure",{{"message",e.what()}});}
 std::ofstream(out/"result.json")<<result.dump(2);std::cout<<result.dump()<<'\n';return result["result"]=="PASS"?0:1;
}
