// Deliberately reproduce the original harness defect, catching SEH without a WER dialog.
// The real, hash-verified Task2 TrackerDevice is linked unchanged.
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <openvr_driver.h>
#include "../build/upstream/task2/outputs/steamvr/driver/tracker_device.hpp"
#include <cstdio>
static bool reproduced=false;
static LONG report(EXCEPTION_POINTERS* info){
 const auto& e=*info->ExceptionRecord;
 reproduced=e.ExceptionCode==EXCEPTION_ACCESS_VIOLATION&&e.NumberParameters>=2&&e.ExceptionInformation[0]==0&&e.ExceptionInformation[1]==0;
 std::printf("exception=0x%08lx operation=%llu address=0x%llx expected_null_read=%s\n",e.ExceptionCode,static_cast<unsigned long long>(e.ExceptionInformation[0]),static_cast<unsigned long long>(e.ExceptionInformation[1]),reproduced?"true":"false");
 HANDLE process=GetCurrentProcess();SymSetOptions(SYMOPT_LOAD_LINES|SYMOPT_UNDNAME);SymInitialize(process,nullptr,TRUE);
 CONTEXT context=*info->ContextRecord;STACKFRAME64 frame{};frame.AddrPC.Offset=context.Rip;frame.AddrStack.Offset=context.Rsp;frame.AddrFrame.Offset=context.Rbp;frame.AddrPC.Mode=frame.AddrStack.Mode=frame.AddrFrame.Mode=AddrModeFlat;
 for(int i=0;i<24&&frame.AddrPC.Offset;++i){
  alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO)+MAX_SYM_NAME]{};auto symbol=reinterpret_cast<SYMBOL_INFO*>(storage);symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=MAX_SYM_NAME;DWORD64 displacement=0;IMAGEHLP_LINE64 line{};line.SizeOfStruct=sizeof(line);DWORD lineDisplacement=0;
  if(SymFromAddr(process,frame.AddrPC.Offset,&displacement,symbol))std::printf("  %s",symbol->Name);else std::printf("  0x%llx",static_cast<unsigned long long>(frame.AddrPC.Offset));
  if(SymGetLineFromAddr64(process,frame.AddrPC.Offset,&lineDisplacement,&line))std::printf(" (%s:%lu)",line.FileName,line.LineNumber);std::printf("\n");
  if(!StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,GetCurrentThread(),&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr))break;
 }
 SymCleanup(process);return EXCEPTION_EXECUTE_HANDLER;
}
static void trigger(pico_ot::steamvr::TrackerDevice* device){
 __try {device->RequestOrientationZero();}
 __except(report(GetExceptionInformation())) {}
}
int main(){
 SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);vr::CleanupDriverContext();
 pico_ot::steamvr::TrackerDevice device("null-context-reproduction");trigger(&device);
 return reproduced?0:1;
}
