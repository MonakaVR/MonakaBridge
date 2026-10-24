// Stored physical PQ, rebased offline witnesses and a TEST_ONLY policy. Never HIL PASS.
// Loads the ordinary product DLL; all UDP sockets are ephemeral loopback.
#define main inherited_provider_main
#include "provider_host/provider_host.cpp"
#undef main
#include "monaka_bridge/output_approval_authority.hpp"
#include "monaka_bridge/config_transaction.hpp"
#include "monaka_bridge/direct.hpp"
namespace lc=mb::local_calibration;
Json read(const fs::path& p){return Json::parse(mb::readConfigBytes(p));}
void write(const fs::path& p,const Json& j){std::ofstream f(p,std::ios::binary);f<<j.dump(2)<<'\n';require(bool(f),"write fixture");}
Json pin(const fs::path& root,const std::string& name,const fs::path& from){fs::copy_file(from,root/name,fs::copy_options::overwrite_existing);return {{"file",name},{"sha256",sha256(root/name)}};}
struct ReplayProvider:lc::CaptureProvider {
 mb::Binding binding;mb::Profile profile;Json witness,raw;std::int64_t seq=0;
 ReplayProvider(const mb::Config& c,Json w,Json samples):binding(c.bindings.begin()->second),profile(c.profiles.at(binding.profile)),witness(std::move(w)),raw(std::move(samples)){}
 bool supported()const override{return true;}
 mb::c1::TrackerObservation observation(std::size_t index,std::int64_t sequence,std::int64_t now)const{
  auto p=sample(binding.source,sequence);p.device_id=binding.device;p.session_id=witness.at("session");p.clock_id=witness.at("clock");
  p.coordinate_space={binding.inputSpace,profile.convention,binding.inputRevision};p.timestamp_ns=p.sent_at_ns=now;
  auto r=raw.at("source").at(index%raw.at("source").size()).at("pose");auto v=r.at("translation_m").get<mb::Vec>();auto q=r.at("rotation_xyzw").get<mb::Quat>();mb::Vec pv{};mb::Quat pq{};
  for(int i=0;i<3;++i){auto a=profile.positionAxes[i];pv[std::abs(a)-1]=a>0?v[i]:-v[i];}
  for(int i=0;i<4;++i){auto a=profile.quaternionAxes[i];pq[std::abs(a)-1]=a>0?q[i]:-q[i];}
  p.position=pv;p.orientation=pq;p.linear_velocity=mb::c1::Derivative{{},"space","derived"};p.capabilities={"position","orientation","linear_velocity"};return p;
 }
 lc::ProviderBatch guard()override{
  const auto t=lc::steadyNow();lc::ProviderBatch b;
  b.identity={binding.source,binding.device,witness.at("session"),witness.at("clock"),binding.inputSpace,profile.convention,"OFFLINE_REPLAY",binding.inputRevision,0,{},witness.at("reference_serial"),"left",witness.at("standing_witness"),witness.at("reference_generation")};
  b.frames={{observation(std::size_t(seq),++seq,t),"OFFLINE_REPLAY",t}};b.referenceReceiptNs=t;b.referenceValid=true;return b;
 }
 lc::ProviderBatch discover(const std::atomic<bool>&)override{return guard();}
 void capture(const std::function<void(const lc::ProviderBatch&)>&,const std::atomic<bool>&)override{throw std::runtime_error("REPLAY_APPROVAL_ONLY");}
};
int main(int argc,char** argv)try{
 require(argc==7,"usage: semantic_hil_rehearsal evidence-root native-result driver-package new-output policy-candidate recorded-evidence");
 auto evidence=fs::absolute(argv[1]),native=fs::absolute(argv[2]),package=fs::absolute(argv[3]),out=fs::absolute(argv[4]);require(!fs::exists(out),"fresh output required");fs::create_directories(out);
 auto saved=evidence/"g3-save-additional-001/native-final";auto raw=read(saved/"raw.json"),result=read(native),witness=read(evidence/"g3-save-additional-001/G3_DISCOVER.json").at("witness");
 auto pre=mb::loadConfig(saved/"expected.json"),current=mb::loadConfig(saved/"candidate.json");require(current.bindings.size()==1,"explicit single saved target");auto b=current.bindings.begin()->second;
 require(result.at("raw_sha256")==sha256(saved/"raw.json")&&result.at("original_result_sha256")==sha256(saved/"result.json")&&result.at("pairs")==323&&result.at("inliers")==323,"native replay provenance");
 require(b.source==witness.at("source")&&b.device==witness.at("device")&&b.inputRevision==witness.at("input_revision")&&!b.spaceApproved,"R0 saved identity");
 auto target=mb::selectWorldCalibrationTarget(pre,b.source,b.device,b.tracker,b.inputSpace,b.inputRevision);
 mb::Rigid W{result.at("world").at("rotation_xyzw").get<mb::Quat>(),result.at("world").at("translation_m").get<mb::Vec>()};
 auto worldCandidate=mb::worldCalibrationCandidate(pre,target,W,true);mb::saveConfig(out/"world-candidate.json",worldCandidate);require(read(out/"world-candidate.json")==read(saved/"candidate.json"),"R2 exact World candidate");
 fs::copy_file(saved/"candidate.json",out/"config.json");auto authority=out/"TEST_ONLY_OutputApproval";fs::create_directories(authority);
 const auto recorded=read(argv[6]);const std::int64_t wall=lc::utcMilliseconds();
 const auto savedWitness=witness;witness["session"]=S2;witness["clock"]=S2; // New process epochs, same declared space in this isolated replay.
 auto policy=read(argv[5]);require(policy.at("schema")=="monaka.physical-criteria.v2"&&policy.at("enabled")==true,"semantic candidate");
 fs::copy_file(fs::absolute(argv[5]),authority/"policy.json");auto policyHash=sha256(authority/"policy.json");
 Json e={{"schema","monaka.physical-evidence.v2"},{"evidence_kind","physical_capture"},{"id","TEST_ONLY_RECORDED_CAPTURE"},{"criteria_id",policy.at("id")},{"criteria_version",policy.at("version")},{"criteria_sha256",policyHash},{"source",b.source},{"device",b.device},{"input_space",b.inputSpace},{"input_revision",b.inputRevision},{"mapping_revision",current.revision},{"world_revision",b.worldRevision},{"world",result.at("world")},{"config_candidate_sha256",sha256(out/"config.json")},{"source_session",savedWitness.at("session")},{"source_clock",savedWitness.at("clock")},{"reference_serial",witness.at("reference_serial")},{"reference_standing",witness.at("standing_witness")},{"reference_generation",witness.at("reference_generation")},{"measured_at_utc_ms",recorded.at("measured_at_utc_ms")},{"lifetime",{{"mode","semantic"}}},{"measurement_mode","trajectory"},{"tool_sha256",sha256(fs::absolute(argv[0]))},{"build_identity","TEST_ONLY_CURRENT_BUILD_REPLAY"},{"measurement_result_id",result.at("result_id")},
 {"semantic_identity",{{"space_generation","TEST_ONLY_SAME_SPACE"},{"attachment_relation","TEST_ONLY_UNCHANGED_FIXTURE"},{"input_convention",current.profiles.at(b.profile).convention},{"reference_role","left"}}}};
 e["config_preimage"]=pin(authority,"preimage.json",saved/"expected.json");e["config_preimage_sha256"]=e["config_preimage"]["sha256"];e["raw"]=pin(authority,"raw.json",saved/"raw.json");e["native_result"]=pin(authority,"result.json",native);write(authority/"evidence.json",e);
 Json declaration={{"schema","monaka.evidence-continuity.v1"},{"operator","TEST_ONLY_REPLAY_OPERATOR"},{"continuity","SAME"},{"source",b.source},{"device",b.device},{"input_space",b.inputSpace},{"input_revision",b.inputRevision},{"current_session",witness.at("session")},{"current_clock",witness.at("clock")},
 {"space_generation","TEST_ONLY_SAME_SPACE"},{"attachment_relation","TEST_ONLY_UNCHANGED_FIXTURE"},{"attachment_unchanged",true},{"config_sha256",sha256(out/"config.json")},{"policy_sha256",policyHash},{"evidence_sha256",sha256(authority/"evidence.json")},{"reference_serial",witness.at("reference_serial")},{"reference_role","left"},{"reference_standing",witness.at("standing_witness")},{"reference_generation",witness.at("reference_generation")}};
 write(authority/"declaration.json",declaration);
 write(authority/"adoption.json",{{"schema","monaka.output-adoption.v1"},{"enabled",true},{"id","TEST_ONLY_NEVER_DEPLOY"},{"policy",{{"file","policy.json"},{"sha256",policyHash}}},{"evidence",Json::array({{{"file","evidence.json"},{"sha256",sha256(authority/"evidence.json")}}})},{"current_declaration",{{"file","declaration.json"},{"sha256",sha256(authority/"declaration.json")}}}});
 auto runApproval=[&](const fs::path& configPath,bool concurrent){
  ReplayProvider provider(current,witness,raw);lc::SessionEnvironment env;env.draftRoot=out/(concurrent?"conflict-drafts":"drafts");env.helperPath=fs::absolute(argv[0]).u8string();env.helperSha256=sha256(env.helperPath);env.outputApprovalAuthority=lc::makeProductionOutputApprovalAuthority(authority,[&]{return wall;});
  lc::CalibrationSession session(provider,env);unsigned request=0;
  auto command=[&](const std::string& op,Json fields=Json::object()){Json j={{"api_version",1},{"request_id",std::to_string(++request)},{"op",op}};if(op!="open"){j["session_id"]=session.sessionId();j["run_id"]=session.runId();}j.update(fields);return j;};
  auto execute=[&](const Json& j,const char* status){auto r=session.execute(lc::parseRequest(j.dump()));write(out/("request-"+std::to_string(concurrent)+"-"+std::to_string(request)+".json"),r);require(r.at("status")==status,r.dump());return r;};
  execute(command("open",{{"purpose","output_approval"},{"config_path",configPath.u8string()},{"source",b.source},{"device",b.device},{"tracker",b.tracker},{"input_space",b.inputSpace},{"input_revision",b.inputRevision},{"readonly",false},{"bridge_id",current.bridgeId},{"installation_path",out.u8string()},{"helper_path",env.helperPath},{"helper_sha256",env.helperSha256},{"association","operator_declared_process_owner_unverified"},{"mirror_address","127.0.0.1"},{"mirror_port",49123},{"reference_role","left"}}),"OPEN");
  execute(command("discover"),"DISCOVERED");execute(command("confirm_target"),"CONFIRMED");auto review=execute(command("review_output"),"APPROVAL_REVIEW");require(review.at("reason")=="",review.dump());auto prepared=execute(command("prepare_output",{{"approval_id",review.at("approval_id")},{"members",Json::array({b.device})}}),"OUTPUT_PREPARED");
  if(concurrent){std::ofstream f(configPath,std::ios::app);f<<' ';}
  auto commit=command("commit_output",{{"approval_id",prepared.at("approval_id")},{"review_token",prepared.at("review_token")},{"candidate_sha256",prepared.at("candidate_sha256")}});execute(commit,concurrent?"REJECTED":"SAVED");
  if(!concurrent){require(execute(commit,"SAVED").at("replayed")==true&&session.commitCalls()==1,"R5 single commit");}else require(session.commitCalls()==0,"concurrent writer reached replace");
 };
 fs::copy_file(out/"config.json",out/"conflict.json");runApproval(out/"conflict.json",true);runApproval(out/"config.json",false);
 auto approved=mb::loadConfig(out/"config.json");auto expected=read(saved/"candidate.json");expected["mapping_revision"]=current.revision+1;expected["mappings"][0]["space_approved"]=true;require(read(out/"config.json")==expected,"R4 only selected approval +1");
 auto driver=out/"driver";fs::copy(package,driver,fs::copy_options::recursive);auto presentation=driver/"resources/presentation";fs::copy_file(out/"config.json",presentation/"bridge-config.json",fs::copy_options::overwrite_existing);
 auto serial=mb::runtimeSerial(mb::outputSource(approved.bridgeId,b.source),b.tracker);
 write(presentation/"selection.json",{{"namespace","monaka.bridge.presentation"},{"version",1},{"publisher_session",SB},{"config_file","bridge-config.json"},{"config_sha256",sha256(presentation/"bridge-config.json")},{"assignments",Json::array({{{"model_id","pico_motion_tracker"},{"device_kind","pico_motion_tracker"},{"source_id",b.source},{"device_id",b.device},{"tracker_id",b.tracker},{"runtime_serial",serial},{"input_session",witness.at("session")}}})}});
 pt::Events events((out/"host-events.jsonl").string());events.scenario="R6-R7-recorded-PQ";{PortReservation reserve;}
 Module module(driver/"bin/win64/driver_monaka_bridge.dll",events);Session host(module);require(host.error==vr::VRInitError_None,"product driver Init");host.host.expectedRenderModels[serial]="{monaka_bridge}pico_motion_tracker";
 mb::Bridge bridge(approved,SB);ReplayProvider producer(approved,witness,raw);mb::Udp sender;mb::c1::MtpPose last;std::ofstream mtp(out/"mtp-replay.jsonl");std::int64_t sequence=0;
 auto flush=[&](std::int64_t now){bridge.fanout.flush([&](auto channel,std::string_view bytes){if(channel==mb::Channel::Steamvr){mtp<<bytes<<'\n';return sender.send(Port,bytes);}return true;},now);};
 auto feed=[&](std::size_t i){auto now=mb::monotonicNs();auto p=producer.observation(i,++sequence,now);require(bridge.receive(wire(p),"OFFLINE_REPLAY",now),"recorded observation rejected");auto logical=bridge.logical().at({b.source,b.device});require(logical.pose&&logical.pose->validity.position&&logical.pose->validity.orientation&&logical.pose->tracking_state=="tracked","R6 Bridge PQ invalid");last=*logical.pose;auto before=host.host.totalPoses;flush(now);host.until([&]{return host.host.totalPoses>before;});validFull(host.pose(),last);};
 for(std::size_t i=0;i<raw.at("source").size();i+=20)feed(i);
 require(host.host.devices.at(1).properties.at(vr::Prop_RenderModelName_String)=="{monaka_bridge}pico_motion_tracker","R7 Rev3 property");
 auto lost=producer.observation(0,++sequence,mb::monotonicNs());lost.modality="none";lost.position.reset();lost.orientation.reset();lost.linear_velocity.reset();lost.validity={false,false};lost.orientation_evidence="none";lost.tracking_state="lost";require(bridge.receive(wire(lost),"OFFLINE_REPLAY",lost.sent_at_ns),"lost receive");flush(lost.sent_at_ns);host.pump(10);require(!host.pose().poseIsValid,"lost not invalidated");feed(2);host.pump(510);require(!host.pose().poseIsValid,"driver expiry");feed(3);
 auto poses=host.host.totalPoses;host.finish();module.unload();{PortReservation released;}
 write(out/"result.json",{{"result","PASS"},{"gates",{{"R0","PASS"},{"R1","PASS_NATIVE_REPLAY_RECEIPT"},{"R2","PASS"},{"R3","PASS"},{"R4","PASS"},{"R5","PASS"},{"R6","PASS"},{"R7","PASS"}}},{"physical_hil","NOT_RUN"},{"policy","TEST_ONLY_NEVER_DEPLOY"},{"policy_sha256",policyHash},{"saved_measurement_utc_ms",recorded.at("measured_at_utc_ms")},{"same_space_restart_reuse",true},{"physical_declaration","SYNTHETIC_ONLY_NOT_CURRENT_HARDWARE"},{"timing","rebased monotonic"},{"velocity","synthetic zero LV for recorded PQ replay; not measured"},{"pose_updates",poses},{"port",Port},{"source_raw_sha256",sha256(saved/"raw.json")},{"native_result_sha256",sha256(native)}});
 std::cout<<"PASS R0-R7 stored capture, production file authority, exact CAS, Bridge, product driver Mock Host; hardware NOT_RUN\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
