#pragma once
#include "monaka_bridge/direct.hpp"
#include <openvr_driver.h>
#include <algorithm>
namespace mb::steamvr {
// Migrated from Task2 TrackerDevice::DisconnectedPose/Update. Already world-calibrated input.
inline vr::DriverPose_t directPose(const DirectSample* sample,std::int64_t now){
 vr::DriverPose_t out{};out.qWorldFromDriverRotation.w=1;out.qDriverFromHeadRotation.w=1;out.qRotation.w=1;out.result=vr::TrackingResult_Uninitialized;
 MtpFeed freshness;if(!sample||!freshness.fresh(*sample,now))return out;
 const auto& p=*sample->pose;out.deviceIsConnected=true;out.poseIsValid=p.modality=="full"&&p.validity.position&&p.validity.orientation;
 const bool rotationOnly=p.modality=="rotation_only"&&p.validity.orientation;
 out.result=out.poseIsValid?vr::TrackingResult_Running_OK:rotationOnly?vr::TrackingResult_Fallback_RotationOnly:vr::TrackingResult_Running_OutOfRange;
 if(out.poseIsValid&&p.position)for(int i=0;i<3;++i)out.vecPosition[i]=(*p.position)[i];
 if((out.poseIsValid||rotationOnly)&&p.orientation){const auto& q=*p.orientation;out.qRotation={q[3],q[0],q[1],q[2]};}
 if(out.poseIsValid&&p.linear_velocity)for(int i=0;i<3;++i)out.vecVelocity[i]=(*p.linear_velocity)[i];
 if((out.poseIsValid||rotationOnly)&&p.angular_velocity)for(int i=0;i<3;++i)out.vecAngularVelocity[i]=(*p.angular_velocity)[i];
 if(out.poseIsValid&&p.linear_acceleration)for(int i=0;i<3;++i)out.vecAcceleration[i]=(*p.linear_acceleration)[i];
 out.poseTimeOffset=std::clamp(double(sample->fixedTime-now)/1e9,-.100,.020);return out;
}
}
