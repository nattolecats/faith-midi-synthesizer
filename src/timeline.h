#pragma once
#include "faith.h"
#include <deque>
#include <mutex>
#include <vector>
#include <map>

// Receipt times are measured on one monotonic clock. Keep events separated on
// the PCM timeline even when the audio device asks for several buffers at once.
class MidiTimeline {
    struct Event {uint64_t frame;Packet packet;std::vector<BYTE> data;bool protectedOff=false;};
    std::multimap<uint64_t,Event> events_;
    std::array<std::array<std::deque<uint64_t>,128>,16> noteStarts_{};
    std::mutex mutex_;
    uint64_t frame_=0;
    size_t bytes_=0;
    DWORD volume_=0xffffffff;
public:
    static constexpr uint64_t latencyFrames=2048;
    bool enqueue(uint64_t receiptFrame,const Packet& packet,std::vector<BYTE> data={}) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(events_.size()>=65536||bytes_+data.size()>4*1024*1024)return false;
        bytes_+=data.size();
        events_.emplace(receiptFrame+latencyFrames,Event{receiptFrame+latencyFrames,packet,std::move(data)});
        return true;
    }
    void render(FaithSynth& synth,int16_t* output,DWORD gain) {
        std::deque<Event> due;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            while(!events_.empty()&&events_.begin()->first<=frame_) {
                auto it=events_.begin();bytes_-=it->second.data.size();due.push_back(std::move(it->second));events_.erase(it);
            }
        }
        for(auto& event:due)switch(event.packet.command) {
        case Command::Short: {
            uint32_t message=event.packet.value;unsigned status=message&240,ch=message&15,note=(message>>8)&127,velocity=(message>>16)&127;
            auto& starts=noteStarts_[ch][note];
            if(status==0x90&&velocity){if(starts.size()<128)starts.push_back(frame_);}
            if((status==0x80||(status==0x90&&!velocity))&&!event.protectedOff&&!starts.empty()) {
                uint64_t earliest=starts.front()+256;starts.pop_front();
                // 22050/32000-Hz engines may need two 44100-Hz blocks to produce
                // a newly started voice. Only extend notes compressed below that.
                if(frame_<earliest){
                    event.protectedOff=true;event.frame=earliest;
                    std::lock_guard<std::mutex> lock(mutex_);events_.emplace(earliest,std::move(event));break;
                }
            }
            if(status==0xb0&&(note==120||note==123))clearNoteTimes(int(ch));
            synth.send(message);break;
        }
        case Command::Long:
            if(event.data.size()==6&&event.data[0]==0xf0&&event.data[1]==0x7e&&event.data[3]==9){clearNoteTimes();}
            synth.sysex(event.data.data(),static_cast<unsigned>(event.data.size()));break;
        case Command::Reset:clearNoteTimes();synth.reset();break;
        case Command::Volume:volume_=event.packet.value;break;
        default:break;
        }
        synth.render(output,gain,volume_);frame_+=128;
    }
private:
    void clearNoteTimes(int onlyChannel=-1){
        for(unsigned ch=0;ch<16;++ch)if(onlyChannel<0||ch==unsigned(onlyChannel))for(auto& note:noteStarts_[ch])note.clear();
        std::lock_guard<std::mutex> lock(mutex_);
        for(auto it=events_.begin();it!=events_.end();)if(it->second.protectedOff&&(onlyChannel<0||(it->second.packet.value&15)==unsigned(onlyChannel)))it=events_.erase(it);else ++it;
    }
};
