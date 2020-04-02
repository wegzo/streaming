#include "control_displaycapture.h"
#include "control_pipeline.h"
#include <sstream>

#define CHECK_HR(hr_) {if(FAILED(hr_)) [[unlikely]] {goto done;}}

class gui_displaycapturedlg final : public CDialogImpl<gui_displaycapturedlg>
{
private:
    const control_pipeline& ctrl_pipeline;
    control_displaycapture_params_t current_params;
    CComboBox combo_device;
public:
    enum { IDD = IDD_DIALOG_DISPLAYCAPTURE_CONF };

    explicit gui_displaycapturedlg(
        const control_pipeline&,
        const control_displaycapture_params_t& current_params);

    control_displaycapture_params_t new_params;
    std::vector<control_displaycapture_params::device_info_t> devices;

    BEGIN_MSG_MAP(gui_displaycapturedlg)
        MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
        COMMAND_HANDLER(IDOK, BN_CLICKED, OnBnClickedOk)
        COMMAND_HANDLER(IDCANCEL, BN_CLICKED, OnBnClickedCancel)
    END_MSG_MAP()

    LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
    LRESULT OnBnClickedOk(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
    LRESULT OnBnClickedCancel(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
};

gui_displaycapturedlg::gui_displaycapturedlg(
    const control_pipeline& ctrl_pipeline,
    const control_displaycapture_params_t& current_params) :
    ctrl_pipeline(ctrl_pipeline),
    current_params(current_params)
{
    assert_(this->current_params);
}

LRESULT gui_displaycapturedlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    this->combo_device.Attach(this->GetDlgItem(IDC_COMBO1));

    // populate devices combo
    this->devices = 
        control_displaycapture::list_displaycapture_devices(this->ctrl_pipeline.dxgifactory);

    int selected = 0, i = 0;
    for(const auto& item : this->devices)
    {
        // TODO: is same device should be used here
        if(item.adapter_ordinal == this->current_params->device_info.adapter_ordinal &&
            item.output_ordinal == this->current_params->device_info.output_ordinal)
            selected = i;

        const LONG w = std::abs(
            item.output.DesktopCoordinates.right - item.output.DesktopCoordinates.left);
        const LONG h =
            std::abs(item.output.DesktopCoordinates.bottom - item.output.DesktopCoordinates.top);

        std::wstringstream sts;
        sts << item.adapter.Description << L": Monitor ";
        sts << item.output_ordinal << L": " << w << L"x" << h << L" @ ";
        sts << item.output.DesktopCoordinates.left << L","
            << item.output.DesktopCoordinates.bottom;

        this->combo_device.AddString(sts.str().c_str());
        i++;
    }

    if(!this->devices.empty())
        this->combo_device.SetCurSel(selected);

    return 0;
}

LRESULT gui_displaycapturedlg::OnBnClickedOk(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
    const int cur_sel = this->combo_device.GetCurSel();

    if(cur_sel >= 0 && cur_sel < (int)this->devices.size())
    {
        this->new_params = std::make_shared<control_displaycapture_params>();
        this->new_params->device_info = this->devices[cur_sel];
    }

    this->EndDialog(IDOK);
    return 0;
}

LRESULT gui_displaycapturedlg::OnBnClickedCancel(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
    this->EndDialog(IDCANCEL);
    return 0;
}


/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////


control_displaycapture::control_displaycapture(control_set_t& active_controls, 
    control_pipeline& pipeline) :
    control_video(active_controls, pipeline),
    videomixer_params(new stream_videomixer_controller),
    pipeline(pipeline),
    reference(nullptr),
    params(new control_displaycapture_params)
{
}

void control_displaycapture::build_video_topology(const media_stream_t& from,
    const media_stream_t& to, const media_topology_t& topology)
{
    if(!this->component)
        return;

    assert_(!this->disabled);

    stream_videomixer_base_t videomixer_stream =
        std::dynamic_pointer_cast<stream_videomixer_base>(to);
    if(!videomixer_stream)
        throw HR_EXCEPTION(E_UNEXPECTED);

    if(!this->reference)
    {
        source_displaycapture::stream_source_base_t displaycapture_stream =
            this->component->create_stream(topology->get_message_generator());
        media_stream_t displaycapture_pointer_stream =
            this->component->create_pointer_stream(
                std::static_pointer_cast<stream_displaycapture>(displaycapture_stream));

        // connect from the 'from' stream to this stream
        if(from)
        {
            displaycapture_pointer_stream->connect_streams(from, topology);
            displaycapture_stream->connect_streams(from, topology);
        }

        // connect from this stream to 'to' stream
        videomixer_stream->connect_streams(displaycapture_pointer_stream,
            this->videomixer_params, topology);
        videomixer_stream->connect_streams(displaycapture_stream,
            this->videomixer_params, topology);

        this->stream = displaycapture_stream;
        this->pointer_stream = displaycapture_pointer_stream;
    }
    else
    {
        assert_(this->reference->stream);
        assert_(this->reference->pointer_stream);
        assert_(!this->stream);

        // only connect from this stream to 'to' stream
        // (since this a duplicate control from the original)
        videomixer_stream->connect_streams(
            this->reference->pointer_stream, this->videomixer_params, topology);
        videomixer_stream->connect_streams(
            this->reference->stream, this->videomixer_params, topology);
    }
}

void control_displaycapture::activate(const control_set_t& last_set, control_set_t& new_set)
{
    source_displaycapture_t component;

    this->stream = nullptr;
    this->pointer_stream = nullptr;
    this->reference = nullptr;

    if(this->new_params)
    {
        this->component = nullptr;
        this->params = this->new_params;
    }

    if(this->disabled)
        goto out;

    // try to find a control to reference in the new set
    for(auto&& control : new_set)
    {
        if(this->is_identical_control(control))
        {
            const control_displaycapture* displaycapture_control = 
                (const control_displaycapture*)control.get();
            this->reference = displaycapture_control;
            component = displaycapture_control->component;

            break;
        }
    }

    if(!component)
    {
        // try to reuse the component stored in the last set's control
        for(auto&& control : last_set)
        {
            if(this->is_identical_control(control))
            {
                const control_displaycapture* displaycapture_control = 
                    (const control_displaycapture*)control.get();
                component = displaycapture_control->component;

                break;
            }
        }

        if(!component)
        {
            // create a new component since it was not found in the last or in the new set
            source_displaycapture_t displaycapture_source(
                new source_displaycapture(this->pipeline.session, this->pipeline.context_mutex));

            if(this->params->device_info.adapter_ordinal == this->pipeline.get_adapter_ordinal())
                displaycapture_source->initialize(
                    this->pipeline.shared_from_this<control_pipeline>(),
                    this->params->device_info.output_ordinal,
                    this->pipeline.d3d11dev, this->pipeline.devctx);
            else
                displaycapture_source->initialize(
                    this->pipeline.shared_from_this<control_pipeline>(),
                    this->params->device_info.adapter_ordinal, 
                    this->params->device_info.output_ordinal,
                    this->pipeline.dxgifactory, this->pipeline.d3d11dev, this->pipeline.devctx);

            component = displaycapture_source;
        }
    }

    new_set.push_back(this->shared_from_this<control_displaycapture>());

    // if the control has its parameters changed, control displaycapture
    // needs to have its video params updated
    if(this->new_params)
    {
        this->new_params = nullptr;
        this->apply_default_video_params();
    }

out:
    this->component = component;

    if(this->component)
        this->event_provider.for_each([this](gui_event_handler* e) { e->on_activate(this, false); });
    else
        this->event_provider.for_each([this](gui_event_handler* e) { e->on_activate(this, true); });
}

control_configurable_class::params_t
control_displaycapture::on_show_config_dialog(HWND parent, control_configurable_class::tag_t&&)
{
    gui_displaycapturedlg dlg(this->pipeline, this->params);
    const INT_PTR ret = dlg.DoModal(parent);

    // do not update parameters if they are the same
    if(dlg.new_params && this->is_same_device(dlg.new_params->device_info))
        return nullptr;

    return dlg.new_params;
}

void control_displaycapture::set_params(const control_configurable_class::params_t& new_params)
{
    control_displaycapture_params_t params =
        std::dynamic_pointer_cast<control_displaycapture_params>(new_params);

    if(!params)
        throw HR_EXCEPTION(E_UNEXPECTED);

    this->new_params = params;
}

std::vector<control_displaycapture_params::device_info_t> 
control_displaycapture::list_displaycapture_devices(IDXGIFactory1* factory)
{
    assert_(factory);

    std::vector<control_displaycapture_params::device_info_t> devices;

    HRESULT hr = S_OK;
    CComPtr<IDXGIAdapter1> adapter;
    for(UINT i = 0; SUCCEEDED(hr = factory->EnumAdapters1(i, &adapter)); i++)
    {
        CComPtr<IDXGIOutput> output;
        for(UINT j = 0; SUCCEEDED(hr = adapter->EnumOutputs(j, &output)); j++)
        {
            control_displaycapture_params::device_info_t device_info;
            CHECK_HR(hr = adapter->GetDesc1(&device_info.adapter));
            CHECK_HR(hr = output->GetDesc(&device_info.output));
            device_info.adapter_ordinal = i;
            device_info.output_ordinal = j;

            devices.push_back(std::move(device_info));
            output = nullptr;
        }

        adapter = nullptr;
    }

done:
    if(hr != DXGI_ERROR_NOT_FOUND && FAILED(hr))
        throw HR_EXCEPTION(hr);

    return devices;
}

D2D1_RECT_F control_displaycapture::get_rectangle(bool dest_params) const
{
    const FLOAT width = (FLOAT)std::abs(
        this->params->device_info.output.DesktopCoordinates.right -
        this->params->device_info.output.DesktopCoordinates.left),
        height = (FLOAT)std::abs(
            this->params->device_info.output.DesktopCoordinates.bottom -
            this->params->device_info.output.DesktopCoordinates.top);

    D2D1_RECT_F rect;
    rect.left = 0.f;
    rect.top = 0.f;
    rect.right = dest_params ? width / 2.f : width;
    rect.bottom = dest_params ? height / 2.f : height;
    return rect;
}

static FLOAT rotation = 0.f;

void control_displaycapture::apply_transformation(
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
            round(video_params.rotate / 90.f));
    }
    else
    {
        params.source_rect = rect;
        params.source_m = transformation;
    }

    this->videomixer_params->set_params(params);
}

void control_displaycapture::set_default_video_params(video_params_t& video_params, bool dest_params)
{
    video_params.rotate = dest_params ? rotation : 0.f;
    video_params.translate = dest_params ? D2D1::Point2F(100.f, 100.f) : D2D1::Point2F();
    video_params.scale = D2D1::Point2F(1.f, 1.f);
}

bool control_displaycapture::is_same_device(
    const control_displaycapture_params::device_info_t& dev_info) const
{
    return (this->params->device_info.adapter_ordinal == dev_info.adapter_ordinal &&
        this->params->device_info.output_ordinal == dev_info.output_ordinal);
}

bool control_displaycapture::is_identical_control(const control_class_t& control) const
{
    const control_displaycapture* displaycapture_control =
        dynamic_cast<const control_displaycapture*>(control.get());

    // check that the control is of displaycapture type and it stores a component
    if(!displaycapture_control || !displaycapture_control->component)
        return false;

    // check that the component isn't requesting a reinitialization
    if(displaycapture_control->component->get_instance_type() ==
        media_component::INSTANCE_NOT_SHAREABLE)
        return false;

    // check that the component will reside in the same session
    if(displaycapture_control->component->session != this->pipeline.session)
        return false;

    // check that the control params match this control's params
    return this->is_same_device(displaycapture_control->params->device_info);
}