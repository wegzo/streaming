#pragma once
#include "presentation_clock.h"
#include "media_session.h"
#include "control_scene.h"
#include "transform_aac_encoder.h"
#include "transform_h264_encoder.h"
#include "transform_color_converter.h"
#include "transform_videoprocessor.h"
#include "transform_audioprocessor.h"
#include "transform_audiomix.h"
#include "sink_mpeg2.h"
#include "sink_audio.h"
#include "sink_preview2.h"
#include "source_loopback.h"
#include "source_displaycapture5.h"
#include <atlbase.h>
#include <d3d11.h>
#include <dxgi.h>
#include <mutex>
#include <vector>
#include <list>
#include <memory>

#pragma comment(lib, "D3D11.lib")
#pragma comment(lib, "DXGI.lib")

// controls the entire pipeline which includes scene switches;
// hosts the video and audio encoder and sinks aswell

// control classes aren't multithread safe

// TODO: pipeline should host components the same way control_scene does

class control_pipeline
{
    friend class control_scene;
public:
    struct mpeg_sink_item
    {
        bool null_file;
        std::wstring filename;
    };
private:
    CHandle stopped_signal;
    HWND preview_wnd;

    presentation_time_source_t time_source;
    media_session_t session, audio_session;
    // these components are present in every scene
    transform_videoprocessor_t videoprocessor_transform;
    transform_h264_encoder_t h264_encoder_transform;
    transform_color_converter_t color_converter_transform;
    transform_aac_encoder_t aac_encoder_transform;
    sink_preview2_t preview_sink;

    mpeg_sink_item item_mpeg_sink;

    std::pair<mpeg_sink_item, sink_mpeg2_t> mpeg_sink;
    std::pair<mpeg_sink_item, sink_audio_t> audio_sink;

    control_scene* scene_active;
    // use list so that the pointer stays valid when adding/erasing scenes
    std::list<control_scene> scenes;

    UINT d3d11dev_adapter;
    CComPtr<IDXGIFactory1> dxgifactory;
    CComPtr<ID3D11Device> d3d11dev;
    CComPtr<ID3D11DeviceContext> devctx;
    std::recursive_mutex context_mutex;

    void activate_components();
    void deactivate_components();

    transform_videoprocessor_t create_videoprocessor();
    transform_h264_encoder_t create_h264_encoder(bool null_file);
    transform_color_converter_t create_color_converter(bool null_file);
    sink_preview2_t create_preview_sink(HWND hwnd);
    transform_aac_encoder_t create_aac_encoder(bool null_file);
    sink_mpeg2_t create_mpeg_sink(
        bool null_file, const std::wstring& filename, 
        const CComPtr<IMFMediaType>& video_input_type,
        const CComPtr<IMFMediaType>& audio_input_type);
    sink_audio_t create_audio_sink(bool null_file, const output_file_t& output);

    /*void reset_components(bool create_new);*/
    // creates and initializes the component
    // or returns the component from the current scene
    source_audio_t create_audio_source(const std::wstring& id, bool capture);
    source_displaycapture5_t create_displaycapture_source(UINT adapter_ordinal, UINT output_ordinal);
    transform_audiomix_t create_audio_mixer();

    // builds the pipeline specific part of the video topology branch
    void build_video_topology_branch(const media_topology_t& video_topology, 
        const media_stream_t& videoprocessor_stream,
        const stream_mpeg2_t& mpeg_sink_stream);
    void build_audio_topology_branch(const media_topology_t& audio_topology,
        const media_stream_t& last_stream,
        const stream_audio_t& audio_sink_stream);
public:
    control_pipeline();

    bool is_running() const {return this->scene_active != NULL;}
    bool is_recording() const {return this->is_running() && !this->item_mpeg_sink.null_file;}

    void update_preview_size() {this->preview_sink->update_size();}
    void initialize(HWND preview_wnd);

    void set_mpeg_sink_item(const mpeg_sink_item&);

    control_scene& create_scene(const std::wstring& name);
    control_scene& get_scene(int index);
    bool is_active(const control_scene&);
    void set_active(control_scene&);
    // stops the pipeline
    void set_inactive();

    void start_recording(const std::wstring& filename, control_scene&);
    void stop_recording();
};