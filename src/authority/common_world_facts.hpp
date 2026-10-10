#pragma once
#include "monaka_bridge/common_world_authority.hpp"
#include <cstring>
#include <tuple>

namespace monaka_bridge::common_world_detail {
namespace w = authority_wire;
inline auto source(const w::SourceSpaceAuthority& s) {
 return std::make_tuple(s.source_id,s.source_authority_session_epoch,s.source_space_id,s.source_space_generation);
}
inline auto anchor(const w::TrustedHmdSourceAuthority& a,bool publisherBound=false) {
 // Schema 1 never contained publisher. Preserve its explicit legacy comparison;
 // schema 2 compares the actual durable publisher, without filling legacy data.
 return std::make_tuple(publisherBound?a.publisher_id:std::string{},source(a.source_space),a.source_space_kind,a.source_time_domain_id);
}
inline auto world(const w::CommonWorldReference& v) {
 return std::make_tuple(v.owner_id,v.world_epoch,v.coordinate_space.id,v.coordinate_space.convention,v.coordinate_space.revision);
}
inline auto mapping(const w::CommonMappingReference& m) {
 return std::make_tuple(world(m.world),source(m.source_space),m.calibration_epoch,m.mapping_revision);
}
template<std::size_t N> inline auto bits(const std::array<double,N>& a) {
 std::array<std::uint64_t,N> b{};
 for(std::size_t i=0;i<N;++i) std::memcpy(&b[i],&a[i],sizeof(double));
 return b;
}
inline auto transform(const w::RigidTransform& t) { return std::make_pair(bits(t.rotation_xyzw),bits(t.translation_xyz)); }
inline w::RigidTransform identity() { return {{0,0,0,1},{0,0,0}}; }
template<class T> inline void header(T& m,const std::string& publisher,const std::string& session,
 const std::string& clock,std::int64_t seq,std::int64_t at) {
 m.version={2,1}; m.publisher_id=publisher; m.session_id=session; m.clock_id=clock;
 m.sequence=seq; m.timestamp_ns=m.sent_at_ns=at; m.timestamp_kind="receive";
 if constexpr(std::is_same_v<T,w::CommonWorldAuthorityPublication> || std::is_same_v<T,w::CommonWorldMappingPublication> ||
  std::is_same_v<T,w::TrustedHmdCommonPose> || std::is_same_v<T,w::TrustedHmdCommonUnavailable> ||
  std::is_same_v<T,w::CommonWorldMappingRevocation> || std::is_same_v<T,w::CommonWorldRevocation>)
  if(m.source_publisher_id) m.version={2,2};
}
template<class T> inline void bindPublisher(T& m,const std::string& id) {
 if(!id.empty()) { m.source_publisher_id=id; m.version={2,2}; }
}
inline constexpr const char* validationUuid = "00000000-0000-4000-8000-000000000001";
template<class T> inline bool validPayload(T m) {
 header(m,"validation",validationUuid,validationUuid,0,0);
 std::string bytes; w::Error error; return w::EncodeEnvelope(m,bytes,error);
}
inline bool validId(const std::string& id) {
 w::TrustedHmdSourceAuthority a;
 a.source_space={id,"token","stage",0}; a.source_space_kind="openxr_view_in_stage"; a.source_time_domain_id="domain";
 return validPayload(a);
}
inline bool exactRequest(const CommonWorldTransitionRequest& a,const CommonWorldTransitionRequest& b,bool publisherBound=false) {
 return std::make_tuple(a.requestId,a.ownerId,a.worldSpaceId,anchor(a.anchor,publisherBound),a.minimumWorldRevision,a.minimumMappingRevision,a.reason)==
        std::make_tuple(b.requestId,b.ownerId,b.worldSpaceId,anchor(b.anchor,publisherBound),b.minimumWorldRevision,b.minimumMappingRevision,b.reason);
}
}
