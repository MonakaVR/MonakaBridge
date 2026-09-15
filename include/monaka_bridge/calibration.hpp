#pragma once
#include "monaka/protocol/v1/codec.hpp"
#include <cmath>
#include <optional>
#include <stdexcept>
namespace mb {
namespace c1=monaka::protocol::v1;
using Vec=c1::Vec3; using Quat=c1::QuatXyzw;
inline Vec add(Vec a,Vec b){for(int i=0;i<3;++i)a[i]+=b[i];return a;}
inline Vec scale(Vec a,double b){for(auto& x:a)x*=b;return a;}
inline Vec cross(Vec a,Vec b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
inline double norm(Vec a){return std::hypot(a[0],a[1],a[2]);}
inline Quat normalized(Quat q){double n=std::hypot(std::hypot(q[0],q[1]),std::hypot(q[2],q[3]));if(!std::isfinite(n)||n<1e-12)throw std::invalid_argument("invalid quaternion");for(auto& x:q)x/=n;return q;}
inline Quat inverse(Quat q){q=normalized(q);q[0]=-q[0];q[1]=-q[1];q[2]=-q[2];return q;}
inline Quat mul(Quat a,Quat b){return {a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0],a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3],a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]};}
inline Vec rotate(Quat q,Vec p){q=normalized(q);auto r=mul(mul(q,{p[0],p[1],p[2],0}),inverse(q));return {r[0],r[1],r[2]};}
inline Quat continuous(Quat q,Quat prev){double dot=0;for(int i=0;i<4;++i)dot+=q[i]*prev[i];if(dot<0)for(auto& x:q)x=-x;return q;}
// Migrated from Task2 TrackerDevice::DeriveAngularVelocity, generic xyzw models.
inline std::optional<Vec> angularVelocity(Quat prev,Quat now,double dt){
 if(!(dt>=.001&&dt<=.100))return {};
 auto d=normalized(mul(now,inverse(prev)));if(d[3]<0)for(auto& x:d)x=-x;
 double s=std::hypot(d[0],d[1],d[2]);if(s<=1e-9)return Vec{0,0,0};
 auto v=scale(Vec{d[0],d[1],d[2]},2*std::atan2(s,d[3])/(s*dt));
 if(!std::isfinite(norm(v))||norm(v)>100)return {};return v;
}
// Diagnostic compatibility only. Never used implicitly by normal calibration.
inline Quat orientationZero(Quat now,Quat zero){return normalized(mul(now,inverse(zero)));}
struct Rigid { Quat rotation{0,0,0,1}; Vec translation{0,0,0}; };
struct Profile {
 std::string convention;
 std::array<int,3> positionAxes{1,2,3}; // signed one-based components
 std::array<int,4> quaternionAxes{1,2,3,4};
 bool approved=false, angularSpaceVerified=false;
 std::string evidence;
};
template<std::size_t N> std::array<double,N> permute(const std::array<double,N>& v,const std::array<int,N>& axes){std::array<double,N> o{};for(std::size_t i=0;i<N;++i)o[i]=v.at(std::abs(axes[i])-1)*(axes[i]<0?-1:1);return o;}
struct Binding {
 std::string source,device,tracker,profile,inputSpace,worldSpace;
 std::uint32_t inputRevision=0,worldRevision=0;
 bool spaceApproved=false;
 Rigid world,mount;
};
struct History {std::optional<Quat> q;std::int64_t timestamp=-1;};
struct Calibrated {std::optional<Vec> position,velocity,omega,acceleration;std::optional<Quat> orientation;};
Calibrated calibrate(const c1::TrackerObservation&,const Binding&,const Profile&,History&,std::int64_t fixedTime);
}
