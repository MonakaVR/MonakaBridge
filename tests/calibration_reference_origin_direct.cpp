// Separate TU because the public OpenVR client/driver headers share interfaces.
#include "helpers.hpp"
#include "direct_pose.hpp"

mb::Rigid referenceOriginDirectPose(const mb::DirectSample& sample,std::int64_t now){
 const auto pose=mb::steamvr::directPose(&sample,now);
 CHECK(pose.poseIsValid&&pose.deviceIsConnected);
 CHECK(pose.qWorldFromDriverRotation.w==1&&pose.qWorldFromDriverRotation.x==0&&pose.qWorldFromDriverRotation.y==0&&pose.qWorldFromDriverRotation.z==0);
 CHECK(pose.vecWorldFromDriverTranslation[0]==0&&pose.vecWorldFromDriverTranslation[1]==0&&pose.vecWorldFromDriverTranslation[2]==0);
 return {{pose.qRotation.x,pose.qRotation.y,pose.qRotation.z,pose.qRotation.w},{pose.vecPosition[0],pose.vecPosition[1],pose.vecPosition[2]}};
}
