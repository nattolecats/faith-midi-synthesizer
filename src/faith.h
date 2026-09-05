#pragma once
#include "common.h"
#include <array>
#include "legacy.h"

inline bool isGmReset(const BYTE* data,size_t size) {
    return size==6&&data&&data[0]==0xf0&&data[1]==0x7e&&data[2]<128&&
        data[3]==9&&(data[4]==1||data[4]==3)&&data[5]==0xf7;
}

// RTPSynthOpen returns a C interface table. See docs/faith-abi.md.
// Type 4 and 5 return an owner wrapping the same interface table.
class FaithEngine {
    HMODULE dll_=nullptr;
    void* synth_=nullptr;
    void* owner_=nullptr;
    LegacySynth legacy_;
    bool legacyMode_=false;
    bool wrapped_=false;
    std::array<bool,16> sustain_{};
    std::array<std::array<unsigned,128>,16> held_{},deferred_{};
    using Render=int (__cdecl*)(void*,int32_t*);
    using Midi=int (__cdecl*)(void*,int,const uint32_t*,unsigned);
    using Reset=int (__cdecl*)(void*,int);
    using Sysex=int (__cdecl*)(void*,int,const BYTE*,unsigned);
    using Close=void (__cdecl*)(void*);
    Render render_=nullptr; Midi midi_=nullptr; Reset reset_=nullptr;
    Sysex sysex_=nullptr; Close close_=nullptr;
public:
    FaithEngine()=default;
    FaithEngine(const FaithEngine&)=delete;
    ~FaithEngine(){ if(owner_) close_(owner_); if(dll_) FreeLibrary(dll_); }
    bool open(DWORD type=readType()) {
        if(type==1||type==3){legacyMode_=true;return legacy_.open(type);}
        if(type!=2 && type!=4 && type!=5) return false;
        auto path=appDirectory()+L"rt_synth_"+std::to_wstring(type)+L".dll";
        dll_=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!dll_) return false;
        auto openFn=reinterpret_cast<void* (__cdecl*)(void*)>(GetProcAddress(dll_,"RTPSynthOpen"));
        close_=reinterpret_cast<Close>(GetProcAddress(dll_,"RTPSynthClose"));
        if(!openFn||!close_) return false;
        uint32_t config[16]{};
        owner_=openFn(config); if(!owner_) return false;
        synth_=type==2?owner_:*static_cast<void**>(owner_);
        wrapped_=type!=2;
        auto t=static_cast<uintptr_t*>(synth_);
        render_=reinterpret_cast<Render>(t[4]); reset_=reinterpret_cast<Reset>(t[6]);
        midi_=reinterpret_cast<Midi>(t[8]); sysex_=reinterpret_cast<Sysex>(t[9]);
        if(wrapped_) return reset_(synth_,64)==0;
        reset(); return true;
    }
    void reset(){
        sustain_.fill(false);held_={};deferred_={};
        if(legacyMode_){legacy_.reset();return;}
        if(wrapped_){
            // In Type 4/5 slot 6 OPENS a MIDI port; slot 7 closes it.
            // Reusing slot 6 as reset leaked ports and left port 0's notes playing.
            auto t=static_cast<uintptr_t*>(synth_);
            reinterpret_cast<void(__cdecl*)(void*,int)>(t[7])(synth_,0);
        }
        reset_(synth_,64);
    }
    void send(uint32_t message){
        if(legacyMode_){legacy_.send(message);return;}
        unsigned channel=message&15,status=message&0xf0,a=(message>>8)&127,b=(message>>16)&127;
        auto raw=[&](uint32_t m){midi_(synth_,0,&m,1);};
        auto release=[&]{
            for(unsigned note=0;note<128;++note)while(deferred_[channel][note]){
                raw(0x80|channel|(note<<8));--deferred_[channel][note];
            }
        };
        // Type 4/5 retain sustained notes after CC64=0. Defer note-offs here instead.
        if(status==0xb0&&a==64){sustain_[channel]=b>=64;if(!sustain_[channel])release();return;}
        if(status==0xb0&&a==121){
            sustain_[channel]=false;release();
            raw(message);
            // Do not depend on the native Reset All Controllers implementation.
            raw(0xb0|channel|(64<<8));
            raw(0xb0|channel|(1<<8));
            raw(0xb0|channel|(33<<8));
            raw(0xe0|channel|(64<<16));
            raw(0xd0|channel);
            raw(0xb0|channel|(11<<8)|(127<<16));
            raw(0xb0|channel|(100<<8)|(127<<16));
            raw(0xb0|channel|(101<<8)|(127<<16));
            return;
        }
        if(status==0xb0&&a==120){held_[channel]={};deferred_[channel]={};}
        if(status==0xb0&&a==123){
            for(unsigned note=0;note<128;++note)while(held_[channel][note]){
                --held_[channel][note];
                if(sustain_[channel])++deferred_[channel][note];else raw(0x80|channel|(note<<8));
            }
            return;
        }
        if(status==0x90&&b){held_[channel][a]=std::min(held_[channel][a]+1,128U);}
        if(status==0x80||(status==0x90&&!b)){
            if(held_[channel][a])--held_[channel][a];
            if(sustain_[channel]){deferred_[channel][a]=std::min(deferred_[channel][a]+1,128U);return;}
        }
        raw(message);
    }
    void sysex(const BYTE* bytes,unsigned n) {
        if(legacyMode_){legacy_.sysex(bytes,n);return;}
        // Legacy parser reads fixed fields without bounds checks. Reject short messages.
        if(n<2||bytes[0]!=0xf0||bytes[n-1]!=0xf7) return;
        if(bytes[1]!=0x7e && bytes[1]!=0x7f) return;
        if(n<6) return;
        if(bytes[1]==0x7f && n<8) return;
        sysex_(synth_,0,bytes,n);
    }
    void renderRaw(int32_t* samples) {
        if(legacyMode_)legacy_.render(samples);else render_(synth_,samples);
    }
};

// The native limits apply to each engine. Keep MIDI channels on independent
// engines so dense chords and drum tails cannot steal another part's notes.
// Type 2 contains global synthesis state. Type 5 does not support independent
// engine mixing either; keep both single.
#include <memory>
class FaithSynth {
    std::array<std::unique_ptr<FaithEngine>,16> engines_{};
    std::array<bool,16> audible_{};
    unsigned count_=0;
    DWORD type_=4;
public:
    bool open(DWORD type=readType()) {
        // Destroy ALL engines before opening any replacement: Type 2/5 have
        // shared native state. A port reset alone can leave voices/controllers.
        audible_.fill(false);
        count_=0;
        for(auto& engine:engines_)engine.reset();
        type_=type;
        count_=(type==1||type==3||type==4)?16:1;
        for(unsigned i=0;i<count_;++i){
            engines_[i]=std::make_unique<FaithEngine>();
            if(!engines_[i]->open(type)){
                for(auto& engine:engines_)engine.reset();
                count_=0;return false;
            }
        }
        for(unsigned channel=0;channel<16;++channel){
            auto cc=[&](unsigned controller,unsigned value){send(0xb0|channel|(controller<<8)|(value<<16));};
            cc(121,0);cc(0,0);cc(32,0);
            cc(7,100);cc(10,64);cc(11,127);
            cc(101,0);cc(100,0);cc(6,2);cc(38,0);
            cc(101,127);cc(100,127);
            send(0xc0|channel);
            send(0xe0|channel|(64<<16));
        }
        return true;
    }
    void reset(){open(type_);}
    void send(uint32_t message){
        if(!count_)return; // A failed reopen stays silent, never uses old voices.
        // Preserve explicit MSB 120 percussion selection on any channel.
        // Other nonzero banks can accidentally select Faith's percussion ROMs;
        // keep their GM fallback and normalize every LSB to zero. Never remap
        // channels: added drum parts need independent programs/controllers.
        unsigned status=message&0xf0,controller=(message>>8)&127,value=(message>>16)&127;
        if(status==0xb0&&(controller==32||(controller==0&&value!=120)))message&=0x0000ffff;
        unsigned engine=count_==1?0:message&15;
        if((message&0xf0)==0x90&&(message&0x7f0000))audible_[engine]=true;
        engines_[engine]->send(message);
    }
    void sysex(const BYTE* data,unsigned size){
        if(isGmReset(data,size)){reset();return;}
        for(unsigned i=0;i<count_;++i)engines_[i]->sysex(data,size);
    }
    void render(int16_t* out,DWORD gain,DWORD channelVolume) {
        std::array<int64_t,256> mix{};
        for(unsigned engine=0;engine<count_;++engine){
            if(!audible_[engine])continue;
            std::array<int32_t,256> samples{};
            engines_[engine]->renderRaw(samples.data());
            for(size_t i=0;i<samples.size();++i)mix[i]+=samples[i];
        }
        for(size_t i=0;i<mix.size();++i) {
            DWORD channel=(i&1)?HIWORD(channelVolume):LOWORD(channelVolume);
            int64_t value=mix[i]*gain*channel/(100LL*65535);
            out[i]=static_cast<int16_t>(std::clamp<int64_t>(value,-32768,32767));
        }
    }
};
