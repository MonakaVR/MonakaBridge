#pragma once
#include "tracker_device.hpp"
#include "monaka_bridge/network.hpp"
namespace mb::steamvr {
class DeviceProvider final:public vr::IServerTrackedDeviceProvider{
 std::unique_ptr<Udp> receiver_;MtpFeed feed_;
 std::map<Key,std::unique_ptr<TrackerDevice>> devices_;
public:
 vr::EVRInitError Init(vr::IVRDriverContext*)override;void Cleanup()override;
 const char* const* GetInterfaceVersions()override;void RunFrame()override;
 bool ShouldBlockStandbyMode()override;void EnterStandby()override;void LeaveStandby()override;
};
}
