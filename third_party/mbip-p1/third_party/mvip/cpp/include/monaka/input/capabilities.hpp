#pragma once
#include "vocabulary.hpp"
#include "monaka/protocol/v2/codec.hpp"
#include <stdexcept>
#include <tuple>

namespace monaka::input {
namespace wire = monaka::protocol::v2;
struct ContractError : std::invalid_argument {
    std::string code;
    explicit ContractError(std::string value):std::invalid_argument(value),code(std::move(value)){}
};
class CapabilitySet {
    unsigned bits_ = 0;
public:
    explicit CapabilitySet(const std::vector<std::string>& tokens = {});
    bool contains(Capability value) const {return (bits_ & (1u << static_cast<unsigned>(value))) != 0;}
    bool operator==(const CapabilitySet& other)const{return bits_==other.bits_;}
    bool operator!=(const CapabilitySet& other)const{return !(*this==other);}
    bool subsetOf(const CapabilitySet& other)const{return (bits_ & ~other.bits_)==0;}
    std::vector<std::string> tokens()const;
};
struct Channel {
    bool supported=false, sample_present=false, valid=false;
    std::string state()const{return !supported?"UNSUPPORTED":!sample_present?"ABSENT":valid?"VALID":"INVALID";}
};
struct Identity {
    std::string family,publisher,source,entity;
    bool operator==(const Identity& b)const{return std::tie(family,publisher,source,entity)==std::tie(b.family,b.publisher,b.source,b.entity);}
};
struct Projection {
    Identity identity;
    std::string session,clock;
    wire::CoordinateSpace space;
    CapabilitySet capabilities;
    std::array<Channel,7> channels{};
};
Projection ProjectCapabilities(const wire::Envelope& message);
wire::Envelope WithCapabilities(const wire::Envelope& message,const CapabilitySet& declaration);
// Pure immutable contract check. The caller owns session authorization and freshness.
void CheckBinding(const Projection& value,const Identity& expected,const std::string& session,const CapabilitySet& declaration);
}
