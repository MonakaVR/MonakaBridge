// Actual provider/session/solver and Bridge/feed/Direct conversion, with fake
// public SDK/mirror only. This executable never initializes a real runtime.
#include "calibration_runtime_fake.hpp"
#include "monaka_bridge/direct.hpp"
#include <iostream>

mb::Rigid referenceOriginDirectPose(const mb::DirectSample&,std::int64_t);
namespace lc=mb::local_calibration;
namespace rf=runtime_fixture;
namespace fs=std::filesystem;
namespace {
const mb::Rigid room{mb::normalized({.13,.47,-.21,.82}),{2.3,.8,-1.7}};
double distance(mb::Vec a,mb::Vec b){return mb::norm(mb::add(a,mb::scale(b,-1)));}
void closePose(const mb::Rigid& actual,const mb::Rigid& expected){CHECK(distance(actual.translation,expected.translation)<1e-4&&mb::rotationDistance(actual.rotation,expected.rotation)<1e-4);}
void require(const lc::Json& value,const char* status){if(value.at("status")!=status)throw std::runtime_error(value.dump());}
void selectPolicy(fixture::Run& run,mb::Policy policy){
 auto config=mb::loadConfig(run.path);config.policy=policy;++config.revision;
 for(auto& [key,binding]:config.bindings)if(key.first=="pico")binding.worldRevision=7;
 mb::saveConfig(run.path,config);run.original=mb::readConfigBytes(run.path);
}
lc::Json command(lc::CalibrationSession& session,const char* op){return {{"api_version",1},{"request_id",lc::opaqueId()},{"op",op},{"session_id",session.sessionId()},{"run_id",session.runId()}};}
template<class F>void rejects(F action){bool failed=false;try{action();}catch(const std::exception&){failed=true;}CHECK(failed);}
}
int main(int argc,char** argv)try{
 CHECK(argc==2);const auto base=fs::absolute(fs::u8path(argv[1])).lexically_normal();fs::create_directories(base);
 {
  fixture::Run run(base);selectPolicy(run,mb::Policy::Steamvr);rf::State state(run.time);state.frame=rf::matrix(room);state.extrinsic={};
  auto provider=rf::provider(state,true);lc::CalibrationSession session(*provider,run.env);
  require(session.execute(run.open()),"OPEN");CHECK(provider->configuration()->referenceOrigin()==lc::ReferenceOrigin::Raw);
  require(session.execute(command(session,"discover")),"DISCOVERED");auto confirmed=session.execute(command(session,"confirm_target"));require(confirmed,"CONFIRMED");
  auto begin=command(session,"begin");begin["confirmation_token"]=confirmed.at("confirmation_token");auto result=session.execute(begin);require(result,"RESULT_READY");
  CHECK(state.origin==vr::TrackingUniverseRawAndUncalibrated&&session.solveCalls()==1);
  auto prepare=command(session,"prepare_commit");prepare["result_id"]=result.at("result").at("result_id");auto prepared=session.execute(prepare);require(prepared,"PREPARED");
  const auto candidate=mb::readConfigBytes(fs::u8path(prepared.at("candidate_path").get<std::string>()));
  auto commit=command(session,"commit");for(const char* field:{"result_id","review_token","candidate_sha256"})commit[field]=prepared.at(field);require(session.execute(commit),"SAVED");
  CHECK(session.commitCalls()==1&&mb::readConfigBytes(run.path)==candidate);session.execute(command(session,"close"));
  const auto saved=mb::loadConfig(run.path);const auto& binding=saved.bindings.at({"pico","device"});
  closePose(binding.world,state.world);CHECK(binding.worldRevision==7); // Keep the selected destination revision.
  const mb::Rigid source{mb::normalized({-.24,.17,.31,.89}),{.31,-.26,.48}};
  const auto rawReference=mb::composeRigid(state.world,source),standingReference=mb::composeRigid(room,rawReference);
  mb::Bridge bridge(saved,SB);auto input=sample("pico",1);input.position=source.translation;input.orientation=source.rotation;
  input.timestamp_ns=run.time;input.sent_at_ns=run.time;CHECK(bridge.receive(wire(input),"synthetic-producer",run.time));
  mb::MtpFeed feed;bool directReceived=false;
  bridge.fanout.flush([&](mb::Channel channel,std::string_view bytes){if(channel==mb::Channel::Steamvr){CHECK(feed.receive(bytes,run.time,"synthetic-direct-feed"));directReceived=true;}return true;},run.time);
  CHECK(directReceived);const auto& direct=feed.devices.at({mb::outputSource(saved.bridgeId,"pico"),"logical-pico"});
  const auto submittedRaw=referenceOriginDirectPose(direct,run.time);closePose(submittedRaw,rawReference);
  const auto displayedStanding=mb::composeRigid(room,submittedRaw);closePose(displayedStanding,standingReference);
  CHECK(distance(submittedRaw.translation,standingReference.translation)>.1&&mb::rotationDistance(submittedRaw.rotation,standingReference.rotation)>.1);
  const auto oldDoubleRoom=mb::composeRigid(room,standingReference);
  CHECK(distance(oldDoubleRoom.translation,standingReference.translation)>.1&&mb::rotationDistance(oldDoubleRoom.rotation,standingReference.rotation)>.1);
  std::cout<<"PASS Steamvr Raw actual provider -> solver -> exact native candidate -> Bridge -> MtpFeed -> actual directPose -> mocked room transform; full position/orientation match reference, no double room transform\n";
 }
 for(auto policy:{mb::Policy::Monaka,mb::Policy::Both,mb::Policy::Disabled}){
  fixture::Run run(base);selectPolicy(run,policy);require(run.execute(run.open()),"OPEN");const auto frozen=run.provider.configuration();
  CHECK(frozen->referenceOrigin()==lc::ReferenceOrigin::Standing);rf::State state(run.time);state.frame=rf::matrix(room);auto provider=rf::provider(state);provider->configure(frozen);std::atomic<bool> cancel{false};
  const auto batch=provider->discover(cancel);CHECK(state.origin==vr::TrackingUniverseStanding&&batch.referenceValid);
  closePose(batch.references.front().pose,mb::composeRigid(room,mb::composeRigid(mb::composeRigid(state.world,state.source()),state.extrinsic)));
  state.frame.m[0][3]+=.01f;rejects([&]{provider->guard();});provider->shutdown();
 }
 std::cout<<"PASS Monaka/Both/Disabled keep Standing reference including nonidentity room/full orientation; exact Standing witness guards still reject changes\n";
 {
  fixture::Run run(base);selectPolicy(run,mb::Policy::Steamvr);require(run.execute(run.open()),"OPEN");const auto frozen=run.provider.configuration();CHECK(frozen->referenceOrigin()==lc::ReferenceOrigin::Raw);
  auto changed=lc::Json::parse(run.original);changed["policy"]="monaka";{std::ofstream file(run.path,std::ios::binary|std::ios::trunc);file<<changed.dump();CHECK(bool(file));}
  CHECK(frozen->referenceOrigin()==lc::ReferenceOrigin::Raw);auto refused=run.execute(run.command("discover"));require(refused,"REJECTED");
  CHECK(refused.at("reason")=="CONFIG_EXACT_BYTES_CHANGED"&&run.provider.discoveries==0&&run.provider.captures==0&&run.session->commitCalls()==0);
 }
 std::cout<<"PASS output-policy reference origin is immutable at Open; same-revision policy drift rejects by exact config snapshot\n";
 std::cout<<"PASS calibration reference origin; fake public SDK/mirror and mocked SteamVR room application only; real SDK/runtime/socket/HIL NOT_RUN; Both Direct frame mismatch UNRESOLVED\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
