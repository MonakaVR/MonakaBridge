#include "monaka_bridge/calibration.hpp"
namespace mb {
Calibrated calibrate(const c1::TrackerObservation& in,const Binding& b,const Profile& p,History& h,std::int64_t time){
 Calibrated out;
 std::optional<Quat> q;
 if(in.validity.orientation && in.orientation)q=normalized(permute(*in.orientation,p.quaternionAxes));
 const bool offset=norm(b.mount.translation)>0;
 std::optional<Vec> omega;
 if(q && p.angularSpaceVerified && in.angular_velocity){
  const auto& d=*in.angular_velocity;
  omega=d.frame=="space"?permute(d.value,p.positionAxes):rotate(*q,d.value);
 }
 if(q && !omega && h.q && time>h.timestamp)omega=angularVelocity(*h.q,*q,double(time-h.timestamp)/1e9);
 if(q){
  if(h.q)*q=continuous(*q,*h.q);
  out.orientation=normalized(mul(mul(b.world.rotation,*q),b.mount.rotation));
  h.q=q;h.timestamp=time;
 }else h=History{};
 if(in.validity.position && in.position && (!offset||q)){
  auto local=permute(*in.position,p.positionAxes);
  if(offset)local=add(local,rotate(*q,b.mount.translation));
  out.position=add(rotate(b.world.rotation,local),b.world.translation);
  if(in.linear_velocity){
   const auto& d=*in.linear_velocity;
   std::optional<Vec> v;
   if(d.frame=="space")v=permute(d.value,p.positionAxes);
   else if(q)v=rotate(*q,d.value);
   if(v && (!offset||omega)){
    if(offset)*v=add(*v,cross(*omega,rotate(*q,b.mount.translation)));
    out.velocity=rotate(b.world.rotation,*v);
   }
  }
  // Offset acceleration needs angular acceleration and centripetal terms, not guaranteed here.
  if(!offset && in.linear_acceleration){
   const auto& d=*in.linear_acceleration;
   if(d.frame=="space")out.acceleration=rotate(b.world.rotation,permute(d.value,p.positionAxes));
   else if(q)out.acceleration=rotate(b.world.rotation,rotate(*q,d.value));
  }
 }
 if(omega)out.omega=rotate(b.world.rotation,*omega);
 return out;
}
}
