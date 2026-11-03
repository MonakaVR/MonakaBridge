#pragma once
#include "monaka_bridge/direct.hpp"
#include <filesystem>
#include <array>
namespace mb::steamvr {
using StatusIcons=std::array<std::string,4>; // Ready, Searching, Standby, Off.
using StatusIconAssignments=std::map<std::string,StatusIcons>;
StatusIconAssignments loadStatusIcons(const std::filesystem::path& package);
// Transient visibility and a conflicting immutable assignment are different.
enum class PresentationState { Unassigned, Hidden, Visible, Revoked };
// Presentation is an immutable Init snapshot. It never owns or changes tracking.
class Presentation {
 struct Assignment {std::string source,device,tracker,inputSession,model;};
 std::map<std::string,Assignment> assignments_;
 std::string publisher_,session_;std::uint32_t revision_=0;
public:
 static Presentation load(const std::filesystem::path& package);
 std::string model(const std::string& serial)const;
 PresentationState evaluate(const std::string& serial,const DirectSample*,std::int64_t now)const;
};
std::filesystem::path currentDriverPackage();
}
