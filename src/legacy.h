#pragma once
#include "common.h"
#include <wincrypt.h>
#include <array>

// Internal entry points are used only for these exact, locally supplied builds.
inline bool verifiedPlayer(HANDLE file,DWORD type) {
    HCRYPTPROV provider=0;HCRYPTHASH hash=0;
    if(!CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT)) return false;
    bool ok=CryptCreateHash(provider,CALG_SHA_256,0,0,&hash)!=FALSE;
    BYTE buffer[16384];DWORD count=0;
    while(ok) {
        if(!ReadFile(file,buffer,sizeof(buffer),&count,nullptr)){ok=false;break;}
        if(!count)break;
        ok=CryptHashData(hash,buffer,count,0)!=FALSE;
    }
    BYTE digest[32];DWORD length=sizeof(digest);
    if(ok)ok=CryptGetHashParam(hash,HP_HASHVAL,digest,&length,0)!=FALSE;
    std::string value;
    if(ok)for(BYTE b:digest){value+="0123456789abcdef"[b>>4];value+="0123456789abcdef"[b&15];}
    if(hash)CryptDestroyHash(hash);CryptReleaseContext(provider,0);
    return ok && value==(type==1?"de8b1380e0aa03f2a3cc6b759040b5436ce1d720deb2815c93105a3b16329cc4":"29853f7bedd51369db5973c79ad29f82197a50de619bd956e19b5808524895ba");
}

class LegacySynth {
    HMODULE dll_=nullptr,dependency_=nullptr;
    void* engine_=nullptr;
    int count_=16;
    uint64_t age_=0;
    int master_=16383;
    struct Channel {int program=0,volume=100,expression=127,pan=64,bend=8192,range=2,rpnMsb=127,rpnLsb=127,bankMsb=0;bool sustain=false,drums=false;};
    struct Voice {int channel=-1,note=0,velocity=0;bool held=false,released=false;uint64_t age=0;};
    std::array<Channel,16> channels_{};
    std::array<Voice,32> voices_{};
    template<class Fn> Fn fn(int index){return reinterpret_cast<Fn>(static_cast<uintptr_t*>(engine_)[index]);}
    using VoiceFn=int(__cdecl*)(void*,int);
    void off(int i,bool immediate=false) {
        fn<VoiceFn>(immediate?7:10)(engine_,i);
        if(immediate) voices_[i]=Voice{};
        else {voices_[i].held=false;voices_[i].released=true;}
    }
    int amplitude(const Voice& v) {
        const auto& c=channels_[v.channel];
        return int(32767LL*v.velocity*v.velocity*c.volume*c.expression*master_/(127LL*127*127*127*16383));
    }
    void levels(int i) {
        const auto& v=voices_[i];const auto& c=channels_[v.channel];int a=amplitude(v);
        fn<int(__cdecl*)(void*,int,int,int)>(11)(engine_,i,a*(127-c.pan)/127,a*c.pan/127);
    }
    void pitch(int i) {
        const auto& c=channels_[voices_[i].channel];
        fn<int(__cdecl*)(void*,int,int)>(12)(engine_,i,(c.bend-8192)*c.range*8);
    }
    void refresh(int channel) {
        for(int i=0;i<count_;++i)if(voices_[i].channel==channel){levels(i);pitch(i);}
    }
public:
    LegacySynth()=default;
    LegacySynth(const LegacySynth&)=delete;
    ~LegacySynth(){if(engine_)fn<void(__cdecl*)(void*)>(2)(engine_);if(dll_)FreeLibrary(dll_);if(dependency_)FreeLibrary(dependency_);}
    bool open(DWORD type) {
        auto path=appDirectory()+L"rt_player_"+std::to_wstring(type)+L".dll";
        HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        if(file==INVALID_HANDLE_VALUE)return false;
        bool valid=verifiedPlayer(file,type);
        if(valid) {
            auto dependency=appDirectory()+L"mclib.dll";
            dependency_=LoadLibraryExW(dependency.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
            if(dependency_)dll_=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        }
        CloseHandle(file);
        if(!dll_)return false;
        // Both supplied players use this constructor; original Type 1/3 voice limits differ.
        engine_=reinterpret_cast<void*(__cdecl*)(int)>(reinterpret_cast<BYTE*>(dll_)+0x4920)(22);
        count_=type==1?16:32;
        return engine_!=nullptr;
    }
    void reset() {
        for(int i=0;i<count_;++i)off(i,true);
        channels_.fill(Channel{});master_=16383;
    }
    void send(uint32_t message) {
        int channel=message&15,status=message&0xf0,a=(message>>8)&127,b=(message>>16)&127;
        auto& c=channels_[channel];
        if(status==0x80 || (status==0x90&&!b)) {
            int index=-1;
            for(int i=0;i<count_;++i)if(voices_[i].channel==channel&&voices_[i].note==a&&voices_[i].held&&(index<0||voices_[i].age<voices_[index].age))index=i;
            if(index>=0){voices_[index].held=false;if(!c.sustain)off(index);}return;
        }
        if(status==0x90) {
            int index=-1;
            for(int i=0;i<count_;++i) {
                if(voices_[i].channel<0||fn<VoiceFn>(5)(engine_,i)==0){index=i;break;}
                if(index<0||voices_[i].age<voices_[index].age)index=i;
            }
            off(index,true);
            voices_[index]={channel,a,b,true,false,++age_};
            BYTE patch[]={static_cast<BYTE>((channel==9||c.drums)?120:121),0,static_cast<BYTE>(c.program),static_cast<BYTE>(a)};
            int amp=amplitude(voices_[index]);
            fn<int(__cdecl*)(void*,int,const BYTE*,int,int,int,int)>(9)(engine_,index,patch,amp*(127-c.pan)/127,amp*c.pan/127,(c.bend-8192)*c.range*8,0);
            return;
        }
        // Bank Select is latched by Program Change; CC121 must not clear it.
        if(status==0xc0){c.program=a;c.drums=c.bankMsb==120;return;}
        if(status==0xe0){c.bend=a|(b<<7);refresh(channel);return;}
        if(status!=0xb0)return;
        switch(a) {
        case 0:c.bankMsb=b;break;
        case 7:c.volume=b;break;
        case 10:c.pan=b;break;
        case 11:c.expression=b;break;
        case 64:
            c.sustain=b>=64;
            if(!c.sustain)for(int i=0;i<count_;++i)if(voices_[i].channel==channel&&!voices_[i].held&&!voices_[i].released)off(i);
            break;
        case 100:c.rpnLsb=b;break;
        case 101:c.rpnMsb=b;break;
        case 6:if(c.rpnMsb==0&&c.rpnLsb==0)c.range=std::min(b,24);break;
        case 120:for(int i=0;i<count_;++i)if(voices_[i].channel==channel)off(i,true);break;
        case 123:
            for(int i=0;i<count_;++i)if(voices_[i].channel==channel&&voices_[i].held){voices_[i].held=false;if(!c.sustain)off(i);}break;
        case 121:
            c.expression=127;c.bend=8192;c.sustain=false;c.rpnMsb=c.rpnLsb=127;
            for(int i=0;i<count_;++i)if(voices_[i].channel==channel&&!voices_[i].held&&!voices_[i].released)off(i);
            break;
        default:break;
        }
        refresh(channel);
    }
    void sysex(const BYTE* data,unsigned n) {
        if(n==6&&data[0]==0xf0&&data[1]==0x7e&&data[3]==9&&(data[4]==1||data[4]==3)&&data[5]==0xf7)reset();
        if(n==8&&data[0]==0xf0&&data[1]==0x7f&&data[3]==4&&data[4]==1&&data[7]==0xf7) {
            master_=(data[5]&127)|((data[6]&127)<<7);for(int c=0;c<16;++c)refresh(c);
        }
    }
    void render(int32_t* output) {
        fn<int(__cdecl*)(void*,int32_t*)>(3)(engine_,output);
        // RTPlayer shifts native samples left by 2, then converts Q24 to PCM16.
        for(int i=0;i<256;++i)output[i]/=128;
    }
};
