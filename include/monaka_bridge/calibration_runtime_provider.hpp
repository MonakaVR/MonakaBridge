#pragma once
#include "calibration_session.hpp"
#include "network.hpp"
#include <openvr.h>
#include <array>
#include <thread>

namespace mb::local_calibration {
// Thin public-SDK seam. These are pinned OpenVR values, not another pose or wire
// model. Only the input owner may use it; construction/configuration is inert.
class CalibrationReferenceApi {
public:
 virtual ~CalibrationReferenceApi()=default;
 virtual void initialize(vr::EVRApplicationType)=0;
 virtual void shutdown()=0;
 virtual std::uint64_t generation()=0;
 virtual bool event(vr::VREvent_t&)=0;
 virtual vr::HmdMatrix34_t standing()=0;
 virtual void poses(vr::ETrackingUniverseOrigin,float,std::array<vr::TrackedDevicePose_t,vr::k_unMaxTrackedDeviceCount>&)=0;
 virtual vr::ETrackedDeviceClass deviceClass(vr::TrackedDeviceIndex_t)=0;
 virtual vr::ETrackedControllerRole role(vr::TrackedDeviceIndex_t)=0;
 virtual std::string serial(vr::TrackedDeviceIndex_t)=0;
 virtual std::uint64_t universe()=0;
};
class CalibrationMirrorTransport {
public:
 virtual ~CalibrationMirrorTransport()=default;
 virtual void open(std::uint16_t)=0;
 virtual std::optional<Datagram> receive()=0;
 virtual void shutdown()=0;
};
// Same proper-rigid solver and unit-basis residual contract as ReferenceRigid
// in the unchanged legacy calibrator. No temporal/physical epsilon is added.
Rigid calibrationReferenceRigid(const vr::HmdMatrix34_t&);
bool sameStandingValues(const vr::HmdMatrix34_t&,const vr::HmdMatrix34_t&);

class RuntimeCaptureProvider final:public CaptureProvider {
public:
 using Sleeper=std::function<void(std::chrono::nanoseconds)>;
 RuntimeCaptureProvider(std::unique_ptr<CalibrationReferenceApi>,std::unique_ptr<CalibrationMirrorTransport>,Clock=steadyNow,Sleeper={});
 bool supported()const override{return true;}
 ProviderBatch discover(const std::atomic<bool>&)override;
 ProviderBatch guard()override;
 void capture(const std::function<void(const ProviderBatch&)>&,const std::atomic<bool>&)override;
 void shutdown()override;
private:
 std::unique_ptr<CalibrationReferenceApi> api_;
 std::unique_ptr<CalibrationMirrorTransport> transport_;
 Clock clock_;Sleeper sleep_;std::thread::id owner_;
 bool started_=false,closed_=false,referenceFrozen_=false;
 InputWitness witness_;vr::TrackedDeviceIndex_t reference_=vr::k_unTrackedDeviceIndexInvalid;
 vr::HmdMatrix34_t standing_{};std::uint64_t universe_=0;
 std::int64_t lastPoll_=-1;
 void validateConfiguration(const ProviderOpenOptions&)const override;
 void requireOwner()const;
 void events(bool enforce);
 vr::TrackedDeviceIndex_t selectReference();
 ProviderBatch poll();
};
// Implemented only in the production entry target. Fixture libraries do not
// link the real OpenVR/Udp wrappers or add fake-provider flags to the product.
std::unique_ptr<CaptureProvider> makeProductionCaptureProvider();
Json productionCalibrationVersion();
} // namespace mb::local_calibration
