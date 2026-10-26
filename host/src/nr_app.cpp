#include "nr_app.h"
#include <windows.h>
#include <tlhelp32.h>
#include <filesystem>
#include <stdexcept>
#include <string>
namespace { namespace fs=std::filesystem; }
bool PrepareNeuralApp() {
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,static_cast<DWORD>(std::size(path)));
    const fs::path here=fs::path(path).parent_path();SetCurrentDirectoryW(here.c_str());
    for(const wchar_t* name:{L"dxgi.dll",L"UnishadeUi.dll",L"amd-nr.addon64",L"amd-nr-panelinput.addon64",L"ReShade.ini",
            L"dlssnr_amd_pass1.dll",L"dlssnr_on_amd_weights.bin"}) {
        if(!fs::is_regular_file(here/name)) {
            const std::wstring message=L"Missing component:\n"+(here/name).wstring()+
                L"\n\nExtract the complete ZIP. For the separately licensed runtime/model, run SETUP-RUNTIME.cmd first to import your existing OpenNR 0.6.0 files. See README.txt.";
            MessageBoxW(nullptr,message.c_str(),L"UnishadeNR setup",MB_ICONINFORMATION);return false;
        }
    }
    // Public builds do not terminate other processes or change another installation.
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(snapshot==INVALID_HANDLE_VALUE)return false;
    PROCESSENTRY32W entry{sizeof(entry)};bool other=false;
    for(BOOL found=Process32FirstW(snapshot,&entry);found;found=Process32NextW(snapshot,&entry))
        if(entry.th32ProcessID!=GetCurrentProcessId() && _wcsicmp(entry.szExeFile,L"Unishade.exe")==0)other=true;
    CloseHandle(snapshot);
    if(other)MessageBoxW(nullptr,L"Quit the running Unishade copy from its tray icon, then open UnishadeNR. Your installation will not be changed.",L"UnishadeNR",MB_ICONINFORMATION);
    return !other;
}
void VerifyNeuralAddon() {
    const HMODULE addon=GetModuleHandleW(L"amd-nr.addon64");
    if(!addon || !GetProcAddress(addon,"UnishadeNR_DrawPanel") || !GetProcAddress(addon,"UnishadeNR_SetPreserveDetail"))
        throw std::runtime_error("OpenNR did not load. Check ReShade.log and enable the bundled add-on under ReShade Add-ons.");
}