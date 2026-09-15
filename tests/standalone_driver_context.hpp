#pragma once
#include <openvr_driver.h>
#include <cstring>
#include <stdexcept>
// Only the logger used by unactivated Task2 TrackerDevice diagnostics is provided.
// No VR_Init, provider Init/Activate, SteamVR connection or runtime DLL is used.
struct StandaloneDriverContext final : vr::IVRDriverContext, vr::IVRDriverLog {
 unsigned logRequests=0,logMessages=0,unexpectedRequests=0;
 StandaloneDriverContext(){if(vr::VRDriverContext())throw std::runtime_error("unexpected existing OpenVR context");vr::OpenVRInternal_ModuleServerDriverContext().Clear();vr::VRDriverContext()=this;}
 ~StandaloneDriverContext(){vr::CleanupDriverContext();}
 void* GetGenericInterface(const char* version,vr::EVRInitError* error)override {
  if(std::strcmp(version,vr::IVRDriverLog_Version)==0){++logRequests;if(error)*error=vr::VRInitError_None;return static_cast<vr::IVRDriverLog*>(this);}
  ++unexpectedRequests;throw std::runtime_error("standalone harness attempted a SteamVR runtime interface");
 }
 vr::DriverHandle_t GetDriverHandle()override{++unexpectedRequests;throw std::runtime_error("standalone harness attempted a SteamVR driver handle");}
 void Log(const char*)override{++logMessages;}
};
