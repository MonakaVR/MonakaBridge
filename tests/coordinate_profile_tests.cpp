#include "helpers.hpp"
#include <iostream>
mb::Vec basis(mb::Vec v){return {v[2],v[1],-v[0]};}
// Independent Rodrigues reference, rather than the production quaternion product.
mb::Vec action(mb::Vec axis,double angle,mb::Vec v){
 const auto length=mb::norm(axis);for(auto& x:axis)x/=length;
 double dot=0;for(int i=0;i<3;++i)dot+=axis[i]*v[i];
 return mb::add(mb::add(mb::scale(v,std::cos(angle)),mb::scale(mb::cross(axis,v),std::sin(angle))),mb::scale(axis,dot*(1-std::cos(angle))));
}
void close(mb::Vec a,mb::Vec b){for(int i=0;i<3;++i)CHECK(near(a[i],b[i]));}
int main()try{
 auto c=mb::loadConfig(std::filesystem::path(ROOT_DIR)/"config/bridge.example.json");
 const auto profile=c.profiles.at("vive-hil-v1");CHECK(profile.approved&&!profile.angularSpaceVerified);
 const auto profileV2=c.profiles.at("vive-hil-v2");CHECK(profileV2.approved&&!profileV2.angularSpaceVerified);
 const std::array<int,3> positionV2{1,2,3};const std::array<int,4> quaternionV2{-1,-2,3,4};
 CHECK(profileV2.positionAxes==positionV2);CHECK(profileV2.quaternionAxes==quaternionV2);
 CHECK(!c.profiles.at("vive-unverified").approved);CHECK(c.bindings.empty());
 auto p=sample();p.coordinate_space.convention="vut-native-v1";
 mb::Binding b=config().bindings.at({"pico","device"});b.profile="vive-hil-v1";
 for(const auto axis:{mb::Vec{1,0,0},mb::Vec{0,1,0},mb::Vec{0,0,1}}){
  p.position=axis;p.orientation=mb::Quat{0,0,0,1};mb::History h;
  auto out=mb::calibrate(p,b,profile,h,10000000);CHECK(out.position&&out.orientation);
  close(*out.position,basis(axis));close(mb::rotate(*out.orientation,axis),axis);
 }
 for(const auto axis:{mb::Vec{1,0,0},mb::Vec{0,1,0},mb::Vec{0,0,1},mb::Vec{1,2,3}}){
  const double angle=1.13,length=mb::norm(axis);
  p.orientation=mb::Quat{axis[0]/length*std::sin(angle/2),axis[1]/length*std::sin(angle/2),axis[2]/length*std::sin(angle/2),std::cos(angle/2)};
  mb::History h;const auto out=mb::calibrate(p,b,profile,h,10000000);CHECK(out.orientation);
  auto opposite=p;for(auto& x:*opposite.orientation)x=-x;mb::History independent;
  const auto negative=mb::calibrate(opposite,b,profile,independent,20000000);CHECK(negative.orientation);
  for(const auto v:{mb::Vec{1,0,0},mb::Vec{0,1,0},mb::Vec{0,0,1},mb::Vec{2,-3,4}}){
   const auto expected=basis(action(axis,angle,v));
   close(mb::rotate(*out.orientation,basis(v)),expected);
   close(mb::rotate(*negative.orientation,basis(v)),expected);
  }
 }
 // Actual config-selected profile goes through the Bridge approval and MTP pipeline.
 c.bindings[{b.source,b.device}]=b;c.policy=mb::Policy::Both;
 mb::Bridge bridge(c,SB);p.position=mb::Vec{1,2,3};p.orientation=mb::Quat{0,0,0,1};
 CHECK(bridge.receive(wire(p),"source",10000000));
 auto out=bridge.logical().at({b.source,b.device}).pose;CHECK(out);close(*out->position,{3,2,-1});
 CHECK(out->coordinate_space.convention=="rh_y_up_neg_z_forward");
 auto unselected=c;unselected.bindings.at({b.source,b.device}).profile="vive-unverified";
 mb::Bridge blocked(unselected,SB);CHECK(blocked.receive(wire(p),"source",10000000));CHECK(!blocked.logical().at({b.source,b.device}).pose);
 // The 2026-09-21 profile is not auto-selected, but exact config selection
 // applies its independently observed signed quaternion components.
 auto v2=c;auto v2Binding=b;v2Binding.profile="vive-hil-v2";
 v2.bindings[{v2Binding.source,v2Binding.device}]=v2Binding;
 p.position=mb::Vec{1,2,3};p.orientation=mb::normalized(mb::Quat{.1,.2,.3,.9});
 mb::Bridge selectedV2(v2,SB);CHECK(selectedV2.receive(wire(p),"source",10000000));
 const auto v2Pose=selectedV2.logical().at({v2Binding.source,v2Binding.device}).pose;CHECK(v2Pose);
 close(*v2Pose->position,{1,2,3});
 const auto expectedV2=mb::normalized(mb::Quat{-.1,-.2,.3,.9});
 for(int component=0;component<4;++component)CHECK(near(v2Pose->orientation->at(component),expectedV2[component]));
 // Existing approved profile selection is unaffected by the candidate's presence.
 auto original=c;original.bindings.at({b.source,b.device}).profile="pico-compat-v1";
 p.coordinate_space.convention=original.profiles.at("pico-compat-v1").convention;p.orientation=mb::Quat{.5,.5,.5,.5};
 mb::Bridge unchanged(original,SB);CHECK(unchanged.receive(wire(p),"source",10000000));
 auto old=unchanged.logical().at({b.source,b.device}).pose;CHECK(old);close(*old->position,{1,2,3});CHECK(*old->orientation==mb::Quat({-.5,.5,-.5,.5}));
 std::cout<<"PASS coordinate profiles: historical v1, HIL-v2 signed components, +X/+Y/+Z, rotations, q/-q, explicit selection and isolation\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
