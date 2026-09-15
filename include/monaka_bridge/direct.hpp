#pragma once
#include "config.hpp"
#include <set>
namespace mb {
struct DirectSample {std::optional<c1::MtpPose> pose;std::optional<c1::MtpTrackerState> state;std::int64_t fixedTime=-1,poseSequence=-1,stateSequence=-1;bool absent=false;std::uint32_t revision=0;};
class MtpFeed {
 struct Source{std::string session,clock;std::set<std::string> retired;};
 std::map<std::string,Source> sources_;
public:
 std::map<Key,DirectSample> devices;
 bool receive(std::string_view,std::int64_t now);
 bool fresh(const DirectSample&,std::int64_t now)const;
};
}
