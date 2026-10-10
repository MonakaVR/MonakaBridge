// The original A2 OS-only test registration replaces only transport/epoch seams.
// Real producer codec, A2 controls, authority, file store, exact gates and sender run.
#define main historical_activation_tests_main
#include "common_world_activation_tests.cpp"
#undef main
#include "monaka_bridge/trusted_live_source.hpp"
#include <iomanip>
#include <sstream>

std::function<std::string()> testUuids() {
 auto n=std::make_shared<unsigned>(0);
 return [n] { std::ostringstream s;s<<"00000000-0000-4000-8000-"<<std::setw(12)<<std::setfill('0')<<std::hex<<++*n;return s.str(); };
}
TrustedLiveSourceProducer producer(std::string id="source-publisher-A",std::size_t cap=16) {
 return TrustedLiveSourceProducer({std::move(id),"registered-test-head","registered-test-stage-view",cap},testUuids());
}
std::uint32_t fbits(float value) {std::uint32_t b;std::memcpy(&b,&value,4);return b;}
J frame(std::string event="sample",std::int64_t id=0,std::string session=std::string(32,'1'),std::int64_t generation=0,std::string token=std::string(32,'7')) {
 J j={{"schema","alvr-openxr-authority-export-1"},{"event",event},{"export_session",session},
  {"export_time_ns","100"},{"export_reconnect_count",1},{"export_drop_count",0},{"export_reject_count",0},{"last_stream_failure",nullptr}};
 if(event=="sample") j.update({{"source_token",token},{"observation_id",std::to_string(id)},
  {"reference_generation",std::to_string(generation)},{"source_locate_time_ns",std::to_string(100+id)},
  {"source_pose_bits",{fbits(1.25f),fbits(2.f),fbits(-3.f),fbits(0.f),fbits(0.f),fbits(0.f),fbits(1.f)}},{"validity_mask",63}});
 return j;
}
void connect(TrustedLiveSourceProducer& p,std::string session=std::string(32,'1')) {CHECK(p.receiveExport(frame("connected",0,session).dump(),1)=="connected");}
void sample(TrustedLiveSourceProducer& p,std::int64_t id=0) {CHECK(p.receiveExport(frame("sample",id).dump(),2+id)=="pose");}
w::Envelope emitted(TrustedLiveSourceProducer& p) {auto e=p.take();CHECK(e);std::string bytes;w::Error error;CHECK(w::EncodeEnvelope(*e,bytes,error));return decode(bytes);}
J diagnostic(TrustedLiveSourceProducer& p){return J::parse(p.snapshot("test-commit","test-runtime"));}
CommonWorldControlRequest control(Rig& r,const std::string& operation) {
 auto s=r.runtime->snapshot();CommonWorldControlRequest q;q.operationId="source-test-"+std::to_string(checks);
 q.operation=operation;q.runtimeInstance=s.runtimeInstance;q.controlSession=s.controlSession;q.sourceLease=s.sourceLease;
 q.sourceGeneration=s.sender?s.sender->ingress.sourceGeneration:0;q.config=r.path;return q;
}
void configureRig(Rig& r,const std::string& publisher,bool disk=true) {
 r.runtime=CommonWorldActivationTestRegistration::make(r.record,r.sink,{}, {},false,
  disk?std::function<std::unique_ptr<CommonWorldRevisionStorage>(const fs::path&)>([](const fs::path& p){return makeCommonWorldFileStorage(p);}):std::function<std::unique_ptr<CommonWorldRevisionStorage>(const fs::path&)>{});
 r.config["enabled"]=false;r.config["codec"]="monaka-wire-2.2";r.config["source_publisher_id"]=publisher;r.save();
}
void prepareRig(Rig& r,const w::Envelope& bootstrap) {
 auto q=control(r,"prepare");q.envelope=bootstrap;auto x=r.runtime->control(q,3);
 CHECK(x.accepted&&x.result=="armed-disabled");CHECK(x.after.durableReceiptCurrent);
 CHECK(x.after.durableState->latest->sourcePublisher==std::get<w::TrustedHmdSourceAuthority>(bootstrap).publisher_id);
 CHECK(x.after.durableState->latest->transition.anchor.publisher_id==std::get<w::TrustedHmdSourceAuthority>(bootstrap).publisher_id);
 CHECK(!x.after.sender->enabled && r.sink->calls==0);
}
void activateTestOnly(Rig& r) {
 CHECK(r.runtime->refreshReadiness()=="readiness-current");const auto t=r.runtime->snapshot().readinessToken;
 CHECK(r.runtime->activateReady(t,true,4)=="active");
}
std::vector<std::string> completePath(const fs::path& dir,const std::string& publisher) {
 auto p=producer(publisher);connect(p);sample(p);auto a=emitted(p),pose=emitted(p);
 Rig r(dir);configureRig(r,publisher);prepareRig(r,a);activateTestOnly(r);r.drain();
 auto q=control(r,"input");q.envelope=pose;CHECK(r.runtime->control(q,5).result=="pose");r.drain();
 for(const auto& bytes:r.sink->frames) {
  auto decoded=decode(bytes);CHECK(std::visit([&](const auto& m)->bool {
   using T=std::decay_t<decltype(m)>;
   if constexpr(std::is_same_v<T,w::CommonWorldAuthorityPublication> || std::is_same_v<T,w::CommonWorldMappingPublication> || std::is_same_v<T,w::TrustedHmdCommonPose>)
    return m.source_publisher_id==publisher && m.version.minor==2;
   return false;
  },decoded));
 }
 CHECK(r.sink->frames.size()==3);CHECK(std::get<w::TrustedHmdCommonPose>(decode(r.sink->frames.back())).source.observation_id==0);
 return r.sink->frames;
}
template<class F>void rejected(F f){bool yes=false;try{f();}catch(...){yes=true;}CHECK(yes);}
int main(int argc,char** argv)try {
 CHECK(argc==2);const auto root=fs::absolute(argv[1])/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());fs::create_directories(root);
 run("canonical publisher independent from sessions",[&]{auto p=producer();connect(p);sample(p);auto a=std::get<w::TrustedHmdSourceAuthority>(emitted(p));CHECK(a.publisher_id==p.publisherId());CHECK(a.publisher_id!=a.session_id&&a.publisher_id!=a.source_space.source_authority_session_epoch);});
 for(const auto& id:{std::string(),std::string(" "),std::string(97,'x'),std::string("bad\n"),std::string("\xff")})
  run("invalid canonical publisher rejected before source "+std::to_string(id.size()),[id]{rejected([&]{auto p=producer(id);});});
 run("otherwise identical A B producers remain distinct",[]{auto a=producer(),b=producer("source-publisher-B");connect(a);connect(b);sample(a);sample(b);auto x=std::get<w::TrustedHmdSourceAuthority>(emitted(a)),y=std::get<w::TrustedHmdSourceAuthority>(emitted(b));CHECK(x.publisher_id!=y.publisher_id);x.publisher_id=y.publisher_id;std::string xb,yb;w::Error e;CHECK(w::EncodeEnvelope(x,xb,e)&&w::EncodeEnvelope(y,yb,e)&&xb==yb);});
 run("production envelope bootstrap pose order",[]{auto p=producer();connect(p);sample(p);auto a=std::get<w::TrustedHmdSourceAuthority>(emitted(p));auto v=std::get<w::TrustedHmdSourcePose>(emitted(p));CHECK(a.sequence==0&&v.sequence==1);CHECK(a.source_space_kind=="openxr_view_in_stage"&&a.source_time_domain_id==v.source.source_time_domain_id);});
 run("missing accepted-source field fails closed",[]{auto p=producer();connect(p);auto j=frame();j.erase("source_token");CHECK(p.receiveExport(j.dump(),2)=="invalid-export"&&!p.connected()&&!p.queueDepth());});
 run("authority export session cannot be invented by a sample",[]{auto p=producer();CHECK(p.receiveExport(frame().dump(),2)=="stale-export-session"&&!p.queueDepth());});
 run("continuous multi-event stream contiguous",[]{auto p=producer();connect(p);for(int i=0;i<8;++i)sample(p,i);for(int i=0;i<9;++i)CHECK(std::visit([i](const auto& m){return m.sequence==i;},emitted(p)));CHECK(!p.take());});
 run("temporary no data preserves authority and sequence",[]{auto p=producer();connect(p);sample(p);auto before=diagnostic(p);p.noData();auto after=diagnostic(p);CHECK(after["lifecycle"]=="TemporaryNoData"&&after["connected"]==true);CHECK(before["sequence_high_water"]==after["sequence_high_water"]&&p.queueDepth()==2);});
 for(const auto& event:{"disconnect","orderly-shutdown","stream-failure"})
  run(std::string("explicit lifecycle ")+event,[event]{auto p=producer();connect(p);sample(p);CHECK(p.receiveExport(frame(event).dump(),3)==event);CHECK(!p.connected()&&!p.queueDepth());if(std::string(event)=="stream-failure")CHECK(!diagnostic(p)["last_stream_failure"].is_null());});
 run("fresh reconnect preserves publisher and source time domain",[]{auto p=producer();connect(p);sample(p);auto first=std::get<w::TrustedHmdSourceAuthority>(emitted(p));p.disconnect("Disconnected");connect(p,std::string(32,'2'));auto f=frame("sample",1,std::string(32,'2'));CHECK(p.receiveExport(f.dump(),5)=="pose");auto next=std::get<w::TrustedHmdSourceAuthority>(emitted(p));CHECK(first.publisher_id==next.publisher_id&&first.session_id!=next.session_id&&first.clock_id!=next.clock_id);CHECK(first.source_time_domain_id==next.source_time_domain_id&&next.sequence==0);CHECK(diagnostic(p)["source_generation"]==2);});
 run("late old export session rejected independently from sequence",[]{auto p=producer();connect(p);sample(p);p.disconnect("Disconnected");connect(p,std::string(32,'2'));CHECK(p.receiveExport(frame("sample",999).dump(),5)=="stale-export-session");CHECK(!p.queueDepth());});
 run("duplicate immutable observation no sequence refresh",[]{auto p=producer();connect(p);sample(p);CHECK(p.receiveExport(frame().dump(),9)=="duplicate-observation");CHECK(p.queueDepth()==2&&diagnostic(p)["sequence_high_water"]==1);});
 run("stale observation ignored",[]{auto p=producer();connect(p);sample(p,2);CHECK(p.receiveExport(frame("sample",1).dump(),9)=="stale-observation");CHECK(p.queueDepth()==2);});
 run("same observation conflict retires source token",[]{auto p=producer();connect(p);sample(p);auto j=frame();j["validity_mask"]=0;CHECK(p.receiveExport(j.dump(),3)=="observation-conflict");CHECK(!p.connected()&&!p.queueDepth());});
 run("out of order locate time rejected",[]{auto p=producer();connect(p);sample(p);auto j=frame("sample",1);j["source_locate_time_ns"]="99";CHECK(p.receiveExport(j.dump(),4)=="stale-observation");});
 run("position metres right handed unchanged signed zero",[]{auto p=producer();connect(p);auto j=frame();j["source_pose_bits"][0]=fbits(-0.f);CHECK(p.receiveExport(j.dump(),2)=="pose");emitted(p);auto v=std::get<w::TrustedHmdSourcePose>(emitted(p));CHECK(std::signbit(v.position[0])&&v.position[1]==2&&v.position[2]==-3);});
 run("quaternion Hamilton xyzw no transform or normalize",[]{auto p=producer();connect(p);auto j=frame();const auto v=std::sqrt(.5f);j["source_pose_bits"][4]=fbits(v);j["source_pose_bits"][6]=fbits(v);CHECK(p.receiveExport(j.dump(),2)=="pose");emitted(p);auto q=std::get<w::TrustedHmdSourcePose>(emitted(p)).orientation;CHECK(q[0]==0&&q[1]==double(v)&&q[2]==0&&q[3]==double(v));});
 for(unsigned flag=0;flag<6;++flag)run("each lost validity fact preserved "+std::to_string(flag),[flag]{auto p=producer();connect(p);auto j=frame();j["validity_mask"]=63u^(1u<<flag);CHECK(p.receiveExport(j.dump(),2)=="unavailable");emitted(p);auto u=std::get<w::TrustedHmdSourceUnavailable>(emitted(p));CHECK(u.validity.has_value());CHECK(diagnostic(p)["device"]["validity_mask"]==j["validity_mask"]&&diagnostic(p)["device"]["lost"]==true);});
 run("nonfinite source never converted into usable pose",[]{auto p=producer();connect(p);auto j=frame();j["source_pose_bits"][0]=0x7fc01234u;CHECK(p.receiveExport(j.dump(),2)=="unavailable");emitted(p);CHECK(std::get<w::TrustedHmdSourceUnavailable>(emitted(p)).reason=="pose_unavailable");CHECK(diagnostic(p)["device"]["lost"]==true);});
 run("invalid quaternion no repair",[]{auto p=producer();connect(p);auto j=frame();j["source_pose_bits"][6]=fbits(2.f);CHECK(p.receiveExport(j.dump(),2)=="unavailable");emitted(p);CHECK(std::holds_alternative<w::TrustedHmdSourceUnavailable>(emitted(p)));});
 run("bounded overflow closes and counts all queued drops",[]{auto p=producer("source-publisher-A",2);connect(p);sample(p);CHECK(p.receiveExport(frame("sample",1).dump(),4)=="backpressure");auto s=diagnostic(p);CHECK(s["queue_depth"]==0&&s["drop_count"]==3&&s["reject_count"]==1&&!p.connected());});
 run("reconnect flushes stale queued observations",[]{auto p=producer();connect(p);sample(p);p.disconnect("Disconnected");CHECK(diagnostic(p)["drop_count"]==2);connect(p,std::string(32,'2'));CHECK(p.receiveExport(frame("sample",1,std::string(32,'2')).dump(),4)=="pose");CHECK(std::get<w::TrustedHmdSourceAuthority>(emitted(p)).sequence==0);});
 run("reference generation boundary fresh publication no relabel",[]{auto p=producer();connect(p);sample(p);auto old=std::get<w::TrustedHmdSourceAuthority>(emitted(p));auto j=frame("sample",1,std::string(32,'1'),1);CHECK(p.receiveExport(j.dump(),4)=="source-changed");auto next=std::get<w::TrustedHmdSourceAuthority>(emitted(p));CHECK(next.source_space.source_space_generation==1&&old.source_space.source_space_generation==0);CHECK(next.session_id!=old.session_id&&next.publisher_id==old.publisher_id&&next.sequence==0);});
 run("duplicate stale gap behavior uses existing production source validator",[]{auto p=producer();connect(p);sample(p);auto a=std::get<w::TrustedHmdSourceAuthority>(emitted(p));auto v=std::get<w::TrustedHmdSourcePose>(emitted(p));TrustedHmdSourceStream s(a.publisher_id);CHECK(s.beginAuthorizedPublisherSession(a.publisher_id,a.session_id,a.clock_id));CHECK(s.receive(a,1)=="authority");CHECK(s.receive(v,2)=="pose"&&s.receive(v,3)=="duplicate");CHECK(s.receive(a,4)=="old");v.sequence=3;CHECK(s.receive(v,5)=="gap"&&!s.streamValid());});
 run("A2 valid producer input existing authority file store gates",[&]{auto p=producer();connect(p);sample(p);Rig r(root/"a2-valid");configureRig(r,p.publisherId());prepareRig(r,emitted(p));auto q=control(r,"input");q.envelope=emitted(p);CHECK(r.runtime->control(q,5).result=="pose");auto s=r.runtime->snapshot();CHECK(s.sender->ingress.runtime.sourcePublisher==p.publisherId());CHECK(s.durableReceiptCurrent&&!s.sender->enabled);std::ifstream f(root/"a2-valid/world.rev",std::ios::binary);std::string magic(8,' ');f.read(magic.data(),8);CHECK(magic=="MCWREV02");});
 run("A2 wrong publisher association rejects before preparation",[&]{auto p=producer("source-publisher-B");connect(p);sample(p);Rig r(root/"wrong-pub");configureRig(r,"source-publisher-A");auto q=control(r,"prepare");q.envelope=emitted(p);CHECK(!r.runtime->control(q,3).accepted);CHECK(!r.runtime->snapshot().sender || !r.runtime->snapshot().sender->enabled);});
 run("A2 reconnect grants neither owner approval nor publication",[&]{auto p=producer();connect(p);sample(p);Rig r(root/"a2-reconnect");configureRig(r,p.publisherId());prepareRig(r,emitted(p));auto old=control(r,"input");old.envelope=emitted(p);auto before=r.runtime->snapshot();CHECK(r.runtime->control(control(r,"source-reconnect"),6).accepted);auto after=r.runtime->snapshot();CHECK(after.sender->ingress.sourceGeneration>before.sender->ingress.sourceGeneration&&after.sourceLease!=before.sourceLease);CHECK(!after.sender->ingress.ownerApproved&&!after.sender->enabled&&r.sink->calls==0);CHECK(r.runtime->control(old,7).result=="stale-source");CHECK(after.durableState->highWater.lastWorldRevision==before.durableState->highWater.lastWorldRevision);});
 run("A2 invalid authority facts rejected by actual source receiver",[&]{auto p=producer();connect(p);sample(p);Rig r(root/"bad-authority");configureRig(r,p.publisherId());auto a=std::get<w::TrustedHmdSourceAuthority>(emitted(p));a.sequence=1;auto q=control(r,"prepare");q.envelope=a;CHECK(!r.runtime->control(q,3).accepted);CHECK(!r.runtime->snapshot().durableReceiptCurrent);});
 std::vector<std::string> aBytes,bBytes;
 run("producer A through A2 durable exact receipt ordered wire22 decode A",[&]{aBytes=completePath(root/"wire-A","source-publisher-A");});
 run("producer B through A2 durable exact receipt ordered wire22 decode B",[&]{bBytes=completePath(root/"wire-B","source-publisher-B");});
 run("same observation payload A B wire distinct",[&]{auto a=std::get<w::TrustedHmdCommonPose>(decode(aBytes.back())),b=std::get<w::TrustedHmdCommonPose>(decode(bBytes.back()));CHECK(a.source_publisher_id!=b.source_publisher_id);CHECK(a.source.observation_id==b.source.observation_id&&a.source_position==b.source_position&&a.source_orientation==b.source_orientation&&d::source(a.source.source_space)==d::source(b.source.source_space));});
 run("sender retry reconnect cannot substitute producer canonical identity",[&]{auto p=producer();connect(p);sample(p);Rig r(root/"wire-retry");configureRig(r,p.publisherId());prepareRig(r,emitted(p));activateTestOnly(r);r.sink->mode=DatagramSubmission::Rejected;CHECK(r.runtime->pump().status=="retryable-rejection");r.sink->mode=DatagramSubmission::Accepted;CHECK(r.runtime->pump().status=="local-accepted");r.drain();for(const auto& b:r.sink->frames)CHECK(J::parse(b)["source_publisher_id"]==p.publisherId());auto before=r.runtime->snapshot().sender->publisherGeneration;CHECK(r.runtime->reconnectTransport(8)=="active");r.drain();CHECK(r.runtime->snapshot().sender->publisherGeneration>before);for(const auto& b:r.sink->frames)CHECK(J::parse(b)["source_publisher_id"]==p.publisherId());});
 run("export session ABA rejected",[]{auto p=producer();connect(p);p.disconnect("Disconnected");CHECK(p.receiveExport(frame("connected").dump(),4)=="stale-export-session"&&!p.connected());});
 run("late session cannot overwrite current exporter build diagnostics",[]{auto p=producer();auto a=frame("connected");a["exporter_build_commit"]=std::string(40,'a');CHECK(p.receiveExport(a.dump(),1)=="connected");p.disconnect("Disconnected");a=frame("connected",0,std::string(32,'2'));a["exporter_build_commit"]=std::string(40,'b');CHECK(p.receiveExport(a.dump(),2)=="connected");auto old=frame();old["exporter_build_commit"]=std::string(40,'a');CHECK(p.receiveExport(old.dump(),3)=="stale-export-session");CHECK(diagnostic(p)["exporter_build_commit"]==std::string(40,'b')&&p.connected());});
 run("publication session reuse fails closed",[]{TrustedLiveSourceProducer p({"source-publisher-A","test-head","test-stage",16},[]{return "00000000-0000-4000-8000-000000000001";});connect(p);p.disconnect("Disconnected");CHECK(p.receiveExport(frame("connected",0,std::string(32,'2')).dump(),4)=="invalid-export"&&!p.connected());});
 run("negative export flags and counters rejected",[]{for(const auto* key:{"validity_mask","export_drop_count","export_reject_count"}){auto p=producer();connect(p);auto j=frame();j[key]=-1;CHECK(p.receiveExport(j.dump(),3)=="invalid-export"&&!p.connected());}});
 run("snapshot complete clock validity queue failure without secrets",[]{auto p=producer();connect(p);sample(p);emitted(p);p.disconnect("StreamFailure");auto j=diagnostic(p);for(auto field:{"build_commit","source_publisher_id","source_type","runtime_instance","connected","lifecycle","source_generation","source_session","reconnect_count","sequence_high_water","last_emitted_envelope","last_stream_failure","queue_depth","drop_count","reject_count","last_successful_a2_handoff","timestamp","device"})CHECK(j.contains(field));CHECK(j["source_publisher_id"]=="source-publisher-A"&&j["last_stream_failure"]=="StreamFailure");CHECK(j.dump().find("secret")==std::string::npos);});
 const auto fixtures=fs::path(ROOT_DIR)/"build/trusted-live-source-fixtures";fs::create_directories(fixtures);
 for(const auto& pair:{std::make_pair("publisher-A.jsonl",aBytes),std::make_pair("publisher-B.jsonl",bBytes)}){std::ofstream out(fixtures/pair.first);for(const auto& b:pair.second)out<<b<<'\n';CHECK(out.good());}
 std::cout<<"PASS trusted live source "<<cases<<" cases / "<<checks<<" assertions; production paths, deterministic source/transport only"<<std::endl;return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
