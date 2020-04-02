#pragma once

#include <Windows.h>
#include <memory>
#include <type_traits>

// base class for configurable controls

// control class previewing can be added simply by including the control to
// current pipeline, but hiding it so it won't be composited to final image

struct control_class_params;
struct control_video_params;

template<typename Params>
class control_configurable
{
    static_assert(std::is_empty_v<typename Params::tag_t>);
public:
    using params_value_type = Params;
    using params_t = std::shared_ptr<params_value_type>;
    using tag_t = typename params_value_type::tag_t;
protected:
    // the tag argument is here just so that the msvc compiler is able to compile
    virtual params_t on_show_config_dialog(HWND parent, tag_t&&) = 0;
public:
    virtual ~control_configurable() = default;

    params_t show_config_dialog(HWND parent) { return this->on_show_config_dialog(parent, {}); }
    virtual void set_params(const params_t& new_params) = 0;
};

using control_configurable_video = control_configurable<control_video_params>;
using control_configurable_class = control_configurable<control_class_params>;