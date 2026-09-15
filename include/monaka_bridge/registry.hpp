#pragma once
#include "monaka/protocol/v1/codec.hpp"
#include <map>
#include <set>
#include <string_view>
namespace mb {
namespace c1=monaka::protocol::v1;
using Key=std::pair<std::string,std::string>;
struct Device {
 std::optional<c1::TrackerObservation> pose;
 std::optional<c1::ObservationDeviceState> state;
 std::int64_t poseSequence=-1,stateSequence=-1,fixedTime=-1,stateAt=-1;
 std::string space,convention;
 std::uint32_t revision=0;
 bool hasSpace=false,absent=false;
};
struct Source {
 std::string session,clock,peer;
 std::set<std::string> retired;
 std::map<std::string,Device> devices;
 std::int64_t lastReceive=-1;
 bool collision=false;
};
class Registry {
public:
 std::int64_t timeoutNs=500000000;
 std::map<std::string,Source> sources;
 std::uint64_t malformed=0,rejected=0,collisions=0;
 bool receive(std::string_view,std::string_view peer,std::int64_t now,c1::Envelope* admitted=nullptr);
 const Device* find(const Key&) const;
 bool fresh(const Key&,std::int64_t now) const;
};
}
