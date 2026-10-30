#include "monaka_bridge/config.hpp"
#include "monaka_bridge/config_transaction.hpp"
#include "monaka_bridge/mapping_selection.hpp"
#include <iostream>
int main(int argc,char** argv)try{
 if(argc<3)throw std::invalid_argument("usage: monaka_bridge_config CONFIG validate|policy VALUE|align TRACKER X Y Z|rename TRACKER NEW_ID|migrate ROUTE ALIGNMENT DEST");
 std::string command=argv[2];
 if(command=="transaction-version"&&argc==3){std::cout<<"monaka-config-transaction-v1\n";return 0;}
 if(command=="commit"&&argc==5){mb::commitConfigDocument(argv[1],mb::readConfigBytes(argv[3]),mb::readConfigBytes(argv[4]));std::cout<<"committed\n";return 0;}
 auto c=mb::loadConfig(argv[1]);
 if(command=="validate"){std::cout<<"valid\n";return 0;}
 if(command=="migrate"&&argc==6){mb::migrateLegacy(argv[3],argv[4],argv[5],c);return 0;}
 if(command=="policy"&&argc==4)c.policy=mb::parsePolicy(argv[3]);
 else if((command=="align"&&argc==7)||(command=="align-binding"&&argc==8)){
  const mb::Binding* selected=nullptr;int first=4;
  if(command=="align-binding"){
   auto found=c.bindings.find({argv[3],argv[4]});
   if(found==c.bindings.end())throw std::invalid_argument("source/device not mapped");
   selected=&found->second;first=5;
  }else selected=&mb::detail::uniqueTracker(c,argv[3]);
  mb::Vec t{std::stod(argv[first]),std::stod(argv[first+1]),std::stod(argv[first+2])};
  // Shared source-space alignment; never per-tracker position-zero calibration.
  bool changed=false;
  for(auto& [k,b]:c.bindings)if(b.source==selected->source&&b.inputSpace==selected->inputSpace&&b.inputRevision==selected->inputRevision){
   changed=changed||b.world.translation!=t;b.world.translation=t;
  }
  if(!changed){mb::commitConfigDocument(argv[1],c.sourceDocument,c.sourceDocument);std::cout<<"unchanged\n";return 0;}
 }else if(command=="rename"&&argc==5){const auto& selected=mb::detail::uniqueTracker(c,argv[3]);c.bindings.at({selected.source,selected.device}).tracker=argv[4];}
 else throw std::invalid_argument("unsupported configuration action");
 if(c.revision==UINT32_MAX)throw std::invalid_argument("revision exhausted");++c.revision;mb::saveConfig(argv[1],c);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
