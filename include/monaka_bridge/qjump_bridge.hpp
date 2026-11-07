#pragma once
#include "bridge.hpp"
#include "qjump_capture.hpp"

namespace mb {
class QJumpBridge {
 qjump::Collector& collector_;Bridge& bridge_;
 using J=nlohmann::json;
 static J observation(const c1::TrackerObservation& p){return {{"source_id",p.source_id},{"device_id",p.device_id},{"session_id",p.session_id},{"clock_id",p.clock_id},{"sequence",std::to_string(p.sequence)},{"space_id",p.coordinate_space.id},{"space_revision",std::to_string(p.coordinate_space.revision)}};}
 static J inputLink(const c1::Input& p){return {{"source_id",p.source_id},{"device_id",p.device_id},{"session_id",p.session_id},{"sequence",std::to_string(p.sequence)}};}
 static J outputIdentity(const c1::MtpPose& p){return {{"publisher_id",p.publisher_id},{"source_id",p.source_id},{"tracker_id",p.tracker_id},{"session_id",p.session_id},{"clock_id",p.clock_id},{"sequence",std::to_string(p.sequence)},{"mapping_revision",std::to_string(p.mapping_revision)},{"space_id",p.coordinate_space.id},{"space_revision",std::to_string(p.coordinate_space.revision)}};}
 template<class P>static J times(const P& p,std::int64_t now){return {{"source_timestamp_ns",std::to_string(p.timestamp_ns)},{"source_sent_at_ns",std::to_string(p.sent_at_ns)},{"source_clock_id",p.clock_id},{"source_timestamp_kind",p.timestamp_kind},{"receipt_or_publication_ns",std::to_string(now)},{"receipt_clock_domain","bridge.service_epoch"}};}
 static std::string lane(const std::string& source,const std::string& entity,const char* stage){return std::to_string(source.size())+":"+source+entity+":"+stage;}
 J context(const Binding& b,const Profile& p)const{return {{"policy",policyName(bridge_.config().policy)},{"mapping_revision",std::to_string(bridge_.config().revision)},{"profile",b.profile},{"transform",{{"position_axes",p.positionAxes},{"quaternion_axes",p.quaternionAxes},{"world_rotation",b.world.rotation},{"world_translation",b.world.translation},{"mount_rotation",b.mount.rotation},{"mount_translation",b.mount.translation},{"input_space",b.inputSpace},{"input_revision",std::to_string(b.inputRevision)},{"world_space",b.worldSpace},{"world_revision",std::to_string(b.worldRevision)}}}};}
 qjump::Sample fromInput(const c1::TrackerObservation& p,const char* stage,std::int64_t now){qjump::Sample s;s.stage=stage;s.stream_id=lane(p.source_id,p.device_id,stage);s.identity=observation(p);s.timestamps=times(p,now);if(std::string(stage)=="bridge_input")s.timestamps["receipt_timestamp_ns"]=std::to_string(now);else s.timestamps["bridge_transform_applied_ns"]=std::to_string(now);s.validity={{"fresh",bridge_.registry.fresh({p.source_id,p.device_id},now)},{"position_valid",p.validity.position},{"rotation_valid",p.validity.orientation},{"modality",p.modality},{"tracking_state",p.tracking_state},{"orientation_evidence",p.orientation_evidence}};s.links["observation"]={{"source_id",p.source_id},{"device_id",p.device_id},{"session_id",p.session_id},{"sequence",std::to_string(p.sequence)}};s.links["input"]=s.links["observation"];s.q=p.orientation;return s;}
public:
 QJumpBridge(qjump::Collector& c,Bridge& b):collector_(c),bridge_(b){
  decltype(bridge_.diagnosticReceive) receive=[this](const auto& p,std::int64_t now){try{auto s=fromInput(p,"bridge_input",now);const auto key=Key{p.source_id,p.device_id};const auto binding=bridge_.config().bindings.find(key);if(binding!=bridge_.config().bindings.end()){const auto profile=bridge_.config().profiles.find(binding->second.profile);if(profile!=bridge_.config().profiles.end())s.context=context(binding->second,profile->second);else s.context["transform_status"]="unknown_profile";}else s.context["transform_status"]="unmapped";s.context["admission_result"]="accepted";s.context["q_frame"]=p.coordinate_space.convention;collector_.offer(std::move(s));}catch(...){collector_.noteDrop();}};
  decltype(bridge_.diagnosticPublication) publication=[this](const auto& p,const auto& b,const auto& profile,const auto& d,const auto& calibrated,const auto& output,std::int64_t now){try{
   auto axis=fromInput(p,"bridge_axis",now);axis.context=context(b,profile);axis.context["history_previous_q"]=d.previous?J(*d.previous):J(nullptr);axis.context["history_selected_q"]=d.selected?J(*d.selected):J(nullptr);axis.q=d.axis;collector_.offer(std::move(axis));
   auto s=fromInput(p,"bridge_calibrated",now);s.context=context(b,profile);s.q=calibrated.orientation;collector_.offer(std::move(s));
   published(output,"queued",std::nullopt,now,context(b,profile));
  }catch(...){collector_.noteDrop();}};
  bridge_.diagnosticReceive=std::move(receive);bridge_.diagnosticPublication=std::move(publication);
 }
 ~QJumpBridge(){bridge_.diagnosticReceive={};bridge_.diagnosticPublication={};}
 void published(const c1::MtpPose& p,const char* phase,std::optional<bool> success,std::int64_t now,J ctx=J::object()){
  qjump::Sample s;s.stage="bridge_published";s.stream_id=lane(p.source_id,p.tracker_id,phase);s.identity=outputIdentity(p);s.timestamps=times(p,now);s.timestamps["bridge_derived_sample_ns"]=std::to_string(p.timestamp_ns);s.timestamps["bridge_sent_at_ns"]=std::to_string(p.sent_at_ns);s.timestamps["bridge_clock_id"]=p.clock_id;s.links["input"]=inputLink(p.input);s.links["observation"]=s.links["input"];s.validity={{"fresh",true},{"position_valid",p.validity.position},{"rotation_valid",p.validity.orientation},{"modality",p.modality},{"tracking_state",p.tracking_state}};s.publication_phase=phase;s.wire_send_success=success;s.context=std::move(ctx);s.context["routing_policy"]=policyName(bridge_.config().policy);s.context["monaka_routed"]=bridge_.config().policy==Policy::Monaka||bridge_.config().policy==Policy::Both;s.q=p.orientation;collector_.offer(std::move(s));
 }
 void sent(Channel channel,std::string_view bytes,bool result,std::int64_t now)noexcept{if(channel!=Channel::Monaka)return;try{c1::Envelope e;c1::Error error;if(c1::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size(),e,error))if(auto p=std::get_if<c1::MtpPose>(&e))published(*p,"actual_wire",result,now);}catch(...){collector_.noteDrop();}}
};
}
