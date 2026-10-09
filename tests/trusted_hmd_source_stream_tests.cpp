#include "monaka_bridge/trusted_hmd_source_stream.hpp"
#include "monaka_bridge/bridge.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <functional>
#include <limits>
using namespace monaka_bridge;
namespace w=authority_wire;
namespace j=nlohmann;
static int checks=0, cases=0;
#define CHECK(x) do { ++checks; if(!(x)) throw std::runtime_error(std::string(#x)+" at line "+std::to_string(__LINE__)); } while(false)
const std::string S1="11111111-1111-4111-8111-111111111111", S2="22222222-2222-4222-8222-222222222222", S3="33333333-3333-4333-8333-333333333333";
std::string root=std::string(ROOT_DIR)+"/third_party/monaka-protocol-v2/fixtures/";
std::vector<std::string> lines(const std::string& path) {
 std::ifstream f(root+path); CHECK(f.good()); std::vector<std::string> out;
 for(std::string s;std::getline(f,s);) if(!s.empty()) out.push_back(s); return out;
}
w::Envelope decode(const std::string& s) {
 w::Envelope out; w::Error err;
 CHECK(w::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(s.data()),s.size(),out,err)); return out;
}
std::string wire(const w::Envelope& value) { std::string out;w::Error err;CHECK(w::EncodeEnvelope(value,out,err));return out; }
void run(const std::string& name,const std::function<void()>& f) {
 try { f(); ++cases; std::cout<<"PASS "<<name<<'\n'; }
 catch(const std::exception& e) {throw std::runtime_error(name+": "+e.what());}
}
int main() try {
 auto raw=lines("v2.1/ordered-streams/source-normal.jsonl");
 auto a=std::get<w::TrustedHmdSourceAuthority>(decode(raw[0]));
 auto p=std::get<w::TrustedHmdSourcePose>(decode(raw[1]));
 auto u=std::get<w::TrustedHmdSourceUnavailable>(decode(raw[2]));
 auto rr=lines("v2.1/ordered-streams/source-revoke-no-pose.jsonl");
 auto r=std::get<w::TrustedHmdSourceRevocation>(decode(rr[2]));
 auto begin=[&](TrustedHmdSourceStream& s){CHECK(s.beginAuthorizedPublisherSession(a.publisher_id,a.session_id,a.clock_id));};
 auto live=[&](TrustedHmdSourceStream& s){begin(s);CHECK(s.receive(a,1)=="authority");CHECK(s.receive(p,2)=="pose");};
 run("exact kit ordered source scenarios",[&]{
  std::ifstream index(root+"v2.1/ordered-streams/index.json");j::json scenarios;index>>scenarios;
  for(auto row:scenarios) {
   const auto file=row["file"].get<std::string>();auto data=lines("v2.1/"+file);
   if(j::json::parse(data[0])["protocol"]!="monaka.hmd_authority") continue;
   TrustedHmdSourceStream s("synthetic-alvr");std::set<std::string> authorized;
   for(std::size_t i=0;i<data.size();++i) {
    auto m=j::json::parse(data[i]);auto session=m["session_id"].get<std::string>();
    if(!authorized.count(session) && m["sequence"]=="0") {
     CHECK(session==S1 || session==S2); CHECK(s.beginAuthorizedPublisherSession("synthetic-alvr",session,m["clock_id"]));authorized.insert(session);
    }
    auto result=s.receiveBytes(data[i],100+static_cast<std::int64_t>(i));
    auto expected=row["outcomes"][i].get<std::string>();
    // 5AA's test-only oracle calls repeated observation presence "pose"; 5AB must ignore it.
    // Preserve the exact bytes and assert the stronger no-refresh observation contract.
    if(expected=="pose" && result=="duplicate-observation") {
     CHECK(s.candidate());CHECK(s.candidate()->received_at_ns<100+static_cast<std::int64_t>(i));
    } else if(result!=expected) throw std::runtime_error(file+":"+std::to_string(i)+" expected "+expected+" actual "+result);
    if(result=="revoked" || result=="gap" || result=="conflict") CHECK(!s.candidate());
   }
   std::cout<<"  "<<file<<" PASS\n";
  }
 });
 run("explicit authorization and fresh bootstrap",[&]{TrustedHmdSourceStream s(a.publisher_id);
  CHECK(s.receive(a,0)=="unauthorized");CHECK(!s.beginAuthorizedPublisherSession("foreign",S1,a.clock_id));begin(s);
  auto before=p;before.sequence=0;CHECK(s.receive(before,0)=="bootstrap-failed");CHECK(!s.streamValid());CHECK(!s.candidate());
 });
 run("unavailable recovery requires new observation",[&]{TrustedHmdSourceStream s(a.publisher_id);live(s);
  CHECK(s.receive(u,3)=="unavailable");auto copy=p;copy.sequence=3;
  CHECK(s.receive(copy,100)=="duplicate-observation");CHECK(!s.candidate());copy.sequence=4;copy.source.observation_id++;
  CHECK(s.receive(copy,101)=="pose");CHECK(s.candidate()->received_at_ns==101);
 });
 run("source observation replay and conflict",[&]{TrustedHmdSourceStream s(a.publisher_id);live(s);
  auto copy=p;copy.sequence=2;copy.source.observation_id--;copy.position[0]=55;
  CHECK(s.receive(copy,100)=="old-observation");CHECK(s.candidate()->received_at_ns==2);
  copy=p;copy.sequence=3;CHECK(s.receive(copy,101)=="duplicate-observation");CHECK(s.candidate()->received_at_ns==2);
  copy.sequence=4;copy.position[0]=55;CHECK(s.receive(copy,102)=="observation-conflict");CHECK(!s.currentAuthority());CHECK(!s.candidate());
 });
 run("observation high-water survives generation and session",[&]{TrustedHmdSourceStream s(a.publisher_id);live(s);
  auto next=a;next.sequence=2;next.source_space.source_space_generation++;
  CHECK(s.receive(next,3)=="authority");auto old=p;old.sequence=3;old.source.source_space=next.source_space;old.source.observation_id--;
  CHECK(s.receive(old,4)=="old-observation");CHECK(!s.candidate());
  CHECK(s.beginAuthorizedPublisherSession(a.publisher_id,S2,a.clock_id));next.sequence=0;next.session_id=S2;
  CHECK(s.receive(next,5)=="authority");old.session_id=S2;old.sequence=1;CHECK(s.receive(old,6)=="old-observation");
 });
 run("gap is immediate and never repaired in same session",[&]{TrustedHmdSourceStream s(a.publisher_id);live(s);
  auto skip=p;skip.sequence=3;CHECK(s.receive(skip,3)=="gap");CHECK(!s.candidate());CHECK(!s.currentAuthority());
  skip.sequence=2;CHECK(s.receive(skip,4)=="closed");CHECK(!s.beginAuthorizedPublisherSession(a.publisher_id,S1,a.clock_id));
  CHECK(s.beginAuthorizedPublisherSession(a.publisher_id,S2,a.clock_id));auto fresh=a;fresh.session_id=S2;
  CHECK(s.receive(fresh,5)=="authority");CHECK(s.receive(p,6)=="retired");CHECK(s.currentAuthority());
 });
 run("duplicate equality includes timestamp/version/evidence and excludes only resend metadata",[&]{
  for(int mutation=0;mutation<4;++mutation) {TrustedHmdSourceStream s(a.publisher_id);live(s);auto copy=p;
   if(mutation==0) copy.sent_at_ns++;
   if(mutation==1) copy.timestamp_ns--;
   if(mutation==2) copy.validity.position_tracked=false;
   if(mutation==3) copy.orientation={0,0,0,-1};
   CHECK(s.receive(copy,100)==(mutation==0?"duplicate":"conflict"));
   if(mutation==0) CHECK(s.candidate()->received_at_ns==2);else CHECK(!s.candidate());
  }
 });
 run("unknown optional field and JSON order do not conflict",[&]{TrustedHmdSourceStream s(a.publisher_id);live(s);
  auto json=j::json::parse(raw[1]);json["future"]="discarded";CHECK(s.receiveBytes(json.dump(),100)=="duplicate");CHECK(s.candidate()->received_at_ns==2);
 });
 run("clock change closes trust",[&]{TrustedHmdSourceStream s(a.publisher_id);live(s);auto next=u;next.clock_id=S3;
  CHECK(s.receive(next,3)=="conflict");CHECK(!s.candidate());CHECK(!s.streamValid());
 });
 run("lower replay cannot invalidate trust even with changed clock/content",[&]{TrustedHmdSourceStream s(a.publisher_id);live(s);
  auto old=a;old.clock_id=S3;old.source_time_domain_id="foreign-time";CHECK(s.receive(old,100)=="old");CHECK(s.candidate()->received_at_ns==2);CHECK(s.streamValid());
 });
 run("transport loss local invalidation retains source high-water",[&]{TrustedHmdSourceStream s(a.publisher_id);live(s);
  s.onTransportDisconnected();CHECK(!s.candidate());CHECK(!s.currentAuthority());CHECK(!s.streamValid());
  CHECK(!s.beginAuthorizedPublisherSession(a.publisher_id,S1,a.clock_id));CHECK(s.beginAuthorizedPublisherSession(a.publisher_id,S2,a.clock_id));
  auto fresh=a;fresh.session_id=S2;CHECK(s.receive(fresh,4)=="authority");auto replay=p;replay.session_id=S2;replay.source.observation_id--;
  CHECK(s.receive(replay,5)=="old-observation");CHECK(s.receive(p,6)=="retired");CHECK(s.currentAuthority());
 });
 run("source facts exact matching",[&]{for(int field=0;field<5;++field){TrustedHmdSourceStream s(a.publisher_id);live(s);auto copy=p;copy.sequence=2;
  if(field==0) copy.source.source_space.source_id="other";
  if(field==1) copy.source.source_space.source_authority_session_epoch="other-token";
  if(field==2) copy.source.source_space.source_space_id="other-space";
  if(field==3) copy.source.source_space.source_space_generation++;
  if(field==4) copy.source.source_time_domain_id="other-time";
  CHECK(s.receive(copy,3)=="untrusted");CHECK(s.candidate()->received_at_ns==2);
 }});
 run("generation stale and immutable conflict across reconnect",[&]{TrustedHmdSourceStream s(a.publisher_id);live(s);auto next=a;next.sequence=2;next.source_space.source_space_generation=2;
  CHECK(s.receive(next,3)=="authority");auto stale=a;stale.sequence=3;CHECK(s.receive(stale,4)=="untrusted");CHECK(s.currentAuthority()->source_space.source_space_generation==2);
  next.sequence=4;next.source_space.source_space_id="changed";CHECK(s.receive(next,5)=="untrusted");CHECK(!s.currentAuthority());
  CHECK(s.beginAuthorizedPublisherSession(a.publisher_id,S2,a.clock_id));next=a;next.session_id=S2;next.source_space.source_space_generation=2;
  CHECK(s.receive(next,6)=="untrusted");
 });
 run("source time-domain change requires fresh token",[&]{TrustedHmdSourceStream s(a.publisher_id);live(s);auto next=a;next.sequence=2;next.source_space.source_space_generation=1;next.source_time_domain_id="other-time";
  CHECK(s.receive(next,3)=="untrusted");next.sequence=3;next.source_time_domain_id=a.source_time_domain_id;next.source_space.source_space_generation=2;
  CHECK(s.receive(next,4)=="untrusted");CHECK(!s.currentAuthority());
 });
 run("revocation without pose and retired token cannot reconnect",[&]{for(auto reason:{"source_session_loss","event_stream_lost"}) {
  TrustedHmdSourceStream s(a.publisher_id);live(s);auto rev=r;rev.reason=reason;CHECK(s.receive(rev,3)=="revoked");CHECK(!s.candidate());
  CHECK(s.beginAuthorizedPublisherSession(a.publisher_id,S2,a.clock_id));auto next=a;next.session_id=S2;next.source_space.source_space_generation=99;
  CHECK(s.receive(next,4)=="untrusted");CHECK(!s.currentAuthority());
 }});
 run("wrong protocols cannot mutate trusted state",[&]{TrustedHmdSourceStream s(a.publisher_id);live(s);
  auto common=lines("v2.1/ordered-streams/common-normal.jsonl");CHECK(s.receiveBytes(common[0],100)=="wrong-protocol");
  for(auto name:{"v2/mtp-pose.json","v2/observation.json","v2/device-state.json","v2/tracker-state.json"}) {
   auto legacy=lines(name);std::string bytes;for(auto& line:legacy) bytes+=line;
   CHECK(s.receiveBytes(bytes,100)=="wrong-protocol");
  }
  CHECK(s.candidate()->received_at_ns==2);CHECK(s.sequenceHighWater()==1);
 });
 run("malformed authorized bytes close immediately",[&]{TrustedHmdSourceStream s(a.publisher_id);live(s);
  CHECK(s.receiveBytes("{",100)=="malformed");CHECK(!s.candidate());CHECK(!s.streamValid());
 });
 run("no eviction on publisher retention saturation",[&]{TrustedHmdSourceStream s(a.publisher_id,1);live(s);
  CHECK(!s.beginAuthorizedPublisherSession(a.publisher_id,S2,a.clock_id));CHECK(!s.candidate());CHECK(!s.streamValid());CHECK(s.publisherSessionsRetained()==1);
 });
 run("no eviction on source retention saturation",[&]{TrustedHmdSourceStream s(a.publisher_id,1);live(s);auto next=a;next.sequence=2;next.source_space.source_authority_session_epoch="another-token";
  CHECK(s.receive(next,3)=="closed");CHECK(!s.currentAuthority());CHECK(!s.streamValid());
 });
 run("U63 maximum has no wrapping acceptance",[&]{TrustedHmdSourceStream s(a.publisher_id);live(s);auto next=p;next.sequence=std::numeric_limits<std::int64_t>::max();
  CHECK(s.receive(next,3)=="gap");CHECK(!s.candidate());next.sequence=0;CHECK(s.receive(next,4)=="closed");
 });
 run("source token retirement survives publisher replacement",[&]{TrustedHmdSourceStream s(a.publisher_id);live(s);
  CHECK(s.beginAuthorizedPublisherSession(a.publisher_id,S2,a.clock_id));auto fresh=a;fresh.session_id=S2;fresh.source_space.source_authority_session_epoch="second-token";
  CHECK(s.receive(fresh,3)=="authority");CHECK(s.beginAuthorizedPublisherSession(a.publisher_id,S3,a.clock_id));fresh=a;fresh.session_id=S3;fresh.source_space.source_space_generation=99;
  CHECK(s.receive(fresh,4)=="untrusted");CHECK(!s.currentAuthority());
 });
 run("all authority types are excluded from legacy latest fanout",[&]{
  mb::Fanout fanout;TrustedHmdSourceStream s(a.publisher_id);live(s);
  std::vector<std::string> files={"source-normal","source-revoke-no-pose","common-normal","common-revoke-no-pose","mapping-revoke-no-pose"};
  std::set<std::size_t> types;
  for(auto file:files) for(auto bytes:lines("v2.1/ordered-streams/"+file+".jsonl")) {
   auto e=decode(bytes);types.insert(e.index());fanout.put(mb::Channel::Monaka,e,100);
   CHECK(fanout.pending(mb::Channel::Monaka)==0);
   if(e.index()>=8) CHECK(s.receiveBytes(bytes,100)=="wrong-protocol");
  }
  CHECK(types.size()==10);CHECK(s.candidate()->received_at_ns==2);
 });
 std::cout<<"PASS source receiver "<<cases<<" cases / "<<checks<<" assertions\n";return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
