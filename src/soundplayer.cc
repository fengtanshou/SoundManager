#include "soundplayer.h"
#include "soundstream.h"
#include "defer.h"

SoundStreamPos SoundStreamPos::fromSeconds(u64 seconds) {
    SoundStreamPos pos{};
    pos.second = seconds % 60;
    pos.minute = seconds / 60;
    pos.hour = pos.minute / 60;
    pos.minute = pos.minute % 60;
    return pos;
}

u64 SoundStreamPos::toSeconds() const {
    return hour * 3600ULL + minute * 60ULL + second;
}


SoundPlayer::SoundPlayer(const std::string &name)
    : _player_playing(false), _player_paused(false), _player_name(name)  {

}

bool SoundPlayer::is_playing() const {
    return _player_playing;
}

bool SoundPlayer::is_paused() const {
    return _player_paused;
}

std::string SoundPlayer::error_msg() const {
    return _player_err_msg;
}

void SoundPlayer::save_stream() {
    if(_raw_stream) _stream_stack.push(_raw_stream);
}

void SoundPlayer::restore_stream() {
    if(!_stream_stack.empty()) {
        _raw_stream = _stream_stack.top();
        _stream_stack.pop();
    }
}

bool SoundPlayer::start() {

    if(_player_playing) {
        _player_err_msg = "Play is Playing Now";
        return false;
    }

    if(_raw_stream == nullptr) {
        _player_err_msg = "Sound Stream Not Set";
        return false;
    }


    if(!_pcm.open(_player_name)) {
        _player_err_msg = _pcm.error_msg();
        return false;
    }



    auto hw_params = _pcm.hw_params();

    if(!generateHwParams(hw_params)) {
        return false;
    }
    _pcm.set_hw_params(hw_params);

    _player_playing = true;

    _player_play_thread = std::thread([=]{

        auto size = _raw_stream->byte_rate(); // TODO
        auto buffer = new char[size];
        defer [=] {
            delete buffer;
        };

        if(_pcm.prepare()) {
            while(is_playing()) {
               if(is_paused()) std::this_thread::yield();  // if paused, give the CPU time to the other threads
               else {
                   auto start = std::chrono::system_clock::now();
                   if(_raw_stream->read(buffer, size) == size) {
                       auto frames = _pcm.bytes_to_frames(size);
                       auto start = buffer;
                       do {
                            auto frame = _pcm.writei(start, frames);
                            if(frame > 0) {
                                start += frame * _raw_stream->block_align();
                                frames -= frame;
                            }

                            if(frame == -EPIPE) {
                                _pcm.prepare();
                            }
                       } while(frames > 0);
                   }
                   auto stop = std::chrono::system_clock::now();
                   auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
                   if(duration < 1000) {
                       std::this_thread::sleep_for(std::chrono::milliseconds(1000 - duration));
                   }

                   if(_raw_stream->pos() >= _raw_stream->total() - 1) {
                       break;
                   }

               }
           }
       }

       _player_playing = false;
       _player_paused = false;
       _pcm.close();
    });

    _player_err_msg = "Success";

    return true;
}

void SoundPlayer::stop() {
    _player_playing = false;
    _player_paused  = false;
    if( _player_play_thread.joinable()) {
        _player_play_thread.join();
    }
}

void SoundPlayer::pause() {
   if(_raw_stream) _player_paused = true;
}

void SoundPlayer::resume() {
   if(_raw_stream) _player_paused = false;
}

u64 SoundPlayer::currentSecond() const {
    return _raw_stream ? _raw_stream->pos() : 0;
}

void SoundPlayer::setPlayPos(const SoundStreamPos &pos) {
    pause();
    if(_raw_stream) {
        _raw_stream->setPos(pos.toSeconds());
    }
    resume();
}

SoundStreamPos SoundPlayer::currentPos() const {
    return SoundStreamPos::fromSeconds(currentSecond());
}

u64 SoundPlayer::totalSeconds() const {
    return _raw_stream ? _raw_stream->total() : 0 ;
}

bool SoundPlayer::generateHwParams(snd::pcm::HardwareParams &hw) {
    if(_raw_stream == nullptr) {
        _player_err_msg = "Sound Stream Not Set";
        return false;
    }

    int dir = 0;
    auto channels = _raw_stream->channels();
    auto sample_rate = _raw_stream->sample_rate();
    auto format = _raw_stream->bits_per_sample();

    if(!hw.set_access(snd::pcm::ACCESS_RW_INTERLEAVED)) {
        _player_err_msg = hw.error_msg();
        return false;
    }
    if(!hw.set_channels(channels)) {
        _player_err_msg = hw.error_msg();
        return false;
    }
    if(!hw.set_format(format == 8 ? snd::pcm::FORMAT_U8 : snd::pcm::FORMAT_S16_LE)) {
        _player_err_msg = hw.error_msg();
        return false;
    }
    if(!hw.set_rate_near(&sample_rate, &dir)) {
        _player_err_msg = hw.error_msg();
        return false;
    }
    unsigned int buffer_time = 50000;
    if(!hw.set_buffer_time_near(&buffer_time, &dir)) {
        _player_err_msg = hw.error_msg();
        return false;
    }
    unsigned long buffer_size = _raw_stream->byte_rate();
    if(!hw.set_buffer_size_near(&buffer_size)) {
        _player_err_msg = hw.error_msg();
        return false;
    }


    return true;
}


SoundPlayer::~SoundPlayer() {
    stop();
}

