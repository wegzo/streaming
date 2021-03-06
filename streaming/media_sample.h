#pragma once

#include <algorithm>
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <deque>
#include <stack>
#include <utility>
#include <optional>
#include <type_traits>
#include <condition_variable>
#include <functional>
#include <stdint.h>
#include <d3d11.h>
#include <d2d1_1.h>
#include <dxgi1_2.h>
#include <atlbase.h>
#include <mfidl.h>
#include <mfapi.h>
#include "assert.h"
#include "enable_shared_from_this.h"
#include "buffer_pool.h"

#pragma comment(lib, "Dxgi.lib")

/*

input media samples are assumed to be immutable;
violation of the assumption makes the pipeline not work

*/

#undef max
#undef min

#ifndef _WIN64
#pragma message("--------- WARNING: x86 target is untested ---------")
#endif

#define SECOND_IN_TIME_UNIT 10000000

//extern const GUID media_sample_lifetime_tracker_guid;

// 100 nanosecond = 1 time_unit
typedef int64_t time_unit;
// frame unit is used to accurately represent a frame position
// relative to the time source
typedef int64_t frame_unit;

constexpr time_unit time_unit_invalid = std::numeric_limits<time_unit>::min();

frame_unit convert_to_frame_unit(time_unit, frame_unit frame_rate_num, frame_unit frame_rate_den);
time_unit convert_to_time_unit(frame_unit, frame_unit frame_rate_num, frame_unit frame_rate_den);

// it should be ensured that the buffer isn't released before enddraw has been called on the
// device context
class media_buffer_texture : public buffer_poolable
{
    friend class buffer_pooled<media_buffer_texture>;
private:
    bool managed_by_this;
protected:
    // TODO: idxgiresource should be cached;
    // if a class is derived from media_buffer_texture, its uninitialize method
    // must call this
    virtual void uninitialize();
public:
    // TODO: the memory buffer should be a derived class of this defined in h264 encoder;
    // rename the texture_buffer to memory_buffer
    DWORD texture_buffer_length;
    std::unique_ptr<BYTE[]> texture_buffer;

    // TODO: remove this; references to mf_sample are commented out in media_sample.cpp
    /*CComPtr<IMFSample> mf_sample;*/
    // TODO: this bitmap should be set in displaycapture aswell so that videomixer
    // doesn't need to create it for every input texture
    CComPtr<ID2D1Bitmap1> bitmap;
    // texture must not be null
    CComPtr<ID3D11Texture2D> texture;
    media_buffer_texture() : managed_by_this(true) {}
    virtual ~media_buffer_texture() {}

    // TODO: if the settings do not match, a new texture should be created
    // and the mismatch should be printed
    // currently, initialize doesn't initialize the bitmap;
    // reinitialize flag is used for resetting the texture data
    void initialize(const CComPtr<ID3D11Device>&,
        const D3D11_TEXTURE2D_DESC&, const D3D11_SUBRESOURCE_DATA*, bool reinitialize = false);
    // the provided texture must not be managed by a media_buffer_texture instance;
    // texture must not be null
    // TODO: this should be defined in a derived class of this, so that
    // wrapped textures aren't part of the same pool as managed textures
    void initialize(const CComPtr<ID3D11Texture2D>&);
};

typedef std::shared_ptr<media_buffer_texture> media_buffer_texture_t;
typedef buffer_pooled<media_buffer_texture> media_buffer_pooled_texture;
typedef std::shared_ptr<media_buffer_pooled_texture> media_buffer_pooled_texture_t;

// set to imfsample to ensure that the sample isn't recycled before imfsample has been released;
// the tracker must be manually removed from the sample
//CComPtr<IUnknown> create_lifetime_tracker(const media_buffer_t&);

class media_buffer_memory : public buffer_poolable
{
    friend class buffer_pooled<media_buffer_memory>;
private:
    void uninitialize() {this->buffer_poolable::uninitialize();}
public:
    // the buffer is readonly
    // TODO: decide if should make this private
    CComPtr<IMFMediaBuffer> buffer;

    virtual ~media_buffer_memory() {}

    // methods for poolable samples;
    // alignment isn't passed; it should be the lowest common divisor
    void initialize(DWORD len);
};

typedef std::shared_ptr<media_buffer_memory> media_buffer_memory_t;
typedef buffer_pooled<media_buffer_memory> media_buffer_memory_pooled;
typedef std::shared_ptr<media_buffer_memory_pooled> media_buffer_memory_pooled_t;

class media_sample_audio_consecutive_frames
{
public:
    frame_unit pos, dur;
    // null buffer indicates a silent frame
    // a wrapper of the original buffer(or the original buffer)
    CComPtr<IMFMediaBuffer> buffer;
    // hosts the memory of the buffer; resetting this causes the buffer
    // become invalid
    media_buffer_memory_t memory_host;

    media_sample_audio_consecutive_frames() : dur(0) {}
};

template<typename FrameType, typename FrameCollection = std::vector<FrameType>>
class media_sample_frames_template : public buffer_poolable
{
    template<typename T> friend class buffer_pooled;
public:
    using sample_t = FrameType;
    using samples_t = FrameCollection;
    static const frame_unit undef_end = std::numeric_limits<frame_unit>::min(),
        undef_first = std::numeric_limits<frame_unit>::max();
private:
    void uninitialize() override;
protected:
    samples_t frames;
    frame_unit end, first;
public:
    media_sample_frames_template() : end(undef_end), first(undef_first) {}
    media_sample_frames_template(const media_sample_frames_template&) = delete;
    media_sample_frames_template& operator=(const media_sample_frames_template&) = delete;
    virtual ~media_sample_frames_template() {}

    // end is the max (pos + dur) of frames;
    // assert failure if is_valid() == false
    frame_unit get_end() const { assert_(this->is_valid()); return this->end; }
    // assert failure if is_valid() == false
    frame_unit get_first() const { assert_(this->is_valid()); return this->first; }
    const samples_t& get_frames() const { return this->frames; }

    sample_t& add_consecutive_frames(const sample_t&);

    void initialize();
    void initialize(const media_sample_frames_template& other);
    // it should be ensured that the end and first are valid for the sample collection;
    // sample collection must not be empty
    void initialize(samples_t&& sample_collection, frame_unit first, frame_unit end);

    // empty frame collection is not valid
    bool is_valid() const { return !this->frames.empty(); }
};

// frametype should be either media_sample_audio_consecutive_frames or a derived type of it
template<typename FrameType>
class media_sample_audio_frames_template : public media_sample_frames_template<FrameType>
{
public:
    using media_sample_frames_template = media_sample_frames_template<FrameType>;
    using sample_t = typename media_sample_frames_template::sample_t;
    using media_sample_frames_template::undef_end;
    using media_sample_frames_template::undef_first;
public:
    virtual ~media_sample_audio_frames_template() {}

    // moves part from this to 'to'
    // returns whether any frames were moved;
    // to can be null, in which case the contents from this are discarded;
    // std::numeric_limits::max can be used for moving all frames;
    // block align is assumed to be the same for both samples
    bool move_frames_to(media_sample_audio_frames_template* to, frame_unit end, UINT32 block_align);
};

typedef media_sample_audio_frames_template<media_sample_audio_consecutive_frames> 
media_sample_audio_frames;
typedef std::shared_ptr<media_sample_audio_frames> media_sample_audio_frames_t;
typedef buffer_pooled<media_sample_audio_frames> media_sample_audio_frames_pooled;
typedef std::shared_ptr<media_sample_audio_frames_pooled> media_sample_audio_frames_pooled_t;

class media_sample_video_frame
{
public:
    frame_unit pos, dur;
    // null buffer indicates a silent frame
    media_buffer_texture_t buffer;

    media_sample_video_frame() : dur(0) {}
    // TODO: remove this
    explicit media_sample_video_frame(frame_unit pos) : pos(pos), dur(1) {}

    frame_unit end() const {assert_(this->dur > 0); return this->pos + this->dur;}
};

// TODO: immutability of samples should be enforced by having const samples in arg structs

// frametype should be either media_sample_video_frame or a derived type of it
template<typename FrameType>
class media_sample_video_frames_template : 
    // TODO: use vector
    public media_sample_frames_template<FrameType, std::deque<FrameType>>
{
public:
    using media_sample_frames_template = 
        media_sample_frames_template<FrameType, std::deque<FrameType>>;
    using sample_t = typename media_sample_frames_template::sample_t;
    using media_sample_frames_template::undef_end;
    using media_sample_frames_template::undef_first;
public:
    virtual ~media_sample_video_frames_template() {}
    bool move_frames_to(media_sample_video_frames_template* to, frame_unit end);
};

typedef media_sample_video_frames_template<media_sample_video_frame> media_sample_video_frames;
typedef std::shared_ptr<media_sample_video_frames> media_sample_video_frames_t;
typedef buffer_pooled<media_sample_video_frames> media_sample_video_frames_pooled;
typedef std::shared_ptr<media_sample_video_frames_pooled> media_sample_video_frames_pooled_t;

class media_sample_h264_frame
{
public:
    time_unit ts, dur;
    CComPtr<IMFSample> sample;
};

class media_sample_h264_frames : public buffer_poolable
{
    friend class buffer_pooled<media_sample_h264_frames>;
private:
    void uninitialize() {this->frames.clear(); this->buffer_poolable::uninitialize();}
public:
    std::deque<media_sample_h264_frame> frames;

    virtual ~media_sample_h264_frames() {}

    void initialize() {assert_(this->frames.empty()); this->buffer_poolable::initialize();}
};

typedef std::shared_ptr<media_sample_h264_frames> media_sample_h264_frames_t;
typedef buffer_pooled<media_sample_h264_frames> media_sample_h264_frames_pooled;
typedef std::shared_ptr<media_sample_h264_frames_pooled> media_sample_h264_frames_pooled_t;

class media_sample_aac_frame
{
public:
    // time unit is used because the media foundation aac encoder returns timestamps
    time_unit ts, dur;
    CComPtr<IMFMediaBuffer> buffer;
    media_buffer_memory_t memory_host;
    CComPtr<IMFSample> sample;
};

class media_sample_aac_frames : public buffer_poolable
{
    // this is very similar to media_sample_audio_frames
    friend class buffer_pooled<media_sample_aac_frames>;
private:
    // called when the buffer is moved back to pool and just before being destroyed
    // TODO: decide if should call reserve here
    void uninitialize() {this->frames.clear(); this->buffer_poolable::uninitialize();}
public:
    std::deque<media_sample_aac_frame> frames;

    virtual ~media_sample_aac_frames() {}

    // buffer pool methods
    void initialize() {assert_(this->frames.empty()); this->buffer_poolable::initialize();}
};

typedef std::shared_ptr<media_sample_aac_frames> media_sample_aac_frames_t;
typedef buffer_pooled<media_sample_aac_frames> media_sample_aac_frames_pooled;
typedef std::shared_ptr<media_sample_aac_frames_pooled> media_sample_aac_frames_pooled_t;

//    // TODO: the session could have properties, which would include the frame rate(or the clock);
//    // additional properties(canvas resolution etc) could be accessed from the control pipeline;
//    // for audio, channel count and bit depth should be bound to media session aswell for
//    // simplicity

// TODO: optional typedefs should be removed after the mixer uses optionals only internally

class media_component_args
{
public:
};

// arg type for components that expect uncompressed audio or video samples
class media_component_frame_args : public media_component_args
{
public:
    // frame_end is greater or equal to the end of the data in the sample
    frame_unit frame_end;
};

typedef std::optional<media_component_frame_args> media_component_frame_args_t;

class media_component_video_args : public media_component_frame_args
{
public:
    // if the sample is non-null, it must not be empty;
    // null buffer frames are silent
    media_sample_video_frames_t sample;
};

typedef std::optional<media_component_video_args> media_component_video_args_t;

class media_component_h264_encoder_args : public media_component_args
{
public:
    // must not be null;
    // frames must be ordered;
    // null buffer frames are simply discarded
    media_sample_video_frames_t sample;
    bool has_frames;
    bool is_valid() const {return !!this->sample;}
};

typedef std::optional<media_component_h264_encoder_args> media_component_h264_encoder_args_t;

// args for components that expect h264 video data
class media_component_h264_video_args : public media_component_args
{
public:
    // must not be null
    media_sample_h264_frames_t sample;
    // for debugging
    bool software;
};

typedef std::optional<media_component_h264_video_args> media_component_h264_video_args_t;

class media_component_audio_args : public media_component_frame_args
{
public:
    // if the sample is non-null, it must not be empty;
    // null buffer frames are silent
    media_sample_audio_frames_t sample;
};

typedef std::optional<media_component_audio_args> media_component_audio_args_t;

class media_component_aac_encoder_args : public media_component_args
{
public:
    // must not be null;
    // frames must be ordered
    media_sample_audio_frames_t sample;
    bool has_frames;
    bool is_valid() const {return !!this->sample;}
};

typedef std::optional<media_component_aac_encoder_args> media_component_aac_encoder_args_t;

// args for components that expect aac audio data
class media_component_aac_audio_args : public media_component_args
{
public:
    // must not be null
    media_sample_aac_frames_t sample;
};

typedef std::optional<media_component_aac_audio_args> media_component_aac_audio_args_t;


/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////


#define CHECK_HR(hr_) {if(FAILED(hr_)) [[unlikely]] {goto done;}}

template<typename T, typename U>
typename media_sample_frames_template<T, U>::sample_t&
media_sample_frames_template<T, U>::add_consecutive_frames(const sample_t& new_frame)
{
    assert_(new_frame.dur > 0);

    this->frames.push_back(new_frame);
    this->end = std::max(new_frame.pos + new_frame.dur, this->end);
    this->first = std::min(new_frame.pos, this->first);

    return this->frames.back();
}

template<typename T, typename U>
void media_sample_frames_template<T, U>::uninitialize()
{
    this->frames.clear();
    this->end = undef_end;
    this->first = undef_first;

    this->buffer_poolable::uninitialize();
}

template<typename T, typename U>
void media_sample_frames_template<T, U>::initialize()
{
    assert_(this->end == undef_end);
    assert_(this->first == undef_first);
    assert_(this->frames.empty());

    this->buffer_poolable::initialize();
}

template<typename T, typename U>
void media_sample_frames_template<T, U>::initialize(const media_sample_frames_template& other)
{
    this->initialize();

    this->frames = other.frames;
    this->end = other.end;
    this->first = other.first;
}

template<typename T, typename U>
void media_sample_frames_template<T, U>::initialize(
    samples_t&& sample_collection, frame_unit first, frame_unit end)
{
    assert_(!sample_collection.empty());

    this->initialize();

    this->frames = std::move(sample_collection);
    this->end = end;
    this->first = first;

#ifdef _DEBUG
    // check that the passed end and first args are valid
    frame_unit collection_end = undef_end,
        collection_first = undef_first;
    for(auto&& item : this->frames)
    {
        collection_end = std::max(collection_end, item.pos + item.dur);
        collection_first = std::min(collection_first, item.pos);
    }

    assert_(end == collection_end);
    assert_(first == collection_first);
#endif
}


/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////


template<typename T>
bool media_sample_audio_frames_template<T>::move_frames_to(
    media_sample_audio_frames_template* to, frame_unit end, UINT32 block_align)
{
    assert_(this->end == undef_end || !this->frames.empty());

    if(end <= this->first)
        return false;

    bool moved = false;
    this->first = undef_first;

    // the media_sample_audio_consecutive_frames should be lightweight, because it seems that
    // the value is moved within the container
    // https://en.wikipedia.org/wiki/Erase%E2%80%93remove_idiom
    this->frames.erase(std::remove_if(this->frames.begin(), this->frames.end(),
        [&](sample_t& elem)
    {
        assert_(elem.dur > 0);

        // TODO: sample could have a flag that indicates whether the frames are ordered,
        // so that the whole list doesn't need to be iterated;
        // this sample would retain the ordered flag only if this was null when
        // frames were moved
        if(elem.pos >= end)
        {
            this->first = std::min(this->first, elem.pos);
            return false;
        }

        moved = true;

        HRESULT hr = S_OK;
        bool remove = false;
        CComPtr<IMFSample> sample;
        CComPtr<IMFMediaBuffer> buffer, old_buffer;
        DWORD buflen = 0;
        sample_t new_frames = elem;

        const frame_unit frame_pos = elem.pos;
        const frame_unit frame_dur = elem.dur;
        const frame_unit frame_end = frame_pos + frame_dur;

        const frame_unit frame_diff_end = std::max(frame_end - end, 0LL);
        const DWORD offset_end = (DWORD)frame_diff_end * block_align;
        const frame_unit new_frame_pos = frame_pos;
        const frame_unit new_frame_dur = frame_dur - frame_diff_end;

        if(elem.buffer)
        {
            old_buffer = elem.buffer;
            CHECK_HR(hr = old_buffer->GetCurrentLength(&buflen));

            assert_(((int)buflen - (int)offset_end) > 0);
            CHECK_HR(hr = MFCreateMediaBufferWrapper(old_buffer, 0, buflen - offset_end, &buffer));
            CHECK_HR(hr = buffer->SetCurrentLength(buflen - offset_end));
        }

        if(offset_end > 0)
        {
            if(elem.buffer)
            {
                // remove the moved part of the old buffer
                CComPtr<IMFMediaBuffer> new_buffer;
                CHECK_HR(hr = MFCreateMediaBufferWrapper(
                    old_buffer, buflen - offset_end, offset_end, &new_buffer));
                CHECK_HR(hr = new_buffer->SetCurrentLength(offset_end));
                elem.buffer = new_buffer;
            }

            const frame_unit new_frame_dur = offset_end / block_align;
            const frame_unit new_frame_pos = frame_pos + frame_dur - new_frame_dur;

            elem.pos = new_frame_pos;
            elem.dur = new_frame_dur;

            this->first = std::min(this->first, elem.pos);
        }
        else
            remove = true;

        assert_((elem.memory_host && buffer) || (!elem.memory_host && !elem.buffer));
        new_frames.memory_host = elem.memory_host;
        new_frames.buffer = buffer;
        new_frames.pos = new_frame_pos;
        new_frames.dur = new_frame_dur;

        if(to)
        {
            to->frames.push_back(new_frames);
            to->end = std::max(to->end, new_frames.pos + new_frames.dur);
            to->first = std::min(to->first, new_frames.pos);
        }

    done:
        if(FAILED(hr))
            throw HR_EXCEPTION(hr);

        return remove;

    }), this->frames.end());

    assert_(this->is_valid() || end >= this->end);
    if(!this->is_valid())
    {
        assert_(this->frames.empty());
        this->end = undef_end;
        this->first = undef_first;
    }

    return moved;
}


/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////


template<typename T>
bool media_sample_video_frames_template<T>::move_frames_to(
    media_sample_video_frames_template* to, frame_unit end)
{
    assert_(this->end == undef_end || !this->frames.empty());

    if(end <= this->first)
        return false;

    bool moved = false;
    this->first = undef_first;

    // the media_sample_video_frame should be lightweight, because it seems that
    // the value is moved within the container
    // https://en.wikipedia.org/wiki/Erase%E2%80%93remove_idiom
    this->frames.erase(std::remove_if(this->frames.begin(), this->frames.end(), 
        [&](sample_t& elem)
    {
        assert_(elem.dur > 0);

        // TODO: sample could have a flag that indicates whether the frames are ordered,
        // so that the whole list doesn't need to be iterated;
        // this sample would retain the ordered flag only if this was null when
        // frames were moved
        if(elem.pos >= end)
        {
            this->first = std::min(this->first, elem.pos);
            return false;
        }

        moved = true;

        bool remove = false;
        sample_t new_frame = elem;

        if(end >= (elem.pos + elem.dur))
        {
            new_frame.dur = elem.dur;
            remove = true;
        }
        else
        {
            new_frame.dur = end - new_frame.pos;
            elem.dur = (elem.pos + elem.dur) - end;
            elem.pos = end;

            this->first = std::min(this->first, elem.pos);
        }

        if(to)
        {
            to->frames.push_back(new_frame);
            to->end = std::max(to->end, new_frame.pos + new_frame.dur);
            to->first = std::min(to->first, new_frame.pos);
        }

        return remove;
    }), this->frames.end());

    assert_(this->is_valid() || end >= this->end);
    if(!this->is_valid())
    {
        this->end = undef_end;
        this->first = undef_first;
    }

    return moved;
}


#undef CHECK_HR