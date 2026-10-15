#include "monaka/bridge_input/normalizer.hpp"
namespace monaka::bridge_input {
CoreRecord Normalizer::normalize(const mvip::wire::Envelope& message)const{
    const auto input=mvip::ProjectCapabilities(message);
    auto output=message;
    if(mode_==Mode::LEGACY_DYNAMIC){
        if(!input.capabilities.subsetOf(binding_.declaration))throw mvip::ContractError("UndeclaredCapability");
        output=mvip::WithCapabilities(message,binding_.declaration);
    }
    const auto facts=mvip::ProjectCapabilities(output);
    mvip::CheckBinding(facts,binding_.identity,binding_.session,binding_.declaration);
    CoreRecord record{std::move(output),facts,{}};
    for(const auto& row:CapabilityMapping){
        if(row.classification!=TranslationClass::PASS_THROUGH)throw mvip::ContractError("UnsupportedMapping");
        record.channels.emplace(row.target,facts.channels[static_cast<size_t>(row.source)]);
    }
    return record;
}
}
