#include "monaka/input/capabilities.hpp"
#include "nlohmann/json.hpp"
#include <fstream>
#include <iostream>
using namespace monaka::input;
using json=nlohmann::json;
int main(int argc,char** argv){
    try{
        if(argc==2&&std::string(argv[1])=="--self-test"){
            if(CapabilitySet({"orientation","position"})!=CapabilitySet({"position","orientation"}))return 1;
            if(!CapabilitySet().tokens().empty())return 1;
            for(const auto& tokens:std::vector<std::vector<std::string>>{{"unknown"},{"position","position"}}){bool rejected=false;try{CapabilitySet c(tokens);}catch(const ContractError&){rejected=true;}if(!rejected)return 1;}
            std::cout<<"PASS vocabulary/set/unknown/duplicate\n";return 0;
        }
        if(argc!=2)return 2;
        std::ifstream f(argv[1],std::ios::binary);if(!f)return 2;
        std::string bytes((std::istreambuf_iterator<char>(f)),{});wire::Envelope env;wire::Error e;
        if(!wire::DecodeEnvelope(reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size(),env,e)){std::cout<<"ERROR:"<<wire::ErrorCodeName(e.code)<<'\n';return 0;}
        const auto p=ProjectCapabilities(env);json channels=json::object();
        for(size_t i=0;i<p.channels.size();++i){const auto& c=p.channels[i];channels[std::string(CapabilityTokens[i])]={{"supported",c.supported},{"sample_present",c.sample_present},{"valid",c.valid},{"state",c.state()}};}
        std::string encoded;if(!wire::EncodeEnvelope(env,encoded,e))return 1;
        std::cout<<json{{"channels",channels},{"wire",json::parse(encoded)}}.dump()<<'\n';
    }catch(const ContractError& e){std::cout<<"ERROR:"<<e.code<<'\n';}
}
