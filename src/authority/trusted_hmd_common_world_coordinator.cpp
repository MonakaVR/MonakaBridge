#include "monaka_bridge/trusted_hmd_common_world_coordinator.hpp"

namespace monaka_bridge {
namespace w = authority_wire;
TrustedHmdCommonWorldCoordinator::TrustedHmdCommonWorldCoordinator(std::string sourcePublisher,
 CommonWorldPersistentHighWater high,CommonWorldAuthorityOwner::TokenFactory wt,
 CommonWorldAuthorityOwner::TokenFactory ct,std::size_t cap,std::int64_t limit)
 : source_(std::move(sourcePublisher),cap),owner_(high,std::move(wt),std::move(ct),cap),publisher_(high.ownerId,cap,limit) {}
std::vector<w::Envelope> TrustedHmdCommonWorldCoordinator::emit(CommonWorldEvents events,std::int64_t at) {
 if(publisher_.needsBootstrap()) {
  const auto snapshot=owner_.fullSnapshot();
  if(snapshot.empty()) return {};
  // Replace a mapping-only change by the complete current snapshot for a silent session.
  if(events.size()>=2 && std::holds_alternative<w::CommonWorldAuthorityPublication>(events[0]))
   return publisher_.publish(events,at);
  auto bootstrap=snapshot; bootstrap.insert(bootstrap.end(),events.begin(),events.end());
  if(!events.empty() && std::holds_alternative<w::CommonWorldMappingPublication>(events[0]))
   bootstrap.erase(bootstrap.begin()+2); // snapshot already contains this exact mapping
  return publisher_.publish(bootstrap,at);
 }
 return publisher_.publish(events,at);
}
CommonWorldOperationResult TrustedHmdCommonWorldCoordinator::afterSource(std::string status,const w::Envelope* message,std::int64_t at) {
 std::string reason="event_stream_lost";
 if(status=="observation-conflict") reason="anchor_authority_lost";
 if(status=="revoked" && message) {
  const auto& r=std::get<w::TrustedHmdSourceRevocation>(*message).reason;
  if(r=="source_session_loss") reason="source_session_changed";
  else if(r=="source_space_ambiguous") reason="source_space_changed";
  else if(r=="authority_withdrawn") reason="anchor_authority_lost";
  else if(r=="capacity_exhausted") reason="capacity_exhausted";
 }
 auto events=owner_.reconcile(source_,reason); // always inspect validity + exact current authority
 if(status=="pose" && source_.candidate()) {
  auto pose=owner_.sourcePose(*source_.candidate()); events.insert(events.end(),pose.begin(),pose.end());
 } else if(status=="unavailable" && message) {
  auto unavailable=owner_.sourceUnavailable(std::get<w::TrustedHmdSourceUnavailable>(*message));
  events.insert(events.end(),unavailable.begin(),unavailable.end());
 }
 return {std::move(status),emit(std::move(events),at)};
}
CommonWorldOperationResult TrustedHmdCommonWorldCoordinator::beginSourceSession(const std::string& publisher,
 const std::string& session,const std::string& clock,const w::Envelope& bootstrap,std::int64_t at) {
 // Atomic authorized replacement + seq0 snapshot; no externally visible half-handoff.
 if(!source_.beginAuthorizedPublisherSession(publisher,session,clock)) return afterSource("session-rejected",nullptr,at);
 return afterSource(source_.receive(bootstrap,at),&bootstrap,at);
}
CommonWorldOperationResult TrustedHmdCommonWorldCoordinator::receiveSource(const w::Envelope& message,std::int64_t at) {
 return afterSource(source_.receive(message,at),&message,at);
}
CommonWorldOperationResult TrustedHmdCommonWorldCoordinator::receiveSourceBytes(const std::string& bytes,std::int64_t at) {
 w::Envelope message; w::Error error;
 if(!w::DecodeEnvelope(reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size(),message,error)) {
  auto status=source_.receiveBytes(bytes,at); return afterSource(status,nullptr,at);
 }
 return receiveSource(message,at);
}
std::vector<w::Envelope> TrustedHmdCommonWorldCoordinator::onSourceTransportDisconnected(std::int64_t at) {
 source_.onTransportDisconnected(); return emit(owner_.reconcile(source_),at);
}
CommonWorldOperationResult TrustedHmdCommonWorldCoordinator::beginPublisherSession(const std::string& session,const std::string& clock,std::int64_t at) {
 if(!publisher_.beginSession(session,clock)) return {"session-rejected",{}};
 return {"session",publisher_.publish(owner_.fullSnapshot(),at)};
}
std::vector<w::Envelope> TrustedHmdCommonWorldCoordinator::commitWorldTransition(const CommonWorldTransitionReceipt& receipt,std::int64_t at) {
 return emit(owner_.commitWorldTransition(receipt),at);
}
std::vector<w::Envelope> TrustedHmdCommonWorldCoordinator::updateMapping(const CommonWorldMappingReceipt& receipt,std::int64_t at) {
 return emit(owner_.updateMapping(receipt),at);
}
std::vector<w::Envelope> TrustedHmdCommonWorldCoordinator::reestablishMapping(const CommonWorldMappingReestablishmentReceipt& receipt,std::int64_t at) {
 return emit(owner_.reestablishMapping(receipt),at);
}
std::vector<w::Envelope> TrustedHmdCommonWorldCoordinator::revokeMapping(std::int64_t at) { return emit(owner_.revokeMapping(),at); }
std::vector<w::Envelope> TrustedHmdCommonWorldCoordinator::revokeWorld(std::int64_t at) { return emit(owner_.revokeWorld(),at); }
}
