#include "monaka_bridge/trusted_hmd_common_world_coordinator.hpp"
#include "../src/authority/common_world_facts.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace monaka_bridge;
namespace w=authority_wire;
namespace d=common_world_detail;
using Events=std::vector<w::Envelope>;
static int cases=0, checks=0;
#define CHECK(x) do { ++checks; if(!(x)) throw std::runtime_error(std::string(#x)+" line "+std::to_string(__LINE__)); } while(false)
const std::string S1="11111111-1111-4111-8111-111111111111",S2="22222222-2222-4222-8222-222222222222",
 S3="33333333-3333-4333-8333-333333333333",S4="44444444-4444-4444-8444-444444444444";
std::string encode(const w::Envelope& v) { std::string out;w::Error e;CHECK(w::EncodeEnvelope(v,out,e));return out; }
w::Envelope decode(const std::string& s) { w::Envelope v;w::Error e;CHECK(w::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(s.data()),s.size(),v,e));return v; }
void append(Events& all,const Events& next) {
 for(const auto& v:next) { const auto bytes=encode(v);CHECK(encode(decode(bytes))==bytes);all.push_back(v); }
}
template<class F> void run(const std::string& name,F f) {
 try { f();++cases;std::cout<<"PASS "<<name<<'\n'; }
 catch(const std::exception& e) {throw std::runtime_error(name+": "+e.what());}
}
CommonWorldAuthorityOwner::TokenFactory factory(std::string prefix) {
 auto counter=std::make_shared<unsigned>(0);return [prefix,counter]{return prefix+std::to_string(++*counter);};
}
struct Rig {
 TrustedHmdCommonWorldCoordinator c;
 w::TrustedHmdSourceAuthority a;
 w::TrustedHmdSourcePose p;
 w::TrustedHmdSourceUnavailable u;
 Events output;
 std::int64_t at=100;
 Rig(std::size_t capacity=128,std::int64_t limit=INT64_MAX)
  : c("synthetic-alvr",{"synthetic-bridge","common-A",9,9},factory("world-"),factory("calibration-"),capacity,limit) {
  std::ifstream input(std::string(ROOT_DIR)+"/third_party/monaka-protocol-v2/fixtures/v2.1/ordered-streams/source-normal.jsonl");
  std::string line;CHECK(bool(std::getline(input,line)));a=std::get<w::TrustedHmdSourceAuthority>(decode(line));
  CHECK(bool(std::getline(input,line)));p=std::get<w::TrustedHmdSourcePose>(decode(line));
  CHECK(bool(std::getline(input,line)));u=std::get<w::TrustedHmdSourceUnavailable>(decode(line));
 }
 void publisher() { auto r=c.beginPublisherSession(S1,S2,at++);CHECK(r.status=="session");append(output,r.events); }
 void source() { auto r=c.beginSourceSession(a.publisher_id,a.session_id,a.clock_id,a,at++);CHECK(r.status=="authority");append(output,r.events); }
 CommonWorldTransitionReceipt receipt() const {
  CHECK(c.owner().pendingTransition());auto request=*c.owner().pendingTransition();
  return {request,request.minimumWorldRevision,request.minimumMappingRevision,true};
 }
 void commit() {append(output,c.commitWorldTransition(receipt(),at++));CHECK(c.owner().currentWorld());CHECK(c.owner().currentMapping());}
 void live() {publisher();source();commit();}
 void pose() {auto r=c.receiveSource(p,at++);CHECK(r.status=="pose");append(output,r.events);}
 void nextPose(std::int64_t sequence) {p.sequence=sequence;++p.source.observation_id;pose();}
 CommonWorldMappingReceipt mapping(std::uint32_t revision) const {
  CHECK(c.owner().currentMapping());auto map=*c.owner().currentMapping();map.mapping.mapping_revision=revision;
  return {map.mapping,map.transform,true};
 }
};
int main(int argc,char** argv) try {
 CHECK(argc==2);const std::filesystem::path directory=argv[1];std::filesystem::create_directories(directory);
 auto save=[&](const std::string& name,const Events& events) {
  std::ofstream out(directory/(name+".jsonl"));CHECK(out.good());
  for(const auto& e:events) out<<encode(e)<<'\n';CHECK(out.good());
 };
 run("no source, silent publisher, no receipt and delayed activation",[&]{
  Rig r;r.publisher();CHECK(r.output.empty());CHECK(!r.c.owner().currentWorld());r.source();
  CHECK(r.output.empty());CHECK(!r.c.owner().currentWorld());auto receipt=r.receipt();receipt.persistentCommitConfirmed=false;
  CHECK(r.c.commitWorldTransition(receipt,r.at++).empty());r.pose();CHECK(r.output.empty());r.commit();
  CHECK(r.output.size()==2);CHECK(std::holds_alternative<w::CommonWorldAuthorityPublication>(r.output[0]));
  CHECK(std::holds_alternative<w::CommonWorldMappingPublication>(r.output[1]));
  CHECK(r.c.receiveSource(r.p,r.at++).events.empty());r.nextPose(2);CHECK(r.output.size()==3);save("normal",r.output);
 });
 run("exact receipt fields, stale receipt, no token mint before durable commit",[&]{
  int mints=0;CommonWorldAuthorityOwner owner({"bridge","common",0,0},[&]{++mints;return "world";},[&]{++mints;return "calibration";});
  Rig r;TrustedHmdSourceStream source(r.a.publisher_id);CHECK(source.beginAuthorizedPublisherSession(r.a.publisher_id,S1,S2));
  CHECK(source.receive(r.a,0)=="authority");CHECK(owner.reconcile(source).empty());CHECK(mints==0);
  auto req=*owner.pendingTransition();CommonWorldTransitionReceipt good{req,1,1,true};
  for(int i=0;i<12;++i) {auto bad=good;
   switch(i) {case 0:bad.request.requestId++;break;case 1:bad.request.ownerId="foreign";break;
    case 2:bad.request.worldSpaceId="foreign";break;case 3:bad.request.anchor.source_space.source_id="foreign";break;
    case 4:bad.request.anchor.source_space.source_authority_session_epoch="foreign";break;
    case 5:bad.request.anchor.source_space.source_space_id="foreign";break;
    case 6:bad.request.anchor.source_space.source_space_generation++;break;
    case 7:bad.request.anchor.source_time_domain_id="foreign";break;
    case 8:bad.request.anchor.source_space_kind="foreign";break;
    case 9:bad.request.minimumWorldRevision++;break;case 10:bad.request.minimumMappingRevision++;break;
    case 11:bad.request.reason="foreign";break;}
   CHECK(owner.commitWorldTransition(bad).empty());CHECK(!owner.currentWorld());CHECK(mints==0);
  }
  CHECK(owner.commitWorldTransition(good).size()==2);CHECK(mints==2);CHECK(owner.commitWorldTransition(good).empty());
 });
 run("pending anchor replacement and ABA request retirement",[&]{
  Rig r;r.publisher();r.source();auto old=r.receipt();auto a=r.a;a.sequence=1;a.source_space.source_space_generation++;
  CHECK(r.c.receiveSource(a,r.at++).status=="authority");CHECK(r.c.owner().pendingTransition()->requestId!=old.request.requestId);
  CHECK(r.c.commitWorldTransition(old,r.at++).empty());CHECK(!r.c.owner().currentWorld());r.commit();
 });
 run("persistent world and mapping monotonicity on replacement",[&]{
  Rig r;r.live();auto a=r.a;a.sequence=1;a.source_space.source_space_generation++;
  auto changed=r.c.receiveSource(a,r.at++);CHECK(changed.events.size()==1);CHECK(!r.c.owner().currentWorld());
  auto receipt=r.receipt();auto bad=receipt;bad.worldRevision=9;CHECK(r.c.commitWorldTransition(bad,r.at++).empty());
  bad=receipt;bad.mappingRevision=10;CHECK(r.c.commitWorldTransition(bad,r.at++).empty());
  CHECK(!r.c.owner().currentWorld());r.commit();CHECK(r.c.owner().highWater().lastMappingRevision==11);
 });
 run("generation replacement ordered revoke, receipt, world, mapping, new observation",[&]{
  Rig r;r.live();r.pose();auto old=r.c.owner().currentWorld()->world;
  auto a=r.a;a.sequence=2;a.source_space.source_space_generation++;
  auto result=r.c.receiveSource(a,r.at++);CHECK(result.events.size()==1);append(r.output,result.events);
  CHECK(std::get<w::CommonWorldRevocation>(result.events[0]).reason=="source_space_changed");
  r.p.source.source_space=a.source_space;r.p.sequence=3;r.p.source.observation_id++;
  CHECK(r.c.receiveSource(r.p,r.at++).events.empty());r.commit();r.nextPose(4);
  CHECK(r.c.owner().currentWorld()->world.world_epoch!=old.world_epoch);save("world-replace",r.output);
 });
 run("source token replacement and unchanged publisher handoff",[&]{
  Rig r;r.live();r.pose();auto old=*r.c.owner().currentMapping();
  auto a=r.a;a.session_id=S3;a.clock_id=S4;
  auto result=r.c.beginSourceSession(a.publisher_id,S3,S4,a,r.at++);CHECK(result.events.empty());
  CHECK(d::mapping(r.c.owner().currentMapping()->mapping)==d::mapping(old.mapping));CHECK(!r.c.owner().pendingTransition());
  a.sequence=1;a.source_space.source_authority_session_epoch="new-source-token";
  result=r.c.receiveSource(a,r.at++);CHECK(result.events.size()==1);
  CHECK(std::get<w::CommonWorldRevocation>(result.events[0]).reason=="source_session_changed");CHECK(r.c.owner().pendingTransition());
 });
 run("unavailable exact evidence, duplicate and old observations do not recover",[&]{
  Rig r;r.live();r.pose();auto map=*r.c.owner().currentMapping();
  auto unavailable=r.c.receiveSource(r.u,r.at++);append(r.output,unavailable.events);CHECK(unavailable.events.size()==1);
  const auto& u=std::get<w::TrustedHmdCommonUnavailable>(unavailable.events[0]);CHECK(u.reason==r.u.reason);
  CHECK(u.validity && !u.validity->position_valid && u.validity->orientation_valid);
  CHECK(d::mapping(map.mapping)==d::mapping(r.c.owner().currentMapping()->mapping));
  r.p.sequence=3;CHECK(r.c.receiveSource(r.p,r.at++).status=="duplicate-observation");
  r.p.sequence=4;--r.p.source.observation_id;CHECK(r.c.receiveSource(r.p,r.at++).status=="old-observation");
  ++r.p.source.observation_id;r.nextPose(5);save("unavailable-recovery",r.output);
 });
 run("unknown unavailable validity stays null",[&]{Rig r;r.live();r.pose();r.u.validity.reset();
  auto result=r.c.receiveSource(r.u,r.at++);CHECK(result.events.size()==1);CHECK(!std::get<w::TrustedHmdCommonUnavailable>(result.events[0]).validity);
 });
 run("same world mapping update and rollback/idempotence",[&]{
  Rig r;r.live();r.pose();auto old=*r.c.owner().currentMapping();auto receipt=r.mapping(11);
  auto update=r.c.updateMapping(receipt,r.at++);CHECK(update.size()==1);append(r.output,update);
  CHECK(std::holds_alternative<w::CommonWorldMappingPublication>(update[0]));
  CHECK(d::world(r.c.owner().currentWorld()->world)==d::world(old.mapping.world));
  CHECK(r.c.owner().currentMapping()->mapping.calibration_epoch==old.mapping.calibration_epoch);
  CHECK(r.c.updateMapping(r.mapping(10),r.at++).empty());CHECK(r.c.updateMapping(receipt,r.at++).empty());
  r.nextPose(2);CHECK(std::get<w::TrustedHmdCommonPose>(r.output.back()).mapping.mapping_revision==11);save("mapping-update",r.output);
 });
 run("equal revision changed mapping content immediately revokes",[&]{
  for(int i=0;i<3;++i){Rig r;r.live();auto receipt=r.mapping(10);
   if(i==0) receipt.transform.translation_xyz[0]=-0.0;
   if(i==1) receipt.mapping.calibration_epoch="changed";
   if(i==2) receipt.mapping.source_space.source_space_id="changed";
   auto result=r.c.updateMapping(receipt,r.at++);CHECK(result.size()==1);CHECK(!r.c.owner().currentMapping());CHECK(r.c.owner().currentWorld());
   CHECK(std::get<w::CommonWorldMappingRevocation>(result[0]).reason=="mapping_conflict");
  }
 });
 run("mapping revoke without pose and fresh reestablishment",[&]{
  Rig r;r.live();r.pose();auto old=*r.c.owner().currentMapping();auto result=r.c.revokeMapping(r.at++);
  CHECK(result.size()==1);append(r.output,result);CHECK(r.c.owner().currentWorld());CHECK(!r.c.owner().currentMapping());
  save("mapping-revoke",r.output);r.nextPose(2);CHECK(r.output.size()==4);
  CommonWorldMappingReestablishmentReceipt receipt{old.mapping.world,r.a,10,true};
  CHECK(r.c.reestablishMapping(receipt,r.at++).empty());receipt.mappingRevision=11;
  result=r.c.reestablishMapping(receipt,r.at++);CHECK(result.size()==1);
  CHECK(r.c.owner().currentMapping()->mapping.calibration_epoch!=old.mapping.calibration_epoch);
  append(r.output,result);r.nextPose(3);save("mapping-reestablish",r.output);
 });
 run("world revocation at EOF immediately blocks all poses",[&]{
  Rig r;r.live();r.pose();auto result=r.c.revokeWorld(r.at++);CHECK(result.size()==1);append(r.output,result);
  CHECK(!r.c.owner().currentWorld());CHECK(!r.c.owner().currentMapping());save("world-revoke-no-pose",r.output);
 });
 run("publisher start with existing authority and restart full snapshot",[&]{
  Rig r;r.source();r.commit();CHECK(r.output.empty());r.publisher();r.pose();auto old=*r.c.owner().currentMapping();
  auto result=r.c.beginPublisherSession(S3,S4,r.at++);CHECK(result.events.size()==2);append(r.output,result.events);
  CHECK(d::mapping(std::get<w::CommonWorldMappingPublication>(result.events[1]).mapping)==d::mapping(old.mapping));
  CHECK(std::get<w::CommonWorldAuthorityPublication>(result.events[0]).sequence==0);
  CHECK(std::get<w::CommonWorldMappingPublication>(result.events[1]).sequence==1);r.nextPose(2);save("publisher-reconnect",r.output);
  CHECK(r.c.beginPublisherSession(S1,S2,r.at++).status=="session-rejected");
 });
 run("restart while mapping revoked remains silent until complete snapshot",[&]{
  Rig r;r.live();auto old=*r.c.owner().currentMapping();r.c.revokeMapping(r.at++);
  auto start=r.c.beginPublisherSession(S3,S4,r.at++);CHECK(start.events.empty());
  CommonWorldMappingReestablishmentReceipt receipt{old.mapping.world,r.a,11,true};
  auto result=r.c.reestablishMapping(receipt,r.at++);CHECK(result.size()==2);
  CHECK(std::holds_alternative<w::CommonWorldAuthorityPublication>(result[0]));
  CHECK(std::get<w::CommonWorldMappingPublication>(result[1]).sequence==1);
 });
 run("all six exact validity facts and binary64 identity, no normalization",[&]{
  for(int flag=0;flag<6;++flag){Rig r;r.live();r.p.position={-0.0,1.0000000000000002,-2.3};
   r.p.orientation={-0.0,0.0,-0.0,-1.000004};r.p.source.source_locate_time_ns=INT64_MIN;
   bool* facts[]={&r.p.validity.position_valid,&r.p.validity.orientation_valid,&r.p.validity.position_tracked,
    &r.p.validity.orientation_tracked,&r.p.validity.view_position_valid,&r.p.validity.view_orientation_valid};*facts[flag]=false;
   r.pose();auto pose=std::get<w::TrustedHmdCommonPose>(r.output.back());
   CHECK(d::bits(pose.source_position)==d::bits(r.p.position));CHECK(d::bits(pose.common_position)==d::bits(r.p.position));
   CHECK(d::bits(pose.source_orientation)==d::bits(r.p.orientation));CHECK(d::bits(pose.common_orientation)==d::bits(r.p.orientation));
   CHECK(pose.source.observation_id==r.p.source.observation_id);CHECK(pose.source.source_locate_time_ns==INT64_MIN);
   CHECK(pose.source.source_time_domain_id==r.p.source.source_time_domain_id);
   CHECK(pose.validity.position_valid==r.p.validity.position_valid);CHECK(pose.validity.orientation_valid==r.p.validity.orientation_valid);
   CHECK(pose.validity.position_tracked==r.p.validity.position_tracked);CHECK(pose.validity.orientation_tracked==r.p.validity.orientation_tracked);
   CHECK(pose.validity.view_position_valid==r.p.validity.view_position_valid);CHECK(pose.validity.view_orientation_valid==r.p.validity.view_orientation_valid);
   auto wire=std::get<w::TrustedHmdCommonPose>(decode(encode(pose)));CHECK(d::bits(wire.common_orientation)==d::bits(r.p.orientation));
   r.p.position[1]=44;CHECK(d::bits(pose.source_position)!=d::bits(r.p.position));
  }
 });
 run("gap/conflict/clock/malformed/disconnect kill epoch even after identical bootstrap",[&]{
  for(int kind=0;kind<6;++kind){Rig r;r.live();r.pose();const auto old=*r.c.owner().currentMapping();Events loss;
   if(kind==0){auto p=r.p;p.sequence=3;loss=r.c.receiveSource(p,r.at++).events;}
   if(kind==1){auto p=r.p;p.position[0]=44;loss=r.c.receiveSource(p,r.at++).events;}
   if(kind==2){auto u=r.u;u.clock_id=S4;loss=r.c.receiveSource(u,r.at++).events;}
   if(kind==3)loss=r.c.receiveSourceBytes("{",r.at++).events;
   if(kind==4)loss=r.c.onSourceTransportDisconnected(r.at++);
   if(kind==5){auto p=r.p;p.sequence=2;p.position[0]=44;loss=r.c.receiveSource(p,r.at++).events;}
   CHECK(loss.size()==1);CHECK(!r.c.owner().currentWorld());CHECK(!r.c.owner().currentMapping());
   auto a=r.a;a.session_id=S3;a.clock_id=S4;
   auto reconnect=r.c.beginSourceSession(a.publisher_id,S3,S4,a,r.at++);
   if(kind==5){CHECK(!r.c.owner().pendingTransition());a.sequence=1;a.source_space.source_space_generation++;
    CHECK(r.c.receiveSource(a,r.at++).status=="authority");}
   else CHECK(reconnect.status=="authority");
   CHECK(r.c.owner().pendingTransition());r.commit();
   CHECK(r.c.owner().currentMapping()->mapping.world.world_epoch!=old.mapping.world.world_epoch);
   CHECK(r.c.owner().currentMapping()->mapping.calibration_epoch!=old.mapping.calibration_epoch);
   CHECK(r.c.owner().currentWorld()->world.coordinate_space.revision==11);
  }
 });
 run("all source revocation reason mappings at EOF",[&]{
  const std::vector<std::pair<std::string,std::string>> reasons={{"event_stream_lost","event_stream_lost"},
   {"source_session_loss","source_session_changed"},{"source_space_ambiguous","source_space_changed"},
   {"authority_withdrawn","anchor_authority_lost"},{"capacity_exhausted","capacity_exhausted"}};
  for(const auto& reason:reasons){Rig r;r.live();r.pose();w::TrustedHmdSourceRevocation revoke;
   d::header(revoke,r.a.publisher_id,S1,S2,2,0);revoke.source_space=r.a.source_space;revoke.reason=reason.first;
   auto result=r.c.receiveSource(revoke,r.at++);CHECK(result.events.size()==1);
   CHECK(std::get<w::CommonWorldRevocation>(result.events[0]).reason==reason.second);CHECK(!r.c.owner().currentWorld());
  }
 });
 run("owner UINT32 world/mapping exhaustion without mint",[&]{
  for(int kind=0;kind<2;++kind){int mints=0;Rig r;CommonWorldAuthorityOwner owner({"bridge","common",kind==0?UINT32_MAX:0,kind==1?UINT32_MAX:0},
   [&]{++mints;return "world";},[&]{++mints;return "calibration";});
   TrustedHmdSourceStream s(r.a.publisher_id);CHECK(s.beginAuthorizedPublisherSession(r.a.publisher_id,S1,S2));CHECK(s.receive(r.a,0)=="authority");
   owner.reconcile(s);CHECK(owner.exhausted());CHECK(!owner.pendingTransition());CHECK(mints==0);
  }
 });
 run("last durable revisions usable once and then no wrap",[&]{Rig r;r.live();auto update=r.mapping(UINT32_MAX);
  CHECK(r.c.updateMapping(update,r.at++).size()==1);auto a=r.a;a.sequence=1;a.source_space.source_space_generation++;
  CHECK(r.c.receiveSource(a,r.at++).events.size()==1);CHECK(r.c.owner().exhausted());CHECK(!r.c.owner().pendingTransition());
 });
 run("token duplicates, invalid IDs, cross-kind reuse and factory exceptions fail closed",[&]{
  for(int kind=0;kind<6;++kind){Rig fixture;auto wt=factory("world-");auto ct=factory("calibration-");
   if(kind==0)wt=[]{return "world";};if(kind==1)ct=[]{return "calibration";};
   if(kind==2)wt=[]{return " ";};if(kind==3)ct=[]{return std::string(97,'x');};
   if(kind==4){wt=[]{return "shared";};ct=[]{return "shared";};}
   if(kind==5)wt=[]()->std::string{throw std::runtime_error("factory failure");};
   TrustedHmdCommonWorldCoordinator c(fixture.a.publisher_id,{"bridge","common",0,0},wt,ct);
   c.beginSourceSession(fixture.a.publisher_id,S1,S2,fixture.a,0);
   auto commit=[&]{auto req=*c.owner().pendingTransition();return c.commitWorldTransition({req,req.minimumWorldRevision,req.minimumMappingRevision,true},1);};
   commit();if(kind<2){CHECK(c.owner().currentWorld());auto a=fixture.a;a.sequence=1;a.source_space.source_space_generation++;
    c.receiveSource(a,2);commit();}
   CHECK(c.owner().exhausted());CHECK(!c.owner().currentWorld());CHECK(!c.owner().pendingTransition());CHECK(c.owner().highWater().lastWorldRevision);
  }
 });
 run("bounded non-evicted token tombstones close owner",[&]{
  Rig r(1);r.live();auto a=r.a;a.sequence=1;a.source_space.source_space_generation++;
  CHECK(r.c.receiveSource(a,r.at++).events.size()==1);CHECK(r.c.commitWorldTransition(r.receipt(),r.at++).empty());
  CHECK(r.c.owner().exhausted());CHECK(!r.c.owner().currentWorld());
 });
 run("publisher UUID validation, session/clock nonreuse and capacity isolation",[&]{
  Rig r(1);r.live();CHECK(r.c.beginPublisherSession("invalid",S4,r.at++).status=="session-rejected");
  CHECK(!r.c.publisher().exhausted());CHECK(r.c.beginPublisherSession(S3,S4,r.at++).status=="session-rejected");
  CHECK(r.c.publisher().exhausted());CHECK(r.c.owner().currentWorld());
  CommonWorldPublisher p("bridge");CHECK(p.beginSession(S1,S2));CHECK(!p.beginSession(S3,S2));CHECK(!p.beginSession(S1,S4));
 });
 run("sequence exhaustion keeps owner, fresh restart reboots snapshot",[&]{
  Rig r(128,3);r.live();r.pose();auto revoked=r.c.revokeMapping(r.at++);CHECK(revoked.size()==1);
  CHECK(std::get<w::CommonWorldMappingRevocation>(revoked[0]).sequence==3);CHECK(!r.c.publisher().sessionActive());
  CHECK(r.c.owner().currentWorld());CHECK(r.c.beginPublisherSession(S3,S4,r.at++).events.empty());
  auto world=r.c.owner().currentWorld()->world;CommonWorldMappingReestablishmentReceipt receipt{world,r.a,11,true};
  CHECK(r.c.reestablishMapping(receipt,r.at++).size()==2);
 });
 run("atomic batch reservation and INT64_MAX-safe budget",[&]{
  Rig r(128,2);r.live();auto a=r.a;a.sequence=1;a.source_space.source_space_generation++;
  CHECK(r.c.receiveSource(a,r.at++).events.size()==1);r.commit();CHECK(r.c.owner().currentWorld());CHECK(!r.c.publisher().sessionActive());
  CHECK(r.c.beginPublisherSession(S3,S4,r.at++).events.size()==2);
  // Default INT64_MAX arithmetic is exercised on every normal bootstrap; reduced limit runs the identical terminal path.
 });
 run("backward publisher clock closes session, authority untouched",[&]{
  Rig r;r.live();auto old=*r.c.owner().currentWorld();CHECK(r.c.updateMapping(r.mapping(11),0).empty());
  CHECK(!r.c.publisher().sessionActive());CHECK(d::world(r.c.owner().currentWorld()->world)==d::world(old.world));
 });
 run("source reconnect bootstrap failure revokes immediately",[&]{
  Rig r;r.live();auto p=r.p;p.session_id=S3;p.clock_id=S4;p.sequence=0;
  auto result=r.c.beginSourceSession(r.a.publisher_id,S3,S4,p,r.at++);
  CHECK(result.status=="bootstrap-failed");CHECK(result.events.size()==1);CHECK(!r.c.owner().currentWorld());
 });
 run("publisher validates owner and exact bootstrap references atomically",[&]{
  for(int field=0;field<3;++field){Rig r;r.source();r.commit();auto snapshot=r.c.owner().fullSnapshot();
   auto& world=std::get<w::CommonWorldAuthorityPublication>(snapshot[0]);
   auto& map=std::get<w::CommonWorldMappingPublication>(snapshot[1]);
   if(field==0){world.world.owner_id="foreign";map.mapping.world.owner_id="foreign";}
   if(field==1)map.mapping.world.world_epoch="foreign";
   if(field==2)map.mapping.source_space.source_space_generation++;
   CommonWorldPublisher publisher("synthetic-bridge");CHECK(publisher.beginSession(S1,S2));
   CHECK(publisher.publish(snapshot,0).empty());CHECK(!publisher.sessionActive());CHECK(!publisher.sequenceHighWater());
  }
 });
 run("explicit replacement with unchanged anchor does not retire source generation",[&]{
  Rig r;r.live();r.pose();append(r.output,r.c.revokeWorld(r.at++));
  r.nextPose(2);CHECK(r.c.owner().pendingTransition());
  CHECK(r.c.owner().pendingTransition()->reason=="explicit_world_replacement");r.commit();r.nextPose(3);
  save("explicit-world-replace",r.output);
 });
 run("retained publications are immutable after source and mapping mutations",[&]{
  Rig r;r.live();r.pose();const auto frozen=r.output;std::vector<std::string> bytes;
  for(const auto& e:frozen)bytes.push_back(encode(e));r.c.updateMapping(r.mapping(11),r.at++);r.nextPose(2);
  for(std::size_t i=0;i<frozen.size();++i)CHECK(encode(frozen[i])==bytes[i]);
  CHECK(std::get<w::TrustedHmdCommonPose>(frozen.back()).mapping.mapping_revision==10);
 });
 std::cout<<"PASS "<<cases<<" cases / "<<checks<<" assertions\n";return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
