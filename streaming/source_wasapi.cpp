#include "source_wasapi.h"
#include "assert.h"
#include "control_pipeline.h"
#include "transform_aac_encoder.h"
#include <initguid.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <Mferror.h>
#include <iostream>
#include <limits>
#include <cmath>

#undef max
#undef min

#define CHECK_HR(hr_) {if(FAILED(hr_)) [[unlikely]] {goto done;}}

// TODO: this->captured_audio doesn't need to be a pointer

source_wasapi::source_wasapi(const media_session_t& session) :
    source_base(session),
    started(false), capture(false), in_wait_queue(false),
    native_frame_base(std::numeric_limits<frame_unit>::min()),
    set_new_frame_base(true),
    next_frame_position(std::numeric_limits<frame_unit>::min()),
    buffer_pool_memory(new buffer_pool_memory_t),
    buffer_pool_audio_frames(new buffer_pool_audio_frames_t),
    captured_audio(new media_sample_audio_mixer_frames),
    sine_wave_counter(0.0)
{
    // the capture callback really cannot be in higher priority mode,
    // because it can cause problems with the work queue under a high load
    this->capture_callback.Attach(new async_callback_t(&source_wasapi::capture_cb));
}

source_wasapi::~source_wasapi()
{
    {
        buffer_pool_audio_frames_t::scoped_lock lock(this->buffer_pool_audio_frames->mutex);
        this->buffer_pool_audio_frames->dispose();
    }
    {
        buffer_pool_memory_t::scoped_lock lock(this->buffer_pool_memory->mutex);
        this->buffer_pool_memory->dispose();
    }

    std::cout << "stopping wasapi..." << std::endl;

    HRESULT hr = S_OK;

    // stop the wait queue loop
    if(this->in_wait_queue)
        hr = MFCancelWorkItem(this->capture_work_key);

    if(this->started)
    {
        hr = this->audio_client->Stop();
        if(!this->capture)
            hr = this->audio_client_render->Stop();
    }
}

source_wasapi::stream_source_base_t source_wasapi::create_derived_stream()
{
    return stream_wasapi_t(new stream_wasapi(this->shared_from_this<source_wasapi>()));
}

bool source_wasapi::get_samples_end(time_unit /*request_time*/, frame_unit& end) const
{
    scoped_lock lock(this->captured_audio_mutex);
    if(!this->captured_audio->is_valid() || this->captured_audio->get_frames().empty())
        return false;

    end = this->captured_audio->get_end();
    return true;
}

void source_wasapi::make_request(request_t& request, frame_unit frame_end)
{
    request.sample.args = std::make_optional<media_component_audiomixer_args>();
    media_component_audiomixer_args& args = *request.sample.args;

    media_sample_audio_mixer_frames_t captured_audio;
    if(!args.sample)
    {
        buffer_pool_audio_frames_t::scoped_lock lock(this->buffer_pool_audio_frames->mutex);
        captured_audio = this->buffer_pool_audio_frames->acquire_buffer();
        captured_audio->initialize();
    }
    else
        captured_audio = args.sample;

    scoped_lock lock(this->captured_audio_mutex);
    const bool moved = this->captured_audio->move_frames_to(captured_audio.get(), frame_end,
        this->resampled_block_align);

    args.frame_end = frame_end;
    // frames are simply skipped if there is no sample for the args
    if(moved)
    {
        args.sample = std::move(captured_audio);
        // the sample must be valid
        assert_(args.sample->is_valid());
    }
}

void source_wasapi::dispatch(request_t& request)
{
    this->session->give_sample(request.stream, request.sample.args.has_value() ?
        &(*request.sample.args) : NULL, request.rp);
}

HRESULT source_wasapi::queue_new_capture()
{
    HRESULT hr = S_OK;

    CComPtr<IMFAsyncResult> result;
    CHECK_HR(hr = this->capture_callback->mf_schedule_work_item(
        this->shared_from_this<source_wasapi>(),
        capture_interval_ms, &this->capture_work_key));

    this->in_wait_queue = true;

done:
    return hr;
}

void source_wasapi::sine_wave(BYTE* data, DWORD len)
{
    float* audio_data = (float*)data;
    for(UINT32 i = 0; i < len / sizeof(float); i += this->channels)
    {
        for(UINT32 j = 0; j < this->channels; j++)
            audio_data[i + j] = (float)(sin(this->sine_wave_counter) * 0.2);

        this->sine_wave_counter += 0.1;
    }
}

void source_wasapi::capture_cb(void*)
{
    HRESULT hr = S_OK;

    // capture_cb must be single threaded

    // nextpacketsize and frames are equal
    UINT32 nextpacketsize = 0, returned_frames = 0;
    UINT64 returned_devposition;
    bool getbuffer = false;

    while(SUCCEEDED(hr = this->audio_capture_client->GetNextPacketSize(&nextpacketsize)) &&
        nextpacketsize)
    {
        BYTE* data;
        DWORD flags;
        UINT64 first_sample_timestamp;
        UINT64 devposition;
        UINT32 frames;
        media_buffer_memory_t buffer;
        const frame_unit old_next_frame_position = this->next_frame_position;
        bool silent = false;
        bool drain = false;

        // no excessive delay should happen between getbuffer and releasebuffer calls
        CHECK_HR(hr = this->audio_capture_client->GetBuffer(
            &data, &returned_frames, &flags, &returned_devposition, &first_sample_timestamp));
        getbuffer = true;
        // try fetch a next packet if no frames were returned
        // or if the frames were already returned;
        // in case of a ts error, the buffer is of little use
        // TODO: the sys time could be used for ts error
        if(returned_frames == 0 || (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR))
        {
            if(flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)
            {
                this->set_new_frame_base = true;
                std::cout << "TIMESTAMP ERROR" << std::endl;
            }

            getbuffer = false;
            CHECK_HR(hr = this->audio_capture_client->ReleaseBuffer(returned_frames));
            continue;
        }

        frames = returned_frames;
        devposition = returned_devposition;

        if((flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) || this->set_new_frame_base)
        {
            std::cout << "DATA DISCONTINUITY, " << devposition << ", "
                << devposition + frames << std::endl;

            media_clock_t clock = this->session->get_clock();
            if(!clock)
            {
                std::cout << "time source was not initialized" << std::endl;
                this->set_new_frame_base = true;
                goto done;
            }

            time_unit first_sample_timestamp_time_unit =
                clock->system_time_to_clock_time((LONGLONG)first_sample_timestamp);
            const time_unit cur_time = clock->get_current_time();

            // just ignore device time if it's drifting more than 1 second
            // TODO: make this same adjustment in source vidcap
            if(std::abs(cur_time - first_sample_timestamp_time_unit) > MAX_TS_DIFF)
            {
                std::cout << "source_wasapi timestamps drifting more than 1 second, "
                    "ignoring device time" << std::endl;
                first_sample_timestamp_time_unit = cur_time;
            }

            // calculate the new sample base from the timestamp
            this->native_frame_base = convert_to_frame_unit(
                first_sample_timestamp_time_unit,
                this->samples_per_second, 1);

            // set new base
            this->set_new_frame_base = false;
            this->next_frame_position =
                (frame_unit)((double)this->session->frame_rate_num /
                    this->samples_per_second * this->native_frame_base);

            drain = true;
        }
        if(flags & AUDCLNT_BUFFERFLAGS_SILENT)
            silent = true;
        // if(!flags) ok

        const DWORD len = frames * this->block_align;
        {
            buffer_pool_memory_t::scoped_lock lock(this->buffer_pool_memory->mutex);
            buffer = this->buffer_pool_memory->acquire_buffer();
            buffer->initialize(len);
        }

        // convert and copy to buffer
        BYTE* buffer_data;
        CHECK_HR(hr = buffer->buffer->Lock(&buffer_data, NULL, NULL));
        /*this->sine_wave(buffer_data, len);*/
        if(silent)
            memset(buffer_data, 0, len);
        else
            memcpy(buffer_data, data, len);
        CHECK_HR(hr = buffer->buffer->Unlock());
        CHECK_HR(hr = buffer->buffer->SetCurrentLength(len));
        // release buffer
        getbuffer = false;
        CHECK_HR(hr = this->audio_capture_client->ReleaseBuffer(returned_frames));

        // TODO: decide if should resample the wasapi buffer without copying it;
        // that would eliminate the need for memory buffer pool in wasapi;
        // (actually, not possible since the resampler might hold onto the buffers for longer)
        // it seems though that the media foundation doesn't have a media buffer that wraps
        // a raw buffer

        // TODO: resampling could take place after the loop(might not be possible)
        // add the new sample to the audio sample
        {
            scoped_lock lock(this->captured_audio_mutex);
            assert_(this->captured_audio);

            if(drain)
            {
                // TODO: decide if the resampler should just discard the drained data;
                // it might help masking the audio glitch on data discontinuity
                media_sample_audio_mixer_frame null_frames;
                this->resampler.resample(old_next_frame_position, null_frames,
                    *this->captured_audio, true);
            }

            media_sample_audio_mixer_frame frames;
            frames.memory_host = buffer;
            frames.pos = 0;
            frames.dur = returned_frames;
            frames.buffer = buffer->buffer;
            this->next_frame_position +=
                this->resampler.resample(this->next_frame_position, frames,
                    *this->captured_audio, false);

            // keep the buffer within the limits
            if(this->captured_audio->is_valid() && this->captured_audio->move_frames_to(
                NULL, this->captured_audio->get_end() - this->get_maximum_buffer_size(), 
                this->resampled_block_align))
            {
                std::cout << "source_wasapi buffer limit reached, excess frames discarded" << std::endl;
            }
        }
    }

done:
    if(getbuffer)
    {
        getbuffer = false;
        this->audio_capture_client->ReleaseBuffer(returned_frames);
    }
    if(FAILED(hr))
    {
        if(hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == AUDCLNT_E_SERVICE_NOT_RUNNING)
        {
            if(!this->is_broken())
                std::cout << "error code: " << std::hex << "0x" << hr << std::dec << std::endl;
            this->set_broken();
        }
        else
            throw HR_EXCEPTION(hr);
    }

    if(FAILED(hr = this->queue_new_capture()) && hr != MF_E_SHUTDOWN)
        throw HR_EXCEPTION(hr);
}

HRESULT source_wasapi::play_silence()
{
    HRESULT hr = S_OK;
    UINT32 frames_padding;
    LPBYTE data;

    CHECK_HR(hr = this->audio_client_render->GetCurrentPadding(&frames_padding));
    CHECK_HR(hr = this->audio_render_client->GetBuffer(
        this->render_buffer_frame_count - frames_padding, &data));
    CHECK_HR(hr = this->audio_render_client->ReleaseBuffer(
        this->render_buffer_frame_count - frames_padding, AUDCLNT_BUFFERFLAGS_SILENT));

done:
    return hr;
}

HRESULT source_wasapi::initialize_render(IMMDevice* device, WAVEFORMATEX* engine_format)
{
    HRESULT hr = S_OK;
    LPBYTE data;

    CHECK_HR(hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL,
        (void**)&this->audio_client_render));
    CHECK_HR(hr = this->audio_client_render->Initialize(
        AUDCLNT_SHAREMODE_SHARED, 0, 0, 0, engine_format, NULL));

    CHECK_HR(hr = this->audio_client_render->GetBufferSize(&this->render_buffer_frame_count));
    CHECK_HR(hr = this->audio_client_render->GetService(
        __uuidof(IAudioRenderClient), (void**)&this->audio_render_client));
    CHECK_HR(hr = this->audio_render_client->GetBuffer(this->render_buffer_frame_count, &data));
    CHECK_HR(hr = this->audio_render_client->ReleaseBuffer(
        this->render_buffer_frame_count, AUDCLNT_BUFFERFLAGS_SILENT));

done:
    return hr;
}

HRESULT source_wasapi::create_waveformat_type(WAVEFORMATEX* format)
{
    HRESULT hr = S_OK;

    UINT32 size = sizeof(WAVEFORMATEX);
    if(format->wFormatTag != WAVE_FORMAT_PCM)
        size += format->cbSize;

    CHECK_HR(hr = MFCreateMediaType(&this->waveformat_type));
    CHECK_HR(hr = MFInitMediaTypeFromWaveFormatEx(this->waveformat_type, format, size));

done:
    return hr;
}

void source_wasapi::initialize(const control_class_t& ctrl_pipeline,
    const std::wstring& device_id, bool capture)
{
    HRESULT hr = S_OK;

    this->source_base::initialize(ctrl_pipeline);

    CComPtr<IMMDeviceEnumerator> enumerator;
    CComPtr<IMMDevice> device;
    WAVEFORMATEX* engine_format = NULL;
    UINT32 buffer_frame_count;
    REFERENCE_TIME def_device_period, min_device_period;

    this->capture = capture;

    CHECK_HR(hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
        (void**)&enumerator));
    CHECK_HR(hr = enumerator->GetDevice(device_id.c_str(), &device));

    CHECK_HR(hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL,
        (void**)&this->audio_client));
    CHECK_HR(hr = this->audio_client->GetMixFormat(&engine_format));

    CHECK_HR(hr = this->audio_client->Initialize(
        AUDCLNT_SHAREMODE_SHARED, this->capture ? 0 : AUDCLNT_STREAMFLAGS_LOOPBACK,
        CAPTURE_BUFFER_DURATION, 0, engine_format, NULL));

    CHECK_HR(hr = this->audio_client->GetBufferSize(&buffer_frame_count));

    CHECK_HR(hr = this->audio_client->GetService(
        __uuidof(IAudioCaptureClient), (void**)&this->audio_capture_client));

    CHECK_HR(hr = this->audio_client->GetDevicePeriod(&def_device_period, &min_device_period));
    assert_(def_device_period < SECOND_IN_TIME_UNIT / 1000 * capture_interval_ms);

    /*
    In Windows 8, the first use of IAudioClient to access the audio device should be
    on the STA thread. Calls from an MTA thread may result in undefined behavior.
    */

    // create waveformat mediatype
    CHECK_HR(hr = this->create_waveformat_type((WAVEFORMATEX*)engine_format));
    CHECK_HR(hr = this->waveformat_type->GetUINT32(
        MF_MT_AUDIO_NUM_CHANNELS, &this->channels));

    // get samples per second
    CHECK_HR(hr =
        this->waveformat_type->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &this->samples_per_second));
    // set block align
    CHECK_HR(hr = this->waveformat_type->GetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, &this->block_align));
    // calculate the actual duration of the allocated buffer
    this->buffer_actual_duration = (REFERENCE_TIME)
        ((double)SECOND_IN_TIME_UNIT * buffer_frame_count / this->samples_per_second);

    // initialize silence fix
    // (https://github.com/jp9000/obs-studio/blob/master/plugins/win-wasapi/win-wasapi.cpp#L199)
    if(!this->capture)
        CHECK_HR(hr = this->initialize_render(device, engine_format));

    this->resampled_block_align = transform_audiomixer2::block_align;

    // TODO: exception thrown here causes memory leak
    // initialize resampler
    this->resampler.initialize(
        (UINT32)this->session->frame_rate_num, transform_aac_encoder::channels,
        transform_audiomixer2::bit_depth,
        this->samples_per_second, this->channels, sizeof(bit_depth_t) * 8);

    // start capturing
    if(!this->capture)
        CHECK_HR(hr = this->audio_client_render->Start());
    CHECK_HR(hr = this->audio_client->Start());
    this->started = true;

done:
    if(engine_format)
        CoTaskMemFree(engine_format);
    if(FAILED(hr))
        throw HR_EXCEPTION(hr);
}


/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////


stream_wasapi::stream_wasapi(const source_wasapi_t& source) :
    stream_source_base(source),
    source(source)
{
}

void stream_wasapi::on_component_start(time_unit)
{
    HRESULT hr = S_OK;
    CHECK_HR(hr = this->source->queue_new_capture());

done:
    if(FAILED(hr) && hr != MF_E_SHUTDOWN)
        throw HR_EXCEPTION(hr);
}