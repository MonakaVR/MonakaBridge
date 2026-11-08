#include "monaka_bridge/calibration_session.hpp"
#include "monaka_bridge/config_transaction.hpp"
#include <fstream>
#include <iostream>
#include <limits>

namespace lc=mb::local_calibration;
namespace fs=std::filesystem;
namespace {
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class F>void rejects(F action,const char* reason=nullptr){
 try{action();}catch(const std::exception& e){if(reason)check(std::string(e.what())==reason,"Unexpected rejection reason");return;}
 throw std::runtime_error("Expected operation rejection");
}
void write(const fs::path& path,const std::string& bytes){std::ofstream output(path,std::ios::binary|std::ios::trunc);output<<bytes;output.flush();check(bool(output),"Fixture write failed");}
std::uintmax_t enough(const fs::path&){return 1024*lc::MiB;}
lc::Json emptyManifest(){return {{"version",1},{"status","CLOSED"},{"created_utc_seconds",0},{"files",lc::Json::object()}};}
void foreign(const fs::path& root,const std::string& document){auto run=root/lc::opaqueId();fs::create_directory(run);write(run/"manifest.json",document);}
}

int main(int argc,char** argv)try{
 check(argc==2,"Fixture output directory required");
 const auto base=fs::absolute(fs::u8path(argv[1]))/lc::opaqueId().substr(0,12);fs::create_directories(base);int cases=0;
 auto pass=[&](const char* message){++cases;std::cout<<"PASS "<<message<<'\n';};
 {
  lc::DraftStore unbound(fs::path{},enough);
  rejects([&]{unbound.begin(lc::opaqueId());},"PATH_NOT_ABSOLUTE_CANONICAL");
  rejects([&]{unbound.write("expected.json","x");});rejects([&]{unbound.inventory();},"PATH_NOT_ABSOLUTE_CANONICAL");
  unbound=lc::DraftStore(base/"bound",enough);unbound.begin(lc::opaqueId());unbound.write("expected.json","owned");unbound.verify();
  pass("unbound construction is inert; begin/write/inventory require binding; move binding works");
 }
 {
  const auto root=base/"foreign";fs::create_directory(root);
  const std::string zip="PK\003\004 unrelated archive";write(root/"backup.zip",zip);
  write(root/lc::opaqueId(),"not a directory");
  fs::create_directory(root/"operator-notes");write(root/"operator-notes"/"manifest.json",emptyManifest().dump());
  fs::create_directory(root/(lc::opaqueId()+".zip"));
  fs::create_directory(root/std::string(64,'A'));write(root/std::string(64,'A')/"manifest.json",emptyManifest().dump());
  fs::create_directory(root/lc::opaqueId()); // Missing manifest is not a historical run.
  foreign(root,"{malformed");foreign(root,"[]");
  auto invalid=[&](const std::function<void(lc::Json&)>& mutate){auto document=emptyManifest();mutate(document);foreign(root,document.dump());};
  invalid([](auto& j){j["version"]=2;});invalid([](auto& j){j["version"]=1.0;});
  invalid([](auto& j){j["schema"]="foreign";});invalid([](auto& j){j.erase("created_utc_seconds");});
  invalid([](auto& j){j["created_utc_seconds"]=-1;});invalid([](auto& j){j["created_utc_seconds"]=0.0;});
  invalid([](auto& j){j["created_utc_seconds"]=std::numeric_limits<std::uint64_t>::max();});
  invalid([](auto& j){j["status"]="foreign";});invalid([](auto& j){j["files"]=lc::Json::array();});
  const auto hash=lc::sha256("x");
  for(const auto& name:{"expected.JSON","result.zip","../expected.json"})invalid([&](auto& j){j["files"][name]={{"sha256",hash},{"bytes",1}};});
  for(int variant=0;variant<7;++variant){
   const auto dir=root/lc::opaqueId();fs::create_directory(dir);write(dir/"expected.json","x");auto j=emptyManifest();
   auto file=lc::Json{{"sha256",hash},{"bytes",1}};
   if(variant==0)file["sha256"]=std::string(64,'A');
   if(variant==1)file["bytes"]=1.0;
   if(variant==2)file["bytes"]=-1;
   if(variant==3)file["foreign"]=true;
   if(variant==4)file["bytes"]=2;
   if(variant==5)file["sha256"]=lc::sha256("y");
   if(variant==6)fs::remove(dir/"expected.json");
   j["files"]["expected.json"]=file;write(dir/"manifest.json",j.dump());
  }
  foreign(root,"{\"version\":1,\"version\":1,\"status\":\"CLOSED\",\"created_utc_seconds\":0,\"files\":{}}");
  lc::DraftStore store(root,enough);check(store.inventory()["runs"].empty(),"Foreign entries charged as runs");
  store.begin(lc::opaqueId());store.write("expected.json","owned");store.verify();
  check(store.inventory()["runs"].size()==1,"Invalid historical entries blocked or counted against Begin");
  check(mb::readConfigBytes(root/"backup.zip")==zip,"Root ZIP changed");
  pass("root ZIP/files/unknown names/extensions and malformed/foreign/inexact manifests ignored before Begin");
 }
 {
  const auto root=base/"sidecars";const auto id=lc::opaqueId();
  lc::DraftStore old(root,enough);old.begin(id);old.write("expected.json","owned historical bytes");old.terminal("UNKNOWN");
  lc::DraftStore manager(root,enough);const auto before=manager.inventory()["runs"][0];
  const std::string zip="PK\003\004 preserve operator ZIP";write(root/id/"operator.zip",zip);
  fs::create_directory(root/id/"operator-directory");write(root/id/"operator-directory"/"anything","untouched");
  for(int i=0;i<40;++i)write(root/id/("foreign-"+std::to_string(i)+".txt"),"unrelated");
  check(manager.inventory()["runs"][0]==before,"Unrelated sidecars affected recognized run quota");
  manager.begin(lc::opaqueId());rejects([&]{manager.prepareCleanup({id});});
  check(mb::readConfigBytes(root/id/"operator.zip")==zip&&fs::exists(root/id/"expected.json"),"Unknown sidecar deleted by cleanup");
  pass("historical ZIP/unknown sidecars neither charged nor deleted; explicit cleanup still refuses them");
 }
 {
  const auto root=base/"active-quota";for(int i=0;i<8;++i){lc::DraftStore store(root,enough);store.begin(lc::opaqueId());}
  lc::DraftStore ninth(root,enough);check(ninth.inventory()["charged_bytes"]==512*lc::MiB,"ACTIVE reservation changed");
  rejects([&]{ninth.begin(lc::opaqueId());},"DRAFT_QUOTA_EXCEEDED");
  pass("eight valid ACTIVE runs still reserve 512MiB and reject ninth Begin");
 }
 {
  const auto root=base/"run-quota";for(int i=0;i<16;++i){lc::DraftStore store(root,enough);store.begin(lc::opaqueId());store.terminal("CLOSED");}
  lc::DraftStore seventeenth(root,enough);check(seventeenth.inventory()["runs"].size()==16,"Valid terminal runs not counted");
  rejects([&]{seventeenth.begin(lc::opaqueId());},"DRAFT_QUOTA_EXCEEDED");
  pass("sixteen valid terminal runs retain run-count quota");
 }
 {
  lc::DraftStore store(base/"owned",enough);store.begin(lc::opaqueId());store.write("candidate.json","owned candidate");
  write(store.path("candidate.json"),"changed candidate");
  rejects([&]{store.verify();},"DRAFT_ARTIFACT_CHANGED");
  rejects([&]{store.write("result.json","result");},"DRAFT_ARTIFACT_CHANGED");
  rejects([&]{store.publishCandidate(mb::Config{});},"DRAFT_ARTIFACT_CHANGED");
  check(!fs::exists(store.path("result.json")),"Owned tamper allowed another artifact write");
  pass("current owned candidate tamper still rejects verify/write/publish before any replacement");
 }
 {
  const auto root=base/"cleanup-token";const auto id=lc::opaqueId();lc::DraftStore old(root,enough);old.begin(id);old.write("expected.json","known");old.terminal("CLOSED");
  lc::DraftStore manager(root,enough);auto review=manager.prepareCleanup({id});
  rejects([&]{manager.cleanup("wrong");},"CLEANUP_REVIEW_TOKEN_INVALID");
  write(root/id/"archive.zip","PK preserve");rejects([&]{manager.cleanup(review["review_token"]);},"CLEANUP_UNKNOWN_FILE");
  check(fs::exists(root/id/"archive.zip")&&fs::exists(root/id/"expected.json"),"Cleanup drift deleted files");
  pass("explicit cleanup token and post-review unknown-file protection remain intact");
 }
 {
  lc::DraftStore files(base/"file-quota",enough);files.begin(lc::opaqueId());
  rejects([&]{files.write("result.json",std::string(2*lc::MiB+1,'x'));},"DRAFT_FILE_QUOTA_EXCEEDED");
  lc::DraftStore low(base/"free-quota",[](const auto&){return 256*lc::MiB-1;});
  rejects([&]{low.begin(lc::opaqueId());},"DRAFT_FREE_SPACE_INSUFFICIENT");
  pass("existing per-file and free-disk limits retained");
 }
 std::cout<<"PASS calibration draft recognition/filter "<<cases<<" cases; filesystem fixtures only; SDK/HIL/CUTOVER NOT_RUN\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
