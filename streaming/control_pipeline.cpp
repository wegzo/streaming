#include "control_pipeline.h"
#include "wtl.h"
#include "gui_threadwnd.h"
#include <iostream>
#include <d3d11_4.h>
#include <d2d1_2.h>

#ifdef _DEBUG
#define CREATE_DEVICE_DEBUG D3D11_CREATE_DEVICE_DEBUG
#define CREATE_DEVICE_DEBUG_D2D1 D2D1_DEBUG_LEVEL_INFORMATION
#else
#define CREATE_DEVICE_DEBUG 0
#define CREATE_DEVICE_DEBUG_D2D1 D2D1_DEBUG_LEVEL_NONE
#endif

#define CHECK_HR(hr_) {if(FAILED(hr_)) {goto done;}}

// if using intel encoder, the adapter must be set to intel aswell

control_pipeline::control_pipeline(HWND gui_thread_hwnd) :
    gui_thread_hwnd(gui_thread_hwnd),
    control_class(controls, event_provider),
    d3d11dev_adapter(0),
    context_mutex(new std::recursive_mutex),
    root_scene(new control_scene(controls, *this)),
    preview_control(new control_preview(controls, *this)),
    recording(false),
    restart_pipeline_requested(false)
{
    this->root_scene->parent = this;

    // initialize graphics
    HRESULT hr = S_OK;
    CComPtr<IDXGIAdapter1> dxgiadapter;
    D3D_FEATURE_LEVEL feature_levels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1,
    };
    D3D_FEATURE_LEVEL feature_level;
    CComPtr<ID3D11Multithread> multithread;
    BOOL was_protected;
    D2D1_FACTORY_OPTIONS d2d1_options;

    CHECK_HR(hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&this->dxgifactory));
    CHECK_HR(hr = this->dxgifactory->EnumAdapters1(this->d3d11dev_adapter, &dxgiadapter));
    CHECK_HR(hr = D3D11CreateDevice(
        dxgiadapter, D3D_DRIVER_TYPE_UNKNOWN, NULL,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT | CREATE_DEVICE_DEBUG,
        feature_levels, ARRAYSIZE(feature_levels), D3D11_SDK_VERSION, &this->d3d11dev,
        &feature_level, &this->devctx));

    // use implicit multithreading protection aswell so that the context cannot be
    // accidentally corrupted;
    // amd h264 encoder probably caused encoding artifacts because the
    // context was being corrupted
    CHECK_HR(hr = this->devctx->QueryInterface(&multithread));
    was_protected = multithread->SetMultithreadProtected(TRUE);

    // get the dxgi device
    CHECK_HR(hr = this->d3d11dev->QueryInterface(&this->dxgidev));

    // create d2d1 factory
    d2d1_options.debugLevel = CREATE_DEVICE_DEBUG_D2D1;
    CHECK_HR(hr = 
        D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, d2d1_options, &this->d2d1factory));

    // create d2d1 device
    CHECK_HR(hr = this->d2d1factory->CreateDevice(this->dxgidev, &this->d2d1dev));

    std::cout << "adapter " << this->d3d11dev_adapter << std::endl;

    // set the gpu thread priority
    /*INT old_priority;
    CHECK_HR(hr = this->dxgidev->GetGPUThreadPriority(&old_priority));
    CHECK_HR(hr = this->dxgidev->SetGPUThreadPriority(3));*/

    this->config = control_pipeline::get_default_config();

done:
    if(FAILED(hr))
        throw HR_EXCEPTION(hr);
}

control_pipeline::~control_pipeline()
{
}

bool control_pipeline::run_in_gui_thread(callable_f f)
{
    if(!this->is_active())
        // gui_thread_wnd might have been destroyed when this isn't active anymore
        return false;

    control_class_t this_ = this->shared_from_this<control_class>();
    // gui_threadwnd returns 0 if the callable_f was not called
    return (SendMessage(this->gui_thread_hwnd, GUI_THREAD_MESSAGE, (WPARAM)&f, (LPARAM)&this_) != 0);
}

void control_pipeline::activate(const control_set_t& last_set, control_set_t& new_set)
{
    // catch all unhandled initialization exceptions
    try
    {
        if(this->disabled)
        {
           // this also breaks the possible circular dependency between control pipeline
            // and the component

            {
                const bool old_disabled = this->root_scene->disabled;
                this->root_scene->disabled = true;
                this->root_scene->activate(last_set, new_set);
                this->root_scene->disabled = old_disabled;
            }
            {
                const bool old_disabled = this->preview_control->disabled;
                this->preview_control->disabled = true;
                this->preview_control->activate(last_set, new_set);
                this->preview_control->disabled = old_disabled;
            }

            this->deactivate_components();

            this->event_provider.for_each([this](gui_event_handler* e) { e->on_activate(this, true); });

            return;
        }

        if(this->restart_pipeline_requested)
        {
            this->restart_pipeline_requested = false;
            this->deactivate_components();
        }

        this->activate_components();

        // add this to the new set
        new_set.push_back(this->shared_from_this<control_pipeline>());

        // activate the preview control
        this->preview_control->activate(last_set, new_set);

        // activate the root scene
        this->root_scene->activate(last_set, new_set);

        this->event_provider.for_each([this](gui_event_handler* e) { e->on_activate(this, false); });
    }
    catch(streaming::exception e)
    {
        streaming::print_error_and_abort(e.what());
    }
}

void control_pipeline::activate_components()
{
    if(!this->time_source)
    {
        this->time_source.reset(new media_clock);
        this->time_source->set_current_time(0);
        // time source must be started early because components might use the time source
        // before the topology is started
        this->time_source->start();
    }
    if(!this->session)
        this->session.reset(new media_session(this->time_source,
            this->config.fps_num, this->config.fps_den));
    if(!this->audio_session)
        this->audio_session.reset(new media_session(this->time_source,
            this->config.sample_rate, 1));

    // must be called after resetting the video session
    frame_unit fps_num, fps_den;
    this->get_session_frame_rate(fps_num, fps_den);

    // create videoprocessor transform
    if(!this->videomixer_transform ||
        this->videomixer_transform->get_instance_type() == media_component::INSTANCE_NOT_SHAREABLE)
    {
        transform_videomixer_t videomixer_transform(new transform_videomixer(this->session,
            this->context_mutex));
        videomixer_transform->initialize(this->shared_from_this<control_class>(),
            transform_h264_encoder::frame_width, transform_h264_encoder::frame_height,
            this->d2d1factory, this->d2d1dev, this->d3d11dev, this->devctx);

        this->videomixer_transform = videomixer_transform;
    }

    // create h264 transform
    if(this->recording && (!this->h264_encoder_transform ||
        this->h264_encoder_transform->get_instance_type() == media_component::INSTANCE_NOT_SHAREABLE))
    {
        // TODO: activating the encoder might fail for random reasons,
        // so notify if the primary encoder cannot be used and use the software encoder as a
        // fallback
        // (signature error during activate call was fixed by a reboot)
        transform_h264_encoder_t h264_encoder_transform;
        try
        {
            h264_encoder_transform.reset(new transform_h264_encoder(
                this->session, this->context_mutex));
            h264_encoder_transform->initialize(this->shared_from_this<control_class>(),
                this->d3d11dev, (UINT32)fps_num, (UINT32)fps_den, false);
        }
        catch(std::exception)
        {
            std::cout << "using system ram for hardware video encoder" << std::endl;

            try
            {
                // try to initialize the h264 encoder without utilizing vram
                h264_encoder_transform.reset(new transform_h264_encoder(
                    this->session, this->context_mutex));
                h264_encoder_transform->initialize(this->shared_from_this<control_class>(),
                    NULL, (UINT32)fps_num, (UINT32)fps_den);
            }
            catch(std::exception)
            {
                std::cout << "using software encoder" << std::endl;
                // use software encoder
                h264_encoder_transform.reset(new transform_h264_encoder(
                    this->session, this->context_mutex));
                h264_encoder_transform->initialize(this->shared_from_this<control_class>(),
                    NULL, (UINT32)fps_num, (UINT32)fps_den, true);
            }
        }

        this->h264_encoder_transform = h264_encoder_transform;
    }
    else if(!this->recording)
        this->h264_encoder_transform = NULL;

    // create color converter transform
    if(this->recording && (!this->color_converter_transform ||
        this->color_converter_transform->get_instance_type() == media_component::INSTANCE_NOT_SHAREABLE))
    {
        transform_color_converter_t color_converter_transform(
            new transform_color_converter(this->session, this->context_mutex));
        color_converter_transform->initialize(this->shared_from_this<control_class>(),
            this->d3d11dev, this->devctx);
        this->color_converter_transform = color_converter_transform;
    }
    else if(!this->recording)
        this->color_converter_transform = NULL;

    // create aac encoder transform
    if(this->recording && (!this->aac_encoder_transform ||
        this->aac_encoder_transform->get_instance_type() == media_component::INSTANCE_NOT_SHAREABLE))
    {
        transform_aac_encoder_t aac_encoder_transform(new transform_aac_encoder(this->audio_session));
        aac_encoder_transform->initialize();

        this->aac_encoder_transform = aac_encoder_transform;
    }
    else if(!this->recording)
        this->aac_encoder_transform = NULL;

    // create audiomixer transform
    if(!this->audiomixer_transform || 
        this->audiomixer_transform->get_instance_type() == media_component::INSTANCE_NOT_SHAREABLE)
    {
        transform_audiomixer2_t audiomixer_transform(new transform_audiomixer2(this->audio_session));
        audiomixer_transform->initialize();

        this->audiomixer_transform = audiomixer_transform;
    }

    output_file_t file_output;

    // create mp4 file sink video part
    if(this->recording && (!this->mp4_sink.first ||
        this->mp4_sink.first->get_instance_type() == media_component::INSTANCE_NOT_SHAREABLE))
    {
        assert_(!file_output);
        file_output.reset(new output_file);
        file_output->initialize(false, this->recording_initiator_wnd,
            this->h264_encoder_transform->output_type,
            this->aac_encoder_transform->output_type);

        sink_file_video_t file_sink(new sink_file_video(this->session));
        file_sink->initialize(file_output, true);

        this->mp4_sink.first = file_sink;
    }
    else if(!this->recording)
        this->mp4_sink.first = NULL;

    // create mp4 file sink audio part
    if(this->recording && (!this->mp4_sink.second ||
        this->mp4_sink.second->get_instance_type() == media_component::INSTANCE_NOT_SHAREABLE))
    {
        if(!file_output)
            throw HR_EXCEPTION(E_UNEXPECTED);

        sink_file_audio_t file_sink(new sink_file_audio(this->audio_session));
        file_sink->initialize(file_output, false);

        this->mp4_sink.second = file_sink;
    }
    else if(!this->recording)
        this->mp4_sink.second = NULL;

    // create video sink(the main/real pull sink)
    if(!this->video_sink)
    {
        sink_video_t video_sink(new sink_video(this->session, this->audio_session));
        video_sink->initialize();

        this->video_sink = video_sink;
    }

    // create audio sink(controlled by video sink)
    if(!this->audio_sink)
    {
        sink_audio_t audio_sink(new sink_audio(this->audio_session));
        audio_sink->initialize();

        this->audio_sink = audio_sink;
    }

    // create video buffering source
    if(!this->video_buffering_source || 
        this->video_buffering_source->get_instance_type() == media_component::INSTANCE_NOT_SHAREABLE)
    {
        source_buffering_video_t video_buffering_source(new source_buffering_video(this->session));
        video_buffering_source->initialize(this->shared_from_this<control_pipeline>(),
            BUFFERING_DEFAULT_VIDEO_LATENCY);

        this->video_buffering_source = video_buffering_source;
    }

    // create audio buffering source
    if(!this->audio_buffering_source ||
        this->audio_buffering_source->get_instance_type() == media_component::INSTANCE_NOT_SHAREABLE)
    {
        source_buffering_audio_t audio_buffering_source(new source_buffering_audio(this->audio_session));
        audio_buffering_source->initialize(this->shared_from_this<control_pipeline>(),
            BUFFERING_DEFAULT_AUDIO_LATENCY);

        this->audio_buffering_source = audio_buffering_source;
    }
}

void control_pipeline::deactivate_components()
{
    if(this->is_recording())
        this->stop_recording();

    // stop the playback by switching to empty topologies
    if(this->video_sink)
    {
        this->video_topology.reset(new media_topology(media_message_generator_t(new media_message_generator)));
        this->audio_topology.reset(new media_topology(media_message_generator_t(new media_message_generator)));
        this->video_sink->switch_topologies(this->video_topology, this->audio_topology);
    }

    this->videomixer_transform = NULL;
    this->h264_encoder_transform = NULL;
    this->color_converter_transform = NULL;
    this->mp4_sink = sink_mp4_t(NULL, NULL);
    this->video_sink = NULL;
    this->aac_encoder_transform = NULL;
    this->audiomixer_transform = NULL;
    this->audio_sink = NULL;
    this->video_buffering_source = NULL;
    this->audio_buffering_source = NULL;

    this->session = NULL;
    this->audio_session = NULL;
    this->time_source = NULL;

    /*Sleep(INFINITE);*/
}

void control_pipeline::build_and_switch_topology()
{
    // catch all unhandled initialization exceptions
    try
    {

    if(this->disabled)
        return;

    this->video_topology.reset(
        new media_topology(media_message_generator_t(new media_message_generator)));
    this->audio_topology.reset(
        new media_topology(media_message_generator_t(new media_message_generator)));

    stream_audio_t audio_stream = 
        this->audio_sink->create_stream(this->audio_topology->get_message_generator());
    stream_video_t video_stream = this->video_sink->create_stream(
        this->video_topology->get_message_generator(), audio_stream);

    frame_unit fps_num, fps_den;
    this->get_session_frame_rate(fps_num, fps_den);
    video_stream->set_pull_rate(fps_num, fps_den);

    // set the topology
    stream_audiomixer2_base_t audiomixer_stream =
        this->audiomixer_transform->create_stream(this->audio_topology->get_message_generator());
    stream_videomixer_base_t videomixer_stream = 
        this->videomixer_transform->create_stream(this->video_topology->get_message_generator());

    // connect the sources to mixers
    this->root_scene->build_video_topology(
        video_stream, videomixer_stream, this->video_topology);
    this->root_scene->build_audio_topology(
        audio_stream, audiomixer_stream, this->audio_topology);

    // connect the buffering sources to mixers
    media_stream_t video_buffering_stream = this->video_buffering_source->create_stream(
        this->video_topology->get_message_generator());
    media_stream_t audio_buffering_stream = this->audio_buffering_source->create_stream(
        this->audio_topology->get_message_generator());

    video_buffering_stream->connect_streams(video_stream, this->video_topology);
    audio_buffering_stream->connect_streams(audio_stream, this->audio_topology);

    videomixer_stream->connect_streams(video_buffering_stream, NULL, this->video_topology);
    audiomixer_stream->connect_streams(audio_buffering_stream, NULL, this->audio_topology);

    if(!this->recording)
    {
        this->preview_control->build_video_topology(
            videomixer_stream, video_stream, this->video_topology);
        audio_stream->connect_streams(audiomixer_stream, this->audio_topology);
    }
    else
    {
        media_stream_t encoder_stream_video =
            this->h264_encoder_transform->create_stream(this->video_topology->get_message_generator());
        media_stream_t color_converter_stream = this->color_converter_transform->create_stream();
        media_stream_t mp4_stream_video = 
            this->mp4_sink.first->create_stream(this->video_topology->get_message_generator());
        media_stream_t encoder_stream_audio =
            this->aac_encoder_transform->create_stream(this->audio_topology->get_message_generator());
        media_stream_t mp4_stream_audio = 
            this->mp4_sink.second->create_stream(this->audio_topology->get_message_generator());

        // TODO: encoder stream is redundant
        video_stream->encoder_stream = 
            std::dynamic_pointer_cast<stream_h264_encoder>(encoder_stream_video);

        this->preview_control->build_video_topology(
            videomixer_stream, color_converter_stream, this->video_topology);
        /*color_converter_stream->connect_streams(preview_stream, this->video_topology);*/
        encoder_stream_video->connect_streams(color_converter_stream, this->video_topology);
        mp4_stream_video->connect_streams(encoder_stream_video, this->video_topology);
        video_stream->connect_streams(mp4_stream_video, this->video_topology);

        encoder_stream_audio->connect_streams(audiomixer_stream, this->audio_topology);
        mp4_stream_audio->connect_streams(encoder_stream_audio, this->audio_topology);
        audio_stream->connect_streams(mp4_stream_audio, this->audio_topology);
    }

    // video sink ensures atomic topology starting/switching for audio and video
    if(!this->video_sink->is_started())
    {
        // start the media session with the topology;
        // it's ok to start with time point of 0 because the time source starts at 0
        this->video_sink->start_topologies(0, this->video_topology, this->audio_topology);
    }
    else
        this->video_sink->switch_topologies(this->video_topology, this->audio_topology);

    }
    catch(streaming::exception e)
    {
    streaming::print_error_and_abort(e.what());
    }
}

void control_pipeline::set_selected_control(control_class* control, selection_type type)
{
    assert_(control != NULL || type == CLEAR);

    if(type == SET || type == CLEAR)
        this->selected_controls.clear();
    if(type == SET || type == ADD)
        this->selected_controls.push_back(control);

    // trigger
    this->event_provider.for_each([](gui_event_handler* e) { e->on_control_selection_changed(); });
}

void control_pipeline::get_session_frame_rate(frame_unit& num, frame_unit& den) const
{
    num = this->session->frame_rate_num;
    den = this->session->frame_rate_den;
}

control_pipeline_config control_pipeline::get_default_config()
{
    control_pipeline_config config;
    config.fps_num = 10;
    config.fps_den = 1;
    config.sample_rate = 48000;

    return config;
}

void control_pipeline::apply_config(const control_pipeline_config& new_config)
{
    this->config = new_config;
    this->restart_pipeline_requested = true;

    this->control_class::activate();
}

void control_pipeline::start_recording(const std::wstring& /*filename*/, ATL::CWindow initiator)
{
    assert_(!this->is_recording());
    assert_(!this->is_disabled());

    this->recording_initiator_wnd = initiator;
    this->recording = true;
    this->control_class::activate();

    std::cout << "recording started" << std::endl;
}

void control_pipeline::stop_recording()
{
    this->recording = false;
    this->control_class::activate();

    std::cout << "recording stopped" << std::endl;
}