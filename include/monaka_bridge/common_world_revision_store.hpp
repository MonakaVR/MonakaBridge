#pragma once
#include "common_world_authority.hpp"
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>

namespace monaka_bridge {
enum class RevisionOperation : std::uint8_t { Transition = 1, MappingUpdate = 2, MappingReestablishment = 3 };
// Value-only candidate. Construction is never a commit or an authorization.
struct CommonWorldRevisionCandidate {
 std::string authorizationEpoch;
 RevisionOperation operation = RevisionOperation::Transition;
 CommonWorldTransitionRequest transition;
 CommonWorldMappingReceipt mapping;
 CommonWorldMappingReestablishmentReceipt reestablishment;
 std::uint32_t worldRevision = 0, mappingRevision = 0;
 std::string sourcePublisher; // immutable exact schema-2 candidate identity
};
struct CommonWorldDurableReceipt { CommonWorldRevisionCandidate committed; };
bool sameCommonWorldRevisionCandidate(const CommonWorldRevisionCandidate&, const CommonWorldRevisionCandidate&);
struct CommonWorldStoredState {
 CommonWorldPersistentHighWater highWater;
 std::string currentAuthorizationEpoch;
 std::vector<std::string> retiredAndCurrentAuthorizationEpochs;
 std::optional<CommonWorldRevisionCandidate> latest;
};
enum class RevisionFaultBoundary { BeforeWrite, DuringWrite, DuringFlush, BeforePublish, AfterPublish };
using RevisionFaultHook = std::function<void(RevisionFaultBoundary)>;
/** Backend holds exclusive interprocess ownership for its entire lifetime.
 * readCommitted ignores uncommitted temporary artifacts. atomicWrite throws on uncertainty;
 * a throw after rename may leave a committed value but NEVER returns a receipt. */
class CommonWorldRevisionStorage {
public:
 virtual ~CommonWorldRevisionStorage() = default;
 virtual std::optional<std::string> readCommitted() = 0;
 virtual void atomicWrite(const std::string&, const std::atomic_bool* cancel) = 0;
};
// Local Windows fixed NTFS only; unsupported platforms/filesystems fail closed.
std::unique_ptr<CommonWorldRevisionStorage> makeCommonWorldFileStorage(
 const std::filesystem::path&, RevisionFaultHook = {});
/** Synchronous serialized durable store. No UI, source, publisher or transport. */
class CommonWorldRevisionStore {
public:
 CommonWorldRevisionStore(std::unique_ptr<CommonWorldRevisionStorage>, CommonWorldPersistentHighWater initial);
 CommonWorldStoredState open();
 // Observation only: busy persistence returns nullopt without waiting for its lock.
 std::optional<CommonWorldStoredState> tryObserve();
 CommonWorldStoredState acquireAuthorization(const std::string& freshEpoch);
 CommonWorldDurableReceipt commit(const CommonWorldRevisionCandidate&, const std::atomic_bool* cancel = nullptr);
 bool matchesCurrentCommit(const CommonWorldDurableReceipt&);
 // Short authorization gate under the SAME serialization point as handover/commit/dispose.
 // callback must not reenter the store. No file I/O takes place inside callback.
 bool acceptCurrentCommit(const CommonWorldDurableReceipt&, const std::function<void()>& callback);
 bool tryAuthorizationGate(const std::string& epoch, const std::function<void()>& callback);
 void dispose();
private:
 std::mutex mutex_;
 std::unique_ptr<CommonWorldRevisionStorage> storage_;
 CommonWorldPersistentHighWater initial_;
 bool disposed_ = false, observedRecord_ = false;
 CommonWorldStoredState read();
};
}
