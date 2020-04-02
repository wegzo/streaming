#pragma once
#include "control_class.h"
#include "control_configurable.h"
#include "source_wasapi.h"
#include "transform_audiomixer2.h"
#include <vector>

struct control_wasapi_params : control_class_params
{
    struct device_info_t
    {
        bool capture;
        std::wstring device_id;
        std::wstring device_friendlyname;
    };

    device_info_t device_info;
};

using control_wasapi_params_t = std::shared_ptr<control_wasapi_params>;

class control_wasapi : 
    public control_class,
    public control_configurable_class
{
    friend class control_scene;
private:
    control_pipeline& pipeline;
    control_wasapi_params_t params, new_params;
    source_wasapi_t component;
    stream_audiomixer2_controller_t audiomixer_params;

    // stream and reference enable multiple controls to use the same
    // component;
    // for this to work, the topology building must have the same order
    // as the activation, which is guaranteed by the assumption that the
    // topology branches are built in the control_set_t order
    media_stream_t stream;
    const control_wasapi* reference;

    // control configurable
    control_configurable_class::params_t
        control_configurable_class::on_show_config_dialog(
            HWND parent, control_configurable_class::tag_t&&) override;

    void build_audio_topology(const media_stream_t& from,
        const media_stream_t& to, const media_topology_t&);
    void activate(const control_set_t& last_set, control_set_t& new_set);

    control_wasapi(control_set_t& active_controls, control_pipeline&);
public:

    // control configurable
    void control_configurable_class::set_params(
        const control_configurable_class::params_t& new_params) override;

    static std::vector<control_wasapi_params::device_info_t> list_wasapi_devices();

    bool is_same_device(const control_wasapi_params::device_info_t&) const;
    bool is_identical_control(const control_class_t&) const;
};