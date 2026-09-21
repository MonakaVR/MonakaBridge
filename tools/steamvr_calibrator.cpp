#include "monaka_bridge/network.hpp"
#include "monaka_bridge/world_calibration.hpp"

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
#include <vector>

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
    bool apply = false;
    bool measureOnlyRequested = false;
    std::string tracker, source, device, inputSpace;
    std::uint32_t inputRevision = 0;
    bool hasInputRevision = false;
    ReferenceKind reference = ReferenceKind::None;
    int points = 3;
    int samples = 60;
    int intervalMs = 8;
    int mirrorPort = 29813;
};

struct AlignmentMapping {
    mb::Config config;
    mb::WorldCalibrationTarget target;
    std::string path;
};

void PrintUsage() {
    std::cout
        << "Monaka Bridge SteamVR rigid world calibrator\n\n"
        << "Usage (calibration requires --config CONFIG):\n"
        << "  monaka_bridge_calibrator [--config CONFIG] --list\n"
        << "  monaka_bridge_calibrator --list-trackers\n"
        << "  monaka_bridge_calibrator --config PATH --tracker ID --source ID --device ID\n"
        << "      --input-space ID --input-revision N --reference <left|right|hmd>\n"
        << "      [--points N] [--samples N] [--interval-ms N] [--mirror-port N]\n"
        << "      [--measure-only|--apply]\n"
        << "  monaka_bridge_calibrator --config PATH --tracker ID --source ID --device ID\n"
        << "      --input-space ID --input-revision N --clear --apply\n\n"
        << "Backward compatibility:\n"
        << "  --controller <left|right> is accepted as an alias for --reference;\n"
        << "  --list-trackers still lists legacy Monaka Direct devices.\n\n"
        << "Native tracker positions come from the read-only Observation mirror (default 29813),\n"
        << "not health JSON or the Direct driver. Keep the tracker and reference rigidly fixed\n"
        << "relative to each other and their orientation nearly unchanged while moving them to\n"
        << "at least three non-collinear locations. Press Enter to capture each point.\n"
        << "Default operation is measure-only; configuration changes require --apply.\n";
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

bool ParseRevision(const char* text, std::uint32_t& value) {
    if (text == nullptr || *text == '\0' || *text == '-') return false;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(text, &end, 10);
    if (end == text || *end != '\0' || parsed > UINT32_MAX) return false;
    value = static_cast<std::uint32_t>(parsed);
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
            options.measureOnlyRequested = true;
        } else if (arg == "--apply") {
            options.apply = true;
        } else if (arg == "--tracker" && i+1<argc) {
            options.tracker = argv[++i];
        } else if (arg == "--source" && i+1<argc) {
            options.source = argv[++i];
        } else if (arg == "--device" && i+1<argc) {
            options.device = argv[++i];
        } else if (arg == "--input-space" && i+1<argc) {
            options.inputSpace = argv[++i];
        } else if (arg == "--input-revision") {
            if (i+1>=argc || !ParseRevision(argv[++i],options.inputRevision)) return false;
            options.hasInputRevision = true;
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
        } else if (arg == "--points") {
            if (i + 1 >= argc || !ParsePositiveInt(argv[++i], options.points)) return false;
        } else if (arg == "--interval-ms") {
            if (i + 1 >= argc || !ParsePositiveInt(argv[++i], options.intervalMs)) {
                return false;
            }
        } else if (arg == "--mirror-port") {
            if (i+1>=argc || !ParsePositiveInt(argv[++i],options.mirrorPort) || options.mirrorPort>65535) return false;
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage();
            std::exit(0);
        } else {
            return false;
        }
    }

    const int modeCount = (options.list ? 1 : 0) +
                          (options.listTrackers ? 1 : 0) +
                          (!options.tracker.empty() ? 1 : 0);
    if (modeCount != 1) {
        return false;
    }
    if(options.apply&&options.measureOnlyRequested)return false;
    if(!options.tracker.empty()){
        if(options.configPath.empty()||options.source.empty()||options.device.empty()||
           options.inputSpace.empty()||!options.hasInputRevision)return false;
        if(options.clear){if(!options.apply||options.reference!=ReferenceKind::None)return false;}
        else if(options.reference==ReferenceKind::None||options.points<3)return false;
    }else if(options.apply||options.measureOnlyRequested||options.clear)return false;
    return true;
}

bool OpenAlignmentMapping(AlignmentMapping& ipc, const Options& options, std::string& error) {
 try {
  ipc.path=options.configPath;ipc.config=mb::loadConfig(ipc.path);
  ipc.target=mb::selectWorldCalibrationTarget(ipc.config,options.source,options.device,
      options.tracker,options.inputSpace,options.inputRevision);
  return true;
 }catch(const std::exception& e){error=e.what();return false;}
}
bool CheckMapping(const AlignmentMapping& ipc,std::string& error) {
 try {
  mb::validateWorldCalibrationTarget(mb::loadConfig(ipc.path),ipc.target);return true;
 }catch(const std::exception& e){error=e.what();return false;}
}
bool WriteAlignment(const AlignmentMapping& ipc,const mb::Rigid& transform,std::string& error) {
 try {
  auto current=mb::loadConfig(ipc.path);
  auto candidate=mb::worldCalibrationCandidate(current,ipc.target,transform,true);
  mb::saveConfig(ipc.path,candidate);return true;
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

void ListMappings(const mb::Config& config) {
    std::cout << "Configured calibration inputs (explicit selection is still required):\n";
    for (const auto& [key,binding] : config.bindings)
        std::cout << "  tracker=" << binding.tracker
                  << " source=" << binding.source
                  << " device=" << binding.device
                  << " input-space=" << binding.inputSpace
                  << " input-revision=" << binding.inputRevision
                  << " profile=" << binding.profile << '\n';
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

class MirrorReader {
public:
    MirrorReader(std::uint16_t port, const mb::WorldCalibrationTarget& target)
        : input_(port), target_(target) {}

    std::optional<mb::Vec> receivePoint() {
        std::optional<mb::Vec> latest;
        for (int count = 0; count < 128; ++count) {
            const auto datagram = input_.receive();
            if (!datagram) break;
            mb::c1::Envelope envelope;mb::c1::Error error;
            if (!mb::c1::DecodeEnvelope(
                    reinterpret_cast<const std::uint8_t*>(datagram->bytes.data()),
                    datagram->bytes.size(),envelope,error)) continue;
            const auto* observation=std::get_if<mb::c1::TrackerObservation>(&envelope);
            if (!observation || observation->source_id!=target_.binding.source ||
                observation->device_id!=target_.binding.device) continue;
            mb::validateCalibrationObservationIdentity(*observation,target_,session_);
            if (lastSequence_>=0 && observation->sequence<lastSequence_)
                throw std::runtime_error("observation sequence moved backward during calibration");
            if (observation->sequence==lastSequence_) continue;
            lastSequence_=observation->sequence;
            if (observation->modality!="full" || !observation->validity.position ||
                !observation->position) continue;
            if (mb::norm(target_.binding.mount.translation)>0 &&
                (!observation->validity.orientation || !observation->orientation)) continue;
            latest=mb::calibrationSourcePoint(*observation,target_,session_);
        }
        return latest;
    }

    const std::optional<std::string>& session() const { return session_; }

private:
    mb::Udp input_;
    const mb::WorldCalibrationTarget& target_;
    std::optional<std::string> session_;
    std::int64_t lastSequence_=-1;
};

std::optional<mb::PointCorrespondence> CapturePoint(
    vr::IVRSystem* system,
    vr::TrackedDeviceIndex_t referenceIndex,
    MirrorReader& mirror,
    const AlignmentMapping& mapping,
    const Options& options,
    int pointIndex) {
    std::string error;
    if(!CheckMapping(mapping,error))throw std::runtime_error(error);
    std::cout << "Point " << pointIndex+1 << "/" << options.points
              << ": place the rigid tracker/reference fixture, keep it still, then press Enter.\n";
    std::string confirmation;if(!std::getline(std::cin,confirmation))
        throw std::runtime_error("capture cancelled before all points were measured");

    mb::Vec sourceSum{},referenceSum{};
    int valid=0;
    vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
    const auto timeoutMs=std::max<std::int64_t>(5000,std::min<std::int64_t>(60000,
        static_cast<std::int64_t>(options.samples)*options.intervalMs*30));
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeoutMs);
    while(valid<options.samples&&std::chrono::steady_clock::now()<deadline){
        system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding,0.0F,
            poses,vr::k_unMaxTrackedDeviceCount);
        const auto native=mirror.receivePoint();
        const auto& reference=poses[referenceIndex];
        if(native&&reference.bDeviceIsConnected&&reference.bPoseIsValid){
            sourceSum=mb::add(sourceSum,*native);
            referenceSum=mb::add(referenceSum,PoseTranslation(reference));
            ++valid;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(options.intervalMs));
    }
    if(valid<options.samples){
        std::cerr << "Too few valid paired mirror/reference frames at point "
                  << pointIndex+1 << ": " << valid << "/" << options.samples << '\n';
        return {};
    }
    if(!CheckMapping(mapping,error))throw std::runtime_error(error);
    const auto sourceMean=mb::scale(sourceSum,1.0/static_cast<double>(valid));
    const auto referenceMean=mb::scale(referenceSum,1.0/static_cast<double>(valid));
    std::cout << std::fixed << std::setprecision(6)
              << "  native/profile point: (" << sourceMean[0] << ", " << sourceMean[1] << ", " << sourceMean[2] << ") m\n"
              << "  SteamVR reference:   (" << referenceMean[0] << ", " << referenceMean[1] << ", " << referenceMean[2] << ") m\n";
    return mb::PointCorrespondence{sourceMean,referenceMean};
}

int RunCalibration(vr::IVRSystem* system,
                   AlignmentMapping& mapping,
                   const Options& options) {
    const auto referenceIndex = FindReference(system, options.reference);
    if (!referenceIndex.has_value()) {
        std::cerr << "Requested reference device was not found in SteamVR: "
                  << ReferenceName(options.reference) << '\n';
        return 5;
    }

    MirrorReader mirror(static_cast<std::uint16_t>(options.mirrorPort),mapping.target);
    std::cout << "Tracker:   source=" << options.source
              << " device=" << options.device
              << " tracker=" << options.tracker << '\n';
    std::cout << "Input:     space=" << options.inputSpace
              << " revision=" << options.inputRevision
              << " profile=" << mapping.target.binding.profile
              << " mirror=127.0.0.1:" << options.mirrorPort << '\n';
    std::cout << "Reference: index=" << *referenceIndex
              << " type=" << ReferenceName(options.reference) << '\n';
    std::cout << "Capture plan: " << options.points << " non-collinear points, "
              << options.samples << " new mirror frames averaged per point.\n";

    std::vector<mb::PointCorrespondence> points;
    for(int point=0;point<options.points;++point){
        auto captured=CapturePoint(system,*referenceIndex,mirror,mapping,options,point);
        if(!captured)return 6;
        points.push_back(*captured);
    }
    const auto result=mb::solveRigidAlignment(points);
    std::string mappingError;if(!CheckMapping(mapping,mappingError)){
        std::cerr << mappingError << '\n';return 7;
    }
    const auto& q=result.transform.rotation;const auto& t=result.transform.translation;
    std::cout << std::fixed << std::setprecision(8)
              << "World rotation xyzw: (" << q[0] << ", " << q[1] << ", " << q[2] << ", " << q[3] << ")\n"
              << "World translation m: (" << t[0] << ", " << t[1] << ", " << t[2] << ")\n"
              << "Residual RMS: " << result.rmsResidual << " m\n"
              << "Residual max: " << result.maxResidual << " m\n"
              << "CALIBRATION_RESULT " << q[0] << ' ' << q[1] << ' ' << q[2] << ' ' << q[3]
              << ' ' << t[0] << ' ' << t[1] << ' ' << t[2] << '\n';

    if (!options.apply) {
        std::cout << "Measurement only (default); configuration was not changed. Re-run with --apply to persist this measurement.\n";
        return 0;
    }

    std::string error;if (!WriteAlignment(mapping,result.transform,error)) {
        std::cerr << error << '\n';
        return 7;
    }
    std::cout << "Applied world.rotation/world.translation; mapping_revision incremented once.\n"
              << "The tracking service was not restarted.\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) try {
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
        const mb::Rigid identity{};
        std::string error;
        if (!WriteAlignment(ipc, identity, error)) {
            std::cerr << error << '\n';
            return 7;
        }
        std::cout << "World rotation/translation cleared; mapping_revision incremented once.\n"
                  << "The tracking service was not restarted.\n";
        return 0;
    }

    vr::EVRInitError initError = vr::VRInitError_None;
    vr::IVRSystem* system = vr::VR_Init(&initError, vr::VRApplication_Utility);
    if (system == nullptr || initError != vr::VRInitError_None) {
        std::cerr << "VR_Init failed: "
                  << vr::VR_GetVRInitErrorAsEnglishDescription(initError) << '\n';
        return 8;
    }
    struct ShutdownOpenVr { ~ShutdownOpenVr(){vr::VR_Shutdown();} } shutdownOpenVr;

    int result = 0;
    if (options.list) {
        ListDevices(system);
        if(!options.configPath.empty())ListMappings(mb::loadConfig(options.configPath));
    } else if (options.listTrackers) {
        ListMonakaTrackers(system);
    } else {
        result = RunCalibration(system, ipc, options);
    }

    return result;
} catch(const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 9;
}
