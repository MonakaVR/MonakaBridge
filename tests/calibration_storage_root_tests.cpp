// Storage selection through the real local session, using only synthetic
// providers and fixture files. No SDK, sockets, helpers or product processes.
#include "calibration_fixture_provider.hpp"
#include <iostream>
#include <vector>

namespace lc=mb::local_calibration;
namespace fs=std::filesystem;
namespace {
int groups=0;
void pass(const char* text){++groups;std::cout<<"PASS "<<text<<'\n';}
struct EnvironmentValue {
 std::wstring name,prior;bool existed=false;
 EnvironmentValue(const wchar_t* key,const fs::path& value):name(key){
  DWORD size=GetEnvironmentVariableW(key,nullptr,0);
  if(size){std::vector<wchar_t> data(size);auto read=GetEnvironmentVariableW(key,data.data(),size);CHECK(read<size);prior.assign(data.data(),read);existed=true;}
  CHECK(SetEnvironmentVariableW(key,value.c_str())!=0);
 }
 ~EnvironmentValue(){SetEnvironmentVariableW(name.c_str(),existed?prior.c_str():nullptr);}
};
std::size_t entries(const fs::path& root){std::size_t n=0;for(const auto& entry:fs::recursive_directory_iterator(root)){(void)entry;++n;}return n;}
void unbound(fixture::Run& run){run.env.draftRoot.clear();run.reset();}
void unchanged(const fixture::Run& run){CHECK(mb::readConfigBytes(run.path)==run.original);}
lc::Json startStatic(fixture::Run& run,lc::Json open){
 open["mode"]="static";
 CHECK(run.execute(open)["status"]=="OPEN");
 CHECK(run.provider.configurationChecks==1&&run.provider.discoveries==0&&run.provider.captures==0);
 CHECK(run.execute(run.command("discover"))["status"]=="DISCOVERED");
 auto confirm=run.execute(run.command("confirm_target"));CHECK(confirm["status"]=="CONFIRMED");
 auto begin=run.command("begin");begin["confirmation_token"]=confirm["confirmation_token"];
 auto result=run.execute(begin);if(result["status"]!="WAITING_POINT")throw std::runtime_error(result.dump());return result;
}
void artifacts(fixture::Run& run,const fs::path& root){
 auto directory=root/run.session->runId();
 CHECK(fs::is_regular_file(directory/"manifest.json")&&fs::is_regular_file(directory/"expected.json"));
 CHECK(mb::readConfigBytes(directory/"expected.json")==run.original);
 CHECK(run.provider.captures==0&&run.session->solveCalls()==0&&run.session->commitCalls()==0);
 CHECK(run.execute(run.command("cancel"))["status"]=="CANCELLED");unchanged(run);
}
void resourceFreeInvalidRetry(const fs::path& base){
 fixture::Run run(base);const auto expected=run.path.parent_path()/"CalibrationDrafts";
 const auto before=entries(run.root);unbound(run);
 CHECK(run.env.draftRoot.empty()&&entries(run.root)==before&&!fs::exists(expected));
 CHECK(run.provider.configurationChecks==0&&run.provider.discoveries==0&&run.provider.captures==0);
 auto bad=run.open();bad["config_path"]=(run.root/"does-not-exist"/"config.json").u8string();
 CHECK(run.execute(bad)["status"]=="REJECTED");
 bad=run.open();bad["source"]="synthetic-absent-identity";CHECK(run.execute(bad)["status"]=="REJECTED");
 bad=run.open();bad["input_revision"]=4294967296ULL;CHECK(run.execute(bad)["status"]=="REJECTED");
 bad=run.open();bad["reference_role"]="unsupported";CHECK(run.execute(bad)["status"]=="REJECTED");
 CHECK(run.session->phase()=="NEW"&&entries(run.root)==before&&!fs::exists(expected));
 CHECK(run.provider.configurationChecks==0&&run.provider.discoveries==0&&run.provider.captures==0);unchanged(run);
 auto valid=run.open();CHECK(run.execute(valid)["status"]=="OPEN");
 CHECK(!fs::exists(expected)&&entries(run.root)==before&&run.provider.configurationChecks==1&&run.provider.discoveries==0);
 CHECK(run.execute(run.command("cancel"))["status"]=="CANCELLED");
 CHECK(!fs::exists(expected)&&entries(run.root)==before);unchanged(run);
 pass("unbound constructor and invalid Open are resource-free; valid retry and Open/cancel create no draft root");
}
void selectedParents(const fs::path& base){
 for(const auto* label:{"selected-A","selected-B"}){
  fixture::Run run(base);auto originalParent=run.path.parent_path();auto selected=run.root/label;fs::create_directory(selected);
  run.path=selected/"selected.json";{std::ofstream file(run.path,std::ios::binary);file<<run.original;CHECK(bool(file));}
  unbound(run);const auto expected=selected/"CalibrationDrafts";auto open=run.open();open["mode"]="static";
  CHECK(!fs::exists(expected));CHECK(run.execute(open)["status"]=="OPEN");
  CHECK(!fs::exists(expected)&&run.provider.discoveries==0&&run.provider.captures==0);
  CHECK(run.execute(run.command("discover"))["status"]=="DISCOVERED");auto confirm=run.execute(run.command("confirm_target"));CHECK(confirm["status"]=="CONFIRMED");
  CHECK(!fs::exists(expected));auto begin=run.command("begin");begin["confirmation_token"]=confirm["confirmation_token"];auto result=run.execute(begin);if(result["status"]!="WAITING_POINT")throw std::runtime_error(result.dump());
  artifacts(run,expected);CHECK(!fs::exists(originalParent/"CalibrationDrafts")&&!fs::exists(run.env.draftRoot));
 }
 pass("each selected config parent owns CalibrationDrafts; installation parent and poisoned environment roots are unused");
}
void explicitOverride(const fs::path& base){
 fixture::Run run(base);const auto overrideRoot=run.root/"explicit-fixture-drafts";run.env.draftRoot=overrideRoot;run.reset();
 CHECK(!fs::exists(overrideRoot));startStatic(run,run.open());artifacts(run,overrideRoot);
 CHECK(!fs::exists(run.path.parent_path()/"CalibrationDrafts"));pass("explicit SessionEnvironment draftRoot override remains authoritative");
}
void rejectedRootRetry(const fs::path& base){
 fixture::Run run(base);const auto blocked=run.path.parent_path()/"CalibrationDrafts",sentinel=run.root/"sentinel.txt";
 {std::ofstream file(sentinel);file<<"original protected fixture bytes";CHECK(bool(file));}
 CHECK(CreateHardLinkW(blocked.c_str(),sentinel.c_str(),nullptr)!=0);unbound(run);const auto before=entries(run.root);
 auto rejected=run.execute(run.open());CHECK(rejected["status"]=="REJECTED"&&rejected["reason"]=="PATH_HARDLINK_REJECTED");
 CHECK(run.session->phase()=="NEW"&&run.provider.configurationChecks==0&&run.provider.discoveries==0&&run.provider.captures==0&&entries(run.root)==before);
 CHECK(mb::readConfigBytes(sentinel)=="original protected fixture bytes");unchanged(run);
 // Retry against another selected config. The rejected root is preserved.
 const auto selected=run.root/"valid-retry";fs::create_directory(selected);run.path=selected/"config.json";
 {std::ofstream file(run.path,std::ios::binary);file<<run.original;CHECK(bool(file));}
 startStatic(run,run.open());artifacts(run,selected/"CalibrationDrafts");CHECK(fs::hard_link_count(blocked)==2&&mb::readConfigBytes(sentinel)=="original protected fixture bytes");
 pass("invalid derived root is rejected before provider configuration; another selected config retries without deleting old artifacts");
}
void readOnlyMeasurement(const fs::path& base){
 fixture::Run run(base);unbound(run);auto open=run.open();open["readonly"]=true;CHECK(run.execute(open)["status"]=="OPEN");
 const auto expected=run.path.parent_path()/"CalibrationDrafts";CHECK(!fs::exists(expected));
 CHECK(run.execute(run.command("discover"))["status"]=="DISCOVERED");auto confirm=run.execute(run.command("confirm_target"));CHECK(confirm["status"]=="CONFIRMED");
 auto begin=run.command("begin");begin["confirmation_token"]=confirm["confirmation_token"];auto result=run.execute(begin);if(result["status"]!="RESULT_READY")throw std::runtime_error(result.dump());
 CHECK(fs::is_regular_file(expected/run.session->runId()/"result.json"));
 auto prepare=run.command("prepare_commit");prepare["result_id"]=result["result"]["result_id"];auto refused=run.execute(prepare);
 CHECK(refused["status"]=="REJECTED"&&refused["reason"]=="READONLY_COMMIT_REJECTED"&&run.session->commitCalls()==0);unchanged(run);
 run.execute(run.command("close"));unchanged(run);pass("read-only fake-provider measurement uses selected local drafts and never saves configuration");
}
}
int main(int argc,char** argv)try{
 CHECK(argc==2);const auto base=fs::absolute(fs::u8path(argv[1])).lexically_normal();fs::create_directories(base);
 const auto poison=base/("poisoned-profile-"+lc::opaqueId());{std::ofstream file(poison);file<<"must remain a file";CHECK(bool(file));}
 EnvironmentValue local(L"LOCALAPPDATA",poison),temp(L"TEMP",poison),tmp(L"TMP",poison),home(L"HOME",poison),profile(L"USERPROFILE",poison),codex(L"CODEX_HOME",poison);
 CHECK(lc::SessionEnvironment{}.draftRoot.empty());
 resourceFreeInvalidRetry(base);selectedParents(base);explicitOverride(base);rejectedRootRetry(base);readOnlyMeasurement(base);
 CHECK(fs::is_regular_file(poison)&&mb::readConfigBytes(poison)=="must remain a file");
 std::cout<<"PASS calibration local storage "<<groups<<" groups; no SDK/socket/helper/process execution; configuration unchanged\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
