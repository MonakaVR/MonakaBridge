#include "tracker_device.hpp"
#include "driver_log.hpp"
#include "direct_pose.hpp"
#include <utility>
namespace mb::steamvr {
TrackerDevice::TrackerDevice(std::string serial,std::string renderModel):serial_(std::move(serial)),pose_(directPose(nullptr,0)),renderModel_(std::move(renderModel)){}
vr::EVRInitError TrackerDevice::Activate(uint32_t objectId) {
    objectId_ = objectId;

    const vr::PropertyContainerHandle_t container =
        vr::VRProperties()->TrackedDeviceToPropertyContainer(objectId_);
    vr::VRProperties()->SetStringProperty(
        container, vr::Prop_SerialNumber_String, serial_.c_str());
    vr::VRProperties()->SetStringProperty(
        container, vr::Prop_ModelNumber_String, "Monaka Bridge Direct");
    appliedRenderModel_.reset();
    ApplyPresentation();
    vr::VRProperties()->SetStringProperty(
        container, vr::Prop_ManufacturerName_String, "MonakaVR");
    vr::VRProperties()->SetStringProperty(
        container, vr::Prop_TrackingSystemName_String, "monaka_bridge");
    vr::VRProperties()->SetBoolProperty(
        container, vr::Prop_DeviceIsWireless_Bool, true);
    // Backend battery semantics are still a raw bucket candidate, so do not
    // advertise SteamVR percentage battery support yet.
    vr::VRProperties()->SetBoolProperty(
        container, vr::Prop_DeviceProvidesBatteryStatus_Bool, false);

    DriverLog("monaka_bridge: activated tracker %s as device %u",
              serial_.c_str(), objectId_);
    return vr::VRInitError_None;
}

void TrackerDevice::Deactivate() {
    objectId_ = vr::k_unTrackedDeviceIndexInvalid;
    pose_ = directPose(nullptr,0);
    presentationVisible_=false;
    appliedRenderModel_.reset();
}

void TrackerDevice::EnterStandby() {}

void* TrackerDevice::GetComponent(const char* /*componentNameAndVersion*/) {
    return nullptr;
}

void TrackerDevice::DebugRequest(const char* /*request*/,
                                 char* responseBuffer,
                                 uint32_t responseBufferSize) {
    if (responseBuffer != nullptr && responseBufferSize > 0) {
        responseBuffer[0] = '\0';
    }
}

vr::DriverPose_t TrackerDevice::GetPose() {
    return pose_;
}


void TrackerDevice::ApplyPresentation(){
 if(objectId_==vr::k_unTrackedDeviceIndexInvalid||renderModel_.empty())return;
 const std::string desired=presentationVisible_&&!presentationRevoked_?renderModel_:"";
 if(appliedRenderModel_&&*appliedRenderModel_==desired)return;
 const auto error=vr::VRProperties()->SetStringProperty(
  vr::VRProperties()->TrackedDeviceToPropertyContainer(objectId_),vr::Prop_RenderModelName_String,desired.c_str());
 // Failed writes remain pending. Presentation failure never changes Pose.
 if(error==vr::TrackedProp_Success)appliedRenderModel_=desired;
}

void TrackerDevice::Update(const DirectSample* sample,std::int64_t now,PresentationState presentation){
 pose_=directPose(sample,now);
 // Loss/expiry hides temporarily; a different identity/config still revokes
 // this registered object's assignment until a new Provider snapshot.
 if(presentation==PresentationState::Revoked)presentationRevoked_=true;
 presentationVisible_=presentation==PresentationState::Visible&&pose_.poseIsValid;
 ApplyPresentation();
 if(objectId_!=vr::k_unTrackedDeviceIndexInvalid){
  if(sample&&sample->state){auto c=vr::VRProperties()->TrackedDeviceToPropertyContainer(objectId_);const auto& b=sample->state->battery;
   vr::VRProperties()->SetBoolProperty(c,vr::Prop_DeviceProvidesBatteryStatus_Bool,b&&b->fraction.has_value());
   if(b&&b->fraction)vr::VRProperties()->SetFloatProperty(c,vr::Prop_DeviceBatteryPercentage_Float,static_cast<float>(*b->fraction));
   if(b&&b->charging)vr::VRProperties()->SetBoolProperty(c,vr::Prop_DeviceIsCharging_Bool,*b->charging);
  }
  vr::VRServerDriverHost()->TrackedDevicePoseUpdated(objectId_,pose_,sizeof(pose_));
 }
}
}
