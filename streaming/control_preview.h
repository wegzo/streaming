#pragma once
#include "control_class.h"
#include "gui_previewwnd.h"
#include "sink_preview2.h"
#include <d2d1.h>

#define DEFAULT_PREVIEW_FPS 60 // 100fps is the maximum currently

class control_preview : public control_class
{
    friend class control_pipeline;
private:
    control_pipeline& pipeline;
    sink_preview2_t component;
    HWND parent;
    frame_unit fps;

    // control_class
    void build_video_topology(const media_stream_t& from,
        const media_stream_t& to, const media_topology_t&);
    void activate(const control_set_t& last_set, control_set_t& new_set);

    control_preview(control_set_t& active_controls, control_pipeline&);
public:
    gui_previewwnd wnd_preview;

    // can be null
    const sink_preview2_t& get_component() const { return this->component; }
    // in preview window coordinates;
    // (canvas rect)
    D2D1_RECT_F get_preview_rect() const { return this->wnd_preview.get_preview_rect(); }
    void get_canvas_size(UINT32& width, UINT32& height) const;

    void set_parent(HWND parent) { this->parent = parent; }

    void set_fps(frame_unit fps) 
    { this->fps = fps; this->wnd_preview.set_timer(1000 / (UINT)this->fps - 1); }
    frame_unit get_fps() const { return this->fps; }

    void set_state(bool render);
    bool is_identical_control(const control_class_t&) const;
};