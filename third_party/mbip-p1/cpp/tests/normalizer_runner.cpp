#include "monaka/bridge_input/normalizer.hpp"
#include "nlohmann/json.hpp"
#include <fstream>
#include <iostream>
using namespace monaka::bridge_input;
using json=nlohmann::json;
int main(int argc,char** argv){
    try{
        if(argc!=4)return 2;
        std::ifstream bindingFile(argv[1]);json b;bindingFile>>b;const auto& id=b.at("identity");
        Binding binding{{id.at("family"),id.at("publisher"),id.at("source"),id.at("entity")},b.at("session"),mvip::CapabilitySet(b.at("declaration").get<std::vector<std::string>>()),b.value("provenance","")};
        const std::string selected=argv[3];if(selected!="STRICT"&&selected!="LEGACY_DYNAMIC")throw mvip::ContractError("UnsupportedMode");
        Normalizer normalizer(binding,selected=="STRICT"?Mode::STRICT:Mode::LEGACY_DYNAMIC);
        std::ifstream f(argv[2],std::ios::binary);if(!f)return 2;std::string bytes((std::istreambuf_iterator<char>(f)),{});
        mvip::wire::Envelope env;mvip::wire::Error e;
        if(!mvip::wire::DecodeEnvelope(reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size(),env,e)){std::cout<<"ERROR:"<<mvip::wire::ErrorCodeName(e.code)<<'\n';return 0;}
        const auto result=normalizer.normalize(env);json channels=json::object();
        for(const auto& [name,c]:result.channels)channels[name]={{"supported",c.supported},{"sample_present",c.sample_present},{"valid",c.valid},{"state",c.state()}};
        std::string encoded;if(!mvip::wire::EncodeEnvelope(result.message,encoded,e))return 1;
        std::cout<<json{{"channels",channels},{"wire",json::parse(encoded)}}.dump()<<'\n';
    }catch(const mvip::ContractError& e){std::cout<<"ERROR:"<<e.code<<'\n';}
}
