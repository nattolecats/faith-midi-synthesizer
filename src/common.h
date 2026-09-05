#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <string>
#include <cstdint>

inline std::wstring appDirectory(HMODULE module = nullptr) {
    wchar_t path[32768]{};
    DWORD n=GetModuleFileNameW(module,path,32768);
    if (!n || n>=32768) return {};
    std::wstring p(path,n);
    return p.substr(0,p.find_last_of(L"\\/")+1);
}
inline const wchar_t* settingsKey = L"Software\\FaithMidiSynth";
inline constexpr DWORD maxVolume=300;
inline DWORD readType() {
    DWORD v=4,size=sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER,settingsKey,L"Type",RRF_RT_REG_DWORD,nullptr,&v,&size);
    return v;
}
inline bool writeType(DWORD v) {
    HKEY key=nullptr;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,settingsKey,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)) return false;
    auto err=RegSetValueExW(key,L"Type",0,REG_DWORD,reinterpret_cast<BYTE*>(&v),sizeof(v));
    RegCloseKey(key);return err==ERROR_SUCCESS;
}
inline DWORD readVolume() {
    DWORD v=100, size=sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER,settingsKey,L"Volume",RRF_RT_REG_DWORD,nullptr,&v,&size);
    return std::min(v,maxVolume);
}
inline bool writeVolume(DWORD v) {
    HKEY key=nullptr;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,settingsKey,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)) return false;
    v=std::min(v,maxVolume);
    auto err=RegSetValueExW(key,L"Volume",0,REG_DWORD,reinterpret_cast<BYTE*>(&v),sizeof(v));
    RegCloseKey(key); return err==ERROR_SUCCESS;
}
enum class Command : uint32_t { Short=1, Long=2, Reset=3, Volume=4, Close=5 };
struct Packet { Command command; uint32_t bytes; uint32_t value; };
constexpr uint32_t maxSysex=65536;
inline bool readExact(HANDLE h,void* data,DWORD size) {
    auto p=static_cast<BYTE*>(data);
    while(size) { DWORD n=0; if(!ReadFile(h,p,size,&n,nullptr)||!n) return false; p+=n;size-=n; }
    return true;
}
inline bool writeExact(HANDLE h,const void* data,DWORD size) {
    auto p=static_cast<const BYTE*>(data);
    while(size) { DWORD n=0; if(!WriteFile(h,p,size,&n,nullptr)||!n) return false;p+=n;size-=n; }
    return true;
}
