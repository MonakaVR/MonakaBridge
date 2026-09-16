#include "device_provider.hpp"
#include "driver_log.hpp"
namespace mb::steamvr {
// Task2 lifecycle retained: dynamic add; registered objects live until provider Cleanup.
vr::EVRInitError DeviceProvider::Init(vr::IVRDriverContext* context){
 VR_INIT_SERVER_DRIVER_CONTEXT(context);
 try{receiver_=std::make_unique<Udp>(29812);}catch(const std::exception& e){DriverLog("monaka_bridge: %s",e.what());return vr::VRInitError_Driver_Unknown;}
 return vr::VRInitError_None;
}
void DeviceProvider::Cleanup(){devices_.clear();receiver_.reset();feed_=MtpFeed{};VR_CLEANUP_SERVER_DRIVER_CONTEXT();}
const char* const* DeviceProvider::GetInterfaceVersions(){return vr::k_InterfaceVersions;}
bool DeviceProvider::ShouldBlockStandbyMode(){return false;}
void DeviceProvider::EnterStandby(){}void DeviceProvider::LeaveStandby(){}
void DeviceProvider::RunFrame(){
 if(!receiver_)return;
 for(int i=0;i<128;++i){auto d=receiver_->receive();if(!d)break;feed_.receive(d->bytes,monotonicNs(),d->peer);}
 for(auto& [key,sample]:feed_.devices){
  if(devices_.count(key)||!sample.pose)continue;
  auto serial=runtimeSerial(key.first,key.second);auto device=std::make_unique<TrackerDevice>(serial);
  if(vr::VRServerDriverHost()->TrackedDeviceAdded(serial.c_str(),vr::TrackedDeviceClass_GenericTracker,device.get()))devices_.emplace(key,std::move(device));
 }
 auto now=monotonicNs();for(auto& [key,device]:devices_){auto s=feed_.devices.find(key);device->Update(s==feed_.devices.end()?nullptr:&s->second,now);}
 vr::VREvent_t event{};while(vr::VRServerDriverHost()->PollNextEvent(&event,sizeof(event))){}
}
}
