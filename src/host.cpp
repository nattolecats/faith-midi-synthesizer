#include "faith.h"
#include "timeline.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

int runHost() {
    HANDLE input=GetStdHandle(STD_INPUT_HANDLE), output=GetStdHandle(STD_OUTPUT_HANDLE);
    FaithSynth synth;
    DWORD status=MMSYSERR_NODRIVER;
    if(!synth.open()) {writeExact(output,&status,4);return 1;}
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    WAVEFORMATEX format{WAVE_FORMAT_PCM,2,44100,176400,4,16,0};
    HWAVEOUT wave=nullptr;
    status=waveOutOpen(&wave,WAVE_MAPPER,&format,reinterpret_cast<DWORD_PTR>(event),0,CALLBACK_EVENT);
    if(status) { writeExact(output,&status,4);CloseHandle(event);return 2; }
    constexpr int bufferCount=4, blocksPerBuffer=4;
    std::array<std::array<int16_t,256*blocksPerBuffer>,bufferCount> buffers{};
    std::array<WAVEHDR,bufferCount> headers{};
    int prepared=0;
    for(auto& hdr:headers) {
        hdr.lpData=reinterpret_cast<char*>(buffers[prepared].data());hdr.dwBufferLength=sizeof(buffers[0]);
        status=waveOutPrepareHeader(wave,&hdr,sizeof(hdr));
        if(status) break;
        ++prepared;
    }
    if(status) {
        writeExact(output,&status,4);
        for(int i=0;i<prepared;++i) waveOutUnprepareHeader(wave,&headers[i],sizeof(WAVEHDR));
        waveOutClose(wave);CloseHandle(event);return 3;
    }
    std::atomic<bool> running{true};
    MidiTimeline timeline;
    LARGE_INTEGER frequency{},origin{};QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&origin);
    std::thread audio([&] {
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_ABOVE_NORMAL);
        int nextBuffer=0;
        while(running) {
            DWORD gain=readVolume();
            for(int filled=0;filled<bufferCount && running;++filled) {
                int i=nextBuffer;
                auto& hdr=headers[i];
                if(hdr.dwFlags&WHDR_INQUEUE) break;
                for(int b=0;b<blocksPerBuffer;++b) timeline.render(synth,buffers[i].data()+256*b,gain);
                if(waveOutWrite(wave,&hdr,sizeof(hdr))) {running=false;break;}
                nextBuffer=(nextBuffer+1)%bufferCount;
            }
            WaitForSingleObject(event,20);
        }
    });
    status=0;writeExact(output,&status,4);
    Packet packet{};
    while(running && readExact(input,&packet,sizeof(packet))) {
        LARGE_INTEGER received{};QueryPerformanceCounter(&received);
        if(packet.bytes>maxSysex) break;
        std::vector<BYTE> bytes(packet.bytes);
        if(packet.bytes&&!readExact(input,bytes.data(),packet.bytes)) break;
        if(packet.command==Command::Close) break;
        uint64_t at=static_cast<uint64_t>((received.QuadPart-origin.QuadPart)*44100/frequency.QuadPart);
        if(!timeline.enqueue(at,packet,std::move(bytes)))break;
    }
    running=false;SetEvent(event);audio.join();
    waveOutReset(wave);
    for(auto& hdr:headers) waveOutUnprepareHeader(wave,&hdr,sizeof(hdr));
    waveOutClose(wave);CloseHandle(event);
    return 0;
}
