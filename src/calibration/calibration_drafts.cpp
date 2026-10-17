#include "monaka_bridge/calibration_session.hpp"
#include "monaka_bridge/config_transaction.hpp"
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <fstream>
#include <set>
#include <algorithm>

namespace mb::local_calibration {
namespace fs=std::filesystem;
namespace {
void need(bool b,const char* s){if(!b)throw std::runtime_error(s);}
struct QuotaLock {
 HANDLE h=INVALID_HANDLE_VALUE;
 explicit QuotaLock(const fs::path& root){auto p=root/"quota.lock";requirePlainPath(p);h=CreateFileW(p.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);need(h!=INVALID_HANDLE_VALUE,"DRAFT_QUOTA_BUSY");}
 ~QuotaLock(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
};
const std::set<std::string> names={"expected.json","result.json","candidate.json","candidate.json.config-write.lock","terminal.json","journal.ndjson","raw.json"};
std::size_t limit(const std::string& name){if(name=="journal.ndjson")return 4*MiB;if(name=="raw.json")return 48*MiB;return 2*MiB;}
std::string readDraft(const fs::path& p,std::size_t maximum){std::ifstream f(p,std::ios::binary);need(bool(f),"DRAFT_READ_ERROR");std::string out;char b[4096];while(f){f.read(b,sizeof(b));out.append(b,std::size_t(f.gcount()));need(out.size()<=maximum,"DRAFT_FILE_QUOTA_EXCEEDED");}need(f.eof(),"DRAFT_READ_ERROR");return out;}
void atomic(const fs::path& p,const std::string& bytes){
 requirePlainPath(p);auto temp=p;temp+=".writing";requirePlainPath(temp);
 need(!fs::exists(temp),"DRAFT_PARTIAL_INSPECT_ONLY");
 {std::ofstream f(temp,std::ios::binary|std::ios::trunc);f.write(bytes.data(),std::streamsize(bytes.size()));f.flush();need(bool(f),"DRAFT_IO_ERROR");}
 need(MoveFileExW(temp.c_str(),p.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,"DRAFT_PUBLISH_ERROR");
}
Json scan(const fs::path& root){
 Json rows=Json::array();std::uintmax_t total=0;std::size_t dirs=0;
 if(!fs::exists(root))return {{"runs",rows},{"charged_bytes",0}};
 requirePlainPath(root);
 for(const auto& entry:fs::directory_iterator(root)){
  requirePlainPath(entry.path());if(entry.path().filename()=="quota.lock")continue;
  need(entry.is_directory()&&++dirs<=16,"DRAFT_ROOT_QUOTA_OR_UNKNOWN_ENTRY");
  std::uintmax_t bytes=0;std::size_t count=0;
  for(const auto& item:fs::directory_iterator(entry.path())){requirePlainPath(item.path());need(item.is_regular_file()&&++count<=32,"DRAFT_UNKNOWN_OR_FILE_QUOTA");bytes+=item.file_size();need(bytes<=64*MiB,"DRAFT_QUOTA_EXCEEDED");}
  auto p=entry.path()/"manifest.json";need(fs::exists(p),"DRAFT_PARTIAL_INSPECT_ONLY");
  auto manifest=Json::parse(readConfigBytes(p));
  need(manifest.at("version")==1&&manifest.at("files").is_object(),"DRAFT_MANIFEST_INVALID");
  std::size_t knownCount=0;
  for(const auto& item:fs::directory_iterator(entry.path()))if(item.path().filename()!="manifest.json"){
   auto n=item.path().filename().u8string();need(names.count(n)&&manifest.at("files").contains(n),"DRAFT_UNKNOWN_INSPECT_ONLY");++knownCount;
   need(sha256(readDraft(item.path(),limit(n)))==manifest.at("files").at(n).at("sha256"),"DRAFT_HASH_INSPECT_ONLY");
  }
  need(knownCount==manifest.at("files").size(),"DRAFT_PARTIAL_INSPECT_ONLY");
  auto status=manifest.at("status").get<std::string>();auto created=manifest.at("created_utc_seconds").get<std::int64_t>();
  const auto now=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  bool expired=now>=created&&now-created>=30*24*60*60;
  auto charged=status=="ACTIVE"?std::max<std::uintmax_t>(bytes,64*MiB):bytes;total+=charged;
  need(total<=512*MiB,"DRAFT_ROOT_QUOTA_EXCEEDED");
  rows.push_back({{"run_id",entry.path().filename().u8string()},{"bytes",bytes},{"files",count},{"status",status},{"expired_inspect_only",expired},{"charged_bytes",charged}});
 }
 return {{"runs",rows},{"charged_bytes",total}};
}
}

std::int64_t steadyNow(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
std::string sha256(const std::string& bytes){
 BCRYPT_ALG_HANDLE alg=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
 need(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"HASH_UNAVAILABLE");
 struct Cleanup{BCRYPT_ALG_HANDLE& a;BCRYPT_HASH_HANDLE& h;~Cleanup(){if(h)BCryptDestroyHash(h);if(a)BCryptCloseAlgorithmProvider(a,0);}} cleanup{alg,hash};
 need(BCryptCreateHash(alg,&hash,nullptr,0,nullptr,0,0)>=0,"HASH_UNAVAILABLE");
 need(bytes.size()<=64*MiB&&BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),ULONG(bytes.size()),0)>=0,"HASH_FAILED");
 unsigned char out[32];need(BCryptFinishHash(hash,out,32,0)>=0,"HASH_FAILED");
 const char* hex="0123456789abcdef";std::string result;for(auto b:out){result+=hex[b>>4];result+=hex[b&15];}return result;
}
std::string opaqueId(){unsigned char bytes[32];need(BCryptGenRandom(nullptr,bytes,sizeof(bytes),BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0,"TOKEN_UNAVAILABLE");std::string s;const char* h="0123456789abcdef";for(auto b:bytes){s+=h[b>>4];s+=h[b&15];}return s;}
void requirePlainPath(const fs::path& p){
 need(p.is_absolute()&&p.lexically_normal()==p,"PATH_NOT_ABSOLUTE_CANONICAL");
 fs::path current;
 for(const auto& part:p){current/=part;auto a=GetFileAttributesW(current.c_str());if(a==INVALID_FILE_ATTRIBUTES){need(GetLastError()==ERROR_FILE_NOT_FOUND||GetLastError()==ERROR_PATH_NOT_FOUND,"PATH_UNAVAILABLE");continue;}need(!(a&FILE_ATTRIBUTE_REPARSE_POINT),"PATH_ALIAS_REJECTED");}
 if(fs::exists(p)&&fs::is_regular_file(p))need(fs::hard_link_count(p)==1,"PATH_HARDLINK_REJECTED");
}
DraftStore::DraftStore(fs::path root,std::function<std::uintmax_t(const fs::path&)> freeBytes):root_(std::move(root)),freeBytes_(std::move(freeBytes)){requirePlainPath(root_);}
fs::path DraftStore::path(const std::string& name)const{need(!run_.empty()&&names.count(name),"DRAFT_NAME_REJECTED");auto p=run_/name;requirePlainPath(p);return p;}
void DraftStore::begin(const std::string& id){
 need(run_.empty()&&id.size()==64&&id.find_first_not_of("0123456789abcdef")==std::string::npos,"DRAFT_RUN_REJECTED");
 requirePlainPath(root_);fs::create_directories(root_);QuotaLock lock(root_);auto status=scan(root_);
 need(status["runs"].size()<16&&status["charged_bytes"].get<std::uintmax_t>()+64*MiB<=512*MiB,"DRAFT_QUOTA_EXCEEDED");
 need((freeBytes_?freeBytes_(root_):fs::space(root_).available)>=256*MiB,"DRAFT_FREE_SPACE_INSUFFICIENT");
 run_=root_/id;requirePlainPath(run_);need(fs::create_directory(run_),"DRAFT_RUN_EXISTS");manifest("ACTIVE");
}
void DraftStore::manifest(const std::string& status){
 Json files=Json::object();std::uintmax_t total=0;
 for(auto& [name,hash]:hashes_){auto p=path(name);auto size=fs::file_size(p);total+=size;files[name]={{"sha256",hash},{"bytes",size}};}
 need(hashes_.size()+2<=32&&total<=64*MiB-2*MiB,"DRAFT_QUOTA_EXCEEDED");
 auto p=run_/"manifest.json";std::int64_t created=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
 if(fs::exists(p))created=Json::parse(readConfigBytes(p)).at("created_utc_seconds").get<std::int64_t>();
 manifestBytes_=Json{{"version",1},{"status",status},{"created_utc_seconds",created},{"files",files}}.dump();atomic(p,manifestBytes_);
}
void DraftStore::write(const std::string& name,const std::string& bytes){need(bytes.size()<=limit(name),"DRAFT_FILE_QUOTA_EXCEEDED");auto p=path(name);QuotaLock lock(root_);if(!hashes_.empty())verify();std::uintmax_t total=bytes.size();for(const auto& [other,h]:hashes_)if(other!=name)total+=fs::file_size(path(other));need(total<=62*MiB&&hashes_.size()+(hashes_.count(name)?0:1)+2<=32,"DRAFT_QUOTA_EXCEEDED");atomic(p,bytes);hashes_[name]=sha256(bytes);manifest("ACTIVE");}
void DraftStore::adopt(const std::string& name){auto p=path(name);need(fs::file_size(p)<=limit(name),"DRAFT_FILE_QUOTA_EXCEEDED");hashes_[name]=sha256(readDraft(p,limit(name)));manifest("ACTIVE");}
void DraftStore::terminal(const std::string& status){if(run_.empty())return;QuotaLock lock(root_);manifest(status);}
void DraftStore::verify()const{need(!run_.empty(),"DRAFT_NOT_OWNED");requirePlainPath(run_);need(readConfigBytes(run_/"manifest.json")==manifestBytes_,"DRAFT_MANIFEST_CHANGED");std::size_t count=0;for(const auto& item:fs::directory_iterator(run_)){requirePlainPath(item.path());auto name=item.path().filename().u8string();if(name=="manifest.json")continue;need(item.is_regular_file()&&hashes_.count(name),"DRAFT_UNKNOWN_FILE");need(sha256(readDraft(item.path(),limit(name)))==hashes_.at(name),"DRAFT_ARTIFACT_CHANGED");++count;}need(count==hashes_.size(),"DRAFT_PARTIAL_INSPECT_ONLY");}
void DraftStore::journal(const Json& event){auto p=path("journal.ndjson");std::string old;if(fs::exists(p))old=readDraft(p,4*MiB);auto line=event.dump()+"\n";need(old.size()+line.size()<=4*MiB,"JOURNAL_QUOTA_EXCEEDED");write("journal.ndjson",old+line);}
Json DraftStore::inventory()const{return scan(root_);}
Json DraftStore::prepareCleanup(const std::vector<std::string>& ids){
 cleanupToken_.clear();cleanupHashes_.clear();cleanupDirs_.clear();
 need(!ids.empty()&&ids.size()<=4,"CLEANUP_BATCH_LIMIT");requirePlainPath(root_);QuotaLock lock(root_);
 std::vector<fs::path> files,dirs;std::uintmax_t bytes=0;Json review=Json::array();
 std::set<std::string> unique;
 for(const auto& id:ids){
  need(id.size()==64&&id.find_first_not_of("0123456789abcdef")==std::string::npos&&unique.insert(id).second,"CLEANUP_ID_REJECTED");
  auto dir=root_/id;requirePlainPath(dir);need(dir!=run_,"CLEANUP_ACTIVE_REJECTED");
  auto manifest=Json::parse(readConfigBytes(dir/"manifest.json"));auto status=manifest.at("status").get<std::string>();
  need(status!="ACTIVE","CLEANUP_ACTIVE_REJECTED");
  auto known=manifest.at("files");need(known.is_object()&&known.size()<=31,"CLEANUP_MANIFEST_INVALID");
  std::set<std::string> seen;
  for(const auto& entry:fs::directory_iterator(dir)){
   auto name=entry.path().filename().u8string();requirePlainPath(entry.path());need(entry.is_regular_file(),"CLEANUP_ALIAS_REJECTED");
   if(name!="manifest.json"){need(names.count(name)&&known.contains(name),"CLEANUP_UNKNOWN_FILE");need(sha256(readDraft(entry.path(),limit(name)))==known.at(name).at("sha256"),"CLEANUP_HASH_CHANGED");seen.insert(name);}
   files.push_back(entry.path());bytes+=entry.file_size();need(files.size()<=128&&bytes<=256*MiB,"CLEANUP_BATCH_LIMIT");
  }
  need(seen.size()==known.size(),"CLEANUP_PARTIAL_INSPECT_ONLY");dirs.push_back(dir);review.push_back({{"run_id",id},{"status",status},{"unknown_outcome_not_resolved_by_deletion",status=="UNKNOWN"}});
 }
 for(const auto& p:files)cleanupHashes_[p]=sha256(readDraft(p,p.filename()=="manifest.json"?2*MiB:limit(p.filename().u8string())));
 cleanupDirs_=dirs;cleanupToken_=opaqueId();return {{"review_token",cleanupToken_},{"runs",review},{"files",files.size()},{"bytes",bytes},{"requires_explicit_confirmation",true},{"config_rollback",false}};
}
void DraftStore::cleanup(const std::string& token){
 need(!cleanupToken_.empty()&&token==cleanupToken_,"CLEANUP_REVIEW_TOKEN_INVALID");cleanupToken_.clear();QuotaLock lock(root_);
 // Validate every frozen path/content before any deletion. Never recurse or
 // infer authority from a disk manifest after an owner session was restarted.
 for(const auto& [p,hash]:cleanupHashes_){requirePlainPath(p);need(p.parent_path().parent_path()==root_,"CLEANUP_OUTSIDE_ROOT");need(sha256(readDraft(p,p.filename()=="manifest.json"?2*MiB:limit(p.filename().u8string())))==hash,"CLEANUP_REVIEW_CHANGED");}
 for(const auto& dir:cleanupDirs_){requirePlainPath(dir);std::size_t count=0;for(const auto& item:fs::directory_iterator(dir)){need(cleanupHashes_.count(item.path()),"CLEANUP_UNKNOWN_FILE");++count;}need(count==std::count_if(cleanupHashes_.begin(),cleanupHashes_.end(),[&](const auto& item){return item.first.parent_path()==dir;}),"CLEANUP_REVIEW_CHANGED");}
 for(const auto& [p,hash]:cleanupHashes_){requirePlainPath(p);need(fs::remove(p),"CLEANUP_IO_ERROR");}
 for(const auto& p:cleanupDirs_){requirePlainPath(p);need(fs::remove(p),"CLEANUP_DIRECTORY_NOT_EMPTY");}
 cleanupHashes_.clear();cleanupDirs_.clear();
}
ProviderBatch UnsupportedProvider::discover(const std::atomic<bool>&){throw std::runtime_error("CAPTURE_UNSUPPORTED_B3_REQUIRED");}
void UnsupportedProvider::capture(const std::function<void(const ProviderBatch&)>&,const std::atomic<bool>&){throw std::runtime_error("CAPTURE_UNSUPPORTED_B3_REQUIRED");}
ProviderBatch UnsupportedProvider::guard(){return {};}
} // namespace mb::local_calibration
