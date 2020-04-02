#pragma once
#include "control_class.h"
#include "control_video.h"
#include "control_configurable.h"
#include "source_vidcap.h"
#include "transform_videomixer.h"
#include <string>
#include <vector>

class control_pipeline;
using control_pipeline_t = std::shared_ptr<control_pipeline>;

struct control_vidcap_params : control_class_params
{
    struct device_info_t
    {
        std::wstring friendly_name;
        std::wstring symbolic_link;
    };

    device_info_t device_info;
    // TODO: vidcap params also include the parameters that were used to initialize
    // the mf media source object
};

using control_vidcap_params_t = std::shared_ptr<control_vidcap_params>;

class control_vidcap final : 
    public control_video,
    public control_configurable_class
{
    friend class control_scene;
private:
    control_pipeline& pipeline;
    control_vidcap_params_t params, new_params;
    source_vidcap_t component;
    stream_videomixer_controller_t videomixer_params;

    media_stream_t stream;
    const control_vidcap* reference;

    // control class
    void build_video_topology(const media_stream_t& from,
        const media_stream_t& to, const media_topology_t&);
    void activate(const control_set_t& last_set, control_set_t& new_set);

    // control configurable
    control_configurable_class::params_t
        control_configurable_class::on_show_config_dialog(
            HWND parent, control_configurable_class::tag_t&&) override;

    // control video
    void apply_transformation(const D2D1::Matrix3x2F&&, bool dest_params);
    void set_default_video_params(video_params_t&, bool dest_params);

    control_vidcap(control_set_t& active_controls, control_pipeline&);
public:
    // control video
    D2D1_RECT_F get_rectangle(bool dest_params) const;

    // control configurable
    void control_configurable_class::set_params(
        const control_configurable_class::params_t& new_params) override;

    static std::vector<control_vidcap_params::device_info_t> list_vidcap_devices();

    bool is_same_device(const control_vidcap_params::device_info_t&) const;
    bool is_identical_control(const control_class_t&) const;
};