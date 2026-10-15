#include "monaka_bridge/config_transaction.hpp"
#include "monaka_bridge/config.hpp"
#include <fstream>
#include <chrono>
#include <atomic>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#endif
namespace mb {
std::string readConfigBytes(const std::filesystem::path& p){
 std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("cannot read configuration");
 std::string s;char buf[4096];while(f){f.read(buf,sizeof(buf));s.append(buf,std::size_t(f.gcount()));if(s.size()>2*1024*1024)throw std::runtime_error("configuration size limit");}if(!f.eof())throw std::runtime_error("configuration read failed");return s;
}
namespace {
struct WriterLock {
#ifdef _WIN32
 HANDLE h=INVALID_HANDLE_VALUE;
 explicit WriterLock(const std::filesystem::path& p){h=CreateFileW(p.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("configuration writer busy or lock unavailable");}
 ~WriterLock(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
#else
 int h=-1;
 explicit WriterLock(const std::filesystem::path& p){h=::open(p.c_str(),O_CREAT|O_RDWR,0600);if(h<0)throw std::runtime_error("configuration lock unavailable");if(flock(h,LOCK_EX|LOCK_NB)!=0){::close(h);h=-1;throw std::runtime_error("configuration writer busy");}}
 ~WriterLock(){if(h>=0)::close(h);}
#endif
 WriterLock(const WriterLock&)=delete;WriterLock& operator=(const WriterLock&)=delete;
};
std::string unique(){static std::atomic<unsigned long> serial{0};return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++serial);}
}
void commitConfigDocument(const std::filesystem::path& path,const std::optional<std::string>& expected,const std::string& candidate,const std::function<void(const char*)>& hook){
 if(candidate.size()>2*1024*1024)throw std::runtime_error("configuration size limit");
 const auto proposed=parseConfigDocument(candidate);
 auto parent=std::filesystem::canonical(path.parent_path().empty()?std::filesystem::path("."):path.parent_path());
 auto target=parent/path.filename();
 if(std::filesystem::is_symlink(target))throw std::runtime_error("configuration symlink targets are not supported");
 if(std::filesystem::exists(target)&&std::filesystem::hard_link_count(target)!=1)throw std::runtime_error("configuration hardlink aliases are not supported");
 auto lockPath=target;lockPath+=".config-write.lock";WriterLock lock(lockPath);
 const auto checkpoint=[&](const char* stage){if(hook)hook(stage);};checkpoint("locked");
 auto check=[&]{
  if(expected){if(!std::filesystem::exists(target)||readConfigBytes(target)!=*expected)throw std::runtime_error("Configuration changed in another tool; reload before applying.");}
  else if(std::filesystem::exists(target))throw std::runtime_error("Configuration exists; an expected snapshot is required.");
 };
 check();
 if(expected){const auto previous=parseConfigDocument(*expected);if(previous.revision==UINT32_MAX||proposed.revision!=previous.revision+1)throw std::runtime_error("mapping revision must increment exactly once");}
 checkpoint("validated");
 auto pending=target;pending+=".pending-txn-"+unique();
 struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code ec;std::filesystem::remove(p,ec);}} cleanup{pending};
 {std::ofstream f(pending,std::ios::binary|std::ios::trunc);f.write(candidate.data(),std::streamsize(candidate.size()));f.flush();if(!f)throw std::runtime_error("configuration write failed");}
 checkpoint("staged");check();
 if(expected){auto backup=target;backup+=".backup-txn-"+unique();std::filesystem::copy_file(target,backup,std::filesystem::copy_options::none);}
 checkpoint("backed-up");check();checkpoint("before-replace");
#ifdef _WIN32
 if(!MoveFileExW(pending.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("configuration replacement failed; reload to verify outcome");
#else
 std::filesystem::rename(pending,target);
#endif
 // No cancellation/throwing callbacks after this linearization point.
}
}
