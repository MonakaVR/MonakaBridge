#include "monaka_bridge/trusted_live_source.hpp"
#include "../authority/common_world_facts.hpp"
#include <nlohmann/json.hpp>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>

namespace monaka_bridge {
namespace w=authority_wire; namespace d=common_world_detail; using J=nlohmann::json;
namespace {
std::int64_t integer(const J& j,const char* name,bool signedValue=false) {
 const auto s=j.at(name).get<std::string>(); std::int64_t n=0;
 auto r=std::from_chars(s.data(),s.data()+s.size(),n);
 if(r.ec!=std::errc() || r.ptr!=s.data()+s.size() || std::to_string(n)!=s || (!signedValue && n<0))
  throw std::runtime_error("invalid source integer");
 return n;
}
bool tokenValid(const std::string& s) {
 return s.size()==32 && std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
std::string encoded(const w::Envelope& e) {
 std::string bytes; w::Error error; if(!w::EncodeEnvelope(e,bytes,error)) throw std::runtime_error("source codec rejection"); return bytes;
}
J optionalEnvelope(const std::optional<w::Envelope>& e) { return e?J::parse(encoded(*e)):J(nullptr); }
}
TrustedLiveSourceProducer::TrustedLiveSourceProducer(TrustedLiveSourceConfig config,std::function<std::string()> fresh):
 config_(std::move(config)),fresh_(std::move(fresh)) {
 if(!fresh_ || config_.capacity<2 || config_.capacity>128) throw std::runtime_error("bounded producer configuration required");
 // Validate every opaque ID with the real production codec, including strict UTF-8.
 w::TrustedHmdSourceAuthority a;
 d::header(a,config_.sourcePublisherId,"11111111-1111-4111-8111-111111111111","22222222-2222-4222-8222-222222222222",0,0);
 a.source_space={config_.sourceId,"source-validation",config_.sourceSpaceId,0};
 a.source_space_kind="openxr_view_in_stage"; a.source_time_domain_id="clock-validation";
 encoded(a);
}
void TrustedLiveSourceProducer::freshPublication() {
 if(generation_==UINT64_MAX || publicationSessions_.size()>=256) throw std::runtime_error("publication lifetime exhausted");
 drops_+=queue_.size(); queue_.clear(); authority_.reset(); sequence_.reset();
 publisherSession_=fresh_(); clock_=fresh_();
 if(!publicationSessions_.insert(publisherSession_).second) throw std::runtime_error("publication session reuse");
 ++generation_;
}
void TrustedLiveSourceProducer::disconnect(const std::string& reason) {
 drops_+=queue_.size(); queue_.clear(); connected_=false; authority_.reset();
 lifecycle_=reason;
 if(reason=="StreamFailure" || reason=="Backpressure" || reason=="InvalidExport") lastFailure_=reason;
}
void TrustedLiveSourceProducer::noData() { if(connected_) lifecycle_="TemporaryNoData"; }
bool TrustedLiveSourceProducer::enqueue(w::Envelope e) {
 if(queue_.size()>=config_.capacity) { ++rejects_; ++drops_; disconnect("Backpressure"); return false; }
 // A malformed typed envelope cannot consume sequence or enter the stream.
 encoded(e); queue_.push_back(std::move(e)); return true;
}
std::string TrustedLiveSourceProducer::receiveExport(const std::string& frame,std::int64_t at) {
 try {
  if(frame.size()>2048 || at<0) throw std::runtime_error("export bound");
  std::vector<std::set<std::string>> keys;
  const auto j=J::parse(frame,[&](int depth,J::parse_event_t event,J& value) {
   if(depth>8) throw std::runtime_error("export depth");
   if(event==J::parse_event_t::object_start) keys.emplace_back();
   if(event==J::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second) throw std::runtime_error("duplicate export key");
   if(event==J::parse_event_t::object_end) keys.pop_back(); return true;
  });
  if(j.at("schema")!="alvr-openxr-authority-export-1") throw std::runtime_error("export schema");
  const auto event=j.at("event").get<std::string>(), session=j.at("export_session").get<std::string>();
  if(!tokenValid(session)) throw std::runtime_error("export session");
  std::optional<std::string> exporterBuild;
  if(j.contains("exporter_build_commit")) {
   const auto build=j.at("exporter_build_commit").get<std::string>();
   if(build.size()!=40 || !std::all_of(build.begin(),build.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');})) throw std::runtime_error("exporter build identity");
   exporterBuild=build;
  }
  for(const auto* field:{"export_drop_count","export_reject_count","export_reconnect_count","export_sample_skip_count","export_min_sample_interval_ms","validity_mask"}) {
   if(j.contains(field) && !j.at(field).is_number_unsigned()) throw std::runtime_error("unsigned export counter/flags required");
  }
  const auto exportDrops=j.at("export_drop_count").get<std::uint64_t>();
  const auto exportRejects=j.at("export_reject_count").get<std::uint64_t>();
  const auto exportSkips=j.value("export_sample_skip_count",std::uint64_t(0));
  const auto exportInterval=j.value("export_min_sample_interval_ms",std::uint64_t(0));
  if(event=="connected") {
   if(connected_ || exportSessions_.count(session)) { ++rejects_; return "stale-export-session"; }
   if(exportSessions_.size()>=128) throw std::runtime_error("export lifetime exhausted");
   exportSessions_.insert(session);
   exporterCommit_=exporterBuild.value_or("");exportDrops_=exportDrops;exportRejects_=exportRejects;
   exportSkips_=exportSkips;exportIntervalMs_=exportInterval;
   exportSession_=session; freshPublication(); connected_=true; ++reconnects_; lifecycle_="Connected";
   return "connected";
  }
  if(!connected_ || session!=exportSession_) { ++rejects_; return "stale-export-session"; }
  if(exporterBuild && !exporterCommit_.empty() && exporterCommit_!=*exporterBuild) throw std::runtime_error("exporter build changed");
  if(exporterBuild) exporterCommit_=*exporterBuild;
  exportDrops_=exportDrops;exportRejects_=exportRejects;
  exportSkips_=exportSkips;exportIntervalMs_=exportInterval;
  if(event=="disconnect" || event=="authority-lost") { disconnect("Disconnected"); return "disconnect"; }
  if(event=="orderly-shutdown") { disconnect("OrderlyShutdown"); return "orderly-shutdown"; }
  if(event=="stream-failure") { disconnect("StreamFailure"); return "stream-failure"; }
  if(event!="sample") throw std::runtime_error("export event");
  const auto token=j.at("source_token").get<std::string>();
  if(!tokenValid(token) || token==std::string(32,'0')) throw std::runtime_error("source token");
  Observation sample{integer(j,"observation_id"),integer(j,"reference_generation"),integer(j,"source_locate_time_ns",true),{},j.at("validity_mask").get<std::uint32_t>()};
  if(sample.validity>63 || !j.at("source_pose_bits").is_array() || j.at("source_pose_bits").size()!=7)
   throw std::runtime_error("source bits/validity");
  for(std::size_t i=0;i<7;++i) {
   const auto& bit=j.at("source_pose_bits")[i];
   if(!bit.is_number_unsigned() || bit.get<std::uint64_t>()>UINT32_MAX) throw std::runtime_error("source float bit range");
   sample.bits[i]=bit.get<std::uint32_t>();
  }
  auto old=history_.find(token);
  if(old!=history_.end()) {
   const auto& h=old->second;
   if(h.retired) { ++rejects_; return "retired-source-token"; }
   if(sample.id<h.id || sample.generation<h.generation || sample.locate<h.locate) { ++rejects_; return "stale-observation"; }
   if(sample.id==h.id) {
    if(sample.generation==h.generation && sample.locate==h.locate && sample.bits==h.bits && sample.validity==h.validity) return "duplicate-observation";
    old->second.retired=true; ++rejects_; disconnect("StreamFailure"); return "observation-conflict";
   }
  } else if(history_.size()>=128) { ++rejects_; disconnect("StreamFailure"); return "history-exhausted"; }
  const bool change=authority_ && (token!=token_ || sample.generation!=authority_->source_space_generation);
  if(change) {
   // A real reference/token change closes the old publication; caller must obtain
   // a fresh A2 callback and EXPLICIT source authorization for this new bootstrap.
   if(token!=token_) history_.at(token_).retired=true;
   freshPublication(); ++reconnects_;
  }
  if(!token_.empty() && token_!=token && history_.count(token_)) history_.at(token_).retired=true;
  token_=token; history_[token]=sample;
  if(!authority_) {
   w::TrustedHmdSourceAuthority a;
   d::header(a,config_.sourcePublisherId,publisherSession_,clock_,0,at);
   a.timestamp_kind="receive"; a.source_space={config_.sourceId,token,config_.sourceSpaceId,sample.generation};
   a.source_space_kind="openxr_view_in_stage"; a.source_time_domain_id="openxr-locate-"+token;
   if(!enqueue(a)) return "backpressure";
   authority_=a.source_space; sequence_=0;
  }
  if(*sequence_==std::numeric_limits<std::int64_t>::max()) { ++rejects_; disconnect("StreamFailure"); return "sequence-exhausted"; }
  const auto next=*sequence_+1;
  std::array<double,7> values{};
  for(std::size_t i=0;i<7;++i) { float value; std::memcpy(&value,&sample.bits[i],sizeof(value)); values[i]=value; }
  w::HmdValidityEvidence validity{bool(sample.validity&1),bool(sample.validity&2),bool(sample.validity&4),
   bool(sample.validity&8),bool(sample.validity&16),bool(sample.validity&32)};
  const auto norm=std::sqrt(values[3]*values[3]+values[4]*values[4]+values[5]*values[5]+values[6]*values[6]);
  const bool numbers=std::all_of(values.begin(),values.end(),[](double v){return std::isfinite(v);}) && std::abs(norm-1)<=1e-5;
  bool accepted=false;
  if(sample.validity==63 && numbers) {
   w::TrustedHmdSourcePose p; d::header(p,config_.sourcePublisherId,publisherSession_,clock_,next,at); p.timestamp_kind="receive";
   p.source={*authority_,sample.id,sample.locate,"openxr-locate-"+token};
   p.position={values[0],values[1],values[2]}; p.orientation={values[3],values[4],values[5],values[6]}; p.validity=validity;
   accepted=enqueue(p);
  } else {
   w::TrustedHmdSourceUnavailable u; d::header(u,config_.sourcePublisherId,publisherSession_,clock_,next,at); u.timestamp_kind="receive";
   u.source_space=*authority_; u.validity=validity;
   u.reason=!numbers?"pose_unavailable":(!validity.orientation_valid || !validity.view_orientation_valid)?"orientation_invalid":
    (!validity.position_valid || !validity.view_position_valid)?"position_invalid":"tracking_lost";
   if(!numbers) ++rejects_; accepted=enqueue(u);
  }
  if(!accepted) return "backpressure";
  sequence_=next; lastReceivedNs_=at; lastLocate_=sample.locate; validity_=sample.validity; lifecycle_="ActiveData";
  poseUsable_=sample.validity==63 && numbers;
  return change?"source-changed":sample.validity==63&&numbers?"pose":"unavailable";
 } catch(...) { ++rejects_; disconnect("InvalidExport"); return "invalid-export"; }
}
std::optional<w::Envelope> TrustedLiveSourceProducer::take() {
 if(queue_.empty()) return {}; auto e=std::move(queue_.front()); queue_.pop_front(); lastEmitted_=e; return e;
}
std::string TrustedLiveSourceProducer::snapshot(const std::string& commit,const std::string& runtime) const {
 J j={{"schema","monaka-trusted-live-producer-snapshot-1"},{"build_commit",commit},{"exporter_build_commit",exporterCommit_.empty()?J(nullptr):J(exporterCommit_)},{"source_publisher_id",config_.sourcePublisherId},
  {"source_type","alvr-openxr-view-in-stage"},{"runtime_instance",runtime},{"connected",connected_},{"lifecycle",lifecycle_},
  {"source_generation",generation_},{"source_session",publisherSession_},{"source_clock",clock_},{"export_session",exportSession_},
  {"source_authority_token",token_},{"reconnect_count",reconnects_},{"sequence_high_water",sequence_?J(*sequence_):J(nullptr)},
  {"last_emitted_envelope",optionalEnvelope(lastEmitted_)},{"last_stream_failure",lastFailure_.empty()?J(nullptr):J(lastFailure_)},
  {"queue_depth",queue_.size()},{"queue_capacity",config_.capacity},{"drop_count",drops_},{"reject_count",rejects_},
  {"export_drop_count",exportDrops_},{"export_reject_count",exportRejects_},{"last_successful_a2_handoff",nullptr},
  {"export_sample_skip_count",exportSkips_},{"export_min_sample_interval_ms",exportIntervalMs_},
  {"timestamp",{{"header_kind","receive"},{"clock","producer-process monotonic nanoseconds"},{"source_clock","OpenXR runtime monotonic locate evaluation"},
   {"source_time_domain_id",token_.empty()?J(nullptr):J("openxr-locate-"+token_)},{"source_locate_time_ns",lastLocate_?J(std::to_string(*lastLocate_)):J(nullptr)},
   {"last_receive_ns",lastReceivedNs_?J(*lastReceivedNs_):J(nullptr)},{"wall_clock_used_for_authority",false},{"physical_capture_timestamp",false}}},
  {"device",{{"source_id",config_.sourceId},{"source_space_id",config_.sourceSpaceId},{"reference","VIEW in STAGE; head reference, no hardware serial claim"},
   {"validity_mask",validity_},{"lost",!connected_ || !poseUsable_}}}};
 return j.dump();
}
}
