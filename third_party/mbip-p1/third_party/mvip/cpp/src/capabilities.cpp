#include "monaka/input/capabilities.hpp"
#include <algorithm>
#include <type_traits>

namespace monaka::input {
CapabilitySet::CapabilitySet(const std::vector<std::string>& tokens){
    for(const auto& token:tokens){
        const auto it=std::find(CapabilityTokens.begin(),CapabilityTokens.end(),token);
        if(it==CapabilityTokens.end())throw ContractError("UnsupportedValue");
        const auto bit=1u<<static_cast<unsigned>(it-CapabilityTokens.begin());
        if(bits_&bit)throw ContractError("InconsistentValidity");
        bits_|=bit;
    }
}
std::vector<std::string> CapabilitySet::tokens()const{
    std::vector<std::string> result;
    for(size_t i=0;i<CapabilityTokens.size();++i)if(bits_&(1u<<i))result.emplace_back(CapabilityTokens[i]);
    return result;
}
Projection ProjectCapabilities(const wire::Envelope& message){
    // The existing full codec owns structural, finite, modality and identity checks.
    std::string encoded;wire::Error error;
    if(!wire::EncodeEnvelope(message,encoded,error))throw ContractError(wire::ErrorCodeName(error.code));
    return std::visit([](const auto& m)->Projection{
        using T=std::decay_t<decltype(m)>;
        constexpr bool pose=std::is_same_v<T,wire::TrackerObservation>||std::is_same_v<T,wire::MtpPose>;
        constexpr bool observation=std::is_same_v<T,wire::TrackerObservation>||std::is_same_v<T,wire::ObservationDeviceState>;
        constexpr bool tracking=std::is_same_v<T,wire::MtpPose>||std::is_same_v<T,wire::MtpTrackerState>;
        if constexpr(!observation&&!tracking)throw ContractError("UnsupportedMessage");
        else{
            Projection out;out.session=m.session_id;out.clock=m.clock_id;out.space=m.coordinate_space;
            out.capabilities=CapabilitySet(m.capabilities);out.identity.source=m.source_id;
            if constexpr(observation){out.identity.family="monaka.observation";out.identity.entity=m.device_id;}
            else{out.identity.family="monaka.tracking";out.identity.publisher=m.publisher_id;out.identity.entity=m.tracker_id;}
            const auto set=[&](Capability c,bool present,bool valid){
                auto& ch=out.channels[static_cast<size_t>(c)];ch={out.capabilities.contains(c),present,out.capabilities.contains(c)&&valid};
            };
            for(size_t i=0;i<out.channels.size();++i)set(static_cast<Capability>(i),false,false);
            if constexpr(pose){
                set(Capability::Position,m.position.has_value(),m.validity.position);
                set(Capability::Orientation,m.orientation.has_value(),m.validity.orientation);
                set(Capability::LinearVelocity,m.linear_velocity.has_value(),m.linear_velocity.has_value()&&m.validity.position);
                set(Capability::AngularVelocity,m.angular_velocity.has_value(),m.angular_velocity.has_value()&&m.validity.orientation);
                set(Capability::LinearAcceleration,m.linear_acceleration.has_value(),m.linear_acceleration.has_value()&&m.validity.position);
            }
            if constexpr(!std::is_same_v<T,wire::MtpPose>){
                set(Capability::BatteryFraction,m.battery&&m.battery->fraction.has_value(),m.battery&&m.battery->fraction.has_value());
                set(Capability::Charging,m.battery&&m.battery->charging.has_value(),m.battery&&m.battery->charging.has_value());
            }
            return out;
        }
    },message);
}
wire::Envelope WithCapabilities(const wire::Envelope& message,const CapabilitySet& declaration){
    return std::visit([&](const auto& m)->wire::Envelope{
        using T=std::decay_t<decltype(m)>;
        if constexpr(std::is_same_v<T,wire::TrackerObservation>||std::is_same_v<T,wire::ObservationDeviceState>||std::is_same_v<T,wire::MtpPose>||std::is_same_v<T,wire::MtpTrackerState>){auto copy=m;copy.capabilities=declaration.tokens();return copy;}
        else throw ContractError("UnsupportedMessage");
    },message);
}
void CheckBinding(const Projection& value,const Identity& expected,const std::string& session,const CapabilitySet& declaration){
    if(!(value.identity==expected))throw ContractError("IdentityMismatch");
    if(value.session!=session)throw ContractError("SessionMismatch");
    if(value.capabilities!=declaration)throw ContractError("CapabilitiesChanged");
}
}
