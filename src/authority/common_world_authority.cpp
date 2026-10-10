#include "monaka_bridge/common_world_authority.hpp"
#include "common_world_facts.hpp"
#include <limits>
#include <stdexcept>

namespace monaka_bridge {
namespace d = common_world_detail;
namespace w = authority_wire;
CommonWorldAuthorityOwner::CommonWorldAuthorityOwner(CommonWorldPersistentHighWater high,
 TokenFactory wt,TokenFactory ct,std::size_t cap)
 : high_(std::move(high)),worldToken_(std::move(wt)),calibrationToken_(std::move(ct)),capacity_(cap) {
 if(!cap || !worldToken_ || !calibrationToken_ || !d::validId(high_.ownerId) || !d::validId(high_.worldSpaceId) ||
    (!high_.sourcePublisher.empty() && !d::validId(high_.sourcePublisher)))
  throw std::invalid_argument("explicit valid installation/world IDs, factories and positive capacity required");
}
CommonWorldEvents CommonWorldAuthorityOwner::revokeWorld(const std::string& reason) {
 CommonWorldEvents events;
 if(world_) {
  w::CommonWorldRevocation r; r.world=world_->world; r.reason=reason;
  r.source_publisher_id=world_->source_publisher_id;
  if(!d::validPayload(r)) throw std::invalid_argument("invalid world revocation reason");
  events.emplace_back(r);
 }
 world_.reset(); mapping_.reset(); pending_.reset(); return events;
}
CommonWorldEvents CommonWorldAuthorityOwner::close() {
 auto events=revokeWorld("capacity_exhausted"); exhausted_=true; anchor_.reset(); return events;
}
std::optional<std::string> CommonWorldAuthorityOwner::mint(const TokenFactory& factory,std::set<std::string>& tokens) {
 if(tokens.size()>=capacity_) return {};
 try {
  auto token=factory();
  if(!d::validId(token) || worldTokens_.count(token) || calibrationTokens_.count(token)) return {};
  tokens.insert(token); return token;
 } catch(...) { return {}; } // external factory failure closes the owner; never reuse
}
CommonWorldEvents CommonWorldAuthorityOwner::reconcile(const TrustedHmdSourceStream& stream,const std::string& loss) {
 if(exhausted_) return {};
 const auto current=stream.currentAuthority();
 if(!stream.streamValid() || !current) {
  auto events=revokeWorld(loss); anchor_.reset(); return events;
 }
 if(!high_.sourcePublisher.empty() && current->publisher_id!=high_.sourcePublisher) return {};
 if(anchor_ && d::anchor(*anchor_,!high_.sourcePublisher.empty())==d::anchor(*current,!high_.sourcePublisher.empty()) && (world_ || pending_)) return {};
 std::string reason="anchor_authority_lost";
 if(anchor_) {
  const auto& old=anchor_->source_space; const auto& next=current->source_space;
  reason=d::anchor(*anchor_,!high_.sourcePublisher.empty())==d::anchor(*current,!high_.sourcePublisher.empty())?"explicit_world_replacement":
   old.source_id!=next.source_id || old.source_authority_session_epoch!=next.source_authority_session_epoch
   ? "source_session_changed" : "source_space_changed";
 }
 auto events=revokeWorld(reason); anchor_=*current;
 if((high_.lastWorldRevision && *high_.lastWorldRevision==UINT32_MAX) || high_.lastMappingRevision==UINT32_MAX ||
    requestId_==std::numeric_limits<std::uint64_t>::max()) {
  auto closed=close(); events.insert(events.end(),closed.begin(),closed.end()); return events;
 }
 pending_=CommonWorldTransitionRequest{++requestId_,high_.ownerId,high_.worldSpaceId,*current,
  high_.lastWorldRevision?*high_.lastWorldRevision+1:0,high_.lastMappingRevision+1,reason};
 return events;
}
CommonWorldEvents CommonWorldAuthorityOwner::commitWorldTransition(const CommonWorldTransitionReceipt& receipt) {
 if(exhausted_ || !pending_ || !anchor_ || !receipt.persistentCommitConfirmed ||
    !d::exactRequest(receipt.request,*pending_,!high_.sourcePublisher.empty()) || d::anchor(receipt.request.anchor,!high_.sourcePublisher.empty())!=d::anchor(*anchor_,!high_.sourcePublisher.empty()) ||
    receipt.worldRevision<pending_->minimumWorldRevision || receipt.mappingRevision<pending_->minimumMappingRevision)
  return {};
 // A committed durable high-water cannot roll back even if token mint subsequently fails.
 high_.lastWorldRevision=receipt.worldRevision; high_.lastMappingRevision=receipt.mappingRevision;
 auto wt=mint(worldToken_,worldTokens_);
 auto ct=wt?mint(calibrationToken_,calibrationTokens_):std::nullopt;
 if(!wt || !ct) return close();
 w::CommonWorldAuthorityPublication world;
 world.world={high_.ownerId,*wt,{high_.worldSpaceId,"rh_y_up_neg_z_forward",receipt.worldRevision}};
 world.anchor_source=anchor_->source_space;
 d::bindPublisher(world,high_.sourcePublisher);
 w::CommonWorldMappingPublication map;
 map.mapping={world.world,world.anchor_source,*ct,receipt.mappingRevision}; map.transform=d::identity();
 d::bindPublisher(map,high_.sourcePublisher);
 if(!d::validPayload(world) || !d::validPayload(map)) return close();
 world_=world; mapping_=map; pending_.reset(); return {world,map};
}
CommonWorldEvents CommonWorldAuthorityOwner::revokeMapping(const std::string& reason) {
 if(!mapping_) return {};
 w::CommonWorldMappingRevocation r; r.mapping=mapping_->mapping; r.reason=reason;
 r.source_publisher_id=mapping_->source_publisher_id;
 if(!d::validPayload(r)) throw std::invalid_argument("invalid mapping revocation reason");
 mapping_.reset(); return {r};
}
CommonWorldEvents CommonWorldAuthorityOwner::updateMapping(const CommonWorldMappingReceipt& receipt) {
 if(exhausted_ || !world_ || !mapping_ || !receipt.persistentCommitConfirmed ||
    receipt.sourcePublisher!=high_.sourcePublisher ||
    d::world(receipt.mapping.world)!=d::world(world_->world)) return {};
 const auto revision=receipt.mapping.mapping_revision;
 if(revision<high_.lastMappingRevision) return {};
 const bool sameContent=d::source(receipt.mapping.source_space)==d::source(mapping_->mapping.source_space) &&
  receipt.mapping.calibration_epoch==mapping_->mapping.calibration_epoch &&
  d::transform(receipt.transform)==d::transform(mapping_->transform);
 if(revision==high_.lastMappingRevision) return sameContent?CommonWorldEvents{}:revokeMapping("mapping_conflict");
 if(!sameContent) return {}; // M2 update changes only the persisted mapping revision
 high_.lastMappingRevision=revision; mapping_->mapping.mapping_revision=revision; return {*mapping_};
}
CommonWorldEvents CommonWorldAuthorityOwner::reestablishMapping(const CommonWorldMappingReestablishmentReceipt& receipt) {
 if(exhausted_ || !world_ || mapping_ || !anchor_ || !receipt.persistentCommitConfirmed ||
    d::world(receipt.world)!=d::world(world_->world) || d::anchor(receipt.anchor,!high_.sourcePublisher.empty())!=d::anchor(*anchor_,!high_.sourcePublisher.empty()) ||
    receipt.mappingRevision<=high_.lastMappingRevision) return {};
 high_.lastMappingRevision=receipt.mappingRevision;
 auto ct=mint(calibrationToken_,calibrationTokens_); if(!ct) return close();
 w::CommonWorldMappingPublication m;
 m.mapping={world_->world,world_->anchor_source,*ct,receipt.mappingRevision}; m.transform=d::identity();
 d::bindPublisher(m,high_.sourcePublisher);
 mapping_=m; return {m};
}
CommonWorldEvents CommonWorldAuthorityOwner::sourcePose(const TrustedHmdSourceObservationSnapshot& snapshot) {
 if(exhausted_ || !world_ || !mapping_ || !anchor_ ||
    (!high_.sourcePublisher.empty() && snapshot.observation.publisher_id!=high_.sourcePublisher)) return {};
 // One value snapshot freezes world, mapping and observation before constructing output.
 const auto map=*mapping_; const auto pose=snapshot.observation; const auto authority=*anchor_;
 if(d::source(pose.source.source_space)!=d::source(authority.source_space) ||
    pose.source.source_time_domain_id!=authority.source_time_domain_id || !d::validPayload(pose)) return {};
 const auto key=std::make_pair(pose.source.source_space.source_id,pose.source.source_space.source_authority_session_epoch);
 auto old=observations_.find(key);
 if(old!=observations_.end() && pose.source.observation_id<=old->second) return {};
 if(old==observations_.end() && observations_.size()>=capacity_) return close();
 observations_[key]=pose.source.observation_id;
 w::TrustedHmdCommonPose out; out.mapping=map.mapping; out.source=pose.source;
 out.source_publisher_id=map.source_publisher_id;
 out.source_position=out.common_position=pose.position;
 out.source_orientation=out.common_orientation=pose.orientation; out.validity=pose.validity;
 return {out};
}
CommonWorldEvents CommonWorldAuthorityOwner::sourceUnavailable(const w::TrustedHmdSourceUnavailable& source) {
 if(!mapping_ || (!high_.sourcePublisher.empty() && source.publisher_id!=high_.sourcePublisher) ||
    d::source(source.source_space)!=d::source(mapping_->mapping.source_space) || !d::validPayload(source)) return {};
 w::TrustedHmdCommonUnavailable out; out.mapping=mapping_->mapping; out.reason=source.reason; out.validity=source.validity;
 out.source_publisher_id=mapping_->source_publisher_id;
 return {out};
}
CommonWorldEvents CommonWorldAuthorityOwner::fullSnapshot() const {
 if(world_ && mapping_) return {*world_,*mapping_};
 return {}; // seq1 must be a live mapping; a world-only snapshot cannot bootstrap
}
}
