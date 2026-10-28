#ifdef _WIN32
#include "monaka_bridge/service_runtime.hpp"
#include "monaka_bridge/service_lifecycle.hpp"
#include <windows.h>
#include <shellapi.h>
#include <bcrypt.h>
#include <io.h>
#include <fcntl.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <sstream>
#include <vector>

namespace mb { namespace {
using Handle=pc_service_lifecycle::Handle;
namespace fs=std::filesystem;
constexpr UINT OpenMessage=WM_APP+41,ExitMessage=WM_APP+42,TrayMessage=WM_APP+43,CloseMessage=WM_APP+44;
void need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
void detachConsole(){
 // Detach only our console. Preserve redirected diagnostic handles before
 // FreeConsole closes standard handles; never manipulate another console UI.
 std::array<HANDLE,2> copies{};int descriptors[2]={_fileno(stdout),_fileno(stderr)};
 for(int i=0;i<2;++i){HANDLE source=reinterpret_cast<HANDLE>(_get_osfhandle(descriptors[i]));DWORD kind=GetFileType(source);if(kind==FILE_TYPE_DISK||kind==FILE_TYPE_PIPE)need(DuplicateHandle(GetCurrentProcess(),source,GetCurrentProcess(),&copies[i],0,FALSE,DUPLICATE_SAME_ACCESS)!=0,"Diagnostic handle duplication failed");}
 FreeConsole();
 for(int i=0;i<2;++i)if(copies[i]){int fd=_open_osfhandle(reinterpret_cast<intptr_t>(copies[i]),_O_WRONLY|_O_BINARY);need(fd>=0,"Diagnostic handle restore failed");need(_dup2(fd,descriptors[i])==0,"Diagnostic descriptor restore failed");_close(fd);SetStdHandle(i?STD_ERROR_HANDLE:STD_OUTPUT_HANDLE,reinterpret_cast<HANDLE>(_get_osfhandle(descriptors[i])));}
}
std::string utf8(const std::wstring& value){
 int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);need(count>0,"Invalid Unicode path");std::string result(count,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),count,nullptr,nullptr);return result;
}
std::wstring wide(const std::string& value){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);need(n>0,"Invalid UTF8");std::wstring out(n,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),n);return out;}
std::string hash(const std::string& bytes){
 BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE digest=nullptr;std::array<unsigned char,32> value{};
 need(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"SHA256 unavailable");
 auto rc=BCryptCreateHash(algorithm,&digest,nullptr,0,nullptr,0,0);
 if(rc>=0)rc=BCryptHashData(digest,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),static_cast<ULONG>(bytes.size()),0);
 if(rc>=0)rc=BCryptFinishHash(digest,value.data(),static_cast<ULONG>(value.size()),0);
 if(digest)BCryptDestroyHash(digest);BCryptCloseAlgorithmProvider(algorithm,0);need(rc>=0,"SHA256 failed");
 const char* alphabet="0123456789abcdef";std::string out;for(auto c:value){out+=alphabet[c>>4];out+=alphabet[c&15];}return out;
}
std::string fileHash(const fs::path& path){need(fs::is_regular_file(path)&&fs::file_size(path)<128*1024*1024,"Invalid executable pin");std::ifstream file(path,std::ios::binary);need(bool(file),"Executable read denied");std::ostringstream bytes;bytes<<file.rdbuf();return hash(bytes.str());}
std::wstring executable(HANDLE process){std::wstring path(32768,L'\0');DWORD size=static_cast<DWORD>(path.size());need(QueryFullProcessImageNameW(process,0,path.data(),&size)!=0,"Executable identity unavailable");path.resize(size);return path;}
std::string scope(const fs::path& config){
 auto name=fs::absolute(config).lexically_normal().wstring();int size=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_UPPERCASE,name.data(),static_cast<int>(name.size()),nullptr,0,nullptr,nullptr,0);need(size>0,"Configuration identity unavailable");std::wstring upper(size,L'\0');need(LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_UPPERCASE,name.data(),static_cast<int>(name.size()),upper.data(),size,nullptr,nullptr,0)>0,"Configuration identity unavailable");return hash(utf8(upper));
}
std::wstring quote(const std::wstring& value){std::wstring out=L"\"";size_t slashes=0;for(auto c:value){if(c==L'\\'){++slashes;continue;}out.append(c==L'"'?slashes*2+1:slashes,L'\\');out+=c;slashes=0;}out.append(slashes*2,L'\\');return out+L'"';}
std::vector<std::wstring> arguments(){int count=0;LPWSTR* raw=CommandLineToArgvW(GetCommandLineW(),&count);need(raw!=nullptr,"Arguments unavailable");std::vector<std::wstring> args(raw,raw+count);LocalFree(raw);return args;}
struct Identity {pc_service_lifecycle::State state;wchar_t pipe[80]{};};
Identity identity(const std::wstring& mapping){
 Handle map(OpenFileMappingW(FILE_MAP_READ,FALSE,mapping.c_str()));need(map.value!=nullptr,"No matching native owner");auto view=static_cast<const Identity*>(MapViewOfFile(map.value,FILE_MAP_READ,0,0,sizeof(Identity)));need(view!=nullptr,"Owner identity unavailable");auto first=view->state.generation;MemoryBarrier();Identity snapshot=*view;MemoryBarrier();auto last=view->state.generation;UnmapViewOfFile(view);need(first&&!(first&1)&&first==last&&snapshot.state.magic==pc_service_lifecycle::kMagic&&snapshot.state.version==1&&snapshot.state.ready==1,"Owner identity not ready");need(snapshot.pipe[79]==0&&snapshot.pipe[0]!=0,"Owner channel unavailable");return snapshot;
}
bool transfer(HANDLE pipe,void* bytes,DWORD size,bool write,HANDLE cancel,DWORD timeout){
 char* at=static_cast<char*>(bytes);DWORD remaining=size;const auto started=GetTickCount64();
 while(remaining){Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));if(!event.value)return false;OVERLAPPED operation{};operation.hEvent=event.value;DWORD count=0;
  BOOL done=write?WriteFile(pipe,at,remaining,&count,&operation):ReadFile(pipe,at,remaining,&count,&operation);
  if(!done){if(GetLastError()!=ERROR_IO_PENDING)return false;HANDLE waits[2]={event.value,cancel};auto elapsed=GetTickCount64()-started;DWORD available=elapsed>=timeout?0:timeout-static_cast<DWORD>(elapsed);DWORD result=WaitForMultipleObjects(cancel?2:1,waits,FALSE,available);
   if(result!=WAIT_OBJECT_0){CancelIoEx(pipe,&operation);WaitForSingleObject(event.value,INFINITE);return false;}
   if(!GetOverlappedResult(pipe,&operation,&count,FALSE))return false;
  }
  if(!count)return false;at+=count;remaining-=count;
 }
 return true;
}
std::string request(const fs::path& config,unsigned char op){
 auto key=scope(config);auto record=identity(L"Local\\MonakaBridge_Controller_"+wide(key));Handle owner(pc_service_lifecycle::OpenIdentity(record.state.pid,record.state.created));
 auto self=executable(GetCurrentProcess());need(fs::equivalent(executable(owner.value),self)&&fileHash(self)==fileHash(executable(owner.value)),"Owner is a different executable; no request sent");
 auto name=L"\\\\.\\pipe\\"+std::wstring(record.pipe);need(WaitNamedPipeW(name.c_str(),2000)!=0,"Owner channel unavailable");Handle pipe(CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr));need(pipe.value!=INVALID_HANDLE_VALUE,"Owner channel connect failed");ULONG server=0;need(GetNamedPipeServerProcessId(pipe.value,&server)&&server==record.state.pid,"Owner pipe identity mismatch");
 std::array<char,66> message{};message[0]=1;message[1]=static_cast<char>(op);std::copy(key.begin(),key.end(),message.begin()+2);need(transfer(pipe.value,message.data(),66,true,nullptr,12000),"Request not confirmed");DWORD length=0;need(transfer(pipe.value,&length,4,false,nullptr,12000)&&length&&length<=4096,"Owner response unconfirmed");std::string result(length,'\0');need(transfer(pipe.value,result.data(),length,false,nullptr,12000),"Owner response incomplete");unsigned char ack=1;need(transfer(pipe.value,&ack,1,true,nullptr,2000),"Owner acknowledgement unconfirmed");return result;
}

class Runtime {
 fs::path config_;std::uint16_t ports_[4];std::thread thread_;std::atomic_bool stop_{false};
 std::mutex operation_,state_;std::condition_variable changed_;bool running_=false,done_=true;std::string fault_;
 void stopOwned(){
  stop_=true;std::unique_lock<std::mutex> lock(state_);
  need(changed_.wait_for(lock,std::chrono::seconds(5),[&]{return done_;}),"Runtime stop unconfirmed; owner retained");lock.unlock();if(thread_.joinable())thread_.join();
 }
 void startOwned(){
  {std::lock_guard<std::mutex> lock(state_);if(running_)return;need(done_,"Prior runtime has not exited; no replacement");}
  if(thread_.joinable())thread_.join();stop_=false;{std::lock_guard<std::mutex> lock(state_);done_=false;fault_.clear();}
  thread_=std::thread([this]{try{runService(config_,ports_,0,[&]{return stop_.load();},[&]{std::lock_guard<std::mutex> lock(state_);running_=true;changed_.notify_all();});}catch(const std::exception& e){std::lock_guard<std::mutex> lock(state_);fault_=e.what();}{std::lock_guard<std::mutex> lock(state_);running_=false;done_=true;}changed_.notify_all();});
  std::unique_lock<std::mutex> lock(state_);need(changed_.wait_for(lock,std::chrono::seconds(5),[&]{return running_||done_;}),"Runtime start unconfirmed; owner retained");if(!running_)throw std::runtime_error("Runtime failed: "+fault_);
 }
public:
 Runtime(fs::path config,const std::uint16_t (&ports)[4]):config_(std::move(config)){std::copy(ports,ports+4,ports_);}
 ~Runtime(){stop_=true;if(thread_.joinable())thread_.join();}
 std::string command(unsigned char op){std::lock_guard<std::mutex> operation(operation_);need(op>=1&&op<=3,"Unknown runtime operation");if(op!=1)stopOwned();if(op!=2)startOwned();return op==2?"STOPPED: native worker exit and sockets released":"RUNNING: native worker ready; tracking/output not verified";}
 bool stopped(){std::lock_guard<std::mutex> lock(state_);return done_;}
};

class Owner {
 fs::path config_,gui_,helper_,profile_;std::string key_,guiHash_,helperHash_,selfHash_,profileHash_;std::wstring self_,pipeName_;Runtime runtime_;
 Handle mapping_,cancel_,job_,guiProcess_;Identity* view_=nullptr;std::thread pipeWorker_,watchdog_;std::mutex guiLock_;
 std::atomic<ULONGLONG> heartbeat_{0};std::mutex runtimeGate_;
 HWND window_=nullptr;NOTIFYICONDATAW icon_{};UINT taskbar_=0;std::atomic_bool exiting_{false};bool iconAdded_=false,readOnly_=false;
 std::string diagnostic_="Starting";std::mutex diagnosticLock_;
 static LRESULT CALLBACK Window(HWND window,UINT message,WPARAM w,LPARAM l){
  auto owner=reinterpret_cast<Owner*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(message==WM_NCCREATE){owner=static_cast<Owner*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(owner));owner->window_=window;}
  if(!owner)return DefWindowProcW(window,message,w,l);
  owner->heartbeat_=GetTickCount64();
  if(message==WM_TIMER)return 0;
  if(message==owner->taskbar_&&owner->taskbar_){owner->addIcon();return 0;}
  try{
   if(message==OpenMessage){owner->openGui();return 0;}
   if(message==CloseMessage){owner->closeGui();return 0;}
   if(message==ExitMessage||message==WM_CLOSE){owner->exit();return 0;}
   if(message==TrayMessage){if(l==WM_LBUTTONDBLCLK)owner->openGui();else if(l==WM_RBUTTONUP||l==WM_CONTEXTMENU)owner->menu();return 0;}
   if(message==WM_COMMAND){auto id=LOWORD(w);if(id==4)owner->openGui();else if(id==5)owner->exit();else if(id>=1&&id<=3)owner->note(owner->runtimeCommand(static_cast<unsigned char>(id)));return 0;}
  }catch(const std::exception& e){owner->note(std::string("UNCONFIRMED: ")+e.what());}
  return DefWindowProcW(window,message,w,l);
 }
 std::string runtimeCommand(unsigned char op){std::lock_guard<std::mutex> lock(runtimeGate_);need(!exiting_,"Native owner exit has started");return runtime_.command(op);}
 void note(const std::string& text){
  {std::lock_guard<std::mutex> lock(diagnosticLock_);diagnostic_=text;}std::cout<<nlohmann::json{{"event","native_owner"},{"state",text},{"pid",GetCurrentProcessId()}}.dump()<<std::endl;
  if(window_){auto label=wide(text);wcsncpy_s(icon_.szInfo,label.c_str(),_TRUNCATE);wcscpy_s(icon_.szInfoTitle,L"Monaka Bridge");icon_.uFlags=NIF_INFO;Shell_NotifyIconW(NIM_MODIFY,&icon_);}
 }
 void addIcon(){icon_.cbSize=sizeof(icon_);icon_.hWnd=window_;icon_.uID=1;icon_.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;icon_.uCallbackMessage=TrayMessage;icon_.hIcon=LoadIconW(nullptr,MAKEINTRESOURCEW(32512));wcscpy_s(icon_.szTip,L"Monaka Bridge native owner");iconAdded_=Shell_NotifyIconW(NIM_ADD,&icon_)!=FALSE;}
 DWORD jobCount(){if(!job_.value)return 0;JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};need(QueryInformationJobObject(job_.value,JobObjectBasicAccountingInformation,&info,sizeof(info),nullptr)!=0,"GUI Job ownership unavailable");return info.ActiveProcesses;}
 static BOOL CALLBACK closeWindow(HWND window,LPARAM parameter){DWORD id=0;GetWindowThreadProcessId(window,&id);if(id==static_cast<DWORD>(parameter))PostMessageW(window,WM_CLOSE,0,0);return TRUE;}
 struct Focus {DWORD pid;bool found=false;};
 static BOOL CALLBACK focusWindow(HWND window,LPARAM parameter){auto& value=*reinterpret_cast<Focus*>(parameter);DWORD id=0;GetWindowThreadProcessId(window,&id);if(id==value.pid&&IsWindowVisible(window)){if(IsIconic(window))ShowWindowAsync(window,SW_RESTORE);SetForegroundWindow(window);value.found=true;return FALSE;}return TRUE;}
 void releaseGui(){if(guiProcess_.value){CloseHandle(guiProcess_.value);guiProcess_.value=nullptr;}if(job_.value){CloseHandle(job_.value);job_.value=nullptr;}}
 void closeGui(){
  std::lock_guard<std::mutex> lock(guiLock_);if(!guiProcess_.value)return;
  if(WaitForSingleObject(guiProcess_.value,0)!=WAIT_OBJECT_0){EnumWindows(closeWindow,static_cast<LPARAM>(GetProcessId(guiProcess_.value)));need(WaitForSingleObject(guiProcess_.value,5000)==WAIT_OBJECT_0,"GUI/calibration stop unconfirmed; native owner retained");}
  need(jobCount()==0,"Owned GUI descendants still active; Job retained");releaseGui();
 }
 void openGui(){
  std::lock_guard<std::mutex> lock(guiLock_);need(!exiting_,"Native owner exit has started");
  if(guiProcess_.value){if(WaitForSingleObject(guiProcess_.value,0)!=WAIT_OBJECT_0){Focus focus{GetProcessId(guiProcess_.value)};EnumWindows(focusWindow,reinterpret_cast<LPARAM>(&focus));need(focus.found,"Owned GUI is starting or unavailable; no duplicate launched");return;}need(jobCount()==0,"GUI exited with owned descendants active; Job retained");releaseGui();}
  need(fileHash(gui_)==guiHash_&&fileHash(helper_)==helperHash_,"GUI/helper build changed; relaunch explicitly");
  if(!profile_.empty())need(fileHash(profile_)==profileHash_,"Selected installation profile changed; relaunch explicitly");
  job_.value=CreateJobObjectW(nullptr,nullptr);need(job_.value!=nullptr,"GUI Job unavailable");JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;need(SetInformationJobObject(job_.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits))!=0,"GUI Job limits unavailable");
  auto owner=std::to_wstring(GetCurrentProcessId())+L":"+std::to_wstring(pc_service_lifecycle::Creation(GetCurrentProcess()));
  auto target=profile_.empty()?L" --config "+quote(config_.wstring())+L" --health "+quote(config_.wstring()+L".status.json")+L" --config-helper "+quote(helper_.wstring())+L" --helper-sha256 "+wide(helperHash_)+L" --installation-name \"Native Bridge owner\"":L" --installation "+quote(profile_.wstring())+L" --profile-sha256 "+wide(profileHash_);
  auto command=quote(gui_.wstring())+L" --native-owner-window --control-window-only"+target+L" "+(readOnly_?L"--read-only":L"--allow-write")+L" --owner-pipe "+quote(pipeName_)+L" --owner-identity "+owner+L" --owner-executable "+quote(self_)+L" --owner-sha256 "+wide(selfHash_);
  STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
  need(CreateProcessW(gui_.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,config_.parent_path().c_str(),&startup,&child)!=0,"GUI launch failed");Handle thread(child.hThread);guiProcess_.value=child.hProcess;
  if(!AssignProcessToJobObject(job_.value,guiProcess_.value)){need(TerminateProcess(guiProcess_.value,2)&&WaitForSingleObject(guiProcess_.value,5000)==WAIT_OBJECT_0,"Suspended GUI cleanup unconfirmed; retained process handle");releaseGui();throw std::runtime_error("Suspended GUI could not enter owned Job");}
  need(ResumeThread(thread.value)!=static_cast<DWORD>(-1),"Owned GUI resume failed; owner retained");
 }
 void menu(){HMENU menu=CreatePopupMenu();need(menu!=nullptr,"Tray menu unavailable");std::string state;{std::lock_guard<std::mutex> lock(diagnosticLock_);state=diagnostic_;}AppendMenuW(menu,MF_STRING|MF_GRAYED,0,wide(state).c_str());AppendMenuW(menu,MF_STRING,4,L"Open Bridge Control...");AppendMenuW(menu,MF_STRING,1,L"Start Bridge");AppendMenuW(menu,MF_STRING,2,L"Stop Bridge");AppendMenuW(menu,MF_STRING,3,L"Restart Bridge");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,5,L"Exit");POINT at{};GetCursorPos(&at);SetForegroundWindow(window_);TrackPopupMenu(menu,TPM_RIGHTBUTTON,at.x,at.y,0,window_,nullptr);DestroyMenu(menu);}
 void exit(){
  if(exiting_.exchange(true))return;
  try{closeGui();heartbeat_=GetTickCount64();{std::lock_guard<std::mutex> lock(runtimeGate_);note(runtime_.command(2));}heartbeat_=GetTickCount64();SetEvent(cancel_.value);PostQuitMessage(0);}
  catch(...){exiting_=false;throw;}
 }
 void serve(){
  try{while(WaitForSingleObject(cancel_.value,0)!=WAIT_OBJECT_0){
   auto path=L"\\\\.\\pipe\\"+pipeName_;Handle pipe(CreateNamedPipeW(path.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,4096,128,0,nullptr));need(pipe.value!=INVALID_HANDLE_VALUE,"Native owner pipe unavailable");
   Handle connected(CreateEventW(nullptr,TRUE,FALSE,nullptr));need(connected.value!=nullptr,"Owner connection event unavailable");OVERLAPPED connection{};connection.hEvent=connected.value;
   bool ready=ConnectNamedPipe(pipe.value,&connection)!=FALSE;auto error=ready?ERROR_SUCCESS:GetLastError();
   if(!ready&&error==ERROR_PIPE_CONNECTED)ready=true;
   if(!ready&&error==ERROR_IO_PENDING){HANDLE waits[2]={connected.value,cancel_.value};if(WaitForMultipleObjects(2,waits,FALSE,INFINITE)!=WAIT_OBJECT_0){CancelIoEx(pipe.value,&connection);WaitForSingleObject(connected.value,INFINITE);break;}DWORD n=0;ready=GetOverlappedResult(pipe.value,&connection,&n,FALSE)!=FALSE;}
   if(!ready)continue;
   std::array<char,66> incoming{};if(!transfer(pipe.value,incoming.data(),66,false,cancel_.value,3000)){DisconnectNamedPipe(pipe.value);continue;}
   std::string result;UINT requested=0;
   try{need(incoming[0]==1&&std::string(incoming.data()+2,64)==key_,"Owner target/version mismatch");auto op=static_cast<unsigned char>(incoming[1]);if(op==4){requested=OpenMessage;result="OPEN_REQUESTED";}else if(op==5){requested=ExitMessage;result="EXIT_REQUESTED";}else if(op==6){need(!exiting_,"Native owner exit has started");result="OWNER_READY";}else if(op==7){requested=CloseMessage;result="CLOSE_REQUESTED";}else result=runtimeCommand(op);result="OK "+result;}
   catch(const std::exception& e){result=std::string("UNCONFIRMED ")+e.what();}
   if(result.size()>4096)result="UNCONFIRMED Diagnostic exceeds bound";DWORD length=static_cast<DWORD>(result.size());unsigned char ack=0;bool sent=transfer(pipe.value,&length,4,true,cancel_.value,3000)&&transfer(pipe.value,result.data(),length,true,cancel_.value,3000)&&transfer(pipe.value,&ack,1,false,cancel_.value,2000)&&ack==1;DisconnectNamedPipe(pipe.value);if(sent&&requested)PostMessageW(window_,requested,0,0);
  }}catch(const std::exception& e){note(std::string("UNCONFIRMED owner IPC: ")+e.what());}
 }
public:
 Owner(fs::path config,fs::path gui,fs::path helper,const std::uint16_t (&ports)[4],bool readOnly,fs::path profile):config_(std::move(config)),gui_(std::move(gui)),helper_(std::move(helper)),profile_(std::move(profile)),key_(scope(config_)),guiHash_(fileHash(gui_)),helperHash_(fileHash(helper_)),self_(executable(GetCurrentProcess())),runtime_(config_,ports),readOnly_(readOnly){if(!profile_.empty())profileHash_=fileHash(profile_);selfHash_=fileHash(self_);auto nonce=uuid();nonce.erase(std::remove(nonce.begin(),nonce.end(),'-'),nonce.end());pipeName_=L"MonakaBridgeOwnerV1_"+wide(nonce);cancel_.value=CreateEventW(nullptr,TRUE,FALSE,nullptr);need(cancel_.value!=nullptr,"Native owner stop event unavailable");}
 ~Owner(){SetEvent(cancel_.value);if(watchdog_.joinable())watchdog_.join();if(pipeWorker_.joinable())pipeWorker_.join();if(iconAdded_)Shell_NotifyIconW(NIM_DELETE,&icon_);if(view_)UnmapViewOfFile(view_);if(window_)DestroyWindow(window_);}
 int run(bool trayOnly){
  WNDCLASSW type{};type.lpfnWndProc=Window;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"MonakaBridgeNativeOwnerV1";need(RegisterClassW(&type)!=0||GetLastError()==ERROR_CLASS_ALREADY_EXISTS,"Native owner window class unavailable");
  taskbar_=RegisterWindowMessageW(L"TaskbarCreated");need(CreateWindowExW(0,type.lpszClassName,wide(key_).c_str(),WS_OVERLAPPED,0,0,0,0,nullptr,nullptr,type.hInstance,this)!=nullptr,"Native owner window unavailable");addIcon();need(iconAdded_,"Native tray icon unavailable");
  mapping_.value=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Identity),(L"Local\\MonakaBridge_Controller_"+wide(key_)).c_str());need(mapping_.value!=nullptr&&GetLastError()!=ERROR_ALREADY_EXISTS,"Native owner identity already occupied");view_=static_cast<Identity*>(MapViewOfFile(mapping_.value,FILE_MAP_WRITE,0,0,sizeof(Identity)));need(view_!=nullptr,"Native owner identity map unavailable");
  Identity record{};record.state.magic=pc_service_lifecycle::kMagic;record.state.version=1;record.state.generation=1;record.state.pid=record.state.ownerPid=GetCurrentProcessId();record.state.created=record.state.ownerCreated=pc_service_lifecycle::Creation(GetCurrentProcess());record.state.ready=1;wcscpy_s(record.pipe,pipeName_.c_str());std::memcpy(view_,&record,sizeof(record));MemoryBarrier();view_->state.generation=2;
  heartbeat_=GetTickCount64();need(SetTimer(window_,1,1000,nullptr)!=0,"Native owner heartbeat unavailable");
  watchdog_=std::thread([this]{while(WaitForSingleObject(cancel_.value,250)==WAIT_TIMEOUT)if(GetTickCount64()-heartbeat_.load()>15000){std::cerr<<"Native tray message loop unresponsive; terminating this owner and its Job"<<std::endl;TerminateProcess(GetCurrentProcess(),71);return;}});
  pipeWorker_=std::thread(&Owner::serve,this);try{note(runtimeCommand(1));}catch(const std::exception& e){note(std::string("UNCONFIRMED: ")+e.what());}if(!trayOnly)try{openGui();}catch(const std::exception& e){note(std::string("UNCONFIRMED: ")+e.what());}
  MSG message{};BOOL next;while((next=GetMessageW(&message,nullptr,0,0))>0){TranslateMessage(&message);DispatchMessageW(&message);}need(next==0,"Native owner message loop failed");return 0;
 }
};
}
int nativeOwner(int,char**){
 detachConsole();
 auto args=arguments();need(args.size()>=9&&args.size()<=13,"Usage: --native-owner CONFIG GUI CONFIG_HELPER INGRESS MONAKA DIRECT MIRROR [--open] [--read-only] [--installation PROFILE]");
 fs::path config=args[2],gui=args[3],helper=args[4];need(config.is_absolute()&&gui.is_absolute()&&helper.is_absolute(),"Native owner requires explicit absolute paths");config=fs::canonical(config);gui=fs::canonical(gui);helper=fs::canonical(helper);loadConfig(config);
 {Handle file(CreateFileW(config.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));BY_HANDLE_FILE_INFORMATION info{};need(file.value!=INVALID_HANDLE_VALUE&&GetFileInformationByHandle(file.value,&info)&&info.nNumberOfLinks==1&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT),"Native owner configuration alias refused");}
 std::uint16_t ports[4];for(int i=0;i<4;i++){size_t used=0;auto number=std::stoul(args[i+5],&used);need(used==args[i+5].size()&&number>0&&number<=65535,"Invalid explicit runtime port");ports[i]=static_cast<std::uint16_t>(number);}
 bool trayOnly=true,readOnly=false,selected=false;fs::path profile;for(size_t i=9;i<args.size();i++){if((args[i]==L"--tray-only"||args[i]==L"--open")&&!selected){trayOnly=args[i]==L"--tray-only";selected=true;}else if(args[i]==L"--read-only"&&!readOnly)readOnly=true;else if(args[i]==L"--installation"&&profile.empty()&&i+1<args.size()){profile=args[++i];need(profile.is_absolute(),"Installation profile must be absolute");profile=fs::canonical(profile);}else throw std::invalid_argument("Unknown/duplicate native owner option");}
 if(!profile.empty()){
  need(fs::file_size(profile)<=65536,"Installation profile exceeds bound");std::ifstream input(profile);nlohmann::json value;input>>value;need(value.at("schema")=="monaka.bridge.installation.v2","Native owner requires a version2 calibration installation profile");
  auto resolve=[&](const char* key){auto path=fs::u8path(value.at(key).get<std::string>());return fs::absolute(path.is_absolute()?path:profile.parent_path()/path).lexically_normal();};
  need(fs::equivalent(resolve("config"),config)&&fs::equivalent(resolve("config_helper"),helper)&&scope(resolve("health"))==scope(fs::path(config.wstring()+L".status.json")),"Profile targets do not match this native installation");
  need(value.at("helper_sha256")==fileHash(helper)&&value.at("mirror_port").get<unsigned>()==ports[3],"Profile helper/mirror differs from native runtime");readOnly=readOnly||value.at("read_only").get<bool>();
 }
 auto name=L"Local\\MonakaBridge_ServiceTray_"+wide(scope(config));Handle singleton(CreateMutexW(nullptr,TRUE,name.c_str()));need(singleton.value!=nullptr,"Installation mutex unavailable");bool owns=GetLastError()!=ERROR_ALREADY_EXISTS;
 if(!owns){auto wait=WaitForSingleObject(singleton.value,0);owns=wait==WAIT_OBJECT_0||wait==WAIT_ABANDONED;}
 if(!owns){auto result=request(config,4);std::cout<<result<<std::endl;need(result.rfind("OK ",0)==0,"Existing owner did not accept reopen");return 0;}
 try{int result;{Owner owner(config,gui,helper,ports,readOnly,profile);result=owner.run(trayOnly);}ReleaseMutex(singleton.value);return result;}catch(...){ReleaseMutex(singleton.value);throw;}
}
int nativeOwnerCommand(int,char**){auto args=arguments();need(args.size()==4,"Usage: --owner-command CONFIG Start|Stop|Restart|Open|Exit|Probe|Close");const std::vector<std::wstring> names={L"Start",L"Stop",L"Restart",L"Open",L"Exit",L"Probe",L"Close"};auto found=std::find(names.begin(),names.end(),args[3]);need(found!=names.end(),"Unknown owner command");auto result=request(fs::canonical(args[2]),static_cast<unsigned char>(found-names.begin()+1));std::cout<<result<<std::endl;return result.rfind("OK ",0)==0?0:2;}
}
#endif
