#include "monaka_bridge/config.hpp"
#include <iostream>
int main(int argc,char** argv)try{
 if(argc<3)throw std::invalid_argument("usage: monaka_bridge_config CONFIG validate|policy VALUE|align TRACKER X Y Z|rename TRACKER NEW_ID|migrate ROUTE ALIGNMENT DEST");
 auto c=mb::loadConfig(argv[1]);std::string command=argv[2];
 if(command=="validate"){std::cout<<"valid\n";return 0;}
 if(command=="migrate"&&argc==6){mb::migrateLegacy(argv[3],argv[4],argv[5],c);return 0;}
 if(command=="policy"&&argc==4)c.policy=mb::parsePolicy(argv[3]);
 else if(command=="align"&&argc==7){
  mb::Binding* selected=nullptr;for(auto& [k,b]:c.bindings)if(b.tracker==argv[3])selected=&b;if(!selected)throw std::invalid_argument("tracker not mapped");
  mb::Vec t{std::stod(argv[4]),std::stod(argv[5]),std::stod(argv[6])};
  // Shared source-space alignment; never per-tracker position-zero calibration.
  for(auto& [k,b]:c.bindings)if(b.source==selected->source&&b.inputSpace==selected->inputSpace&&b.inputRevision==selected->inputRevision)b.world.translation=t;
 }else if(command=="rename"&&argc==5){bool found=false;for(auto& [k,b]:c.bindings)if(b.tracker==argv[3]){b.tracker=argv[4];found=true;}if(!found)throw std::invalid_argument("tracker not mapped");}
 else throw std::invalid_argument("unsupported configuration action");
 if(c.revision==UINT32_MAX)throw std::invalid_argument("revision exhausted");++c.revision;mb::saveConfig(argv[1],c);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
