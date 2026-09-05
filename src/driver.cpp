#include "common.h"
#include <mmddk.h>
#include <mutex>
#include <vector>
#include <new>

static HMODULE moduleHandle;
struct Client {
    HANDLE input=nullptr, output=nullptr, process=nullptr;
    MIDIOPENDESC desc{};
    DWORD flags=0, volume=0xffffffff;
    std::mutex mutex;
    ~Client() {
        if(input) CloseHandle(input);
        if(output) CloseHandle(output);
        if(process) {
            if(WaitForSingleObject(process,3000)==WAIT_TIMEOUT) TerminateProcess(process,1);
            CloseHandle(process);
        }
    }
    bool start() {
        SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
        HANDLE childRead=nullptr,childWrite=nullptr;
        if(!CreatePipe(&childRead,&input,&sa,65536)) return false;
        if(!CreatePipe(&output,&childWrite,&sa,4096)) {CloseHandle(childRead);return false;}
        SetHandleInformation(input,HANDLE_FLAG_INHERIT,0);
        SetHandleInformation(output,HANDLE_FLAG_INHERIT,0);
        STARTUPINFOEXW si{}; si.StartupInfo.cb=sizeof(si);
        si.StartupInfo.dwFlags=STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput=childRead;
        si.StartupInfo.hStdOutput=childWrite;
        si.StartupInfo.hStdError=childWrite;
        SIZE_T size=0; InitializeProcThreadAttributeList(nullptr,1,0,&size);
        std::vector<BYTE> attributes(size);
        si.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        bool initialized=InitializeProcThreadAttributeList(si.lpAttributeList,1,0,&size)!=FALSE;
        HANDLE handles[]={childRead,childWrite};
        bool ok=initialized && UpdateProcThreadAttribute(si.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,handles,sizeof(handles),nullptr,nullptr);
        auto exe=appDirectory(moduleHandle)+L"FaithMidiSettings.exe";
        auto command=L"\""+exe+L"\" --host";
        PROCESS_INFORMATION pi{};
        if(ok) ok=CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&si.StartupInfo,&pi)!=FALSE;
        if(initialized) DeleteProcThreadAttributeList(si.lpAttributeList);
        CloseHandle(childRead);CloseHandle(childWrite);
        if(!ok) return false;
        process=pi.hProcess;CloseHandle(pi.hThread);
        const auto deadline=GetTickCount64()+5000;
        while(GetTickCount64()<deadline) {
            DWORD available=0;
            if(!PeekNamedPipe(output,nullptr,0,nullptr,&available,nullptr)) return false;
            if(available>=4) {DWORD result=1;return readExact(output,&result,4)&&!result;}
            if(WaitForSingleObject(process,10)==WAIT_OBJECT_0) return false;
        }
        return false;
    }
    bool send(Command command,DWORD value=0,const void* data=nullptr,DWORD bytes=0) {
        std::lock_guard<std::mutex> lock(mutex);
        Packet packet{command,bytes,value};
        return writeExact(input,&packet,sizeof(packet)) && (!bytes||writeExact(input,data,bytes));
    }
    void callback(UINT msg,DWORD_PTR p1=0) {
        DriverCallback(desc.dwCallback,flags, reinterpret_cast<HDRVR>(desc.hMidi),msg,desc.dwInstance,p1,0);
    }
};

extern "C" DWORD WINAPI modMessage(UINT id,UINT msg,DWORD_PTR user,DWORD_PTR p1,DWORD_PTR p2) {
    if(msg==MODM_GETNUMDEVS) return 1;
    if(msg==DRVM_INIT || msg==DRVM_EXIT || msg==DRVM_ENABLE || msg==DRVM_DISABLE) return MMSYSERR_NOERROR;
    if(id) return MMSYSERR_BADDEVICEID;
    auto c=reinterpret_cast<Client*>(user);
    switch(msg) {
    case MODM_GETDEVCAPS: {
        if(!p1) return MMSYSERR_INVALPARAM;
        MIDIOUTCAPSW caps{};
        caps.wMid=0xffff;caps.wPid=1;caps.vDriverVersion=0x100;
        wcscpy_s(caps.szPname,L"Faith MIDI Synthesizer");
        caps.wTechnology=MOD_SWSYNTH;caps.wVoices=64;caps.wNotes=64;caps.wChannelMask=0xffff;
        caps.dwSupport=MIDICAPS_VOLUME|MIDICAPS_LRVOLUME;
        memcpy(reinterpret_cast<void*>(p1),&caps,std::min<size_t>(p2,sizeof(caps)));
        return 0;
    }
    case MODM_OPEN: {
        if(!user||!p1) return MMSYSERR_INVALPARAM;
        auto client=new(std::nothrow) Client;
        if(!client) return MMSYSERR_NOMEM;
        client->desc=*reinterpret_cast<MIDIOPENDESC*>(p1);
        client->flags=HIWORD(p2 & CALLBACK_TYPEMASK);
        if(!client->start()) {delete client;return MMSYSERR_NOTENABLED;}
        *reinterpret_cast<DWORD_PTR*>(user)=reinterpret_cast<DWORD_PTR>(client);
        client->callback(MOM_OPEN);return 0;
    }
    case MODM_CLOSE:
        if(!c) return MMSYSERR_INVALHANDLE;
        c->send(Command::Close);c->callback(MOM_CLOSE);delete c;return 0;
    case MODM_DATA:
        return c && c->send(Command::Short,static_cast<DWORD>(p1))?0:MMSYSERR_NOTENABLED;
    case MODM_LONGDATA: {
        if(!c||!p1||p2<sizeof(MIDIHDR)) return MMSYSERR_INVALPARAM;
        auto hdr=reinterpret_cast<MIDIHDR*>(p1);
        if(!(hdr->dwFlags&MHDR_PREPARED)) return MIDIERR_UNPREPARED;
        if(hdr->dwFlags&MHDR_INQUEUE) return MIDIERR_STILLPLAYING;
        if(hdr->dwBufferLength>maxSysex||(!hdr->lpData&&hdr->dwBufferLength)) return MMSYSERR_INVALPARAM;
        hdr->dwFlags=(hdr->dwFlags&~MHDR_DONE)|MHDR_INQUEUE;
        bool ok=c->send(Command::Long,0,hdr->lpData,hdr->dwBufferLength);
        hdr->dwFlags=(hdr->dwFlags&~MHDR_INQUEUE)|MHDR_DONE;
        c->callback(MOM_DONE,p1);return ok?0:MMSYSERR_NOTENABLED;
    }
    case MODM_PREPARE:
    case MODM_UNPREPARE: {
        if(!p1||p2<sizeof(MIDIHDR)) return MMSYSERR_INVALPARAM;
        auto hdr=reinterpret_cast<MIDIHDR*>(p1);
        if(hdr->dwFlags&MHDR_INQUEUE) return MIDIERR_STILLPLAYING;
        if(msg==MODM_PREPARE) hdr->dwFlags|=MHDR_PREPARED; else hdr->dwFlags&=~MHDR_PREPARED;
        return 0;
    }
    case MODM_RESET: return c && c->send(Command::Reset)?0:MMSYSERR_NOTENABLED;
    case MODM_GETVOLUME:
        if(!p1) return MMSYSERR_INVALPARAM;
        *reinterpret_cast<DWORD*>(p1)=c?c->volume:0xffffffff;return 0;
    case MODM_SETVOLUME:
        if(!c) return MMSYSERR_INVALHANDLE;
        c->volume=static_cast<DWORD>(p1);return c->send(Command::Volume,c->volume)?0:MMSYSERR_NOTENABLED;
    default:return MMSYSERR_NOTSUPPORTED;
    }
}
extern "C" LRESULT CALLBACK DriverProc(DWORD_PTR id,HDRVR driver,UINT msg,LPARAM p1,LPARAM p2) {
    switch(msg) {
    case DRV_LOAD:case DRV_FREE:case DRV_OPEN:case DRV_CLOSE:case DRV_ENABLE:case DRV_DISABLE:return 1;
    case DRV_QUERYCONFIGURE:return 0;
    case DRV_INSTALL:case DRV_REMOVE:return DRV_OK;
    default:return DefDriverProc(id,driver,msg,p1,p2);
    }
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {moduleHandle=instance;DisableThreadLibraryCalls(instance);}
    return TRUE;
}
