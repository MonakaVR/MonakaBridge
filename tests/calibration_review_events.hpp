#pragma once
// Test-only IPC. No producer/observation model or production dependency.
#include <windows.h>
#include <array>
#include <cstddef>
#include <string>
#include "monaka_bridge/calibration_session.hpp"
namespace fixture {
class ReviewEvents {
 static constexpr std::size_t count=11;
 const std::array<const char*,count> keys{{"reader-paused","writer-paused","worker-stopped","writer-stop-requested","writer-failed","writer-finished","writer-deadline-expired","writer-call-ready","release-reader","release-writer","trigger-write"}};
 struct State {LONG magic,version,count,writerTid,workerTid,lastWriteError;LONG64 frequency;volatile LONG64 points[11];};
 static_assert(offsetof(State,points)==32&&sizeof(State)==120,"test IPC layout");
 std::array<HANDLE,count> events{};std::array<std::wstring,count> names;
 HANDLE mapping=nullptr;State* state=nullptr;std::wstring mapName;
 static std::string ascii(const std::wstring& value){std::string result;for(auto c:value){if(c>127)ExitProcess(83);result.push_back(static_cast<char>(c));}return result;}
 std::size_t index(const std::string& name)const{for(std::size_t i=0;i<count;++i)if(name==keys[i])return i;ExitProcess(83);return 0;}
public:
 explicit ReviewEvents(const std::string& run){
  const auto prefix=L"Local\\MonakaB2Review_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::wstring(run.begin(),run.end())+L"_";
  for(std::size_t i=0;i<count;++i){names[i]=prefix+std::wstring(keys[i],keys[i]+std::char_traits<char>::length(keys[i]));events[i]=CreateEventW(nullptr,TRUE,FALSE,names[i].c_str());if(!events[i])ExitProcess(83);}
  mapName=prefix+L"state";mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(State),mapName.c_str());if(!mapping)ExitProcess(83);
  state=static_cast<State*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(State)));if(!state)ExitProcess(83);
  ZeroMemory(state,sizeof(State));state->magic=0x42324556;state->version=1;state->count=LONG(count);LARGE_INTEGER f;QueryPerformanceFrequency(&f);state->frequency=f.QuadPart;
 }
 ~ReviewEvents(){if(state)UnmapViewOfFile(state);if(mapping)CloseHandle(mapping);for(auto e:events)if(e)CloseHandle(e);}
 ReviewEvents(const ReviewEvents&)=delete;ReviewEvents& operator=(const ReviewEvents&)=delete;
 void writer(){InterlockedCompareExchange(&state->writerTid,LONG(GetCurrentThreadId()),0);}
 void worker(){InterlockedCompareExchange(&state->workerTid,LONG(GetCurrentThreadId()),0);}
 void failed(DWORD error){InterlockedExchange(&state->lastWriteError,LONG(error));signal("writer-failed");}
 void signal(const std::string& name){const auto i=index(name);LARGE_INTEGER t;QueryPerformanceCounter(&t);InterlockedCompareExchange64(&state->points[i],t.QuadPart,0);if(!SetEvent(events[i]))ExitProcess(83);}
 void wait(const std::string& name){if(WaitForSingleObject(events[index(name)],10000)!=WAIT_OBJECT_0)ExitProcess(84);}
 mb::local_calibration::Json descriptor()const{
  auto result=mb::local_calibration::Json{{"version",1},{"mapping",ascii(mapName)},{"state_bytes",sizeof(State)},{"events",mb::local_calibration::Json::object()}};
  for(std::size_t i=0;i<count;++i)result["events"][keys[i]]=ascii(names[i]);return result;
 }
};
} // namespace fixture
