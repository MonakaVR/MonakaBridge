#include "calibration_b4_fixture.hpp"
#include "monaka_bridge/output_approval_authority.hpp"
#include "monaka_bridge/trajectory_result_json.hpp"
#include <iostream>
using namespace b4_fixture;
using Json=lc::Json;
Json rigid(const mb::Rigid& r){return {{"rotation_xyzw",r.rotation},{"translation_m",r.translation}};}
struct Files {
 Run run;std::filesystem::path root;Json policy,evidence,manifest,result,raw,preimage;std::int64_t wall=1800000000000LL;
 explicit Files(const std::filesystem::path& base):run(base),root(run.seed.root/"OutputApproval"){
  std::filesystem::create_directories(root);
  auto config=mb::parseConfigDocument(run.seed.original);const auto& b=config.bindings.at({"pico","device"});
  preimage=Json::parse(run.seed.original);preimage["mapping_revision"]=config.revision-1;
  policy={{"schema","monaka.physical-criteria.v1"},{"enabled",true},{"id","TEST_ONLY"},{"version","1"},{"valid_from_utc_ms",wall-1000},{"valid_until_utc_ms",wall+600000},
   {"scope",{{"source",b.source},{"input_space",b.inputSpace},{"profile",b.profile},{"world_space",b.worldSpace}}},{"measurement_mode","trajectory"},
   {"limits",{{"min_pairs",80},{"min_inliers",80},{"min_inlier_ratio",.8},{"rms_position",.025},{"rms_orientation",.08},{"max_position",.03},{"max_orientation",.1},{"all_max_position",.04},{"all_max_orientation",.12}}}};
  result={{"mode","trajectory"},{"result_id","TEST_ONLY_SYNTHETIC_RESULT"},{"world",rigid(b.world)},{"pairs",100},{"inliers",100},{"rms_position",.01},{"rms_orientation",.02},{"max_position",.02},{"max_orientation",.03},{"all_residuals",{{"max_position",.02},{"max_orientation",.03}}},{"config_sha256",lc::sha256(preimage.dump())}};
  raw={{"source",Json::array()},{"reference",Json::array()}};
  evidence={{"schema","monaka.physical-evidence.v1"},{"evidence_kind","physical_capture"},{"id","TEST_ONLY_NEVER_SHIP"},{"criteria_id","TEST_ONLY"},{"criteria_version","1"},
   {"source",b.source},{"device",b.device},{"input_space",b.inputSpace},{"input_revision",b.inputRevision},{"mapping_revision",config.revision},{"world_revision",b.worldRevision},{"world",rigid(b.world)},
   {"config_candidate_sha256",lc::sha256(run.seed.original)},{"source_session",S1},{"source_clock",S1},{"reference_serial","fixture-reference"},{"reference_standing","fixture-standing"},{"reference_generation",1},
   {"measured_at_utc_ms",wall-100},{"expires_at_utc_ms",wall+300000},{"measurement_mode","trajectory"},{"tool_sha256",std::string(64,'a')},{"build_identity","TEST_ONLY_SYNTHETIC_BUILD"},{"measurement_result_id","TEST_ONLY_SYNTHETIC_RESULT"}};
  manifest={{"schema","monaka.output-adoption.v1"},{"enabled",true},{"id","TEST_ONLY_NEVER_SHIP"}};publish();
  run.seed.env.outputApprovalAuthority=lc::makeProductionOutputApprovalAuthority(root,[this]{return wall;},run.seed.env.clock);run.reset();
 }
 Json pin(const char* name,const Json& value){auto bytes=value.dump();std::ofstream f(root/name,std::ios::binary);f<<bytes;return {{"file",name},{"sha256",lc::sha256(bytes)}};}
 void publish(){manifest["policy"]=pin("policy.json",policy);evidence["criteria_sha256"]=manifest["policy"]["sha256"];evidence["config_preimage"]=pin("preimage.json",preimage);evidence["config_preimage_sha256"]=evidence["config_preimage"]["sha256"];evidence["native_result"]=pin("result.json",result);evidence["raw"]=pin("raw.json",raw);manifest["evidence"]=Json::array({pin("evidence.json",evidence)});pin("adoption.json",manifest);}
};
int main(int argc,char** argv){try{
 CHECK(argc==2);std::filesystem::path root=argv[1];unsigned count=0;
 auto test=[&](const char* name,auto body){body();++count;std::cout<<"PASS "<<name<<std::endl;};
 test("production file authority commit exact diff and idempotency",[&]{Files f(root);auto reviewed=f.run.review();CHECK(reviewed.at("reason")=="");auto prepared=f.run.prepare(reviewed);CHECK(prepared.at("status")=="OUTPUT_PREPARED");auto command=f.run.commit(prepared);auto saved=f.run.execute(command);CHECK(saved.at("status")=="SAVED");auto actual=mb::loadConfig(f.run.seed.path);auto before=mb::parseConfigDocument(f.run.seed.original);CHECK(actual.revision==before.revision+1&&actual.bindings.at({"pico","device"}).spaceApproved&&!actual.bindings.at({"pico","sibling"}).spaceApproved);before.revision++;before.bindings.at({"pico","device"}).spaceApproved=true;auto expected=f.root/"expected-approved.json";mb::saveConfig(expected,before);CHECK(Json::parse(mb::readConfigBytes(expected))==Json::parse(mb::readConfigBytes(f.run.seed.path)));CHECK(f.run.execute(command).at("replayed")==true&&f.run.session->commitCalls()==1);CHECK(f.run.execute(f.run.commit(prepared)).at("status")=="REJECTED");});
 for(const std::string mutation:{"authority absent","policy absent","policy invalid","policy disabled","evidence absent","wrong hash","stale revision","wrong source","wrong device","wrong world","wrong config","wrong group","expired","changed result","RMS pass max fail","inlier pass all-pair max fail","NaN"})test(mutation.c_str(),[&]{Files f(root);
  if(mutation=="policy invalid")f.policy["limits"]["max_position"]="invalid";
  if(mutation=="policy disabled")f.policy["enabled"]=false;
  if(mutation=="stale revision")f.evidence["input_revision"]=2;
  if(mutation=="wrong source")f.evidence["source"]="foreign";
  if(mutation=="wrong device")f.evidence["device"]="missing";
  if(mutation=="wrong world")f.evidence["world_revision"]=99;
  if(mutation=="wrong config")f.evidence["config_candidate_sha256"]=std::string(64,'b');
  if(mutation=="wrong group")f.policy["scope"]["profile"]="foreign";
  if(mutation=="expired")f.evidence["expires_at_utc_ms"]=f.wall-1;
  if(mutation=="changed result")f.result["result_id"]="changed";
  if(mutation=="RMS pass max fail")f.result["max_position"]=.031;
  if(mutation=="inlier pass all-pair max fail")f.result["all_residuals"]["max_position"]=.041;
  if(mutation=="NaN")f.result["max_position"]=nullptr;
  f.publish();
  if(mutation=="authority absent"){f.run.seed.env.outputApprovalAuthority.reset();f.run.reset();}
  if(mutation=="policy absent")std::filesystem::rename(f.root/"adoption.json",f.root/"absent.json");
  if(mutation=="wrong hash"){std::ofstream file(f.root/"result.json",std::ios::app);file<<' ';}
  if(mutation=="evidence absent"){f.manifest["evidence"]=Json::array();f.pin("adoption.json",f.manifest);}
  auto reviewed=f.run.review();CHECK(!reviewed.at("reason").get<std::string>().empty());CHECK(f.run.prepare(reviewed).at("status")=="REJECTED");CHECK(f.run.session->commitCalls()==0&&mb::readConfigBytes(f.run.seed.path)==f.run.seed.original);
 });
 test("concurrent writer preserves external change",[&]{Files f(root);auto p=f.run.prepare(f.run.review());{std::ofstream out(f.run.seed.path,std::ios::app);out<<' ';}CHECK(f.run.execute(f.run.commit(p)).at("status")=="REJECTED");CHECK(f.run.session->commitCalls()==0);});
 for(const std::string reason:{"SOURCE_LIVENESS_GAP","REFERENCE_LIVENESS_GAP","SELECTED_LIFETIME_PEER_SPACE_CONFLICT","INPUT_REFERENCE_FRAME_CONFLICT","REFERENCE_FRAME_CHANGED","REFERENCE_QUERY_FRAME_CHANGED","STANDING_EVENT_CHANGED","REFERENCE_CONNECTION_EVENT","SOURCE_SESSION_CHANGED","SOURCE_CLOCK_CHANGED","INPUT_REVISION_CHANGED","CONFIG_EXACT_BYTES_CHANGED","REFERENCE_GENERATION_CHANGED","WORKER_TTL_EXPIRED","EXPLICIT_ABSENCE"})test(reason.c_str(),[&]{fixture::Run r(root);auto result=r.result().at("result");CHECK(result.contains("max_position")&&result.contains("max_orientation")&&result.contains("all_residuals"));auto round=Json::parse(result.dump());CHECK(result==round);r.provider.edit([&]{r.provider.guardHook=[reason](auto&){throw std::runtime_error(reason);};});r.session->tick();auto event=r.session->invalidationDiagnostic();CHECK(event.at("first_failure")==reason);auto response=r.execute(r.command("confirm_target"));CHECK(response.at("first_failure")==reason);auto terminal=Json::parse(mb::readConfigBytes(r.env.draftRoot/r.session->runId()/"terminal.json"));CHECK(terminal.at("first_failure")==reason);r.session->tick();CHECK(r.session->invalidationDiagnostic().at("first_failure")==reason);});
 std::cout<<"TOTAL "<<count<<"; OFFLINE synthetic fixtures only; physical validation NOT_RUN\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
