#pragma once
#include "control_class.h"
#include "control_video.h"
#include "source_displaycapture.h"
#include "transform_videomixer.h"
#include <vector>

class control_pipeline;
using control_pipeline_t = std::shared_ptr<control_pipeline>;

struct control_displaycapture_params : control_class_params
{
    struct device_info_t
    {
        UINT adapter_ordinal, output_ordinal;
        DXGI_ADAPTER_DESC1 adapter;
        DXGI_OUTPUT_DESC output;
    };

    device_info_t device_info;
};

using control_displaycapture_params_t = std::shared_ptr<control_displaycapture_params>;

class control_displaycapture : 
    public control_video,
    public control_configurable_class
{
    friend class control_scene;
private:
    control_pipeline& pipeline;
    control_displaycapture_params_t params, new_params;
    source_displaycapture_t component;
    stream_videomixer_controller_t videomixer_params;

    media_stream_t stream, pointer_stream;
    const control_displaycapture* reference;

    // control_class
    void build_video_topology(const media_stream_t& from,
        const media_stream_t& to, const media_topology_t&);
    void activate(const control_set_t& last_set, control_set_t& new_set);

    // control configurable
    control_configurable_class::params_t
        control_configurable_class::on_show_config_dialog(
            HWND parent, control_configurable_class::tag_t&&) override;

    // control_video
    void apply_transformation(const D2D1::Matrix3x2F&&, bool dest_params);
    void set_default_video_params(video_params_t&, bool dest_params);

    control_displaycapture(control_set_t& active_controls, control_pipeline&);
public:
    // control_video
    D2D1_RECT_F get_rectangle(bool dest_params) const;

    // control configurable
    void control_configurable_class::set_params(
        const control_configurable_class::params_t& new_params) override;

    static std::vector<control_displaycapture_params::device_info_t> 
        list_displaycapture_devices(IDXGIFactory1* factory);

    // TODO: set displaycapture params sets the initial videoprocessor params aswell

    bool is_same_device(const control_displaycapture_params::device_info_t&) const;
    bool is_identical_control(const control_class_t&) const;
};