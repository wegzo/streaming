#include "control_vidcap.h"
#include "control_pipeline.h"

#define CHECK_HR(hr_) {if(FAILED(hr_)) [[unlikely]] {goto done;}}

class gui_vidcapdlg final : public CDialogImpl<gui_vidcapdlg>
{
private:
    control_vidcap_params_t current_params;
    CComboBox combo_device;
public:
    enum { IDD = IDD_DIALOG_VIDCAP_CONF };

    explicit gui_vidcapdlg(const control_vidcap_params_t& current_params);

    control_vidcap_params_t new_params;
    std::vector<control_vidcap_params::device_info_t> devices;

    BEGIN_MSG_MAP(gui_vidcapdlg)
        MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
        COMMAND_HANDLER(IDOK, BN_CLICKED, OnBnClickedOk)
        COMMAND_HANDLER(IDCANCEL, BN_CLICKED, OnBnClickedCancel)
    END_MSG_MAP()

    LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
    LRESULT OnBnClickedOk(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
    LRESULT OnBnClickedCancel(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
};

gui_vidcapdlg::gui_vidcapdlg(const control_vidcap_params_t& current_params) :
    current_params(current_params)
{
    assert_(this->current_params);
}

LRESULT gui_vidcapdlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    this->combo_device.Attach(this->GetDlgItem(IDC_COMBO1));

    // populate devices combo
    this->devices = control_vidcap::list_vidcap_devices();

    int selected = 0, i = 0;
    for(const auto& item : this->devices)
    {
        if(item.symbolic_link == this->current_params->device_info.symbolic_link)
            selected = i;

        this->combo_device.AddString(item.friendly_name.c_str());
        i++;
    }

    if(!this->devices.empty())
        this->combo_device.SetCurSel(selected);

    return 0;
}

LRESULT gui_vidcapdlg::OnBnClickedOk(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
    const int cur_sel = this->combo_device.GetCurSel();

    if(cur_sel >= 0 && cur_sel < (int)this->devices.size())
    {
        this->new_params = std::make_shared<control_vidcap_params>();
        this->new_params->device_info = this->devices[cur_sel];
    }

    this->EndDialog(IDOK);
    return 0;
}

LRESULT gui_vidcapdlg::OnBnClickedCancel(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
    this->EndDialog(IDCANCEL);
    return 0;
}


/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////


control_vidcap::control_vidcap(control_set_t& active_controls,
    control_pipeline& pipeline) :
    control_video(active_controls, pipeline),
    pipeline(pipeline),
    videomixer_params(new stream_videomixer_controller),
    reference(nullptr),
    params(new control_vidcap_params)
{
    this->apply_default_video_params();
}

void control_vidcap::build_video_topology(const media_stream_t& from,
    const media_stream_t& to, const media_topology_t& topology)
{
    if(!this->component)
        return;

    assert_(!this->disabled);

    stream_videomixer_base_t videomixer_stream = 
        std::dynamic_pointer_cast<stream_videomixer_base>(to);
    if(!videomixer_stream)
        throw HR_EXCEPTION(E_UNEXPECTED);

    // only add the component to the topology if it's not waiting for a device
    if(!this->reference)
    {
        if(this->component->get_state() == source_vidcap::INITIALIZED)
        {
            media_stream_t vidcap_stream =
                this->component->create_stream(topology->get_message_generator());

            if(from)
                vidcap_stream->connect_streams(from, topology);
            videomixer_stream->connect_streams(vidcap_stream, this->videomixer_params, topology);

            this->stream = vidcap_stream;
        }
    }
    else
    {
        if(this->reference->component->get_state() == source_vidcap::INITIALIZED)
        {
            assert_(this->reference->stream);
            assert_(!this->stream);

            // only connect from this stream to 'to' stream
            // (since this a duplicate control from the original)
            videomixer_stream->connect_streams(
                this->reference->stream, this->videomixer_params, topology);
        }
    }
}

void control_vidcap::activate(const control_set_t& last_set, control_set_t& new_set)
{
    source_vidcap_t component;

    this->stream = nullptr;
    this->reference = nullptr;

    if(this->new_params)
    {
        this->component = nullptr;
        this->params = this->new_params;
    }

    if(this->disabled)
        goto out;

    for(auto&& control : new_set)
    {
        if(this->is_identical_control(control))
        {
            const control_vidcap* vidcap_control = (const control_vidcap*)control.get();
            this->reference = vidcap_control;
            component = vidcap_control->component;

            break;
        }
    }

    if(!component)
    {
        for(auto&& control : last_set)
        {
            if(this->is_identical_control(control))
            {
                const control_vidcap* vidcap_control = (const control_vidcap*)control.get();
                component = vidcap_control->component;

                break;
            }
        }

        if(!component)
        {
            source_vidcap_t vidcap_source(
                new source_vidcap(this->pipeline.session, this->pipeline.context_mutex));
            vidcap_source->initialize(this->pipeline.shared_from_this<control_pipeline>(),
                this->pipeline.d3d11dev,
                this->pipeline.devctx,
                this->params->device_info.symbolic_link);

            component = vidcap_source;
        }
    }

    new_set.push_back(this->shared_from_this<control_vidcap>());

    if(this->new_params)
    {
        this->new_params = nullptr;
        this->apply_default_video_params();
    }

out:
    this->component = component;

    if(this->component)
    {
        // update the transformations when the new control_vidcap activates;
        // this allows components to reactivate the active scene and update their native size
        this->control_video::apply_transformation(false);
        this->control_video::apply_transformation(true);

        this->event_provider.for_each([this](gui_event_handler* e) { e->on_activate(this, false); });
    }
    else
        this->event_provider.for_each([this](gui_event_handler* e) { e->on_activate(this, true); });
}

control_configurable_class::params_t
control_vidcap::on_show_config_dialog(HWND parent, control_configurable_class::tag_t&&)
{
    gui_vidcapdlg dlg(this->params);
    const INT_PTR ret = dlg.DoModal(parent);

    // do not update parameters if they are the same
    if(dlg.new_params && this->is_same_device(dlg.new_params->device_info))
        return nullptr;

    return dlg.new_params;
}

void control_vidcap::set_params(const control_configurable_class::params_t& new_params)
{
    control_vidcap_params_t params =
        std::dynamic_pointer_cast<control_vidcap_params>(new_params);
    
    if(!params)
        throw HR_EXCEPTION(E_UNEXPECTED);

    this->new_params = params;
}

std::vector<control_vidcap_params::device_info_t> control_vidcap::list_vidcap_devices()
{
    std::vector<control_vidcap_params::device_info_t> device_list;

    HRESULT hr = S_OK;
    CComPtr<IMFAttributes> attributes;
    IMFActivate** devices = nullptr;

    CHECK_HR(hr = MFCreateAttributes(&attributes, 1));
    CHECK_HR(hr = attributes->SetGUID(
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
        MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID));

    UINT32 count;
    CHECK_HR(hr = MFEnumDeviceSources(attributes, &devices, &count));
    for(UINT32 i = 0; i < count; i++)
    {
        control_vidcap_params::device_info_t device_info;

        WCHAR* friendly_name = nullptr, *symbolic_link = nullptr;
        UINT32 len;
        CHECK_HR(hr = devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
            &friendly_name, &len));
        device_info.friendly_name = friendly_name;
        CoTaskMemFree(friendly_name);

        CHECK_HR(hr = devices[i]->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
            &symbolic_link, &len));
        device_info.symbolic_link = symbolic_link;
        CoTaskMemFree(symbolic_link);

        device_list.push_back(std::move(device_info));
    }

done:
    if(FAILED(hr))
        throw HR_EXCEPTION(hr);

    return device_list;
}

D2D1_RECT_F control_vidcap::get_rectangle(bool /*dest_params*/) const
{
    const source_vidcap_t& component = this->reference ?
        this->reference->component : this->component;

    if(component)
    {
        UINT32 width, height;
        component->get_size(width, height);

        return D2D1::RectF(0.f, 0.f, (FLOAT)width, (FLOAT)height);
    }
    else
        return D2D1::RectF();
}

void control_vidcap::apply_transformation(
    const D2D1::Matrix3x2F&& transformation, bool dest_params)
{
    const D2D1_RECT_F rect = this->get_rectangle(dest_params);
    stream_videomixer_controller::params_t params;

    this->videomixer_params->get_params(params);

    if(dest_params)
    {
        const video_params_t video_params = this->get_video_params(dest_params);
        params.dest_rect = rect;
        params.dest_m = transformation;
        params.axis_aligned_clip = ((video_params.rotate / 90.f) ==
            std::round(video_params.rotate / 90.f));
    }
    else
    {
        params.source_rect = rect;
        params.source_m = transformation;
    }

    this->videomixer_params->set_params(params);
}

void control_vidcap::set_default_video_params(video_params_t& video_params, bool dest_params)
{
    video_params.rotate = dest_params ? 0.f : 0.f;
    video_params.translate = dest_params ? D2D1::Point2F(100.f, 100.f) : D2D1::Point2F();
    video_params.scale = D2D1::Point2F(1.f, 1.f);
}

bool control_vidcap::is_same_device(const control_vidcap_params::device_info_t& dev_info) const
{
    return (this->params->device_info.symbolic_link == dev_info.symbolic_link);
}

bool control_vidcap::is_identical_control(const control_class_t& control) const
{
    const control_vidcap* vidcap_control = dynamic_cast<const control_vidcap*>(control.get());

    if(!vidcap_control || !vidcap_control->component)
        return false;

    if(vidcap_control->component->get_instance_type() == media_component::INSTANCE_NOT_SHAREABLE)
        return false;

    if(vidcap_control->component->session != this->pipeline.session)
        return false;

    return this->is_same_device(vidcap_control->params->device_info);
}