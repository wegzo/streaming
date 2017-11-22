#pragma once
#include "media_topology.h"
#include "source_loopback.h"
#include "source_displaycapture5.h"
#include <mmdeviceapi.h>
#include <string>
#include <vector>
#include <utility>

// control_scene needs to maintain a link between the component and the name of the item;
// components are shared between scenes

class control_pipeline;

class control_scene
{
    friend class control_pipeline;
public:
    struct displaycapture_item
    {
        UINT adapter_ordinal, output_ordinal;
        DXGI_ADAPTER_DESC1 adapter;
        DXGI_OUTPUT_DESC output;
    };
    struct audio_item
    {
        bool capture;
        std::wstring item_name;
        std::wstring device_id;
        std::wstring device_friendlyname;
    };
private:
    control_pipeline& pipeline;
    media_topology_t video_topology, audio_topology;

    std::vector<displaycapture_item> displaycapture_items;
    std::vector<audio_item> audio_items;

    std::vector<std::pair<displaycapture_item, source_displaycapture5_t>> displaycapture_sources;
    std::vector<std::pair<audio_item, source_loopback_t>> audio_sources;

    bool list_available_audio_items(std::vector<audio_item>& audios, EDataFlow);

    void reset_topology(bool create_new);
    // called by pipeline
    void activate_scene();
    void deactivate_scene();

    explicit control_scene(control_pipeline&);
public:
    std::wstring scene_name;

    // returns false if nothing was found
    bool list_available_displaycapture_items(std::vector<displaycapture_item>& displaycaptures);
    bool list_available_audio_items(std::vector<audio_item>& audios);

    void add_displaycapture_item(const displaycapture_item&);
    /*void remove_video(const std::wstring& item_name);*/
    /*void rename_video(const std::wstring& old_name, const std::wstring& new_name);*/

    void add_audio_item(const audio_item&);
    /*void remove_audio(const std::wstring& item_name);*/
};