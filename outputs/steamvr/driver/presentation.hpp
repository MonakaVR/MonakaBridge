#pragma once
#include "monaka_bridge/direct.hpp"
#include <filesystem>
namespace mb::steamvr {
inline constexpr const char* PicoRenderModel="{monaka_bridge}pico_motion_tracker";
// Presentation is an immutable Init snapshot. It never owns or changes tracking.
class Presentation {
 struct Assignment {std::string source,device,tracker,inputSession;};
 std::map<std::string,Assignment> assignments_;
 std::string publisher_,session_;std::uint32_t revision_=0;
public:
 static Presentation load(const std::filesystem::path& package);
 bool eligible(const std::string& serial,const DirectSample*,std::int64_t now)const;
};
std::filesystem::path currentDriverPackage();
}
