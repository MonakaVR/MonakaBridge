#include "calibration_b4_fixture.hpp"
#include "monaka_bridge/output_approval_authority.hpp"
#include "monaka_bridge/trajectory_result_json.hpp"
#include <iostream>
using namespace b4_fixture;using Json=lc::Json;
struct FilesV2 {
 Run run;std::filesystem::path root;Json policy,evidence,manifest,result,raw,preimage,declaration;
 std::int64_t wall=1800000000000LL;mb::Config config;lc::InputWitness witness;bool available=true;std::int64_t poseAge=0,referenceAge=0;
 explicit FilesV2(const std::filesystem::path& base):run(base),root(run.seed.root/"authority"),config(mb::parseConfigDocument(run.seed.original)){
  std::filesystem::create_directories(root);const auto& b=config.bindings.at({"pico","device"});
  preimage=Json::parse(run.seed.original);preimage["mapping_revision"]=config.revision-1;
  mb::TrajectoryResult r;r.world=b.world;r.pairs=r.inliers=100;r.residuals={.01,.02,.02,.03};r.allResiduals=r.residuals;
  r.quality.extent={.2,.2,.2};r.quality.rotation=3;r.quality.axisDiversity=.1;r.quality.validRatio=1;r.quality.spanSeconds=9.9;r.quality.samples=100;r.quality.sufficient=true;
  result=mb::trajectoryResultJson(r);result.update({{"result_id","TEST_ONLY_NATIVE_RESULT"},{"config_sha256",lc::sha256(preimage.dump())}});
  raw={{"source",Json::array()},{"reference",Json::array()}};
  policy={{"schema","monaka.physical-criteria.v2"},{"enabled",true},{"id","TEST_ONLY_SEMANTIC_POLICY"},{"version","1"},{"lifetime",{{"mode","semantic"}}},
   {"scope",{{"source",b.source},{"input_space",b.inputSpace},{"profile",b.profile},{"world_space",b.worldSpace}}},{"measurement_mode","trajectory"},{"all_pair_max_mode","diagnostic"},
   {"limits",{{"min_pairs",80},{"min_inliers",80},{"min_inlier_ratio",.8},{"rms_position",.025},{"rms_orientation",.08},{"max_position",.05},{"max_orientation",.15}}},
   {"motion_quality",{{"valid_ratio",.7},{"span_seconds",4.5},{"extent_m",{.15,.15,.10}},{"accumulated_rotation_rad",2},{"axis_diversity",.01}}},{"measurement_options",result.at("measurement_options")}};
  witness.source=b.source;witness.device=b.device;witness.space=b.inputSpace;witness.revision=b.inputRevision;witness.convention=config.profiles.at(b.profile).convention;
  witness.session=witness.clock=S1;witness.referenceRole="left";witness.referenceSerial="fixture-reference";witness.standing="fixture-standing";witness.referenceGeneration=1;
  evidence={{"schema","monaka.physical-evidence.v2"},{"evidence_kind","physical_capture"},{"id","TEST_ONLY_NEVER_INSTALL"},{"criteria_id",policy.at("id")},{"criteria_version","1"},
   {"source",b.source},{"device",b.device},{"input_space",b.inputSpace},{"input_revision",b.inputRevision},{"mapping_revision",config.revision},{"world_revision",b.worldRevision},{"world",result.at("world")},
   {"config_candidate_sha256",lc::sha256(run.seed.original)},{"source_session",S1},{"source_clock",S1},{"reference_serial",witness.referenceSerial},{"reference_standing",witness.standing},{"reference_generation",1},
   {"measured_at_utc_ms",wall-864000000},{"lifetime",{{"mode","semantic"}}},{"measurement_mode","trajectory"},{"tool_sha256",std::string(64,'a')},{"build_identity","TEST_ONLY"},{"measurement_result_id",result.at("result_id")},
   {"semantic_identity",{{"space_generation","TEST_ONLY_SPACE_A"},{"attachment_relation","TEST_ONLY_FIXTURE_A"},{"input_convention",witness.convention},{"reference_role","left"}}}};
  declaration={{"schema","monaka.evidence-continuity.v1"},{"operator","TEST_ONLY_OPERATOR"},{"continuity","SAME"},{"source",b.source},{"device",b.device},{"input_space",b.inputSpace},{"input_revision",b.inputRevision},
   {"current_session",S1},{"current_clock",S1},{"config_sha256",lc::sha256(run.seed.original)},{"space_generation","TEST_ONLY_SPACE_A"},{"attachment_relation","TEST_ONLY_FIXTURE_A"},{"attachment_unchanged",true},
   {"reference_serial",witness.referenceSerial},{"reference_role","left"},{"reference_standing",witness.standing},{"reference_generation",1}};
  manifest={{"schema","monaka.output-adoption.v1"},{"enabled",true},{"id","TEST_ONLY_NEVER_INSTALL"}};publish();
  run.seed.env.outputApprovalAuthority=lc::makeProductionOutputApprovalAuthority(root,[this]{return wall;},run.seed.env.clock);run.reset();
 }
 Json pin(const char* name,const Json& value){auto bytes=value.dump();std::ofstream f(root/name,std::ios::binary);f<<bytes;return {{"file",name},{"sha256",lc::sha256(bytes)}};}
 void publish(){manifest["policy"]=pin("policy.json",policy);evidence["criteria_sha256"]=manifest["policy"]["sha256"];
  evidence["config_preimage"]=pin("preimage.json",preimage);evidence["config_preimage_sha256"]=evidence["config_preimage"]["sha256"];
  evidence["native_result"]=pin("result.json",result);evidence["raw"]=pin("raw.json",raw);auto ep=pin("evidence.json",evidence);manifest["evidence"]=Json::array({ep});
  declaration["policy_sha256"]=evidence["criteria_sha256"];declaration["evidence_sha256"]=ep["sha256"];manifest["current_declaration"]=pin("declaration.json",declaration);pin("adoption.json",manifest);
 }
 void legacy(){policy["schema"]="monaka.physical-criteria.v1";policy.erase("lifetime");policy["valid_from_utc_ms"]=wall-1000;policy["valid_until_utc_ms"]=wall+100000;
  policy["limits"]["all_max_position"]=.05;policy["limits"]["all_max_orientation"]=.15;
  evidence["schema"]="monaka.physical-evidence.v1";evidence.erase("lifetime");evidence["expires_at_utc_ms"]=wall+100000;
 }
 void changeConfig(Json document){run.seed.original=document.dump();{std::ofstream f(run.seed.path,std::ios::binary);f<<run.seed.original;}config=mb::parseConfigDocument(run.seed.original);}
 lc::OutputApprovalCriteria verify(){const auto& b=config.bindings.at({"pico","device"});auto target=mb::freezeMeasurementTarget(config,"pico","device",b.tracker,b.inputSpace,b.inputRevision);
  const auto bytes=mb::readConfigBytes(run.seed.path),hash=lc::sha256(bytes);auto authority=lc::makeProductionOutputApprovalAuthority(root,[this]{return wall;},[]{return 1000000000LL;});
  return authority->verify({target,witness,config,hash,1000000000LL,available,poseAge,referenceAge});
 }
};
int main(int argc,char** argv)try{
 CHECK(argc==2);std::filesystem::path root=argv[1];Json receipts=Json::array();
 auto test=[&](const char* name,const char* expected,auto edit){FilesV2 f(root);edit(f);f.publish();std::string actual="APPROVAL_EVIDENCE_VALID";Json detail;
  try{auto result=f.verify();detail=result.evaluation;CHECK(result.eligible.count({"pico","device"})==1);if(f.policy.at("schema")=="monaka.physical-criteria.v2"&&!f.policy.at("lifetime").contains("not_after_utc_ms")&&!f.evidence.at("lifetime").contains("not_after_utc_ms"))CHECK(!result.expiresAt);}
  catch(const lc::EvidenceEvaluationError& e){actual=e.what();detail=e.evaluation;CHECK(detail.at("status")!="VALID");}
  if(actual!=expected)throw std::runtime_error(std::string(name)+" expected "+expected+" got "+actual);
  CHECK(mb::readConfigBytes(f.run.seed.path)==f.run.seed.original);receipts.push_back({{"case",name},{"reason",actual},{"evaluation",detail}});std::cout<<"PASS "<<name<<" "<<actual<<std::endl;
 };
 auto valid=[](FilesV2&){};
 test("no expiry semantic evidence","APPROVAL_EVIDENCE_VALID",valid);
 test("optional expiry absent","APPROVAL_EVIDENCE_VALID",valid);
 test("optional expiry null","APPROVAL_EVIDENCE_VALID",[](auto& f){f.policy["lifetime"]["not_after_utc_ms"]=nullptr;f.evidence["lifetime"]["not_after_utc_ms"]=nullptr;});
 test("explicit expiry valid","APPROVAL_EVIDENCE_VALID",[](auto& f){f.evidence["lifetime"]["not_after_utc_ms"]=f.wall+100000;});
 test("explicit expiry expired","APPROVAL_TIME_POLICY_EXPIRED",[](auto& f){f.evidence["lifetime"]["not_after_utc_ms"]=f.wall;});
 test("policy expiry expired","APPROVAL_TIME_POLICY_EXPIRED",[](auto& f){f.policy["lifetime"]["not_after_utc_ms"]=f.wall;});
 test("policy not yet valid","APPROVAL_TIME_POLICY_NOT_YET_VALID",[](auto& f){f.policy["lifetime"]["not_before_utc_ms"]=f.wall+1;});
 test("legacy timestamp compatibility","APPROVAL_EVIDENCE_VALID",[](auto& f){f.legacy();});
 test("legacy null does not migrate","APPROVAL_INTEGER_INVALID",[](auto& f){f.legacy();f.evidence["expires_at_utc_ms"]=nullptr;});
 test("legacy expired","APPROVAL_EVIDENCE_EXPIRED_OR_FUTURE",[](auto& f){f.legacy();f.evidence["expires_at_utc_ms"]=f.wall-1;});
 test("invalid lifetime mode","APPROVAL_LIFETIME_MODE_INVALID",[](auto& f){f.evidence["lifetime"]["mode"]="forever";});
 test("malformed time","APPROVAL_INTEGER_INVALID",[](auto& f){f.evidence["lifetime"]["not_after_utc_ms"]="tomorrow";});
 test("mixed lifetime schema","APPROVAL_MIXED_LIFETIME_SCHEMA",[](auto& f){f.evidence["expires_at_utc_ms"]=nullptr;});
 for(const char* name:{"GUI restart","Bridge restart"})test(name,"APPROVAL_EVIDENCE_VALID",valid);
 for(const char* name:{"PICO provider restart Same","service restart Same","sender restart declared Same"})test(name,"APPROVAL_EVIDENCE_VALID",[](auto& f){f.witness.session=S2;f.witness.clock=S2;f.declaration["current_session"]=S2;f.declaration["current_clock"]=S2;});
 test("space revision changed","APPROVAL_CURRENT_WITNESS_MISMATCH",[](auto& f){f.witness.revision++;});
 test("space generation changed","APPROVAL_SPACE_GENERATION_CHANGED",[](auto& f){f.declaration["space_generation"]="TEST_ONLY_SPACE_B";});
 test("continuity unknown","APPROVAL_CONTINUITY_UNKNOWN",[](auto& f){f.declaration["continuity"]="UNKNOWN";});
 test("continuity changed","APPROVAL_CONTINUITY_CHANGED",[](auto& f){f.declaration["continuity"]="CHANGED";});
 test("source changed","APPROVAL_EVIDENCE_SOURCE_REVISION_MISMATCH",[](auto& f){f.evidence["source"]="foreign";});
 test("device changed","APPROVAL_EVIDENCE_DEVICE_MISMATCH",[](auto& f){f.evidence["device"]="foreign";});
 test("World revision changed","APPROVAL_EVIDENCE_WORLD_MISMATCH",[](auto& f){f.evidence["world_revision"]=99;});
 test("World transform changed","APPROVAL_EVIDENCE_WORLD_MISMATCH",[](auto& f){f.evidence["world"]["translation_m"][0]=99;});
 test("Mount changed","APPROVAL_EVIDENCE_CONFIG_MISMATCH",[](auto& f){auto j=Json::parse(f.run.seed.original);for(auto& b:j["mappings"])if(b["source_id"]=="pico"&&b["device_id"]=="device")b["mount"]["translation"][0]=.1;f.changeConfig(j);});
 test("profile changed","APPROVAL_EVIDENCE_CONFIG_MISMATCH",[](auto& f){auto j=Json::parse(f.run.seed.original);j["profiles"]["pico"]["position_axes"]={-1,2,3};f.changeConfig(j);});
 test("capability contract changed","APPROVAL_EVIDENCE_CONFIG_MISMATCH",[](auto& f){auto j=Json::parse(f.run.seed.original);for(auto& b:j["mappings"])if(b["source_id"]=="pico"&&b["device_id"]=="device")b["runtime_contract"]["id"]="TEST_ONLY_CHANGED_CONTRACT";f.changeConfig(j);});
 test("config hash changed","APPROVAL_EVIDENCE_CONFIG_MISMATCH",[](auto& f){f.evidence["config_candidate_sha256"]=std::string(64,'b');});
 test("reference semantics changed","APPROVAL_EVIDENCE_REFERENCE_MISMATCH",[](auto& f){f.witness.referenceSerial="foreign";});
 test("Standing reset","APPROVAL_EVIDENCE_REFERENCE_MISMATCH",[](auto& f){f.witness.standing="other";});
 test("reference role changed","APPROVAL_REFERENCE_ROLE_CHANGED",[](auto& f){f.witness.referenceRole="right";});
 test("attachment relation changed","APPROVAL_ATTACHMENT_CHANGED",[](auto& f){f.declaration["attachment_relation"]="OTHER_FIXTURE";});
 test("attachment declared changed","APPROVAL_ATTACHMENT_CHANGED",[](auto& f){f.declaration["attachment_unchanged"]=false;});
 test("attachment declaration missing","APPROVAL_ATTACHMENT_DECLARATION_MISSING",[](auto& f){f.declaration.erase("attachment_unchanged");});
 test("saved semantics unknown","APPROVAL_SAVED_SEMANTICS_MISSING",[](auto& f){f.evidence["semantic_identity"]["space_generation"]=nullptr;});
 test("operator declaration missing","APPROVAL_OPERATOR_DECLARATION_REQUIRED",[](auto& f){f.declaration.erase("operator");});
 test("lifetime mode wrong type","APPROVAL_LIFETIME_MODE_INVALID",[](auto& f){f.evidence["lifetime"]["mode"]=true;});
 test("current witness unavailable","APPROVAL_CURRENT_WITNESS_UNAVAILABLE",[](auto& f){f.available=false;});
 test("current witness stale","APPROVAL_CURRENT_WITNESS_STALE",[](auto& f){f.poseAge=50000001;});
 test("reference witness stale","APPROVAL_CURRENT_WITNESS_STALE",[](auto& f){f.referenceAge=50000001;});
 test("current witness mismatch","APPROVAL_CURRENT_WITNESS_MISMATCH",[](auto& f){f.witness.device="foreign";});
 test("declaration old process","APPROVAL_CURRENT_DECLARATION_SESSION_MISMATCH",[](auto& f){f.witness.session=S2;});
 test("all-pair diagnostic only","APPROVAL_EVIDENCE_VALID",[](auto& f){f.result["all_residuals"]["max_position"]=20.;f.result["all_residuals"]["max_orientation"]=3.;});
 test("Native quality insufficient","APPROVAL_MOTION_QUALITY_FAILED",[](auto& f){f.result["quality"]["axis_diversity"]=0;});
 test("Native quality missing","APPROVAL_NATIVE_QUALITY_MISSING",[](auto& f){f.result.erase("quality");});
 test("inlier max failed","APPROVAL_RESIDUAL_CRITERIA_FAILED",[](auto& f){f.result["max_position"]=.051;});
 for(const char* file:{"policy.json","evidence.json","result.json","raw.json","declaration.json"}){FilesV2 f(root);{std::ofstream stream(f.root/file,std::ios::app);stream<<' ';}bool rejected=false;try{f.verify();}catch(const lc::EvidenceEvaluationError& e){CHECK(std::string(e.what())=="APPROVAL_HASH_MISMATCH");rejected=true;}CHECK(rejected);receipts.push_back({{"case",std::string(file)+" SHA changed"},{"reason","APPROVAL_HASH_MISMATCH"}});}
 {FilesV2 f(root);auto reviewed=f.run.review();CHECK(reviewed.at("reason")=="");CHECK(reviewed.at("evidence_evaluation").at("status")=="VALID");auto prepared=f.run.prepare(reviewed);CHECK(prepared.at("status")=="OUTPUT_PREPARED");auto cmd=f.run.commit(prepared);CHECK(f.run.execute(cmd).at("status")=="SAVED");CHECK(f.run.execute(cmd).at("replayed")==true);CHECK(f.run.session->commitCalls()==1);receipts.push_back({{"case","semantic production commit and idempotence"},{"reason","PASS"}});}
 {FilesV2 f(root);auto prepared=f.run.prepare(f.run.review());f.declaration["operator"]="TEST_ONLY_CHANGED_OPERATOR";f.publish();auto rejected=f.run.execute(f.run.commit(prepared));CHECK(rejected.at("status")=="REJECTED");CHECK(rejected.at("reason")=="APPROVAL_CRITERIA_CHANGED_REVIEW_REQUIRED");CHECK(f.run.session->commitCalls()==0);receipts.push_back({{"case","declaration changed during review"},{"reason",rejected.at("reason")}});}
 std::ofstream report(root/"evidence-lifetime-results.json");report<<Json({{"status","PASS"},{"hardware","NOT_RUN"},{"cases",receipts}}).dump(2)<<'\n';
 std::cout<<"TOTAL "<<receipts.size()<<"; private fixtures; hardware NOT_RUN\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
