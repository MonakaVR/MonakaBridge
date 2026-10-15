#pragma once
#include "mapping.hpp"
#include <map>
namespace monaka::bridge_input {
namespace mvip=monaka::input;
enum class Mode { STRICT, LEGACY_DYNAMIC };
struct Binding {
    mvip::Identity identity;
    std::string session;
    mvip::CapabilitySet declaration;
    std::string provenance;
};
struct CoreRecord {
    mvip::wire::Envelope message;
    mvip::Projection source;
    std::map<std::string,mvip::Channel> channels;
};
class Normalizer {
    Binding binding_;
    Mode mode_;
public:
    explicit Normalizer(Binding binding,Mode mode=Mode::STRICT):binding_(std::move(binding)),mode_(mode){
        if(binding_.provenance.empty())throw mvip::ContractError("MissingDeclaration");
    }
    CoreRecord normalize(const mvip::wire::Envelope& message)const;
};
}
