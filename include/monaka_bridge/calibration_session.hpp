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
    virtual bool supported()const=0;
    virtual ProviderBatch discover(const std::atomic<bool>& cancel)=0;
    virtual void capture(const std::function<void(const ProviderBatch&)>& sink,const std::atomic<bool>& cancel)=0;
    virtual ProviderBatch guard()=0;
};
class UnsupportedProvider final:public CaptureProvider {
public:
    bool supported()const override{return false;}
    ProviderBatch discover(const std::atomic<bool>&)override;
    void capture(const std::function<void(const ProviderBatch&)>&,const std::atomic<bool>&)override;
    ProviderBatch guard()override;
};

// Explicit library injection for isolated tests, never populated by CLI flags.
struct SessionEnvironment {
    std::filesystem::path draftRoot;
    std::string helperPath,helperSha256;
    Clock clock=steadyNow;
    std::function<void(const char*)> transactionHook;
    std::function<void()> afterReplace;
    std::function<void(Json)> progress;
};

class DraftStore {
public:
    explicit DraftStore(std::filesystem::path root,
        std::function<std::uintmax_t(const std::filesystem::path&)> freeBytes={});
    void begin(const std::string& runId);
    void write(const std::string& name,const std::string& bytes);
    void adopt(const std::string& name);
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
};

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
    std::size_t solveCalls()const;
    std::size_t commitCalls()const;
private:
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
