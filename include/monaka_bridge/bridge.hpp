#pragma once
#include "config.hpp"
#include <deque>
#include <functional>
namespace mb {
enum class Channel { Monaka,Steamvr,Mirror };
class Fanout {
 struct Item{c1::Envelope value;std::int64_t queuedAt;};
 using QueueKey=std::string;
 struct Queue{std::map<QueueKey,Item> latest;std::deque<QueueKey> order;};
 std::map<Channel,Queue> queues_;
public:
 using Send=std::function<bool(Channel,std::string_view)>;
 std::uint64_t sent=0,errors=0,dropped=0;
 std::size_t capacity=512;
 void put(Channel,c1::Envelope,std::int64_t now);
 void clear(Channel);
 void flush(const Send&,std::int64_t now,std::size_t budget=32);
 std::size_t pending(Channel c)const;
};
struct Logical {
 History history;
 std::optional<c1::MtpPose> pose;
 std::string inputSession,stateSignature;
 std::int64_t inputSequence=-1,poseSequence=0,stateSequence=0;
 bool wasFresh=false;
};
class Bridge {
 Config config_;
 std::string session_;
 std::map<Key,Logical> logical_;
 std::map<Key,Key> trackerOwners_;
 void publish(const c1::Envelope&,std::int64_t now);
 bool approved(const Binding&,const Device&)const;
public:
 Registry registry;
 Fanout fanout;
 Bridge(Config,std::string session);
 bool receive(std::string_view,std::string_view peer,std::int64_t now);
 void tick(std::int64_t now);
 void reconfigure(Config,std::int64_t now);
 const Config& config()const{return config_;}
 const std::map<Key,Logical>& logical()const{return logical_;}
};
}
