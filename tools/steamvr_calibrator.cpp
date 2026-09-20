#include "monaka_bridge/config.hpp"
#include "monaka_bridge/mapping_selection.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <openvr.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

namespace {

enum class ReferenceKind {
    None,
    LeftController,
    RightController,
    Hmd,
};

struct Options {
    std::string configPath;
    bool list = false;
    bool listTrackers = false;
    bool clear = false;
    bool measureOnly = false;
    std::string trackerSerial;
    ReferenceKind reference = ReferenceKind::None;
    int samples = 120;
    int intervalMs = 8;
};

struct AlignmentMapping {
    mb::Config config;
    std::string path, source, space;
    std::uint32_t revision=0;
    mb::Vec translation{};
};

void PrintUsage() {
    std::cout
        << "Monaka Bridge SteamVR translation calibrator\n\n"
        << "Usage (calibration requires --config CONFIG):\n"
        << "  monaka_bridge_calibrator --list\n"
        << "  monaka_bridge_calibrator --list-trackers\n"
        << "  monaka_bridge_calibrator --config PATH --tracker SERIAL --clear\n"
        << "  monaka_bridge_calibrator --tracker <serial> --reference <left|right|hmd>\n"
        << "      [--measure-only] [--samples <count>] [--interval-ms <milliseconds>]\n\n"
        << "Backward compatibility:\n"
        << "  --controller <left|right> is accepted as an alias for --reference.\n\n"
        << "Calibration assumes the selected reference device and Monaka Direct tracker pose\n"
        << "reference points are held at the same physical point while sampling.\n"
        << "--measure-only calculates the suggested translation without applying it.\n";
}

bool ParsePositiveInt(const char* text, int& value) {
    if (text == nullptr || *text == '\0') {
        return false;
    }
    char* end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || parsed <= 0 || parsed > 100000) {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

bool ParseReference(const std::string& text, ReferenceKind& reference) {
    if (text == "left") {
        reference = ReferenceKind::LeftController;
        return true;
    }
    if (text == "right") {
        reference = ReferenceKind::RightController;
        return true;
    }
    if (text == "hmd") {
        reference = ReferenceKind::Hmd;
        return true;
    }
    return false;
}

bool ParseArgs(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config" && i+1<argc) { options.configPath=argv[++i];
        } else if (arg == "--list") {
            options.list = true;
        } else if (arg == "--list-trackers") {
            options.listTrackers = true;
        } else if (arg == "--clear") {
            options.clear = true;
        } else if (arg == "--measure-only") {
            options.measureOnly = true;
        } else if (arg == "--tracker") {
            if (i + 1 >= argc) {
                return false;
            }
            options.trackerSerial = argv[++i];
        } else if (arg == "--reference") {
            if (i + 1 >= argc || !ParseReference(argv[++i], options.reference)) {
                return false;
            }
        } else if (arg == "--controller") {
            if (i + 1 >= argc) {
                return false;
            }
            const std::string role = argv[++i];
            if (role == "left") {
                options.reference = ReferenceKind::LeftController;
            } else if (role == "right") {
                options.reference = ReferenceKind::RightController;
            } else {
                return false;
            }
        } else if (arg == "--samples") {
            if (i + 1 >= argc || !ParsePositiveInt(argv[++i], options.samples)) {
                return false;
            }
        } else if (arg == "--interval-ms") {
            if (i + 1 >= argc || !ParsePositiveInt(argv[++i], options.intervalMs)) {
                return false;
            }
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage();
            std::exit(0);
        } else {
            return false;
        }
    }

    const int modeCount = (options.list ? 1 : 0) +
                          (options.listTrackers ? 1 : 0) +
                          (options.clear ? 1 : 0) +
                          (!options.trackerSerial.empty() && !options.clear ? 1 : 0);
    if (modeCount != 1) {
        return false;
    }
    if (options.clear && options.trackerSerial.empty()) return false;
    if (!options.clear && !options.trackerSerial.empty() && options.reference == ReferenceKind::None) {
        return false;
    }
    if (options.measureOnly && options.trackerSerial.empty()) {
        return false;
    }
    return true;
}

bool OpenAlignmentMapping(AlignmentMapping& ipc, const Options& options, std::string& error) {
 try {
  ipc.path=options.configPath;ipc.config=mb::loadConfig(ipc.path);
  const auto* selected=&mb::detail::runtimeTracker(ipc.config,options.trackerSerial);
  ipc.source=selected->source;ipc.space=selected->inputSpace;ipc.revision=selected->inputRevision;ipc.translation=selected->world.translation;
  return true;
 }catch(const std::exception& e){error=e.what();return false;}
}
bool WriteTranslation(AlignmentMapping& ipc,const std::array<double,3>& t,std::string& error) {
 try {
  // Refuse stale calibration result after concurrent config edits.
  auto current=mb::loadConfig(ipc.path);if(current.revision!=ipc.config.revision)throw std::runtime_error("configuration changed during measurement");
  for(auto& [key,b]:current.bindings)if(b.source==ipc.source&&b.inputSpace==ipc.space&&b.inputRevision==ipc.revision)b.world.translation=t;
  if(current.revision==UINT32_MAX)throw std::runtime_error("revision exhausted");++current.revision;mb::saveConfig(ipc.path,current);return true;
 }catch(const std::exception& e){error=e.what();return false;}
}

std::string GetDeviceStringProperty(vr::IVRSystem* system,
                                    vr::TrackedDeviceIndex_t index,
                                    vr::ETrackedDeviceProperty property) {
    char buffer[vr::k_unMaxPropertyStringSize]{};
    vr::ETrackedPropertyError propertyError = vr::TrackedProp_Success;
    system->GetStringTrackedDeviceProperty(
        index,
        property,
        buffer,
        static_cast<std::uint32_t>(sizeof(buffer)),
        &propertyError);
    if (propertyError != vr::TrackedProp_Success) {
        return {};
    }
    return buffer;
}

std::string GetDeviceSerial(vr::IVRSystem* system, vr::TrackedDeviceIndex_t index) {
    return GetDeviceStringProperty(system, index, vr::Prop_SerialNumber_String);
}

bool IsMonakaBridgeTracker(vr::IVRSystem* system, vr::TrackedDeviceIndex_t index) {
    if (system->GetTrackedDeviceClass(index) != vr::TrackedDeviceClass_GenericTracker) {
        return false;
    }
    return GetDeviceStringProperty(system, index, vr::Prop_TrackingSystemName_String) ==
           "monaka_bridge";
}

const char* DeviceClassName(vr::ETrackedDeviceClass deviceClass) {
    switch (deviceClass) {
    case vr::TrackedDeviceClass_HMD:
        return "HMD";
    case vr::TrackedDeviceClass_Controller:
        return "Controller";
    case vr::TrackedDeviceClass_GenericTracker:
        return "GenericTracker";
    case vr::TrackedDeviceClass_TrackingReference:
        return "TrackingReference";
    case vr::TrackedDeviceClass_DisplayRedirect:
        return "DisplayRedirect";
    default:
        return "Invalid";
    }
}

const char* RoleName(vr::ETrackedControllerRole role) {
    switch (role) {
    case vr::TrackedControllerRole_LeftHand:
        return "Left";
    case vr::TrackedControllerRole_RightHand:
        return "Right";
    default:
        return "-";
    }
}

const char* ReferenceName(ReferenceKind reference) {
    switch (reference) {
    case ReferenceKind::LeftController:
        return "Left Controller";
    case ReferenceKind::RightController:
        return "Right Controller";
    case ReferenceKind::Hmd:
        return "HMD";
    default:
        return "Invalid";
    }
}

std::array<double, 3> PoseTranslation(const vr::TrackedDevicePose_t& pose) {
    return {
        static_cast<double>(pose.mDeviceToAbsoluteTracking.m[0][3]),
        static_cast<double>(pose.mDeviceToAbsoluteTracking.m[1][3]),
        static_cast<double>(pose.mDeviceToAbsoluteTracking.m[2][3])};
}

void ListDevices(vr::IVRSystem* system) {
    vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
    system->GetDeviceToAbsoluteTrackingPose(
        vr::TrackingUniverseStanding,
        0.0F,
        poses,
        vr::k_unMaxTrackedDeviceCount);

    std::cout << "SteamVR devices in TrackingUniverseStanding:\n";
    for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
        const vr::ETrackedDeviceClass deviceClass = system->GetTrackedDeviceClass(i);
        if (deviceClass == vr::TrackedDeviceClass_Invalid) {
            continue;
        }

        const vr::ETrackedControllerRole role =
            system->GetControllerRoleForTrackedDeviceIndex(i);
        const std::string serial = GetDeviceSerial(system, i);
        std::cout << "  [" << i << "] " << DeviceClassName(deviceClass)
                  << " role=" << RoleName(role)
                  << " connected=" << (poses[i].bDeviceIsConnected ? 1 : 0)
                  << " valid=" << (poses[i].bPoseIsValid ? 1 : 0)
                  << " serial=" << serial;
        if (poses[i].bPoseIsValid) {
            const auto p = PoseTranslation(poses[i]);
            std::cout << std::fixed << std::setprecision(4)
                      << " p=(" << p[0] << ", " << p[1] << ", " << p[2] << ")";
        }
        std::cout << '\n';
    }
}

void ListMonakaTrackers(vr::IVRSystem* system) {
    vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
    system->GetDeviceToAbsoluteTrackingPose(
        vr::TrackingUniverseStanding,
        0.0F,
        poses,
        vr::k_unMaxTrackedDeviceCount);

    for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
        if (!IsMonakaBridgeTracker(system, i) || !poses[i].bDeviceIsConnected) {
            continue;
        }
        const std::string serial = GetDeviceSerial(system, i);
        if (!serial.empty()) {
            std::cout << serial << '\n';
        }
    }
}

std::optional<vr::TrackedDeviceIndex_t> FindTracker(
    vr::IVRSystem* system,
    const std::string& serial) {
    for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
        if (!IsMonakaBridgeTracker(system, i)) {
            continue;
        }
        if (GetDeviceSerial(system, i) == serial) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<vr::TrackedDeviceIndex_t> FindReference(
    vr::IVRSystem* system,
    ReferenceKind reference) {
    if (reference == ReferenceKind::Hmd) {
        for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
            if (system->GetTrackedDeviceClass(i) == vr::TrackedDeviceClass_HMD) {
                return i;
            }
        }
        return std::nullopt;
    }

    const vr::ETrackedControllerRole wantedRole =
        reference == ReferenceKind::LeftController
            ? vr::TrackedControllerRole_LeftHand
            : vr::TrackedControllerRole_RightHand;
    for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
        if (system->GetTrackedDeviceClass(i) != vr::TrackedDeviceClass_Controller) {
            continue;
        }
        if (system->GetControllerRoleForTrackedDeviceIndex(i) == wantedRole) {
            return i;
        }
    }
    return std::nullopt;
}

int RunCalibration(vr::IVRSystem* system,
                   AlignmentMapping& ipc,
                   const Options& options) {
    const auto trackerIndex = FindTracker(system, options.trackerSerial);
    if (!trackerIndex.has_value()) {
        std::cerr << "Monaka Direct tracker serial was not found in SteamVR: "
                  << options.trackerSerial << '\n';
        return 4;
    }

    const auto referenceIndex = FindReference(system, options.reference);
    if (!referenceIndex.has_value()) {
        std::cerr << "Requested reference device was not found in SteamVR: "
                  << ReferenceName(options.reference) << '\n';
        return 5;
    }

    std::array<double, 3> deltaSum{0.0, 0.0, 0.0};
    int validSamples = 0;
    vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};

    std::cout << "Tracker:   index=" << *trackerIndex
              << " serial=" << options.trackerSerial << '\n';
    std::cout << "Reference: index=" << *referenceIndex
              << " type=" << ReferenceName(options.reference) << '\n';
    std::cout << "Sampling " << options.samples
              << " frames; keep both reference points together and still.\n";

    for (int sample = 0; sample < options.samples; ++sample) {
        system->GetDeviceToAbsoluteTrackingPose(
            vr::TrackingUniverseStanding,
            0.0F,
            poses,
            vr::k_unMaxTrackedDeviceCount);

        const auto& trackerPose = poses[*trackerIndex];
        const auto& referencePose = poses[*referenceIndex];
        if (trackerPose.bDeviceIsConnected && trackerPose.bPoseIsValid &&
            referencePose.bDeviceIsConnected && referencePose.bPoseIsValid) {
            const auto trackerPosition = PoseTranslation(trackerPose);
            const auto referencePosition = PoseTranslation(referencePose);
            for (std::size_t axis = 0; axis < deltaSum.size(); ++axis) {
                deltaSum[axis] += referencePosition[axis] - trackerPosition[axis];
            }
            ++validSamples;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(options.intervalMs));
    }

    const int minimumValidSamples = std::max(10, options.samples / 4);
    if (validSamples < minimumValidSamples) {
        std::cerr << "Too few valid paired poses: " << validSamples
                  << "/" << options.samples << '\n';
        return 6;
    }

    std::array<double, 3> meanDelta{};
    std::array<double, 3> currentTranslation{
        ipc.translation[0],
        ipc.translation[1],
        ipc.translation[2]};
    std::array<double, 3> updatedTranslation{};
    for (std::size_t axis = 0; axis < meanDelta.size(); ++axis) {
        meanDelta[axis] = deltaSum[axis] / static_cast<double>(validSamples);
        updatedTranslation[axis] = currentTranslation[axis] + meanDelta[axis];
    }

    std::cout << std::fixed << std::setprecision(6)
              << "Valid samples: " << validSamples << '\n'
              << "Current t: (" << currentTranslation[0] << ", "
              << currentTranslation[1] << ", " << currentTranslation[2] << ") m\n"
              << "Measured delta: (" << meanDelta[0] << ", "
              << meanDelta[1] << ", " << meanDelta[2] << ") m\n"
              << "Updated t: (" << updatedTranslation[0] << ", "
              << updatedTranslation[1] << ", " << updatedTranslation[2] << ") m\n"
              << "CALIBRATION_RESULT " << updatedTranslation[0] << ' '
              << updatedTranslation[1] << ' ' << updatedTranslation[2] << '\n';

    if (options.measureOnly) {
        std::cout << "Measurement only; world translation was not changed.\n";
        return 0;
    }

    std::string error;
    if (!WriteTranslation(ipc, updatedTranslation, error)) {
        std::cerr << error << '\n';
        return 7;
    }

    std::cout << "World translation update requested.\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!ParseArgs(argc, argv, options)) {
        PrintUsage();
        return 2;
    }

    AlignmentMapping ipc;
    std::string ipcError;
    if (!options.list && !options.listTrackers && !OpenAlignmentMapping(ipc, options, ipcError)) {
        std::cerr << ipcError << '\n';
        return 3;
    }

    if (options.clear) {
        const std::array<double, 3> zero{0.0, 0.0, 0.0};
        std::string error;
        if (!WriteTranslation(ipc, zero, error)) {
            std::cerr << error << '\n';
            return 7;
        }
        std::cout << "World translation cleared.\n";
        return 0;
    }

    vr::EVRInitError initError = vr::VRInitError_None;
    vr::IVRSystem* system = vr::VR_Init(&initError, vr::VRApplication_Utility);
    if (system == nullptr || initError != vr::VRInitError_None) {
        std::cerr << "VR_Init failed: "
                  << vr::VR_GetVRInitErrorAsEnglishDescription(initError) << '\n';
        return 8;
    }

    int result = 0;
    if (options.list) {
        ListDevices(system);
    } else if (options.listTrackers) {
        ListMonakaTrackers(system);
    } else {
        result = RunCalibration(system, ipc, options);
    }

    vr::VR_Shutdown();
    return result;
}
