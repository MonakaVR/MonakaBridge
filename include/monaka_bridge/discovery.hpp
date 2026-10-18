#pragma once
#include "bridge.hpp"
#include <nlohmann/json.hpp>

namespace mb {
// Local diagnostics only. Never supplies Registry authorization, normalization,
// mapping, pose data or output. Owner-thread only; the health writer gets a value
// snapshot. All tracking input still goes through receive().
class DiscoveryCache {
 struct Lifetime {std::string session,clock,peer;std::set<std::string> retired;std::int64_t last=-1;};
 struct Entry {
  std::string session,clock,space,convention;std::uint32_t revision=0;
  std::vector<std::string> advertised;
  std::int64_t poseSequence=-1,stateSequence=-1,last=-1,sample=-1;
 };
 std::map<std::string,Lifetime> sources_;
 std::map<Key,Entry> entries_;
 struct Receipt {std::string session,clock,peer;std::int64_t poseSequence=-1,stateSequence=-1,last=-1;};
 // Configured-device liveness is independent of unknown diagnostic sessions.
 // Session transitions must never erase the recent online-slot refusal fact.
 std::map<Key,Receipt> receipts_;
 std::uint64_t dropped_=0,ignored_=0,errors_=0;
 std::int64_t lastDrop_=-1;
 void observe(const c1::Envelope&,std::string_view,std::int64_t,const Bridge&);
 void drop(std::int64_t now){++dropped_;lastDrop_=now;}
public:
 static constexpr std::size_t maxSources=8,maxDevices=32,maxRetired=16;
 static constexpr std::int64_t recentNs=3000000000LL;
 // Canonical diagnostic predecode, followed unconditionally by the original
 // receive(raw) path. A diagnostic failure never grants/denies tracking input.
 bool receive(Bridge&,std::string_view raw,std::string_view peer,std::int64_t now);
 nlohmann::json snapshot(const Bridge&,std::int64_t now)const;
};
}
