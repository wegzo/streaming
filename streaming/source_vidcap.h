#pragma once
#include "source_base.h"
#include "video_source_helper.h"
#include "media_component.h"
#include "media_stream.h"
#include "media_sample.h"
#include "transform_videomixer.h"
#include "async_callback.h"
#include <memory>
#include <string>
#include <queue>
#include <mfapi.h>
#include <mfreadwrite.h>
#include <d3d11.h>

#pragma comment(lib, "Mfplat.lib")
#pragma comment(lib, "Mfreadwrite.lib")

// TODO: a preview dialog should be created where the output formats can be selected
// (control class will handle the creation of the dialog and the preview pipeline);
// that preview dialog can be generalized to a properties window

class source_vidcap final : public source_base<media_component_videomixer_args>
{
    friend class stream_vidcap;
    class device_notification_listener;
    struct source_reader_callback_t;
public:
    typedef std::lock_guard<std::mutex> scoped_lock;
    typedef async_callback<source_vidcap> async_callback_t;
    typedef buffer_pool<media_buffer_pooled_texture> buffer_pool_texture_t;

    enum state_t
    {
        UNINITIALIZED,
        INITIALIZED,
        WAITING_FOR_DEVICE
    };
private:
    std::shared_ptr<device_notification_listener> listener;
    // state must be only modified in the gui thread
    state_t state;

    mutable std::mutex source_helper_mutex;
    video_source_helper source_helper;
    std::shared_ptr<buffer_pool_texture_t> buffer_pool_texture;
    CComPtr<source_reader_callback_t> source_reader_callback;
    context_mutex_t context_mutex;

    frame_unit next_frame_pos;

    UINT32 reset_token;
    CComPtr<ID3D11Device> d3d11dev;
    CComPtr<ID3D11DeviceContext> d3d11devctx;
    CComPtr<IMFDXGIDeviceManager> devmngr;

    std::atomic<bool> reset_size;
    volatile bool is_capture_initialized, is_helper_initialized;
    std::mutex queue_new_capture_mutex;

    // mutable
    mutable std::mutex size_mutex;
    UINT32 frame_width, frame_height;

    CComPtr<IMFMediaType> output_type;
    CComPtr<IMFMediaSource> device;
    /*
    By default, when the application releases the source reader, the source reader 
    shuts down the media source by calling IMFMediaSource::Shutdown on the media source.
    */
    CComPtr<IMFSourceReader> source_reader;
    CComPtr<IMFAttributes> source_reader_attributes;
    // transformed to lower case
    std::wstring symbolic_link;

    void initialize_buffer(const media_buffer_texture_t&, const D3D11_TEXTURE2D_DESC&);
    media_buffer_texture_t acquire_buffer(const D3D11_TEXTURE2D_DESC&);

    stream_source_base_t create_derived_stream() override;
    bool get_samples_end(time_unit request_time, frame_unit& end) const override;
    void make_request(request_t&, frame_unit frame_end) override;
    void dispatch(request_t&) override;

    HRESULT queue_new_capture();
    void initialize_async();
public:
    explicit source_vidcap(const media_session_t& session, context_mutex_t);
    ~source_vidcap();

    void get_size(UINT32& width, UINT32& height) const
    { scoped_lock lock(this->size_mutex); width = this->frame_width; height = this->frame_height; }
    state_t get_state() const { return this->state; }

    void initialize(const control_class_t&, 
        const CComPtr<ID3D11Device>&,
        const CComPtr<ID3D11DeviceContext>&,
        const std::wstring& symbolic_link);
};

typedef std::shared_ptr<source_vidcap> source_vidcap_t;

class stream_vidcap final : public stream_source_base<source_base<media_component_videomixer_args>>
{
private:
    source_vidcap_t source;
    void on_component_start(time_unit) override;
public:
    explicit stream_vidcap(const source_vidcap_t&);
};

typedef std::shared_ptr<stream_vidcap> stream_vidcap_t;