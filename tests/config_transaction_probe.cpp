#include "monaka_bridge/config_transaction.hpp"
#include "monaka_bridge/config.hpp"
#include <fstream>
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
int main(int argc,char**argv)try{
 if(argc!=6)throw std::runtime_error("probe TARGET EXPECTED CANDIDATE STAGE hold|fail|commit");
 if(std::string(argv[5])=="typed"||std::string(argv[5])=="typed-threads"){
  auto c=mb::parseConfigDocument(mb::readConfigBytes(argv[2]));c.sourcePath=std::filesystem::weakly_canonical(argv[1]);c.policy=mb::Policy::Both;++c.revision;
  if(std::string(argv[5])=="typed-threads"){std::atomic<unsigned> wins{0};auto worker=[&]{try{mb::saveConfig(argv[1],c);++wins;}catch(const std::exception&){};};std::thread a(worker),b(worker);a.join();b.join();if(wins!=1)throw std::runtime_error("same snapshot threads did not have exactly one winner");return 0;}
  mb::saveConfig(argv[1],c);return 0;
 }
 mb::commitConfigDocument(argv[1],mb::readConfigBytes(argv[2]),mb::readConfigBytes(argv[3]),[&](const char* stage){
  if(std::string(stage)!=argv[4])return;
  {std::ofstream marker(std::string(argv[1])+".probe");marker<<stage;}
  if(std::string(argv[5])=="fail")throw std::runtime_error("injected transaction failure");
  if(std::string(argv[5])=="hold")for(;;)std::this_thread::sleep_for(std::chrono::milliseconds(50));
 });
 if(std::string(argv[4])=="after-commit"){{std::ofstream marker(std::string(argv[1])+".probe");marker<<"committed";}if(std::string(argv[5])=="hold")for(;;)std::this_thread::sleep_for(std::chrono::milliseconds(50));}
 std::cout<<"committed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
