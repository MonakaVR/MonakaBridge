#pragma once
#include "monaka_bridge/direct.hpp"
#include <openvr_driver.h>
namespace mb::steamvr {
class TrackerDevice final:public vr::ITrackedDeviceServerDriver{
 std::string serial_;vr::TrackedDeviceIndex_t objectId_=vr::k_unTrackedDeviceIndexInvalid;vr::DriverPose_t pose_{};
public:
 explicit TrackerDevice(std::string serial);
 vr::EVRInitError Activate(uint32_t)override;void Deactivate()override;void EnterStandby()override;
 void* GetComponent(const char*)override;void DebugRequest(const char*,char*,uint32_t)override;
 vr::DriverPose_t GetPose()override;const std::string& Serial()const{return serial_;}
 void Update(const DirectSample*,std::int64_t now);
};
}
