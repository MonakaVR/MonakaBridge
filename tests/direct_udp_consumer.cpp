#include "monaka_bridge/network.hpp"
#include "direct_pose.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
int main(int argc,char** argv)try{
 if(argc!=4)return 2;mb::Udp udp(static_cast<std::uint16_t>(std::stoul(argv[1])));mb::MtpFeed feed;
 std::ofstream out(argv[2]);const auto start=mb::monotonicNs(),duration=std::stoll(argv[3])*1000000;
 std::cout<<"ready\n"<<std::flush;
 while(mb::monotonicNs()-start<duration){
  auto packet=udp.receive();if(!packet){std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;}
  auto now=mb::monotonicNs();if(!feed.receive(packet->bytes,now))continue;
  for(const auto& [key,s]:feed.devices){if(!s.pose||!feed.fresh(s,now))continue;const auto& p=*s.pose;auto d=mb::steamvr::directPose(&s,now);
   nlohmann::json row={{"source",p.source_id},{"tracker",p.tracker_id},{"session",p.session_id},{"sequence",p.sequence},{"input_sequence",p.input.sequence},{"input_session",p.input.session_id},{"revision",p.mapping_revision},{"valid",d.poseIsValid},{"connected",d.deviceIsConnected},{"position",{d.vecPosition[0],d.vecPosition[1],d.vecPosition[2]}},{"orientation",{d.qRotation.x,d.qRotation.y,d.qRotation.z,d.qRotation.w}},{"velocity",{d.vecVelocity[0],d.vecVelocity[1],d.vecVelocity[2]}},{"time_offset",d.poseTimeOffset}};
   out<<row.dump()<<'\n';out.flush();
  }
 }
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
