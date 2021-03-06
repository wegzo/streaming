#include "transform_color_converter.h"
#include <d3d11_1.h>
#include <iostream>
#include <mfapi.h>
#include <Mferror.h>

#define CHECK_HR(hr_) {if(FAILED(hr_)) [[unlikely]] {goto done;}}

transform_color_converter::transform_color_converter(
    const media_session_t& session, context_mutex_t context_mutex) :
    media_component(session),
    texture_pool(new buffer_pool),
    buffer_pool_video_frames(new buffer_pool_video_frames_t),
    context_mutex(context_mutex)
{
}

transform_color_converter::~transform_color_converter()
{
    {
        // dispose the pool so that the cyclic dependency between the wrapped container and its
        // elements is broken
        buffer_pool::scoped_lock lock(this->texture_pool->mutex);
        this->texture_pool->dispose();
    }
    {
        buffer_pool_video_frames_t::scoped_lock lock(this->buffer_pool_video_frames->mutex);
        this->buffer_pool_video_frames->dispose();
    }
}

void transform_color_converter::initialize(
    const control_class_t& ctrl_pipeline,
    UINT32 frame_width_in, UINT32 frame_height_in,
    UINT32 frame_width_out, UINT32 frame_height_out,
    DXGI_COLOR_SPACE_TYPE color_space,
    const CComPtr<ID3D11Device>& d3d11dev, ID3D11DeviceContext* devctx)
{
    HRESULT hr = S_OK;

    this->ctrl_pipeline = ctrl_pipeline;
    this->d3d11dev = d3d11dev;
    CHECK_HR(hr = this->d3d11dev->QueryInterface(&this->videodevice));
    CHECK_HR(hr = devctx->QueryInterface(&this->videocontext));
    this->frame_width_in = frame_width_in;
    this->frame_height_in = frame_height_in;
    this->frame_width_out = frame_width_out;
    this->frame_height_out = frame_height_out;
    this->color_space = color_space;
    
    // check the supported capabilities of the video processor
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc;
    desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    desc.InputFrameRate.Numerator = (UINT)this->session->frame_rate_num;
    desc.InputFrameRate.Denominator = (UINT)this->session->frame_rate_den;
    desc.InputWidth = this->frame_width_in;
    desc.InputHeight = this->frame_height_in;
    desc.OutputFrameRate.Numerator = (UINT)this->session->frame_rate_num;
    desc.OutputFrameRate.Denominator = (UINT)this->session->frame_rate_den;
    desc.OutputWidth = this->frame_width_out;
    desc.OutputHeight = this->frame_height_out;
    desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    CHECK_HR(hr = this->videodevice->CreateVideoProcessorEnumerator(&desc, &this->enumerator));
    UINT flags;
    // https://msdn.microsoft.com/en-us/library/windows/desktop/mt427455(v=vs.85).aspx
    // b8g8r8a8 and nv12 must be supported by direct3d 11 devices
    // as video processor input and output;
    // it must be also supported by texture2d for render target
    CHECK_HR(hr = this->enumerator->CheckVideoProcessorFormat(DXGI_FORMAT_B8G8R8A8_UNORM, &flags));
    if(!(flags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT))
        CHECK_HR(hr = E_UNEXPECTED);
    CHECK_HR(hr = this->enumerator->CheckVideoProcessorFormat(DXGI_FORMAT_NV12, &flags));
    if(!(flags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT))
        CHECK_HR(hr = E_UNEXPECTED);

done:
    if(FAILED(hr))
        throw HR_EXCEPTION(hr);
}

media_stream_t transform_color_converter::create_stream()
{
    return media_stream_t(
        new stream_color_converter(this->shared_from_this<transform_color_converter>()));
}


/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////


stream_color_converter::stream_color_converter(const transform_color_converter_t& transform) :
    transform(transform)
{
    HRESULT hr = S_OK;

    // TODO: decide if the transform should create the video processor,
    // because this might be a somewhat heavy operation

    CHECK_HR(hr = this->transform->videodevice->CreateVideoProcessor(
        this->transform->enumerator, 0, &this->videoprocessor));

    // Auto stream processing(the default) can hurt power consumption
    {
        scoped_lock lock(*this->transform->context_mutex);
        this->transform->videocontext->VideoProcessorSetStreamAutoProcessingMode(
            this->videoprocessor, 0, FALSE);
    }

    // set the output color space that is commonly used with h264 encoding
    {
        CComPtr<ID3D11VideoContext1> video_context;
        CHECK_HR(hr = this->transform->videocontext->QueryInterface(&video_context));

        scoped_lock lock(*this->transform->context_mutex);
        // DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709 partial range
        // DXGI_COLOR_SPACE_YCBCR_FULL_G22_LEFT_P709 for full range
        video_context->VideoProcessorSetOutputColorSpace1(
            this->videoprocessor, 
            this->transform->color_space);

        // srgb
        // TODO: hdr input formats need to be handled somehow
        video_context->VideoProcessorSetStreamColorSpace1(
            this->videoprocessor, 0, DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    }
done:
    if(FAILED(hr))
        throw HR_EXCEPTION(hr);
}

void stream_color_converter::initialize_buffer(const media_buffer_texture_t& buffer)
{
    // create output texture with nv12 color format
    D3D11_TEXTURE2D_DESC desc;
    desc.Width = this->transform->frame_width_out;
    desc.Height = this->transform->frame_height_out;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.Format = DXGI_FORMAT_NV12;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    buffer->initialize(this->transform->d3d11dev, desc, nullptr);
}

media_buffer_texture_t stream_color_converter::acquire_buffer()
{
    transform_color_converter::buffer_pool::scoped_lock lock(this->transform->texture_pool->mutex);
    media_buffer_texture_t buffer = this->transform->texture_pool->acquire_buffer();
    lock.unlock();

    this->initialize_buffer(buffer);
    return buffer;
}

void stream_color_converter::process(media_component_h264_encoder_args_t& this_args,
    const request_packet& this_rp)
{
    HRESULT hr = S_OK;
    media_component_h264_encoder_args_t args;
    media_sample_video_frames_t frames;

    if(!this_args)
        goto done;

    {
        transform_color_converter::buffer_pool_video_frames_t::scoped_lock lock(
            this->transform->buffer_pool_video_frames->mutex);
        frames = this->transform->buffer_pool_video_frames->acquire_buffer();
    }
    frames->initialize();
    /*frames->end = this_args->sample->end;*/

    for(const auto& item : this_args->sample->get_frames())
    {
        media_sample_video_frame frame(item.pos);

        if(item.buffer)
        {
            // TODO: acquire buffer here should also allocate device resources the same way
            // videomixer does
            CComPtr<ID3D11Texture2D> texture = item.buffer->texture;
            media_buffer_texture_t output_buffer = this->acquire_buffer();
            assert_(texture);

#ifdef _DEBUG
            {
                D3D11_TEXTURE2D_DESC desc;
                texture->GetDesc(&desc);
                assert_(desc.Width == this->transform->frame_width_in);
                assert_(desc.Height == this->transform->frame_height_in);
            }
#endif

            // create the output view
            CComPtr<ID3D11VideoProcessorOutputView> output_view;
            D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC view_desc;
            view_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
            view_desc.Texture2D.MipSlice = 0;
            // TODO: include this outputview in the pool aswell
            CHECK_HR(hr = this->transform->videodevice->CreateVideoProcessorOutputView(
                output_buffer->texture, this->transform->enumerator, &view_desc, &output_view));

            // create the input view for the sample to be converted
            CComPtr<ID3D11VideoProcessorInputView> input_view;
            D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC desc;
            desc.FourCC = 0; // uses the same format the input resource has
            desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
            desc.Texture2D.MipSlice = 0;
            desc.Texture2D.ArraySlice = 0;
            CHECK_HR(hr = this->transform->videodevice->CreateVideoProcessorInputView(
                texture, this->transform->enumerator, &desc, &input_view));

            // convert
            D3D11_VIDEO_PROCESSOR_STREAM stream;
            RECT dst_rect;
            dst_rect.top = dst_rect.left = 0;
            dst_rect.right = this->transform->frame_width_out;
            dst_rect.bottom = this->transform->frame_height_out;

            stream.Enable = TRUE;
            stream.OutputIndex = 0;
            stream.InputFrameOrField = 0;
            stream.PastFrames = 0;
            stream.FutureFrames = 0;
            stream.ppPastSurfaces = NULL;
            stream.pInputSurface = input_view;
            stream.ppFutureSurfaces = NULL;
            stream.ppPastSurfacesRight = NULL;
            stream.pInputSurfaceRight = NULL;
            stream.ppFutureSurfacesRight = NULL;

            scoped_lock lock(*this->transform->context_mutex);

            // set the target rectangle for the output
            // (sets the rectangle where the output blit on the output texture will appear)
            this->transform->videocontext->VideoProcessorSetOutputTargetRect(
                this->videoprocessor, TRUE, &dst_rect);

            // set the source rectangle of the stream
            // (the part of the stream texture which will be included in the blit);
            // false indicates that the whole source is read
            this->transform->videocontext->VideoProcessorSetStreamSourceRect(
                this->videoprocessor, 0, FALSE, &dst_rect);

            // set the destination rectangle of the stream
            // (where the stream will appear in the output blit)
            this->transform->videocontext->VideoProcessorSetStreamDestRect(
                this->videoprocessor, 0, TRUE, &dst_rect);

            // blit
            const UINT stream_count = 1;
            CHECK_HR(hr = this->transform->videocontext->VideoProcessorBlt(
                this->videoprocessor, output_view,
                0, stream_count, &stream));

            frame.buffer = output_buffer;
        }

        frames->add_consecutive_frames(frame);
    }

done:
    if(FAILED(hr))
    {
        PRINT_ERROR(hr);
        this->transform->request_reinitialization(this->transform->ctrl_pipeline);
    }

    args = this_args;
    if(args)
        args->sample = frames;

    // set the args in pending packet to null so that the sample can be reused
    this_args.reset();

    // give the sample to downstream
    this->transform->session->give_sample(this, args.has_value() ? &(*args) : NULL, this_rp);
}

media_stream::result_t stream_color_converter::request_sample(const request_packet& rp, const media_stream*)
{
    if(!this->transform->session->request_sample(this, rp))
        return FATAL_ERROR;
    return OK;
}

media_stream::result_t stream_color_converter::process_sample(
    const media_component_args* args_, const request_packet& rp, const media_stream*)
{
    media_component_h264_encoder_args_t args;
    if(args_)
    {
        args = std::make_optional(
            static_cast<const media_component_h264_encoder_args&>(*args_));
        assert_(args->is_valid());
    }

    this->process(args, rp);
    return OK;
}