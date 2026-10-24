#pragma once
#include "world_calibration.hpp"
#include <tuple>

namespace mb {

// Native configuration scope only: not an operator confirmation token, runtime
// session/clock witness, sequence floor, output approval, or file-CAS authority.
class FrozenMeasurementTarget {
public:
    FrozenMeasurementTarget(const FrozenMeasurementTarget&) = default;
    FrozenMeasurementTarget(FrozenMeasurementTarget&&) = default;
    const WorldCalibrationTarget& target() const { return target_; }
    const std::string& publisher() const { return publisher_; }
private:
    struct Member {
        Binding binding;
        Profile profile;
        std::optional<CapabilityProfile> contract;
    };
    using Group = std::tuple<std::string,std::string,std::uint32_t>;
    FrozenMeasurementTarget(WorldCalibrationTarget, std::string, Group, std::map<Key,Member>);
    const WorldCalibrationTarget target_;
    const std::string publisher_;
    const Group group_;
    const std::map<Key,Member> members_;
    friend FrozenMeasurementTarget freezeMeasurementTarget(const Config&,const std::string&,
        const std::string&,const std::string&,const std::string&,std::uint32_t);
    friend void validateFrozenMeasurementTarget(const Config&,const FrozenMeasurementTarget&);
};

// Explicit mapping selection; approved coordinate profile/evidence still needed.
// Does not set spaceApproved or infer a capability declaration for missing ones.
FrozenMeasurementTarget freezeMeasurementTarget(const Config&,const std::string& source,
    const std::string& device,const std::string& tracker,const std::string& inputSpace,
    std::uint32_t inputRevision);
void validateFrozenMeasurementTarget(const Config&,const FrozenMeasurementTarget&);

} // namespace mb
