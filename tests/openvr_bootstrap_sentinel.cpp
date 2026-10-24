// Test-only DLL beside a byte-identical copy of the actual calibrator. Every
// imported OpenVR function leaves evidence; DllMain performs no initialization.
#define NOMINMAX
#include <windows.h>
namespace {void called(){wchar_t p[32768];DWORD n=GetEnvironmentVariableW(L"MONAKA_B2_VR_SENTINEL",p,32768);if(n&&n<32768){HANDLE h=CreateFileW(p,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(h!=INVALID_HANDLE_VALUE){DWORD done;WriteFile(h,"VR_CALL\n",8,&done,nullptr);CloseHandle(h);}}}}
extern "C" {
__declspec(dllexport) bool VR_GetRuntimePath(char* path,unsigned capacity,unsigned* required){called();if(required)*required=0;if(path&&capacity)*path=0;return false;}
__declspec(dllexport) void VR_ShutdownInternal(){called();}
__declspec(dllexport) unsigned VR_InitInternal2(int* error,int,const char*){called();if(error)*error=1;return 0;}
__declspec(dllexport) unsigned VR_GetInitToken(){called();return 0;}
__declspec(dllexport) bool VR_IsInterfaceVersionValid(const char*){called();return false;}
__declspec(dllexport) void* VR_GetGenericInterface(const char*,int* error){called();if(error)*error=1;return nullptr;}
__declspec(dllexport) const char* VR_GetVRInitErrorAsEnglishDescription(int){called();return "test-only OpenVR sentinel invoked";}
}
