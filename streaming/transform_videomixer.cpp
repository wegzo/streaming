#include "transform_videomixer.h"
#include "assert.h"
#include <iostream>
#include <algorithm>
#include <limits>

#define CHECK_HR(hr_) {if(FAILED(hr_)) [[unlikely]] {goto done;}}

struct transform_videomixer::device_context_resources : media_buffer_texture
{
    bool drawing;
    CComPtr<ID2D1DeviceContext> ctx;
    // the texture that is bound to this brush must be immutable;
    // it is assumed that the input samples are immutable
    CComPtr<ID2D1BitmapBrush1> bitmap_brush;

    device_context_resources() : drawing(false) {}
    virtual ~device_context_resources() {}
    void uninitialize() {this->media_buffer_texture::uninitialize();}
};

transform_videomixer::transform_videomixer(
    const media_session_t& session, context_mutex_t context_mutex) :
    transform_videomixer_base(session), 
    context_mutex(context_mutex), 
    texture_pool(new buffer_pool),
    buffer_pool_video_frames(new buffer_pool_video_frames_t),
    buffer_pool_video_mixer_frames(new buffer_pool_video_mixer_frames_t)
{
}

transform_videomixer::~transform_videomixer()
{
    {
        buffer_pool::scoped_lock lock(this->texture_pool->mutex);
        this->texture_pool->dispose();
    }
    {
        buffer_pool_video_frames_t::scoped_lock lock(this->buffer_pool_video_frames->mutex);
        this->buffer_pool_video_frames->dispose();
    }
    {
        buffer_pool_video_mixer_frames_t::scoped_lock lock(this->buffer_pool_video_mixer_frames->mutex);
        this->buffer_pool_video_mixer_frames->dispose();
    }
}

void transform_videomixer::initialize(
    const control_class_t& ctrl_pipeline,
    UINT32 canvas_width, UINT32 canvas_height,
    const CComPtr<ID2D1Factory1>& d2d1factory,
    const CComPtr<ID2D1Device>& d2d1dev,
    const CComPtr<ID3D11Device>& d3d11dev,
    const CComPtr<ID3D11DeviceContext>& devctx)
{
    this->ctrl_pipeline = ctrl_pipeline;
    this->d3d11dev = d3d11dev;
    this->d3d11devctx = devctx;
    this->d2d1factory = d2d1factory;
    this->d2d1dev = d2d1dev;
    this->canvas_width = canvas_width;
    this->canvas_height = canvas_height;
}

transform_videomixer::stream_mixer_t transform_videomixer::create_derived_stream()
{
    return stream_videomixer_base_t(
        new stream_videomixer(this->shared_from_this<transform_videomixer>()));
}


/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////


stream_videomixer::stream_videomixer(const transform_videomixer_t& transform) :
    stream_mixer(transform),
    transform(transform)
{
}

void stream_videomixer::initialize_texture(const media_buffer_texture_t& texture)
{
    D3D11_TEXTURE2D_DESC desc;
    desc.Width = this->transform->canvas_width;
    desc.Height = this->transform->canvas_height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    texture->initialize(this->transform->d3d11dev, desc, NULL);
}

void stream_videomixer::initialize_resources(const device_context_resources_t& resources)
{
    HRESULT hr = S_OK;

    CComPtr<IDXGISurface> dxgisurface;
    D2D1_BITMAP_PROPERTIES1 output_bitmap_props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));

    this->initialize_texture(resources);

    // initialize resources
    CHECK_HR(hr = resources->texture->QueryInterface(&dxgisurface));
    CHECK_HR(hr = this->transform->d2d1dev->CreateDeviceContext(
        D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &resources->ctx));
    CHECK_HR(hr = resources->ctx->CreateBitmapFromDxgiSurface(
        dxgisurface, output_bitmap_props, &resources->bitmap));
    // set the initial source for bitmap brush to output texture;
    // it will be switched when mixing
    CHECK_HR(hr = resources->ctx->CreateBitmapBrush(resources->bitmap, &resources->bitmap_brush));

done:
    if(FAILED(hr))
        throw HR_EXCEPTION(hr);
}

stream_videomixer::device_context_resources_t stream_videomixer::acquire_buffer()
{
    transform_videomixer::buffer_pool::scoped_lock lock(this->transform->texture_pool->mutex);
    if(this->transform->texture_pool->is_empty())
    {
        device_context_resources_t resources = this->transform->texture_pool->acquire_buffer();
        lock.unlock();

        this->initialize_resources(resources);
        return resources;
    }
    else
    {
        device_context_resources_t resources = this->transform->texture_pool->acquire_buffer();
        lock.unlock();

        // common buffer pool objects must be initialized every time
        this->initialize_texture(resources);

        return resources;
    }
}

bool stream_videomixer::move_frames(in_arg_t& to, in_arg_t& from, const in_arg_t& reference,
    frame_unit end, bool discarded)
{
    assert_(reference);
    assert_(!to && !from);

    // optimized specialization if the whole sample is moved
    if(reference->sample && end >= reference->sample->get_end())
    {
        to = reference;
        to->frame_end = end;

        if(discarded)
            std::cout << "discarded video frames" << std::endl;

        return true;
    }

    // optimized specialization if nothing is moved
    if(reference->sample && end <= reference->sample->get_first())
    {
        to = std::make_optional<in_arg_t::value_type>();
        from = reference;

        to->frame_end = end;

        return false;
    }

    to = std::make_optional<in_arg_t::value_type>();
    from = std::make_optional<in_arg_t::value_type>();

    to->frame_end = end;
    from->frame_end = reference->frame_end;

    if(reference->sample)
    {
        {
            transform_videomixer::buffer_pool_video_mixer_frames_t::scoped_lock lock(
                this->transform->buffer_pool_video_mixer_frames->mutex);
            from->sample = this->transform->buffer_pool_video_mixer_frames->acquire_buffer();
        }

        from->sample->initialize(*reference->sample);

        if(!discarded)
        {
            transform_videomixer::buffer_pool_video_mixer_frames_t::scoped_lock lock(
                this->transform->buffer_pool_video_mixer_frames->mutex);
            to->sample = this->transform->buffer_pool_video_mixer_frames->acquire_buffer();
            to->sample->initialize();
        }

        const bool moved = from->sample->move_frames_to(to->sample.get(), end);
        if(moved && discarded)
            std::cout << "discarded video frames" << std::endl;
    }
    if(end >= from->frame_end)
        from.reset();

    // reset the samples of 'from' and 'to' if they contain no data,
    // because frame collections with empty data is not currently allowed
    if(from && from->sample && !from->sample->is_valid())
        from->sample.reset();
    if(to && to->sample && !to->sample->is_valid())
        to->sample.reset();

    return !from;
}

void stream_videomixer::mix(out_arg_t& out_arg, args_t& packets,
    frame_unit first, frame_unit end)
{
    // packets.container might be empty

    assert_(!out_arg);

    // arg in packets can be null when frame_count == 0 or
    // if a source passed null args on a drain point;
    // the whole leftover buffer is merged to packets

    HRESULT hr = S_OK;
    const frame_unit frame_count = end - first;
    assert_(frame_count > 0);

    // limit the processing videomixer does in a single take;
    // this greatly reduces the amount of vram being allocated when the pipeline is overloaded
    constexpr size_t maximum_frame_count = 1;
    frame_unit allowed_frames[maximum_frame_count];
    for(size_t i = 0; i < maximum_frame_count; i++)
        allowed_frames[i] = -1;

    // TODO: use z order instead of stream_index so that the user input can be instant
    // sort the packets list for correct z order
    std::sort(packets.container.begin(), packets.container.end(),
        [](const packet_t& a, const packet_t& b)
        {
            // user_params.z_order;
            // control_pipeline stores all stream controllers and control_scene
            // uses those to update z order

            // TODO: stream controller should be reinitialized on activate
            // (the params should be copied over)

            // set streams with invalid user params behind everything else
            return (a.stream_index < b.stream_index);
        });

    media_sample_video_frames::samples_t frames;
    for(frame_unit i = 0; i < frame_count; i++)
        frames.push_back(media_sample_video_frame(first + i));

    // indicates whether the out arg will have any encodeable frames
    bool has_frames = false;

    // TODO: audio mixer should allocate a buffer of end-first,
    // but after mixing it creates a buffer wrapper with min pos and max end of mixed samples;
    // this will make the audio mixing process equivalent to video mixing;
    // (this assumes that gaps won't occur between samples, which isn't true)
    // if it turns out that mp4 container doesn't support aac with gaps,
    // the media buffer wrapper ends can be extended to first and end;
    // the audio sources should be modified like in video sources to provide silent
    // buffers that cover gaps

    // TODO: the end of samples in case if the sample is empty should be properly defined

    media_sample_video_frames_t sample;

    // draw
    for(auto&& item : packets.container)
    {
        // stream_mixer::process might create empty args and samples
        if(!item.arg || !item.arg->sample)
            continue;

        assert_(end >= item.arg->sample->get_end());
        // empty frame collection isn't allowed
        assert_(item.arg->sample->is_valid());
        for(const auto& frame_ : item.arg->sample->get_frames())
        {
            has_frames = true;

            for(frame_unit pos = frame_.pos; pos < (frame_.pos + frame_.dur); pos++)
            {
                assert_(first <= pos);
                assert_(end > pos);

                // limit the amount of processing videomixer does in a single take;
                // this greatly reduces the amount of vram being allocated 
                // when the pipeline is overloaded
                bool allowed = false;
                for(size_t i = 0; i < maximum_frame_count; i++)
                {
                    if(allowed_frames[i] == -1)
                        allowed_frames[i] = pos;
                    if(allowed_frames[i] == pos)
                    {
                        allowed = true;
                        break;
                    }
                }
                if(!allowed)
                    continue;

                const size_t index = (size_t)(pos - first);
                device_context_resources_t frame =
                    std::static_pointer_cast<transform_videomixer::device_context_resources>(
                        frames[index].buffer);
                if(!frame)
                {
                    frame = this->acquire_buffer();
                    frames[index].buffer = frame;

                    frame->ctx->BeginDraw();
                    frame->drawing = true;
                    frame->ctx->SetTarget(frame->bitmap);
                    frame->ctx->Clear(D2D1::ColorF(D2D1::ColorF::Black));
                }

                if(frame_.buffer)
                {
                    CComPtr<ID3D11Texture2D> texture = frame_.buffer->texture;
                    // params is valid only when the frame stores a non silent buffer
                    const auto& params = frame_.params;
                    const auto& user_params = item.valid_user_params ? item.user_params : params;

                    /////////////////////////////////////////////////////////////////
                    /////////////////////////////////////////////////////////////////
                    /////////////////////////////////////////////////////////////////

                    // TODO: cache geometry

                    using namespace D2D1;
                    CComPtr<ID2D1Bitmap1> bitmap;
                    CComPtr<IDXGISurface> surface;
                    Matrix3x2F src_to_dest, src2_to_dest2;
                    Matrix3x2F world, brush;
                    bool invert;

                    // src_rect_m * M = dest_rect_m <=> M = src_rect_t -1 * dest_rect_m
                    src_to_dest = Matrix3x2F::Scale(
                        params.source_rect.right - params.source_rect.left,
                        params.source_rect.bottom - params.source_rect.top) *
                        Matrix3x2F::Translation(params.source_rect.left, params.source_rect.top);
                    invert = src_to_dest.Invert();
                    src_to_dest = src_to_dest * Matrix3x2F::Scale(
                        params.dest_rect.right - params.dest_rect.left,
                        params.dest_rect.bottom - params.dest_rect.top) *
                        Matrix3x2F::Translation(params.dest_rect.left, params.dest_rect.top) *
                        params.dest_m;

                    src2_to_dest2 = Matrix3x2F::Scale(
                        user_params.source_rect.right - user_params.source_rect.left,
                        user_params.source_rect.bottom - user_params.source_rect.top) *
                        Matrix3x2F::Translation(
                            user_params.source_rect.left, user_params.source_rect.top) *
                        user_params.source_m;
                    invert = src2_to_dest2.Invert();
                    src2_to_dest2 = src2_to_dest2 * Matrix3x2F::Scale(
                        user_params.dest_rect.right - user_params.dest_rect.left,
                        user_params.dest_rect.bottom - user_params.dest_rect.top) *
                        Matrix3x2F::Translation(user_params.dest_rect.left, user_params.dest_rect.top) *
                        user_params.dest_m;

                    world = src_to_dest * src2_to_dest2;
                    brush = params.source_m;

                    CHECK_HR(hr = texture->QueryInterface(&surface));
                    CHECK_HR(hr = frame->ctx->CreateBitmapFromDxgiSurface(
                        surface,
                        BitmapProperties1(
                            D2D1_BITMAP_OPTIONS_NONE,
                            PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
                        &bitmap));

                    frame->bitmap_brush->SetBitmap(bitmap);
                    /*frame->bitmap_brush->SetInterpolationMode1(D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);*/
                    frame->bitmap_brush->SetTransform(brush);

                    if(!user_params.axis_aligned_clip)
                    {
                        CComPtr<ID2D1RectangleGeometry> geometry;
                        D2D1_LAYER_PARAMETERS1 layer_params;
                        Matrix3x2F world_inverted, layer;

                        world_inverted = world;
                        invert = world_inverted.Invert();
                        layer = user_params.dest_m * world_inverted;

                        CHECK_HR(hr = this->transform->d2d1factory->CreateRectangleGeometry(
                            user_params.dest_rect, &geometry));
                        layer_params = LayerParameters1(
                            InfiniteRect(), geometry, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, layer);

                        frame->ctx->SetTransform(world);
                        frame->ctx->PushLayer(layer_params, NULL);
                    }
                    else
                    {
                        frame->ctx->SetTransform(user_params.dest_m);
                        // the world transform is applied to the axis aligned clip when push is called
                        frame->ctx->PushAxisAlignedClip(
                            user_params.dest_rect,
                            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

                        frame->ctx->SetTransform(world);
                    }

                    frame->ctx->FillRectangle(params.source_rect, frame->bitmap_brush);

                    if(!user_params.axis_aligned_clip)
                        frame->ctx->PopLayer();
                    else
                        frame->ctx->PopAxisAlignedClip();
                }
            }
        }
    }

done:
    // call end draw for every frame
    for(frame_unit i = 0; i < frame_count; i++)
    {
        device_context_resources_t frame =
            std::static_pointer_cast<transform_videomixer::device_context_resources>(
                frames[(size_t)i].buffer);
        if(frame && frame->drawing)
        {
            frame->drawing = false;
            const HRESULT hr2 = frame->ctx->EndDraw();
            if(FAILED(hr2) && hr == S_OK)
                hr = hr2;
        }
    }

    if(FAILED(hr))
    {
        if(hr != D2DERR_RECREATE_TARGET)
            throw HR_EXCEPTION(hr);

        if(FAILED(hr))
            PRINT_ERROR(hr);

        this->transform->request_reinitialization(this->transform->ctrl_pipeline);
    }
    else
    {
        {
            transform_videomixer::buffer_pool_video_frames_t::scoped_lock lock(
                this->transform->buffer_pool_video_frames->mutex);
            sample = this->transform->buffer_pool_video_frames->acquire_buffer();
        }
        assert_(end > 0);
        sample->initialize(std::move(frames), first, end);

        out_arg = std::make_optional<out_arg_t::value_type>();
        out_arg->sample = std::move(sample);
        out_arg->has_frames = has_frames;
    }
}


/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////


void stream_videomixer_controller::get_params(params_t& params) const
{
    scoped_lock lock(this->mutex);
    params = this->params;
}

void stream_videomixer_controller::set_params(const params_t& params)
{
    scoped_lock lock(this->mutex);
    this->params = params;
}