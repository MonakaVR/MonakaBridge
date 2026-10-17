#pragma once
#include "calibration_fixture_provider.hpp"
#include "monaka_bridge/static_calibration_session.hpp"
namespace b4_fixture {
namespace lc=mb::local_calibration;
struct Provider final:fixture::Provider {
 std::int64_t sequence=0,stepNs=8000000;mb::Rigid world{mb::normalized({.2,-.3,.1,.9}),{1,2,-.5}};
 std::optional<mb::Vec> point;std::function<void(lc::ProviderBatch&)> change;
 explicit Provider(std::atomic<std::int64_t>& t):fixture::Provider(t){}
 lc::ProviderBatch guard()override{
  std::lock_guard<std::recursive_mutex> lock(fixtureMutex);time+=stepNs;double t=double(time.load()-100*lc::second)/lc::second;
  mb::Vec v=point.value_or(mb::Vec{std::cos(2*t),std::sin(2*t),.2*std::sin(3*t)});
  auto p=sample("pico",++sequence);p.timestamp_ns=p.sent_at_ns=time;p.orientation=mb::Quat{0,0,0,1};p.position=v;
  latest.frames={{p,"fixture-peer",time}};latest.references={{time,{world.rotation,mb::add(mb::rotate(world.rotation,v),world.translation)},true}};latest.referenceReceiptNs=time;latest.referenceValid=true;
  auto result=latest;if(change)change(result);return result;
 }
 lc::ProviderBatch discover(const std::atomic<bool>&)override{++discoveries;return guard();}
};
struct Authority final:lc::OutputApprovalAuthority {
 mutable unsigned calls=0;std::string expectedHash,expectedFrame;bool adopted=true,expired=false,foreign=false,changed=false;
 lc::OutputApprovalCriteria verify(const lc::OutputApprovalContext& context)const override{
  ++calls;if(!adopted||(!expectedHash.empty()&&context.configSha256!=expectedHash)||(!expectedFrame.empty()&&context.input.standing!=expectedFrame))throw std::runtime_error("SYNTHETIC_EVIDENCE_SCOPE_MISMATCH");
  lc::OutputApprovalCriteria result;result.policyId=changed?"TEST_ONLY_CHANGED":"TEST_ONLY_SYNTHETIC_POLICY_NOT_FOR_PRODUCT";result.evidenceId="TEST_ONLY_fixture";result.evidenceSha256=std::string(64,'a');result.expiresAt=expired?context.now:10000*lc::second;
  result.eligible.insert({"pico",foreign?"foreign":"device"});return result;
 }
};
struct Run {
 fixture::Run seed;Provider provider;std::unique_ptr<lc::CalibrationSession> session;int request=0;
 explicit Run(std::filesystem::path root):seed(root),provider(seed.time){auto j=lc::Json::parse(seed.original);for(auto& member:j["mappings"])if(member["source_id"]=="other")member["space_approved"]=false;seed.original=j.dump();{std::ofstream f(seed.path,std::ios::binary|std::ios::trunc);f<<seed.original;}reset();}
 void reset(){session=std::make_unique<lc::CalibrationSession>(provider,seed.env);}
 lc::Json command(const std::string& op){auto j=seed.command(op);j["request_id"]="b4-"+std::to_string(++request);if(op!="open"){j["session_id"]=session->sessionId();j["run_id"]=session->runId();}return j;}
 lc::Json open(const std::string& mode="static",bool readOnly=false){auto j=seed.open();j["request_id"]="b4-"+std::to_string(++request);j["readonly"]=readOnly;if(mode=="approval")j["purpose"]="output_approval";else{j["mode"]="static";j["static_samples"]=4;}return j;}
 lc::Json execute(const lc::Json& j){return session->execute(lc::parseRequest(j.dump()));}
 lc::Json confirmed(const std::string& mode="static",bool ro=false){CHECK(execute(open(mode,ro)).at("status")=="OPEN");CHECK(execute(command("discover")).at("status")=="DISCOVERED");auto c=execute(command("confirm_target"));CHECK(c.at("status")=="CONFIRMED");return c;}
 lc::Json begin(){auto c=confirmed();auto j=command("begin");j["confirmation_token"]=c.at("confirmation_token");auto out=execute(j);CHECK(out.at("status")=="WAITING_POINT");return out;}
 lc::Json capture(const lc::Json& waiting){auto j=command("capture_point");j["point_token"]=waiting.at("point_token");return execute(j);}
 lc::Json result(){auto out=begin();for(auto point:std::vector<mb::Vec>{{0,0,0},{1,0,0},{0,1,0}}){provider.edit([&]{provider.point=point;});out=capture(out);}if(out.at("status")!="RESULT_READY")throw std::runtime_error(out.dump());return out;}
 lc::Json review(bool ro=false){confirmed("approval",ro);auto out=execute(command("review_output"));CHECK(out.at("status")=="APPROVAL_REVIEW");return out;}
 lc::Json prepare(const lc::Json& reviewed,lc::Json members=lc::Json::array({"device"})){auto j=command("prepare_output");j["approval_id"]=reviewed.at("approval_id");j["members"]=members;return execute(j);}
 lc::Json commit(const lc::Json& prepared){auto j=command("commit_output");for(auto f:{"approval_id","review_token","candidate_sha256"})j[f]=prepared.at(f);return j;}
};
} // namespace b4_fixture
