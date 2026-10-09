#pragma once
#include "monaka_bridge/registry.hpp"
#include <nlohmann/json.hpp>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
namespace mb {
// All file I/O belongs to the worker. Admission only attempts a bounded queue lock.
class DiagnosticWriter {
public:
 using Json=nlohmann::json; using Sink=std::function<void(const Json&)>;
 explicit DiagnosticWriter(Sink, std::size_t capacity=2048);
 ~DiagnosticWriter();
 void emit(std::string type, std::string source="component", Json identity=nullptr, Json session=nullptr, Json validity=nullptr, Json details=Json::object(), bool critical=true, Json identities=nullptr) noexcept;
 void close();
 std::uint64_t dropped() const {return dropped_;}
 std::uint64_t criticalDropped() const {return criticalDropped_;}
 static Json build();
private:
 struct Item{Json event;bool critical;};
 Sink sink_;std::size_t capacity_;std::deque<Item> queue_;std::mutex mutex_;std::condition_variable wake_;std::thread worker_;
 // Service emits from its one ingress thread. Reserved SPSC transition ring never takes a lock.
 std::vector<Json> criticalQueue_;std::atomic<std::uint64_t> criticalHead_{0},criticalTail_{0};
 std::atomic<bool> stopping_{false};std::atomic<std::uint64_t> dropped_{0},criticalDropped_{0},writeFailures_{0};
 void run();
};
class DiagnosticObserver {
public:
 explicit DiagnosticObserver(DiagnosticWriter& writer):writer_(writer){}
 void admitted(const c1::Envelope&,std::string_view peer,std::int64_t now) noexcept;
 void rejected(const c1::Envelope&,std::string reason) noexcept;
 void tick(std::int64_t now,std::int64_t timeout) noexcept;
 void mapped(const std::string& source,const std::string& device,const std::string& tracker) noexcept;
private:
 struct Tracker{bool registered=false,firstPose=false,firstPosition=false;std::optional<bool> p,q;};
 struct Lifetime{std::string session;std::int64_t generation=0,last=-1;bool connected=false;std::map<std::string,Tracker> trackers;};
 std::map<std::string,Lifetime> sources_;DiagnosticWriter& writer_;
 static nlohmann::json identity(const std::string&,const std::string&);
 static nlohmann::json session(const Lifetime&);
 void disconnect(const std::string&,Lifetime&,const std::string&);
};
}
