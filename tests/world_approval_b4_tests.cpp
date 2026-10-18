#include "calibration_b4_fixture.hpp"
#include <algorithm>
#include <iostream>
using namespace b4_fixture;
namespace {
lc::Json canonical(lc::Json value){auto& rows=value["mappings"];std::sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){return std::make_pair(a.at("source_id").template get<std::string>(),a.at("device_id").template get<std::string>())<std::make_pair(b.at("source_id").template get<std::string>(),b.at("device_id").template get<std::string>());});return value;}
bool group(const lc::Json& row){return row.at("source_id")=="pico"&&row.at("input_space")=="native"&&row.at("input_revision")==1;}
}
int main(int argc,char** argv)try{
 CHECK(argc==2);auto evidence=std::filesystem::absolute(argv[1]);std::filesystem::create_directories(evidence);
 auto shortRoot=std::filesystem::temp_directory_path()/("wa-b4-"+lc::opaqueId().substr(0,12));std::filesystem::create_directories(shortRoot);Run run(shortRoot);
 auto before=lc::Json::parse(run.seed.original);for(auto& row:before["mappings"])row["space_approved"]=true;
 auto prototype=before["mappings"][0];for(const auto& row:before["mappings"])if(row["source_id"]=="pico"&&row["device_id"]=="device")prototype=row;
 for(const auto& id:std::vector<std::string>{"sibling-2","sibling-3","other-space","other-revision"}){auto row=prototype;row["device_id"]=id;row["tracker_id"]=id;row["unknown_binding_extension"]={{"keep",id}};if(id=="other-space")row["input_space"]="other-space";if(id=="other-revision")row["input_revision"]=2;before["mappings"].push_back(row);}
 run.seed.original=before.dump();{std::ofstream file(run.seed.path,std::ios::binary|std::ios::trunc);file<<run.seed.original;}run.reset();
 auto measured=run.result();CHECK(mb::readConfigBytes(run.seed.path)==run.seed.original);auto request=run.command("prepare_commit");request["result_id"]=measured.at("result").at("result_id");auto prepared=run.execute(request);CHECK(prepared.at("status")=="PREPARED");
 auto bytes=mb::readConfigBytes(std::filesystem::u8path(prepared.at("candidate_path").get<std::string>()));auto after=lc::Json::parse(bytes);auto expected=before;expected["mapping_revision"]=before.at("mapping_revision").get<unsigned>()+1;
 lc::Json world;for(const auto& row:after["mappings"])if(row["source_id"]=="pico"&&row["device_id"]=="device")world=row.at("world");CHECK(!world.is_null());unsigned invalidated=0,other=0;
 for(auto& row:expected["mappings"])if(group(row)){row["world"]=world;row["space_approved"]=false;++invalidated;}else ++other;
 CHECK(invalidated==4&&other==3&&canonical(after)==canonical(expected));CHECK(mb::readConfigBytes(run.seed.path)==run.seed.original);
 auto commit=run.command("commit");for(auto field:{"result_id","review_token","candidate_sha256"})commit[field]=prepared.at(field);CHECK(run.execute(commit).at("status")=="SAVED");CHECK(run.session->commitCalls()==1&&mb::readConfigBytes(run.seed.path)==bytes);
 std::ofstream receipt(evidence/"summary.json");receipt<<lc::Json({{"result","PASS"},{"four_group_approvals_invalidated",invalidated},{"other_groups_unchanged",other},{"measurement_and_prepare_do_not_persist",true},{"physical_hil","NOT_RUN"},{"fixture_root",shortRoot.u8string()}}).dump(2);CHECK(bool(receipt));
 std::cout<<"PASS World approval B4 static candidate/save: four approved members cleared, other source/space/revision and all extensions/Mount/profile/contracts unchanged, one revision, measure/prepare non-persistent, exact native commit; no real SDK/socket/HIL\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
