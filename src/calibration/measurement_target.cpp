#include "monaka_bridge/measurement_target.hpp"
#include <cstring>
#include <limits>

namespace mb {
namespace {
void require(bool ok,const char* reason) { if(!ok) throw std::invalid_argument(reason); }
bool sameDouble(double a,double b) {
    static_assert(sizeof(double)==sizeof(std::uint64_t) && std::numeric_limits<double>::is_iec559,
        "measurement scope requires IEEE-754 binary64");
    if(!std::isfinite(a)||!std::isfinite(b)) return false;
    std::uint64_t left,right;
    std::memcpy(&left,&a,sizeof(a));std::memcpy(&right,&b,sizeof(b));
    return left==right;
}
template<std::size_t N> bool sameComponents(const std::array<double,N>& a,const std::array<double,N>& b) {
    for(std::size_t i=0;i<N;++i) if(!sameDouble(a[i],b[i])) return false;
    return true;
}
bool sameRigid(const Rigid& a,const Rigid& b) {
    return sameComponents(a.rotation,b.rotation)&&sameComponents(a.translation,b.translation);
}
bool sameBinding(const Binding& a,const Binding& b) {
    return a.source==b.source&&a.device==b.device&&a.tracker==b.tracker&&a.profile==b.profile&&
        a.inputSpace==b.inputSpace&&a.worldSpace==b.worldSpace&&a.inputRevision==b.inputRevision&&
        a.worldRevision==b.worldRevision&&a.spaceApproved==b.spaceApproved&&
        sameRigid(a.world,b.world)&&sameRigid(a.mount,b.mount);
}
bool sameProfile(const Profile& a,const Profile& b) {
    return a.convention==b.convention&&a.positionAxes==b.positionAxes&&a.quaternionAxes==b.quaternionAxes&&
        a.approved==b.approved&&a.angularSpaceVerified==b.angularSpaceVerified&&a.evidence==b.evidence;
}
bool sameContract(const CapabilityProfile& a,const CapabilityProfile& b) {
    return a.id==b.id&&a.provenance==b.provenance&&a.inputDeclaration==b.inputDeclaration&&
        a.outputDeclaration==b.outputDeclaration&&a.inputMode==b.inputMode&&a.outputMode==b.outputMode;
}
auto groupOf(const Binding& b) { return std::make_tuple(b.source,b.inputSpace,b.inputRevision); }
void compatibleGroup(const Binding& b,const Binding& selected) {
    require(b.profile==selected.profile&&b.worldSpace==selected.worldSpace&&b.worldRevision==selected.worldRevision,
        "measurement group has incompatible profile/world space");
}
} // namespace

FrozenMeasurementTarget::FrozenMeasurementTarget(WorldCalibrationTarget target,std::string publisher,
    Group group,std::map<Key,Member> members):target_(std::move(target)),publisher_(std::move(publisher)),
    group_(std::move(group)),members_(std::move(members)) {}

FrozenMeasurementTarget freezeMeasurementTarget(const Config& c,const std::string& source,
    const std::string& device,const std::string& tracker,const std::string& inputSpace,std::uint32_t inputRevision) {
    validateConfig(c);
    auto target=selectWorldCalibrationTarget(c,source,device,tracker,inputSpace,inputRevision);
    auto group=groupOf(target.binding);
    std::map<Key,FrozenMeasurementTarget::Member> members;
    for(const auto& [key,b]:c.bindings) if(groupOf(b)==group) {
        compatibleGroup(b,target.binding);
        const auto profile=c.profiles.find(b.profile);
        require(profile!=c.profiles.end(),"measurement member profile missing");
        std::optional<CapabilityProfile> contract;
        const auto found=c.contracts.find(key);
        if(found!=c.contracts.end()) contract=found->second;
        members.emplace(key,FrozenMeasurementTarget::Member{b,profile->second,std::move(contract)});
    }
    return FrozenMeasurementTarget(std::move(target),c.bridgeId,std::move(group),std::move(members));
}

void validateFrozenMeasurementTarget(const Config& current,const FrozenMeasurementTarget& frozen) {
    validateConfig(current);
    const auto& selected=frozen.target_.binding;
    selectWorldCalibrationTarget(current,selected.source,selected.device,selected.tracker,selected.inputSpace,selected.inputRevision);
    require(current.bridgeId==frozen.publisher_,"measurement publisher changed");
    require(current.revision==frozen.target_.mappingRevision,"measurement mapping_revision changed");
    // Compute membership from the frozen key, never a replacement current key.
    std::map<Key,const Binding*> members;
    for(const auto& [key,b]:current.bindings) if(groupOf(b)==frozen.group_) {
        compatibleGroup(b,selected);
        members.emplace(key,&b);
    }
    require(members.size()==frozen.members_.size(),"measurement group membership changed");
    auto now=members.begin();
    for(const auto& [key,member]:frozen.members_) {
        require(now!=members.end()&&now->first==key,"measurement group member key changed");
        require(sameBinding(*now->second,member.binding),"measurement member binding changed");
        const auto profile=current.profiles.find(member.binding.profile);
        require(profile!=current.profiles.end()&&sameProfile(profile->second,member.profile),"measurement member profile changed");
        const auto contract=current.contracts.find(key);
        require((contract!=current.contracts.end())==member.contract.has_value(),"measurement member contract presence changed");
        if(member.contract) require(sameContract(contract->second,*member.contract),"measurement member contract changed");
        ++now;
    }
}
} // namespace mb
