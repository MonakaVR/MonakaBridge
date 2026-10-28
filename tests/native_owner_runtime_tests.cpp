#include "monaka_bridge/native_owner_runtime.hpp"
#include <iostream>
using mb::native_owner::Runtime;
void check(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
int main()try{
 std::atomic_bool fault{false};std::atomic_int transitions{0};
 Runtime runtime([&](const auto& stopping,const auto& ready){ready();while(!stopping()){if(fault.load())throw std::runtime_error("TEST_BOUNDARY_POST_READY_FAULT");std::this_thread::sleep_for(std::chrono::milliseconds(1));}},[&]{++transitions;});
 check(runtime.command(1).find("RUNNING")==0,"Initial ready failed");check(runtime.snapshot().find("RUNNING")==0,"Current ready missing");fault=true;
 auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);while((runtime.snapshot().find("TEST_BOUNDARY_POST_READY_FAULT")==std::string::npos||transitions<3)&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
 check(runtime.snapshot().find("UNCONFIRMED: native worker exited with fault:")==0,"Post-ready fault retained historical RUNNING state");check(transitions>=3,"Worker transition not published");
 fault=false;check(runtime.command(1).find("RUNNING")==0,"Explicit recovery failed");check(runtime.command(2).find("STOPPED")==0,"Stop failed");check(runtime.snapshot().find("STOPPED")==0,"Current stop missing");
 std::cout<<"PASS real product Runtime authority: ready -> injected boundary fault -> explicit recovery -> confirmed stop; transition publication\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
