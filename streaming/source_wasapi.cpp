#include "source_wasapi.h"
#include "assert.h"
#include <initguid.h>
#include <mmdeviceapi.h>
#include <Mferror.h>
#include <iostream>
#include <limits>
#include <cmath>

#undef max
#undef min

#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000
#define AUTOCONVERT_PCM (0)
#define CHECK_HR(hr_) {if(FAILED(hr_)) goto done;}
//static void CHECK_HR(HRESULT hr)
//{
//    if(FAILED(hr))
//        throw std::exception();
//}

void source_wasapi::make_silence(UINT32 frames, UINT32 channels, bit_depth_t* buffer)
{
    const UINT32 samples = frames * channels;
    for(UINT32 i = 0; i < samples; i++)
        buffer[i] = 0.f;
}

source_wasapi::source_wasapi(const media_session_t& session) : 
    media_source(session), started(false), 
    generate_sine(false), sine_var(0),
    consumed_samples_end(std::numeric_limits<frame_unit>::min()),
    next_frame_position(std::numeric_limits<frame_unit>::min()),
    capture(false),
    frame_base(std::numeric_limits<frame_unit>::min()),
    devposition_base(std::numeric_limits<frame_unit>::min())
{
    HRESULT hr = S_OK;
    DWORD task_id;
    CHECK_HR(hr = MFLockSharedWorkQueue(L"Capture", 0, &task_id, &this->work_queue_id));

    this->process_callback.Attach(new async_callback_t(&source_wasapi::process_cb, this->work_queue_id));
    this->serve_callback.Attach(new async_callback_t(&source_wasapi::serve_cb));

    /*this->stream_base.time = this->stream_base.sample = -1;*/

done:
    if(FAILED(hr))
        throw std::exception();
}

source_wasapi::~source_wasapi()
{
    if(this->started)
    {
        this->audio_client->Stop();
        if(!this->capture)
            this->audio_client_render->Stop();
    }
    MFUnlockWorkQueue(this->work_queue_id);
}

HRESULT source_wasapi::add_event_to_wait_queue()
{
    HRESULT hr = S_OK;

    CComPtr<IMFAsyncResult> asyncresult;
    CHECK_HR(hr = MFCreateAsyncResult(NULL, &this->process_callback->native, NULL, &asyncresult));
    // use the priority of 1 for audio
    CHECK_HR(hr = this->process_callback->mf_put_waiting_work_item(
        this->shared_from_this<source_wasapi>(),
        this->process_event, 1, asyncresult, &this->callback_key));

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

void source_wasapi::serve_cb(void*)
{
    std::unique_lock<std::recursive_mutex> lock(this->serve_mutex, std::try_to_lock);
    if(!lock.owns_lock())
        return;

    // splice the raw buffer to the cached buffer(O(1) operation);
    // raw buffer becomes empty
    {
        scoped_lock lock(this->raw_buffer_mutex), lock2(this->buffer_mutex);
        this->buffer.splice(this->buffer.end(), this->raw_buffer);
    }

    // try to serve
    HRESULT hr = S_OK;
    request_t request;
    while(this->requests.get(request))
    {
        // samples are collected up to the request time;
        // sample that goes over the request time will not be collected;
        // cutting operation is preferred so that when changing scene the audio plays
        // up to the switch point

        bool dispatch = false;
        // use the buffer allocated in request stream for the audio sample
        stream_wasapi* stream = reinterpret_cast<stream_wasapi*>(request.stream);
        /*media_sample_audio audio(media_buffer_samples_t(new media_buffer_samples));*/
        stream->audio_buffer->samples.clear();
        media_sample_audio audio(stream->audio_buffer);
        audio.bit_depth = sizeof(bit_depth_t) * 8;
        audio.channels = this->channels;
        audio.sample_rate = this->samples_per_second;

        const double sample_duration = SECOND_IN_TIME_UNIT / (double)this->samples_per_second;
        const frame_unit request_end = (frame_unit)(request.rp.request_time / sample_duration);

        std::unique_lock<std::recursive_mutex> lock2(this->buffer_mutex);
        size_t consumed_samples = 0;
        frame_unit consumed_samples_end = this->consumed_samples_end;
        for(auto it = this->buffer.begin(); it != this->buffer.end() && !dispatch; it++)
        {
            CComPtr<IMFSample> sample;
            CComPtr<IMFMediaBuffer> buffer, oldbuffer;
            DWORD buflen;

            LONGLONG sample_pos, sample_dur;
            CHECK_HR(hr = (*it)->GetSampleTime(&sample_pos));
            CHECK_HR(hr = (*it)->GetSampleDuration(&sample_dur));

            const frame_unit frame_pos = sample_pos;
            const frame_unit frame_end = frame_pos + sample_dur;

            // too new sample for the request
            if(frame_pos >= request_end)
            {
                dispatch = true;
                break;
            }
            // request can be dispatched
            if(frame_end >= request_end)
                dispatch = true;

            CHECK_HR(hr = (*it)->GetBufferByIndex(0, &oldbuffer));
            CHECK_HR(hr = oldbuffer->GetCurrentLength(&buflen));

            const frame_unit frame_diff_end = std::max(frame_end - request_end, 0LL);
            const DWORD offset_end  = (DWORD)frame_diff_end * this->get_block_align();
            const frame_unit new_sample_time = sample_pos;
            const frame_unit new_sample_dur = sample_dur - frame_diff_end;

            // discard frames that have older time stamp than the last consumed one
            /*if(new_sample_time >= consumed_samples_end)
            {*/
                if(new_sample_time < consumed_samples_end)
                    std::cout << "wrong timestamp in source_wasapi" << std::endl;

                assert_(((int)buflen - (int)offset_end) > 0);
                CHECK_HR(hr = MFCreateMediaBufferWrapper(oldbuffer, 0, buflen - offset_end, &buffer));
                CHECK_HR(hr = buffer->SetCurrentLength(buflen - offset_end));
                if(offset_end > 0)
                {
                    // remove the consumed part of the old buffer
                    CComPtr<IMFMediaBuffer> new_buffer;
                    CHECK_HR(hr = MFCreateMediaBufferWrapper(
                        oldbuffer, buflen - offset_end, offset_end, &new_buffer));
                    CHECK_HR(hr = new_buffer->SetCurrentLength(offset_end));
                    CHECK_HR(hr = (*it)->RemoveAllBuffers());
                    CHECK_HR(hr = (*it)->AddBuffer(new_buffer));
                    const LONGLONG new_sample_dur = offset_end / this->get_block_align();
                    const LONGLONG new_sample_time = sample_pos + sample_dur - new_sample_dur;
                    CHECK_HR(hr = (*it)->SetSampleTime(new_sample_time));
                    CHECK_HR(hr = (*it)->SetSampleDuration(new_sample_dur));
                }
                else
                    consumed_samples++;
                CHECK_HR(hr = MFCreateSample(&sample));
                CHECK_HR(hr = sample->AddBuffer(buffer));
                CHECK_HR(hr = sample->SetSampleTime(new_sample_time));
                CHECK_HR(hr = sample->SetSampleDuration(new_sample_dur));

                // TODO: define this flag in audio processor
                // because of data discontinuity the new sample base might yield sample times
                // that 'travel back in time', so keep track of last consumed sample and discard
                // samples that are older than that;
                // the audio might become out of sync by the amount of discarded frames
                // (actually it won't)
                /*if(!(request.rp.flags & AUDIO_DISCARD_PREVIOUS_SAMPLES))*/
                    // TODO: pushing samples to a container could be deferred to
                    // dispatching block
                    audio.buffer->samples.push_back(sample);
            /*}
            else
            {
                std::cout << "discarded" << std::endl;
                consumed_samples++;
            }*/

            consumed_samples_end = std::max(new_sample_time + new_sample_dur, consumed_samples_end);
        }

        if(dispatch)
        {
            // update the consumed samples position
            this->consumed_samples_end = consumed_samples_end;

            // erase all consumed samples
            for(size_t i = 0; i < consumed_samples; i++)
                this->buffer.pop_front();

            // pop the request from the queue
            this->requests.pop(request);

            lock2.unlock();
            lock.unlock();
            // dispatch the request
            request.stream->process_sample(audio, request.rp, NULL);
            /*this->session->give_sample(request.stream, audio, request.rp, false);*/
            lock.lock();
        }
        else
            break;
    }

done:
    if(FAILED(hr))
        throw std::exception();
}

void source_wasapi::process_cb(void*)
{
    ResetEvent(this->process_event);

    std::unique_lock<std::recursive_mutex> lock(this->process_mutex, std::try_to_lock);
    assert_(lock.owns_lock());
    if(!lock.owns_lock())
        return;

    HRESULT hr = S_OK;
    // nextpacketsize and frames are equal
    UINT32 nextpacketsize = 0, returned_frames = 0;
    UINT64 returned_devposition;
    bool getbuffer = false;
    while(SUCCEEDED(hr = this->audio_capture_client->GetNextPacketSize(&nextpacketsize)) && 
        nextpacketsize)
    {
        CComPtr<IMFSample> sample;
        CComPtr<IMFMediaBuffer> buffer;
        BYTE* data;
        DWORD flags;
        UINT64 first_sample_timestamp;
        UINT64 devposition;
        UINT32 frames;

        // TODO: source loopback must be reinitialized if the device becomes invalid

        // no excessive delay should happen between getbuffer and releasebuffer calls
        CHECK_HR(hr = this->audio_capture_client->GetBuffer(
            &data, &returned_frames, &flags, &returned_devposition, &first_sample_timestamp));
        getbuffer = true;
        // try fetch a next packet if no frames were returned
        // or if the frames were already returned
        if(returned_frames == 0)
        {
            getbuffer = false;
            CHECK_HR(hr = this->audio_capture_client->ReleaseBuffer(returned_frames));
            continue;
        }

        frames = returned_frames;
        devposition = returned_devposition;

        CHECK_HR(hr = MFCreateSample(&sample));

        bool silent = false;

        if(flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY || 
            this->frame_base == std::numeric_limits<frame_unit>::min())
        {
            std::cout << "DATA DISCONTINUITY, " << devposition << ", " << devposition + frames << std::endl;

            presentation_time_source_t time_source = this->session->get_time_source();
            if(!time_source)
            {
                this->frame_base = std::numeric_limits<frame_unit>::min();
                goto done;
            }

            // calculate the new sample base from the timestamp
            const double frame_duration = SECOND_IN_TIME_UNIT / (double)this->samples_per_second;
            this->devposition_base = (frame_unit)devposition;
            this->frame_base = (frame_unit)
                (time_source->system_time_to_time_source((time_unit)first_sample_timestamp) /
                frame_duration);
        }
        if(flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)
        {
            std::cout << "TIMESTAMP ERROR" << std::endl;
        }
        if(flags & AUDCLNT_BUFFERFLAGS_SILENT)
        {
            silent = true;
            /*std::cout << "SILENT" << std::endl;*/
        }
        if(!flags)
        {
            /*std::cout << "OK, " << devposition << ", " << devposition + frames << std::endl;*/
            /*std::cout << "OK" << std::endl;*/
        }

        // convert and copy to buffer
        BYTE* buffer_data;
        const DWORD len = frames * this->get_block_align();
        CHECK_HR(hr = MFCreateMemoryBuffer(len, &buffer));
        CHECK_HR(hr = buffer->Lock(&buffer_data, NULL, NULL));
        if(silent)
            make_silence(frames, this->channels, (float*)buffer_data);
        else
            memcpy(buffer_data, data, len);
        CHECK_HR(hr = buffer->Unlock());
        CHECK_HR(hr = buffer->SetCurrentLength(len));
        CHECK_HR(hr = sample->AddBuffer(buffer));

        // set the time and duration in frames
        const frame_unit sample_time = this->frame_base + 
            ((frame_unit)devposition - this->devposition_base);
        CHECK_HR(hr = sample->SetSampleTime(sample_time));
        CHECK_HR(hr = sample->SetSampleDuration(frames));
        this->next_frame_position = sample_time + (frame_unit)frames;

        // add sample
        {
            scoped_lock lock(this->raw_buffer_mutex);
            this->raw_buffer.push_back(sample);
        }

        getbuffer = false;
        CHECK_HR(hr = this->audio_capture_client->ReleaseBuffer(returned_frames));
    }

done:
    if(getbuffer)
    {
        getbuffer = false;
        this->audio_capture_client->ReleaseBuffer(returned_frames);
    }
    if(FAILED(hr) && hr != MF_E_SHUTDOWN)
        throw std::exception();

    if(!this->capture)
        CHECK_HR(hr = this->play_silence());

    lock.unlock();
    this->add_event_to_wait_queue();
    this->serve_requests();
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

void source_wasapi::serve_requests()
{
    const HRESULT hr = this->serve_callback->mf_put_work_item(this->shared_from_this<source_wasapi>());
    if(FAILED(hr) && hr != MF_E_SHUTDOWN)
        throw std::exception();
}

HRESULT source_wasapi::initialize_render(IMMDevice* device, WAVEFORMATEX* engine_format)
{
    HRESULT hr = S_OK;
    LPBYTE data;

    CHECK_HR(hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, 
        (void**)&this->audio_client_render));
    CHECK_HR(hr = this->audio_client_render->Initialize(
        AUDCLNT_SHAREMODE_SHARED, 0, BUFFER_DURATION, 0, engine_format, NULL));

    CHECK_HR(hr = this->audio_client_render->GetBufferSize(&this->render_buffer_frame_count));
    CHECK_HR(hr = this->audio_client_render->GetService(
        __uuidof(IAudioRenderClient), (void**)&this->audio_render_client));
    CHECK_HR(hr = this->audio_render_client->GetBuffer(this->render_buffer_frame_count, &data));
    CHECK_HR(hr = this->audio_render_client->ReleaseBuffer(
        this->render_buffer_frame_count, AUDCLNT_BUFFERFLAGS_SILENT));

done:
    return hr;
}

void source_wasapi::initialize(const std::wstring& device_id, bool capture)
{
    HRESULT hr = S_OK;

    CComPtr<IMMDeviceEnumerator> enumerator;
    CComPtr<IMMDevice> device;
    WAVEFORMATEX* engine_format = NULL;
    UINT32 buffer_frame_count;

    this->capture = capture;

    CHECK_HR(hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
        (void**)&enumerator));
    CHECK_HR(hr = enumerator->GetDevice(device_id.c_str(), &device));

    CHECK_HR(hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&this->audio_client));
    CHECK_HR(hr = this->audio_client->GetMixFormat(&engine_format));

    CHECK_HR(hr = this->audio_client->Initialize(
        AUDCLNT_SHAREMODE_SHARED, 
        (this->capture ? 0 : AUDCLNT_STREAMFLAGS_LOOPBACK) | AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 
        BUFFER_DURATION, 0, engine_format, NULL));

    CHECK_HR(hr = this->audio_client->GetBufferSize(&buffer_frame_count));

    CHECK_HR(hr = this->audio_client->GetService(
        __uuidof(IAudioCaptureClient), (void**)&this->audio_capture_client));

    CHECK_HR(hr = this->audio_client->GetService(
        __uuidof(IAudioClock), (void**)&this->audio_clock));

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
    this->buffer_actual_duration = 
        (REFERENCE_TIME)((double)SECOND_IN_TIME_UNIT * buffer_frame_count / this->samples_per_second);

    // initialize silence fix
    // (https://github.com/jp9000/obs-studio/blob/master/plugins/win-wasapi/win-wasapi.cpp#L199)
    if(!this->capture)
        CHECK_HR(hr = this->initialize_render(device, engine_format));

    //// create waitable timer
    //assert_(!this->process_event);
    //this->process_event.Attach(CreateWaitableTimer(NULL, FALSE, NULL));
    //if(!this->process_event)
    //    CHECK_HR(hr = HRESULT_FROM_WIN32(GetLastError()));
    //LARGE_INTEGER first_fire;
    //first_fire.QuadPart = -BUFFER_DURATION / 2; // negative means relative time
    //LONG time_between_fires = BUFFER_DURATION / 2 / MILLISECOND_IN_TIMEUNIT;
    //if(!SetWaitableTimer(this->process_event, &first_fire, time_between_fires, NULL, NULL, FALSE))
    //    CHECK_HR(hr = HRESULT_FROM_WIN32(GetLastError()));

    // create manual reset event handle
    assert_(!this->process_event);
    this->process_event.Attach(CreateEvent(
        NULL, TRUE, FALSE, NULL));  
    if(!this->process_event)
        CHECK_HR(hr = E_FAIL);
    CHECK_HR(hr = this->audio_client->SetEventHandle(this->process_event));

    //// set the event to mf queue
    //CHECK_HR(hr = this->add_event_to_wait_queue());

    assert_(this->serve_callback);
    CHECK_HR(hr = this->add_event_to_wait_queue());

    // start capturing
    if(!this->capture)
        CHECK_HR(hr = this->audio_client_render->Start());
    CHECK_HR(hr = this->audio_client->Start());

    this->started = true;

done:
    if(engine_format)
        CoTaskMemFree(engine_format);
    if(FAILED(hr))
        throw std::exception();
}

media_stream_t source_wasapi::create_stream()
{
    return media_stream_t(new stream_wasapi(this->shared_from_this<source_wasapi>()));
}

//void source_wasapi::move_buffer(media_sample_audio& sample)
//{
//    this->buffer.lock();
//    sample.buffer->lock();
//
//    sample.buffer->samples.swap(this->buffer.samples);
//    this->buffer.samples.clear();
//
//    sample.bit_depth = sizeof(bit_depth_t) * 8;
//    sample.sample_rate = this->samples_per_second;
//    sample.channels = this->channels;
//
//    sample.buffer->unlock();
//    this->buffer.unlock();
//}


/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////


stream_wasapi::stream_wasapi(const source_wasapi_t& source) : 
    source(source),
    audio_buffer(new media_buffer_samples)
{
}

media_stream::result_t stream_wasapi::request_sample(request_packet& rp, const media_stream*)
{
    source_wasapi::request_t request;
    request.stream = this;
    request.rp = rp;
    this->source->requests.push(request);

    return OK;
}

media_stream::result_t stream_wasapi::process_sample(
    const media_sample& sample_view, request_packet& rp, const media_stream*)
{
    return this->source->session->give_sample(this, sample_view, rp, true) ? OK : FATAL_ERROR;
}