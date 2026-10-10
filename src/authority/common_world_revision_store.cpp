#include "monaka_bridge/common_world_revision_store.hpp"
#include "common_world_facts.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#endif

namespace monaka_bridge {
namespace {
namespace d = common_world_detail;
namespace w = authority_wire;
constexpr std::size_t maxBytes = 65536, maxEpochs = 128;
void require(bool ok, const char* why) { if(!ok) throw std::runtime_error(why); }
void cancelled(const std::atomic_bool* c) { require(!c || !c->load(), "revision commit cancelled before publish"); }
struct Writer {
 std::string bytes;
 void number(std::uint64_t n, unsigned size) { for(unsigned i=0;i<size;++i) bytes.push_back(static_cast<char>(n>>(8*i))); }
 void str(const std::string& s) { require(s.size()<=4096,"oversize string"); number(s.size(),4); bytes+=s; }
 void source(const w::SourceSpaceAuthority& s) { str(s.source_id); str(s.source_authority_session_epoch); str(s.source_space_id); number(static_cast<std::uint64_t>(s.source_space_generation),8); }
 void anchor(const w::TrustedHmdSourceAuthority& a, bool bound) { if(bound) str(a.publisher_id); source(a.source_space); str(a.source_space_kind); str(a.source_time_domain_id); }
 void world(const w::CommonWorldReference& v) { str(v.owner_id); str(v.world_epoch); str(v.coordinate_space.id); str(v.coordinate_space.convention); number(v.coordinate_space.revision,4); }
 void map(const w::CommonMappingReference& m) { world(m.world); source(m.source_space); str(m.calibration_epoch); number(m.mapping_revision,4); }
 void transform(const w::RigidTransform& t) { for(auto b:d::bits(t.rotation_xyzw)) number(b,8); for(auto b:d::bits(t.translation_xyz)) number(b,8); }
};
struct Reader {
 const std::string& bytes; std::size_t offset=0;
 std::uint64_t number(unsigned n) { require(n<=bytes.size()-offset,"truncated revision record"); std::uint64_t v=0; for(unsigned i=0;i<n;++i) v|=std::uint64_t(static_cast<unsigned char>(bytes[offset++]))<<(8*i); return v; }
 std::string str() { const auto n=number(4); require(n<=4096 && n<=bytes.size()-offset,"invalid string length"); auto s=bytes.substr(offset,static_cast<std::size_t>(n)); offset+=static_cast<std::size_t>(n); return s; }
 w::SourceSpaceAuthority source() { w::SourceSpaceAuthority s; s.source_id=str(); s.source_authority_session_epoch=str(); s.source_space_id=str(); const auto g=number(8); require(g<=INT64_MAX,"invalid source generation"); s.source_space_generation=static_cast<std::int64_t>(g); return s; }
 w::TrustedHmdSourceAuthority anchor(bool bound) { w::TrustedHmdSourceAuthority a; if(bound) a.publisher_id=str(); a.source_space=source(); a.source_space_kind=str(); a.source_time_domain_id=str(); return a; }
 w::CommonWorldReference world() { w::CommonWorldReference v; v.owner_id=str(); v.world_epoch=str(); v.coordinate_space.id=str(); v.coordinate_space.convention=str(); v.coordinate_space.revision=static_cast<std::uint32_t>(number(4)); return v; }
 w::CommonMappingReference map() { w::CommonMappingReference m; m.world=world(); m.source_space=source(); m.calibration_epoch=str(); m.mapping_revision=static_cast<std::uint32_t>(number(4)); return m; }
 w::RigidTransform transform() { w::RigidTransform t; for(auto& v:t.rotation_xyzw) { auto b=number(8); std::memcpy(&v,&b,8); } for(auto& v:t.translation_xyz) { auto b=number(8); std::memcpy(&v,&b,8); } return t; }
};
void candidate(Writer& out, const CommonWorldRevisionCandidate& c, bool bound) {
 if(bound) out.str(c.sourcePublisher);
 out.str(c.authorizationEpoch); out.number(static_cast<unsigned>(c.operation),1); out.number(c.worldRevision,4); out.number(c.mappingRevision,4);
 if(c.operation==RevisionOperation::Transition) {
  const auto& r=c.transition; out.number(r.requestId,8); out.str(r.ownerId); out.str(r.worldSpaceId); out.anchor(r.anchor,bound);
  out.number(r.minimumWorldRevision,4); out.number(r.minimumMappingRevision,4); out.str(r.reason);
 } else if(c.operation==RevisionOperation::MappingUpdate) { out.map(c.mapping.mapping); out.transform(c.mapping.transform); if(bound) out.str(c.mapping.sourcePublisher); }
 else if(c.operation==RevisionOperation::MappingReestablishment) { out.world(c.reestablishment.world); out.anchor(c.reestablishment.anchor,bound); out.number(c.reestablishment.mappingRevision,4); }
 else throw std::runtime_error("unsupported revision operation");
}
CommonWorldRevisionCandidate candidate(Reader& in, bool bound) {
 CommonWorldRevisionCandidate c; if(bound) c.sourcePublisher=in.str(); c.authorizationEpoch=in.str(); c.operation=static_cast<RevisionOperation>(in.number(1));
 c.worldRevision=static_cast<std::uint32_t>(in.number(4)); c.mappingRevision=static_cast<std::uint32_t>(in.number(4));
 if(c.operation==RevisionOperation::Transition) {
  auto& r=c.transition; r.requestId=in.number(8); r.ownerId=in.str(); r.worldSpaceId=in.str(); r.anchor=in.anchor(bound);
  r.minimumWorldRevision=static_cast<std::uint32_t>(in.number(4)); r.minimumMappingRevision=static_cast<std::uint32_t>(in.number(4)); r.reason=in.str();
 } else if(c.operation==RevisionOperation::MappingUpdate) { c.mapping.mapping=in.map(); c.mapping.transform=in.transform(); if(bound) c.mapping.sourcePublisher=in.str(); }
 else if(c.operation==RevisionOperation::MappingReestablishment) { c.reestablishment.world=in.world(); c.reestablishment.anchor=in.anchor(bound); c.reestablishment.mappingRevision=static_cast<std::uint32_t>(in.number(4)); }
 else throw std::runtime_error("unsupported revision operation");
 return c;
}
std::string key(const CommonWorldRevisionCandidate& c) { Writer out; const bool bound=!c.sourcePublisher.empty(); out.number(bound,1); candidate(out,c,bound); return out.bytes; }
void validate(const CommonWorldRevisionCandidate& c, const CommonWorldPersistentHighWater& h) {
 require(d::validId(c.authorizationEpoch),"invalid authorization epoch");
 require(c.sourcePublisher==h.sourcePublisher,"candidate source publisher mismatch");
 const bool bound=!h.sourcePublisher.empty();
 require(!bound || d::validId(h.sourcePublisher),"invalid persisted source publisher");
 if(c.operation==RevisionOperation::Transition) {
  const auto& r=c.transition;
  require(!bound || r.anchor.publisher_id==c.sourcePublisher,"transition source publisher mismatch");
  require(r.requestId && r.ownerId==h.ownerId && r.worldSpaceId==h.worldSpaceId && d::validPayload(r.anchor),"transition identity/anchor mismatch");
  w::CommonWorldRevocation reason; reason.world={h.ownerId,"validation",{h.worldSpaceId,"rh_y_up_neg_z_forward",c.worldRevision}}; reason.reason=r.reason;
  require(d::validPayload(reason) && c.worldRevision>=r.minimumWorldRevision && c.mappingRevision>=r.minimumMappingRevision && c.mappingRevision>0,"invalid transition revisions/reason");
 } else {
  const auto world=c.operation==RevisionOperation::MappingUpdate?c.mapping.mapping.world:c.reestablishment.world;
  require(world.owner_id==h.ownerId && world.coordinate_space.id==h.worldSpaceId && world.coordinate_space.revision==c.worldRevision,"mapping world identity mismatch");
  if(c.operation==RevisionOperation::MappingUpdate) {
   require(c.mapping.sourcePublisher==c.sourcePublisher,"mapping receipt publisher mismatch");
   w::CommonWorldMappingPublication m; m.mapping=c.mapping.mapping; m.transform=c.mapping.transform;
   require(d::validPayload(m) && m.mapping.mapping_revision==c.mappingRevision && d::transform(m.transform)==d::transform(d::identity()),"invalid M2 mapping/transform");
  } else if(c.operation==RevisionOperation::MappingReestablishment) {
   require(!bound || c.reestablishment.anchor.publisher_id==c.sourcePublisher,"reestablishment source publisher mismatch");
   w::CommonWorldAuthorityPublication a; a.world=world; a.anchor_source=c.reestablishment.anchor.source_space;
   require(d::validPayload(a) && d::validPayload(c.reestablishment.anchor) && c.reestablishment.mappingRevision==c.mappingRevision,"invalid reestablishment");
  } else throw std::runtime_error("unsupported operation");
 }
}
std::string checksum(const std::string& bytes) {
#ifdef _WIN32
 BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
 require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"SHA256 provider failed");
 std::string digest(32,'\0');
 const auto created=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0);
 const auto fed=created>=0?BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),static_cast<ULONG>(bytes.size()),0):created;
 const auto finished=fed>=0?BCryptFinishHash(hash,reinterpret_cast<PUCHAR>(digest.data()),32,0):fed;
 if(hash) BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm,0);
 require(finished>=0,"SHA256 failed"); return digest;
#else
 throw std::runtime_error("revision store backend unsupported on this platform");
#endif
}
std::string serialize(const CommonWorldStoredState& state) {
 const bool bound=!state.highWater.sourcePublisher.empty();
 Writer out; out.bytes=bound?"MCWREV02":"MCWREV01"; out.number(bound?2:1,4); out.str(state.highWater.ownerId); out.str(state.highWater.worldSpaceId);
 if(bound) out.str(state.highWater.sourcePublisher);
 out.number(state.highWater.lastWorldRevision.has_value(),1); out.number(state.highWater.lastWorldRevision.value_or(0),4); out.number(state.highWater.lastMappingRevision,4);
 out.str(state.currentAuthorizationEpoch); out.number(state.retiredAndCurrentAuthorizationEpochs.size(),4);
 for(const auto& e:state.retiredAndCurrentAuthorizationEpochs) out.str(e);
 out.number(state.latest.has_value(),1); if(state.latest) candidate(out,*state.latest,bound);
 require(out.bytes.size()+32<=maxBytes,"revision record capacity exhausted"); out.bytes+=checksum(out.bytes); return out.bytes;
}
CommonWorldStoredState deserialize(const std::string& bytes) {
 require(bytes.size()>=44 && bytes.size()<=maxBytes,"invalid revision record size");
 const auto payload=bytes.substr(0,bytes.size()-32);
 require(checksum(payload)==bytes.substr(bytes.size()-32),"revision SHA256 mismatch");
 const bool bound=payload.substr(0,8)=="MCWREV02";
 require(bound || payload.substr(0,8)=="MCWREV01","invalid revision header"); Reader in{payload,8};
 require(in.number(4)==(bound?2:1),"incompatible revision version"); CommonWorldStoredState s;
 s.highWater.ownerId=in.str(); s.highWater.worldSpaceId=in.str();
 if(bound) { s.highWater.sourcePublisher=in.str(); require(d::validId(s.highWater.sourcePublisher),"invalid persisted source publisher"); }
 const auto present=in.number(1); require(present<=1,"invalid optional flag");
 const auto wr=static_cast<std::uint32_t>(in.number(4)); if(present) s.highWater.lastWorldRevision=wr; else require(wr==0,"noncanonical absent highwater");
 s.highWater.lastMappingRevision=static_cast<std::uint32_t>(in.number(4)); s.currentAuthorizationEpoch=in.str();
 const auto count=in.number(4); require(count>0 && count<=maxEpochs,"authorization history capacity/format invalid");
 std::set<std::string> seen;
 for(std::size_t i=0;i<count;++i) { auto e=in.str(); require(d::validId(e) && seen.insert(e).second,"invalid/reused authorization epoch"); s.retiredAndCurrentAuthorizationEpochs.push_back(e); }
 require(s.currentAuthorizationEpoch==s.retiredAndCurrentAuthorizationEpochs.back(),"current authorization mismatch");
 const auto latest=in.number(1); require(latest<=1,"invalid latest flag");
 if(latest) { s.latest=candidate(in,bound); validate(*s.latest,s.highWater); require(seen.count(s.latest->authorizationEpoch) && s.highWater.lastWorldRevision==s.latest->worldRevision && s.highWater.lastMappingRevision==s.latest->mappingRevision,"committed highwater mismatch"); }
 require(in.offset==payload.size(),"trailing revision bytes"); return s;
}
#ifdef _WIN32
struct Handle {
 HANDLE value=INVALID_HANDLE_VALUE;
 explicit Handle(HANDLE h=INVALID_HANDLE_VALUE):value(h) {}
 ~Handle() { if(value!=INVALID_HANDLE_VALUE) CloseHandle(value); }
 Handle(const Handle&)=delete; Handle& operator=(const Handle&)=delete;
 void close() { if(value!=INVALID_HANDLE_VALUE) { auto h=value; value=INVALID_HANDLE_VALUE; require(CloseHandle(h)!=0,"close failed"); } }
};
class WindowsStorage final : public CommonWorldRevisionStorage {
 std::filesystem::path path_, temp_;
 RevisionFaultHook hook_;
 Handle lock_;
 void fault(RevisionFaultBoundary b) { if(hook_) hook_(b); }
public:
 WindowsStorage(const std::filesystem::path& path,RevisionFaultHook hook):path_(std::filesystem::absolute(path)),temp_(path_.wstring()+L".tmp"),hook_(std::move(hook)) {
  require(std::filesystem::is_directory(path_.parent_path()),"store parent directory must already exist");
  wchar_t volume[MAX_PATH]{}, filesystem[MAX_PATH]{};
  require(GetVolumePathNameW(path_.c_str(),volume,MAX_PATH)!=0 && GetDriveTypeW(volume)==DRIVE_FIXED &&
   GetVolumeInformationW(volume,nullptr,0,nullptr,nullptr,nullptr,filesystem,MAX_PATH)!=0 && std::wstring(filesystem)==L"NTFS","revision store requires local fixed NTFS");
  lock_.value=CreateFileW((path_.wstring()+L".lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
  require(lock_.value!=INVALID_HANDLE_VALUE,"revision store already owned or lock unavailable");
 }
 std::optional<std::string> readCommitted() override {
  Handle file(CreateFileW(path_.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
  if(file.value==INVALID_HANDLE_VALUE) { require(GetLastError()==ERROR_FILE_NOT_FOUND,"cannot read committed revision"); return {}; }
  LARGE_INTEGER size{}; require(GetFileSizeEx(file.value,&size)!=0 && size.QuadPart>=0 && size.QuadPart<=maxBytes,"invalid committed file size");
  std::string bytes(static_cast<std::size_t>(size.QuadPart),'\0'); DWORD count=0;
  require(ReadFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)!=0 && count==bytes.size(),"committed read failed"); return bytes;
 }
 void atomicWrite(const std::string& bytes,const std::atomic_bool* cancel) override {
  cancelled(cancel); fault(RevisionFaultBoundary::BeforeWrite);
  // Under the lifetime lock this exact sibling belongs exclusively to this store.
  // A previous interrupted attempt is uncommitted, never a recovery source.
  Handle file(CreateFileW(temp_.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
  require(file.value!=INVALID_HANDLE_VALUE,"temporary revision create failed");
  try {
   DWORD count=0; const auto first=static_cast<DWORD>(bytes.size()/2);
   require(WriteFile(file.value,bytes.data(),first,&count,nullptr)!=0 && count==first,"partial revision write failed"); fault(RevisionFaultBoundary::DuringWrite);
   const auto second=static_cast<DWORD>(bytes.size())-first;
   require(WriteFile(file.value,bytes.data()+first,second,&count,nullptr)!=0 && count==second,"revision write failed");
   fault(RevisionFaultBoundary::DuringFlush); require(FlushFileBuffers(file.value)!=0,"revision flush failed"); file.close();
   fault(RevisionFaultBoundary::BeforePublish); cancelled(cancel);
   require(MoveFileExW(temp_.c_str(),path_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,"atomic revision publish failed");
   fault(RevisionFaultBoundary::AfterPublish);
  } catch(...) { file.close(); DeleteFileW(temp_.c_str()); throw; }
 }
};
#endif
}
std::unique_ptr<CommonWorldRevisionStorage> makeCommonWorldFileStorage(const std::filesystem::path& path,RevisionFaultHook hook) {
#ifdef _WIN32
 return std::make_unique<WindowsStorage>(path,std::move(hook));
#else
 throw std::runtime_error("Common World file persistence unsupported on this OS");
#endif
}
CommonWorldRevisionStore::CommonWorldRevisionStore(std::unique_ptr<CommonWorldRevisionStorage> storage,CommonWorldPersistentHighWater initial)
 : storage_(std::move(storage)),initial_(std::move(initial)) {
 require(storage_ && d::validId(initial_.ownerId) && d::validId(initial_.worldSpaceId),"explicit valid persistent identity required");
 require(initial_.sourcePublisher.empty() || d::validId(initial_.sourcePublisher),"invalid explicit source publisher");
 open();
}
bool sameCommonWorldRevisionCandidate(const CommonWorldRevisionCandidate& a,const CommonWorldRevisionCandidate& b) { return key(a)==key(b); }
CommonWorldStoredState CommonWorldRevisionStore::read() {
 require(!disposed_,"revision store disposed"); auto bytes=storage_->readCommitted();
 if(!bytes) { require(!observedRecord_,"previously committed revision disappeared"); return {initial_,{},{},{}}; }
 auto s=deserialize(*bytes); require(s.highWater.ownerId==initial_.ownerId && s.highWater.worldSpaceId==initial_.worldSpaceId,"persistent installation/world identity mismatch");
 require(s.highWater.sourcePublisher==initial_.sourcePublisher,"persistent publisher/schema mode mismatch; explicit legacy-only or schema-2 identity required");
 require(!initial_.lastWorldRevision || (s.highWater.lastWorldRevision && *s.highWater.lastWorldRevision>=*initial_.lastWorldRevision),"persisted world below supplied floor");
 require(s.highWater.lastMappingRevision>=initial_.lastMappingRevision,"persisted mapping below supplied floor");
 observedRecord_=true; return s;
}
std::optional<CommonWorldStoredState> CommonWorldRevisionStore::tryObserve() {
 std::unique_lock<std::mutex> guard(mutex_,std::try_to_lock);
 if(!guard.owns_lock()) return {};
 return read();
}
CommonWorldStoredState CommonWorldRevisionStore::open() { std::lock_guard<std::mutex> guard(mutex_); return read(); }
CommonWorldStoredState CommonWorldRevisionStore::acquireAuthorization(const std::string& epoch) {
 std::lock_guard<std::mutex> guard(mutex_); auto s=read();
 require(d::validId(epoch) && s.retiredAndCurrentAuthorizationEpochs.size()<maxEpochs &&
  std::find(s.retiredAndCurrentAuthorizationEpochs.begin(),s.retiredAndCurrentAuthorizationEpochs.end(),epoch)==s.retiredAndCurrentAuthorizationEpochs.end(),"invalid/reused/exhausted runtime authorization epoch");
 s.currentAuthorizationEpoch=epoch; s.retiredAndCurrentAuthorizationEpochs.push_back(epoch);
 storage_->atomicWrite(serialize(s),nullptr); observedRecord_=true; return s;
}
CommonWorldDurableReceipt CommonWorldRevisionStore::commit(const CommonWorldRevisionCandidate& c,const std::atomic_bool* cancel) {
 std::lock_guard<std::mutex> guard(mutex_); cancelled(cancel); auto s=read(); validate(c,s.highWater);
 require(c.authorizationEpoch==s.currentAuthorizationEpoch,"superseded runtime authorization");
 if(s.latest && key(c)==key(*s.latest)) return {*s.latest};
 if(c.operation==RevisionOperation::Transition) require(!s.highWater.lastWorldRevision || c.worldRevision>*s.highWater.lastWorldRevision,"stale/conflicting world revision");
 else require(s.highWater.lastWorldRevision==c.worldRevision,"stale/conflicting mapping world");
 require(c.mappingRevision>s.highWater.lastMappingRevision,"stale/conflicting mapping revision");
 s.highWater.lastWorldRevision=c.worldRevision; s.highWater.lastMappingRevision=c.mappingRevision; s.latest=c;
 storage_->atomicWrite(serialize(s),cancel); observedRecord_=true;
 return {c};
}
bool CommonWorldRevisionStore::matchesCurrentCommit(const CommonWorldDurableReceipt& receipt) {
 std::lock_guard<std::mutex> guard(mutex_); const auto s=read();
 return s.latest && receipt.committed.authorizationEpoch==s.currentAuthorizationEpoch && key(receipt.committed)==key(*s.latest);
}
bool CommonWorldRevisionStore::acceptCurrentCommit(const CommonWorldDurableReceipt& receipt,const std::function<void()>& callback) {
 std::unique_lock<std::mutex> guard(mutex_,std::try_to_lock); if(!guard.owns_lock()) return false; const auto s=read();
 if(!s.latest || receipt.committed.authorizationEpoch!=s.currentAuthorizationEpoch || key(receipt.committed)!=key(*s.latest)) return false;
 callback(); return true;
}
bool CommonWorldRevisionStore::tryAuthorizationGate(const std::string& epoch,const std::function<void()>& callback) {
 std::unique_lock<std::mutex> guard(mutex_,std::try_to_lock); if(!guard.owns_lock()) return false; const auto s=read();
 if(s.currentAuthorizationEpoch!=epoch) return false;
 callback(); return true;
}
void CommonWorldRevisionStore::dispose() { std::lock_guard<std::mutex> guard(mutex_); disposed_=true; storage_.reset(); }
}
