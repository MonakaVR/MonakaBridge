#pragma once
// Test-only implementations of the pinned OpenVR interfaces. No VR_Init/runtime.
#include <openvr_driver.h>
#include <nlohmann/json.hpp>
#include "monaka_bridge/network.hpp"
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#include <vector>

namespace provider_test {
using Json = nlohmann::json;
inline void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
struct Events {
    std::ofstream stream;
    std::string scenario;
    std::uint64_t sequence = 0;
    explicit Events(const std::string& path): stream(path) { require(bool(stream), "event log open"); }
    void write(std::string event, Json fields = Json::object()) {
        require(sequence<20000 && stream.tellp()<16*1024*1024,"bounded event log limit");
        fields["event"] = event; fields["case"] = scenario;
        fields["event_sequence"] = sequence++; fields["monotonic_ns"] = mb::monotonicNs();
        stream << fields.dump() << '\n'; stream.flush();
        require(bool(stream), "event log write");
    }
};
struct StrictHost final: vr::IVRDriverContext, vr::IVRServerDriverHost,
    vr::IVRProperties, vr::IVRDriverLog, vr::IVRSettings, vr::IVRDriverManager, vr::IVRResources {
    enum class Stage { Pending, Activating, Active, Deactivated };
    struct Device {
        std::string serial; vr::ITrackedDeviceServerDriver* driver;
        Stage stage = Stage::Pending;
        std::map<vr::ETrackedDeviceProperty, Json> properties;
        vr::DriverPose_t pose{}; unsigned poses = 0;
    };
    Events& events;
    std::map<uint32_t, Device> devices;
    std::map<std::string, unsigned> attempts;
    std::map<std::string, unsigned> interfaceRequests;
    std::map<std::string, std::string> expectedRenderModels; // Explicit test oracle per full serial.
    std::string missingInterface;
    int32_t inputPort=0;
    unsigned rejectNext = 0, totalPoses = 0, calls = 0;
    bool alive = true;
    explicit StrictHost(Events& e): events(e) {}
    [[noreturn]] void unexpected(const char* method) const {
        throw std::runtime_error(std::string("unexpected OpenVR call: ") + method);
    }
    void live() { require(alive, "callback after context retirement"); ++calls; }
    void* GetGenericInterface(const char* name, vr::EVRInitError* error) override {
        live(); require(name != nullptr, "null interface name"); ++interfaceRequests[name];
        events.write("GetGenericInterface", {{"name", name}, {"missing", missingInterface == name}});
        if (error) *error = vr::VRInitError_None;
        if (missingInterface == name) { if(error) *error = vr::VRInitError_Init_InterfaceNotFound; return nullptr; }
        if (!std::strcmp(name, vr::IVRServerDriverHost_Version)) return static_cast<vr::IVRServerDriverHost*>(this);
        if (!std::strcmp(name, vr::IVRProperties_Version)) return static_cast<vr::IVRProperties*>(this);
        if (!std::strcmp(name, vr::IVRDriverLog_Version)) return static_cast<vr::IVRDriverLog*>(this);
        if (!std::strcmp(name, vr::IVRSettings_Version)) return static_cast<vr::IVRSettings*>(this);
        if (!std::strcmp(name, vr::IVRDriverManager_Version)) return static_cast<vr::IVRDriverManager*>(this);
        if (!std::strcmp(name, vr::IVRResources_Version)) return static_cast<vr::IVRResources*>(this);
        unexpected("unknown interface");
    }
    vr::DriverHandle_t GetDriverHandle() override { unexpected("context driver handle"); }
    bool TrackedDeviceAdded(const char* serial, vr::ETrackedDeviceClass kind, vr::ITrackedDeviceServerDriver* driver) override {
        live(); require(serial && *serial && driver, "invalid Added arguments");
        require(kind == vr::TrackedDeviceClass_GenericTracker, "device class must be GenericTracker");
        for (const auto& [id,d]: devices) require(d.serial != serial && d.driver != driver, "duplicate serial/pointer");
        ++attempts[serial]; const bool accepted = rejectNext == 0;
        events.write("TrackedDeviceAdded", {{"serial",serial},{"class",int(kind)},
            {"pointer",reinterpret_cast<uintptr_t>(driver)},{"accepted",accepted},{"attempt",attempts[serial]}});
        if (!accepted) { --rejectNext; return false; } // Never retain a rejected pointer.
        const auto id = uint32_t(devices.size() + 1);
        devices.emplace(id, Device{serial,driver}); return true;
    }
    void activatePending() {
        for (auto& [id,d]: devices) if (d.stage == Stage::Pending) {
            d.stage = Stage::Activating;
            events.write("Activate", {{"phase","call"},{"object_id",id},{"serial",d.serial}});
            const auto result = d.driver->Activate(id);
            events.write("Activate", {{"phase","return"},{"object_id",id},{"result",int(result)}});
            require(result == vr::VRInitError_None, "Activate failed"); d.stage = Stage::Active;
            require(d.properties.at(vr::Prop_SerialNumber_String) == d.serial, "serial property mismatch");
            require(d.properties.at(vr::Prop_ModelNumber_String) == "Monaka Bridge Direct", "model number");
            require(d.properties.at(vr::Prop_TrackingSystemName_String) == "monaka_bridge", "tracking system");
            require(d.properties.at(vr::Prop_ManufacturerName_String) == "MonakaVR", "manufacturer");
            require(d.properties.at(vr::Prop_DeviceIsWireless_Bool) == true, "wireless property");
            require(d.properties.at(vr::Prop_DeviceProvidesBatteryStatus_Bool) == false, "initial battery claim");
            const auto expected=expectedRenderModels.find(d.serial);
            if(expected==expectedRenderModels.end())require(!d.properties.count(vr::Prop_RenderModelName_String), "unexpected render model selection");
            else require(d.properties.at(vr::Prop_RenderModelName_String)==expected->second,"explicit render model property");
        }
    }
    void deactivateAll() {
        for (auto& [id,d]: devices) if (d.stage == Stage::Active) {
            events.write("Deactivate", {{"phase","call"},{"object_id",id}});
            d.driver->Deactivate(); d.stage = Stage::Deactivated;
            const auto pose = d.driver->GetPose();
            require(!pose.poseIsValid && !pose.deviceIsConnected, "Deactivate must invalidate GetPose");
            events.write("Deactivate", {{"phase","return"},{"object_id",id},{"valid",pose.poseIsValid}});
        }
    }
    void TrackedDevicePoseUpdated(uint32_t id, const vr::DriverPose_t& pose, uint32_t size) override {
        live(); require(size == sizeof(vr::DriverPose_t), "pose ABI size");
        auto found = devices.find(id); require(found != devices.end(), "unknown object ID");
        auto& d=found->second; require(d.stage == Stage::Active, "pose before Activate/after Deactivate");
        for (const auto* values: {pose.vecPosition,pose.vecVelocity,pose.vecAcceleration,
                                pose.vecAngularVelocity,pose.vecAngularAcceleration,
                                pose.vecWorldFromDriverTranslation,pose.vecDriverFromHeadTranslation})
            for(int i=0;i<3;++i) require(std::isfinite(values[i]), "nonfinite pose vector");
        for (const auto& q: {pose.qRotation,pose.qWorldFromDriverRotation,pose.qDriverFromHeadRotation})
            require(std::isfinite(q.x)&&std::isfinite(q.y)&&std::isfinite(q.z)&&std::isfinite(q.w), "nonfinite quaternion");
        require(std::isfinite(pose.poseTimeOffset), "nonfinite pose offset");
        d.pose=pose; ++d.poses; ++totalPoses;
        events.write("TrackedDevicePoseUpdated", {{"object_id",id},{"size",size},
            {"connected",pose.deviceIsConnected},{"valid",pose.poseIsValid},{"result",int(pose.result)},
            {"position",{pose.vecPosition[0],pose.vecPosition[1],pose.vecPosition[2]}},
            {"orientation_wxyz",{pose.qRotation.w,pose.qRotation.x,pose.qRotation.y,pose.qRotation.z}},
            {"velocity",{pose.vecVelocity[0],pose.vecVelocity[1],pose.vecVelocity[2]}},
            {"angular_velocity",{pose.vecAngularVelocity[0],pose.vecAngularVelocity[1],pose.vecAngularVelocity[2]}},
            {"acceleration",{pose.vecAcceleration[0],pose.vecAcceleration[1],pose.vecAcceleration[2]}},
            {"pose_time_offset",pose.poseTimeOffset}});
    }
    bool PollNextEvent(vr::VREvent_t* event, uint32_t size) override {
        live(); require(event && size==sizeof(*event), "event ABI"); return false;
    }
    vr::PropertyContainerHandle_t TrackedDeviceToPropertyContainer(vr::TrackedDeviceIndex_t id) override {
        live(); require(devices.count(id)!=0, "unknown property object");
        const auto stage=devices.at(id).stage;
        require(stage==Stage::Activating||stage==Stage::Active, "property container outside active lifetime");
        events.write("PropertyContainer",{{"object_id",id},{"container",0x10000ULL+id}});
        return 0x10000ULL+id;
    }
    vr::ETrackedPropertyError WritePropertyBatch(vr::PropertyContainerHandle_t container, vr::PropertyWrite_t* batch, uint32_t count) override {
        live(); require(container>0x10000 && container<0x10000+vr::k_unMaxTrackedDeviceCount, "invalid property container");
        const auto id=uint32_t(container-0x10000); require(devices.count(id)!=0, "unowned property container");
        auto& d=devices.at(id); require(d.stage==Stage::Active||d.stage==Stage::Activating, "property write lifetime");
        require(batch && count>0 && count<=16, "invalid property batch");
        for(uint32_t i=0;i<count;++i) {
            auto& p=batch[i]; require(p.writeType==vr::PropertyWrite_Set && p.pvBuffer, "unsupported property operation");
            Json value; vr::PropertyTypeTag_t tag=0;
            switch(p.prop) {
            case vr::Prop_RenderModelName_String: {
                require(expectedRenderModels.count(d.serial)!=0,"unprovisioned render model write");
                tag=vr::k_unStringPropertyTag;
                require(p.unBufferSize>0&&p.unBufferSize<=vr::k_unMaxPropertyStringSize,"render model string size");
                require(static_cast<char*>(p.pvBuffer)[p.unBufferSize-1]=='\0',"render model termination");
                value=static_cast<char*>(p.pvBuffer);
                require(value.get<std::string>().size()+1==p.unBufferSize,"render model embedded NUL");
                require(value==expectedRenderModels.at(d.serial)||value=="","unknown render model property value");
                break;
            }
            case vr::Prop_SerialNumber_String: case vr::Prop_ModelNumber_String:
            case vr::Prop_ManufacturerName_String: case vr::Prop_TrackingSystemName_String:
                tag=vr::k_unStringPropertyTag;
                require(p.unBufferSize>0 && p.unBufferSize<=vr::k_unMaxPropertyStringSize, "string property size");
                require(static_cast<char*>(p.pvBuffer)[p.unBufferSize-1]=='\0', "unterminated string property");
                require(std::strlen(static_cast<char*>(p.pvBuffer))+1==p.unBufferSize, "embedded NUL property");
                value=static_cast<char*>(p.pvBuffer); break;
            case vr::Prop_DeviceIsWireless_Bool: case vr::Prop_DeviceProvidesBatteryStatus_Bool: case vr::Prop_DeviceIsCharging_Bool:
                tag=vr::k_unBoolPropertyTag; require(p.unBufferSize==sizeof(bool), "bool ABI"); value=*static_cast<bool*>(p.pvBuffer); break;
            case vr::Prop_DeviceBatteryPercentage_Float:
                tag=vr::k_unFloatPropertyTag; require(p.unBufferSize==sizeof(float), "float ABI");
                value=*static_cast<float*>(p.pvBuffer); require(std::isfinite(value.get<float>()) && value>=0 && value<=1, "battery bounds"); break;
            default: unexpected("unapproved property");
            }
            require(p.unTag==tag, "property type tag"); d.properties[p.prop]=value; p.eError=vr::TrackedProp_Success;
            events.write("SetProperty",{{"container",container},{"object_id",id},{"property",int(p.prop)},{"tag",tag},{"value",value}});
        }
        return vr::TrackedProp_Success;
    }
    vr::ETrackedPropertyError ReadPropertyBatch(vr::PropertyContainerHandle_t,vr::PropertyRead_t*,uint32_t) override { unexpected("property read"); }
    const char* GetPropErrorNameFromEnum(vr::ETrackedPropertyError) override { unexpected("property error name"); }
    void Log(const char* message) override { live(); require(message, "null log"); events.write("Log",{{"message",message}}); }
    void VsyncEvent(double) override { unexpected("VsyncEvent"); }
    void VendorSpecificEvent(uint32_t,vr::EVREventType,const vr::VREvent_Data_t&,double) override { unexpected("VendorSpecificEvent"); }
    bool IsExiting() override { unexpected("IsExiting"); }
    void GetRawTrackedDevicePoses(float,vr::TrackedDevicePose_t*,uint32_t) override { unexpected("GetRawTrackedDevicePoses"); }
    void RequestRestart(const char*,const char*,const char*,const char*) override { unexpected("RequestRestart"); }
    uint32_t GetFrameTimings(vr::Compositor_FrameTiming*,uint32_t) override { unexpected("GetFrameTimings"); }
    void SetDisplayEyeToHead(uint32_t,const vr::HmdMatrix34_t&,const vr::HmdMatrix34_t&) override { unexpected("SetDisplayEyeToHead"); }
    void SetDisplayProjectionRaw(uint32_t,const vr::HmdRect2_t&,const vr::HmdRect2_t&) override { unexpected("SetDisplayProjectionRaw"); }
    void SetRecommendedRenderTargetSize(uint32_t,uint32_t,uint32_t) override { unexpected("SetRecommendedRenderTargetSize"); }
    const char* GetSettingsErrorNameFromEnum(vr::EVRSettingsError) override { unexpected("settings error"); }
    void SetBool(const char*,const char*,bool,vr::EVRSettingsError*) override { unexpected("settings SetBool"); }
    void SetInt32(const char*,const char*,int32_t,vr::EVRSettingsError*) override { unexpected("settings SetInt32"); }
    void SetFloat(const char*,const char*,float,vr::EVRSettingsError*) override { unexpected("settings SetFloat"); }
    void SetString(const char*,const char*,const char*,vr::EVRSettingsError*) override { unexpected("settings SetString"); }
    bool GetBool(const char*,const char*,vr::EVRSettingsError*) override { unexpected("settings GetBool"); }
    int32_t GetInt32(const char* section,const char* key,vr::EVRSettingsError* error) override { live();require(std::string(section)=="driver_monaka_bridge"&&std::string(key)=="input_port"&&inputPort>=49152,"only isolated ephemeral input_port is permitted");if(error)*error=vr::VRSettingsError_None;return inputPort; }
    float GetFloat(const char*,const char*,vr::EVRSettingsError*) override { unexpected("settings GetFloat"); }
    void GetString(const char*,const char*,char*,uint32_t,vr::EVRSettingsError*) override { unexpected("settings GetString"); }
    void RemoveSection(const char*,vr::EVRSettingsError*) override { unexpected("settings RemoveSection"); }
    void RemoveKeyInSection(const char*,const char*,vr::EVRSettingsError*) override { unexpected("settings RemoveKey"); }
    uint32_t GetDriverCount() const override { unexpected("GetDriverCount"); }
    uint32_t GetDriverName(vr::DriverId_t,char*,uint32_t) override { unexpected("GetDriverName"); }
    vr::DriverHandle_t GetDriverHandle(const char*) override { unexpected("manager GetDriverHandle"); }
    bool IsEnabled(vr::DriverId_t) const override { unexpected("IsEnabled"); }
    uint32_t LoadSharedResource(const char*,char*,uint32_t) override { unexpected("LoadSharedResource"); }
    uint32_t GetResourceFullPath(const char*,const char*,char*,uint32_t) override { unexpected("GetResourceFullPath"); }
};
} // namespace provider_test
