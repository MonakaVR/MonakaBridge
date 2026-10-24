#pragma once
#include "measurement_target.hpp"
#include "trajectory_calibration.hpp"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

namespace mb::local_calibration {
using Json=nlohmann::json;
using Clock=std::function<std::int64_t()>;
constexpr std::size_t KiB=1024,MiB=1024*KiB;
constexpr std::int64_t second=1000000000LL;
std::int64_t steadyNow();
std::string sha256(const std::string&);
std::string opaqueId();
std::string diagnostic(const std::exception&);
void requirePlainPath(const std::filesystem::path&);
Json parseRequest(const std::string&);

// Internal owning configuration, not a source/session witness or wire model.
// Identity/profile/revision are accessed through the existing Frozen type.
class ProviderOpenOptions {
public:
    ProviderOpenOptions(const ProviderOpenOptions&)=default;
    const FrozenMeasurementTarget& measurementTarget()const{return target_;}
    const std::optional<CapabilityProfile>& capabilityProfile()const{return contract_;}
    const std::filesystem::path& configPath()const{return deployment_.configPath;}
    const std::filesystem::path& installationPath()const{return deployment_.installationPath;}
    const std::string& configSha256()const{return deployment_.configSha256;}
    const std::string& mirrorAddress()const{return deployment_.mirrorAddress;}
    std::uint16_t mirrorPort()const{return deployment_.mirrorPort;}
    const std::string& referenceRole()const{return deployment_.referenceRole;}
    bool readOnly()const{return deployment_.readOnly;}
    bool staticMode()const{return deployment_.staticMode;}
    const char* association()const{return "operator_declared / process_owner_unverified";}
private:
    struct Deployment {
        std::filesystem::path configPath,installationPath;
        std::string configSha256,mirrorAddress,referenceRole;
        std::uint16_t mirrorPort;
        bool readOnly;
        bool staticMode=false;
    };
    const FrozenMeasurementTarget target_;
    const std::optional<CapabilityProfile> contract_;
    const Deployment deployment_;
    ProviderOpenOptions(FrozenMeasurementTarget target,std::optional<CapabilityProfile> contract,Deployment deployment)
        :target_(std::move(target)),contract_(std::move(contract)),deployment_(std::move(deployment)){}
    friend class CalibrationSession;
};

struct InputWitness {
    std::string source,device,session,clock,space,convention,peer;
    std::uint32_t revision=0;
    std::int64_t initialPoseFloor=-1;
    std::optional<std::int64_t> initialStateFloor;
    std::string referenceSerial,referenceRole,standing;
    std::uint64_t referenceGeneration=0;
};
struct Frame { c1::Envelope envelope;std::string peer;std::int64_t receiptNs=0; };
struct ProviderBatch {
    InputWitness identity;
    std::vector<Frame> frames;
    std::vector<TimedCalibrationPose> references;
    std::int64_t referenceReceiptNs=0;
    bool referenceValid=false,absent=false,eventOverflow=false;
};
// Implementations own all capture/reference resources. B3 must feed every
// selected guard envelope, including review/solve backlog. B2 ships no capture.
class CaptureProvider {
public:
    virtual ~CaptureProvider()=default;
    void configure(std::shared_ptr<const ProviderOpenOptions>);
    std::shared_ptr<const ProviderOpenOptions> configuration()const{return configuration_;}
    virtual bool supported()const=0;
    virtual ProviderBatch discover(const std::atomic<bool>& cancel)=0;
    virtual void capture(const std::function<void(const ProviderBatch&)>& sink,const std::atomic<bool>& cancel)=0;
    virtual ProviderBatch guard()=0;
    // Called exactly once by the input owner after discovery starts, including
    // failure/cancellation. N1 releases its SDK/transport on that same thread.
    virtual void shutdown(){}
protected:
    // Validation only: no retained borrowed state, capture resources or IO.
    virtual void validateConfiguration(const ProviderOpenOptions&)const{}
private:
    bool configurationAttempted_=false;
    std::shared_ptr<const ProviderOpenOptions> configuration_;
};
class UnsupportedProvider final:public CaptureProvider {
public:
    bool supported()const override{return false;}
    ProviderBatch discover(const std::atomic<bool>&)override;
    void capture(const std::function<void(const ProviderBatch&)>&,const std::atomic<bool>&)override;
    ProviderBatch guard()override;
};

// Explicit library injection for isolated tests, never populated by CLI flags.
class OutputApprovalAuthority;
struct SessionEnvironment {
    std::filesystem::path draftRoot;
    std::string helperPath,helperSha256;
    Clock clock=steadyNow;
    std::function<void(const char*)> transactionHook;
    std::function<void()> afterReplace;
    std::function<void(Json)> progress;
    // Native-only deterministic fault barriers; never populated by JSON/CLI.
    std::function<void(const char*)> ioHook;
    std::function<void()> draftPublishHook;
    // Production file authority or isolated native test implementation.
    std::shared_ptr<const OutputApprovalAuthority> outputApprovalAuthority;
};

class DraftStore {
public:
    explicit DraftStore(std::filesystem::path root,
        std::function<std::uintmax_t(const std::filesystem::path&)> freeBytes={});
    void begin(const std::string& runId);
    void write(const std::string& name,const std::string& bytes);
    void publishCandidate(const Config&,const std::function<void()>& afterWrite={});
    std::filesystem::path path(const std::string& name)const;
    void terminal(const std::string& status);
    void verify()const;
    void journal(const Json&);
    Json inventory()const;
    Json prepareCleanup(const std::vector<std::string>& runIds);
    void cleanup(const std::string& reviewToken);
private:
    std::filesystem::path root_,run_;
    std::map<std::string,std::string> hashes_;
    std::string manifestBytes_;
    std::string cleanupToken_;
    std::map<std::filesystem::path,std::string> cleanupHashes_;
    std::vector<std::filesystem::path> cleanupDirs_;
    std::function<std::uintmax_t(const std::filesystem::path&)> freeBytes_;
    void manifest(const std::string& status);
    void adopt(const std::string& name); // Caller owns root quota lock.
};

struct CaptureSnapshot;
class CalibrationSession {
public:
    CalibrationSession(CaptureProvider&,SessionEnvironment);
    ~CalibrationSession();
    CalibrationSession(const CalibrationSession&)=delete;
    Json execute(const Json&);
    void tick();
    void requestCancel(bool eof=false);
    void latchControl(const Json&);
    std::string phase()const;
    bool terminal()const;
    bool lifetimeExpired()const;
    std::string sessionId()const;
    std::string runId()const;
    const InputWitness* frozenInput()const;
    std::pair<std::int64_t,std::optional<std::int64_t>> highwaters()const;
    CaptureSnapshot captureSnapshot()const;
    Json invalidationDiagnostic()const;
    std::size_t solveCalls()const;
    std::size_t commitCalls()const;
private:
    static std::shared_ptr<const ProviderOpenOptions> makeProviderOptions(const Config&,
        FrozenMeasurementTarget,ProviderOpenOptions::Deployment);
    struct Impl;std::unique_ptr<Impl> impl_;
};

// Bounded producer-facing outbox. Writer acknowledges only after a successful
// write, so an in-flight control still occupies a slot. No blocking producer IO.
class EventOutbox {
public:
    bool push(Json,bool progress=false,bool terminal=false,std::int64_t now=0);
    bool reserveControl();
    bool publishReserved(Json,bool terminal=false);
    std::optional<Json> next()const;
    void acknowledge(std::uint64_t);
    void broken();
    bool failed()const;
    std::size_t size()const;
private:
    mutable std::mutex mutex_;
    std::deque<Json> controls_;
    std::optional<Json> progress_,terminal_;
    std::uint64_t sequence_=0,last_=0;
    std::int64_t progressAt_=-1;
    bool broken_=false;
    std::size_t reserved_=0;
};
bool acceptEventChain(const Json&,std::uint64_t& last);
int runLocalApi(CaptureProvider&,SessionEnvironment);
int calibrationLocalDispatch(int argc,char** argv);
} // namespace mb::local_calibration
