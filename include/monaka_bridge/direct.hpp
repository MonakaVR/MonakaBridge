#pragma once
#include "config.hpp"
#include <set>
namespace mb {
inline std::string outputSource(const std::string& publisher,const std::string& source){return std::to_string(publisher.size())+":"+publisher+source;}
struct DirectSample {std::optional<c1::MtpPose> pose;std::optional<c1::MtpTrackerState> state;std::int64_t fixedTime=-1,poseSequence=-1,stateSequence=-1,absentAt=-1;bool absent=false;std::uint32_t revision=0;};
class MtpFeed {
 struct Source{std::int64_t lastReceive=-1;std::string session,clock,peer;std::set<std::string> retired;};
 std::map<std::string,Source> sources_;
public:
 std::map<Key,DirectSample> devices;
 bool receive(std::string_view,std::int64_t now,std::string_view peer="");
 bool fresh(const DirectSample&,std::int64_t now)const;
};
}
