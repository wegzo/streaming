#include "control_wasapi.h"
#include "control_pipeline.h"
#include <sstream>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>

#define CHECK_HR(hr_) {if(FAILED(hr_)) [[unlikely]] {goto done;}}

class gui_wasapidlg final : public CDialogImpl<gui_wasapidlg>
{
private:
    control_wasapi_params_t current_params;
    CComboBox combo_device;
public:
    enum { IDD = IDD_DIALOG_WASAPI_CONF };

    explicit gui_wasapidlg(const control_wasapi_params_t& current_params);

    control_wasapi_params_t new_params;
    std::vector<control_wasapi_params::device_info_t> devices;

    BEGIN_MSG_MAP(gui_wasapidlg)
        MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
        COMMAND_HANDLER(IDOK, BN_CLICKED, OnBnClickedOk)
        COMMAND_HANDLER(IDCANCEL, BN_CLICKED, OnBnClickedCancel)
    END_MSG_MAP()

    LRESULT OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
    LRESULT OnBnClickedOk(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
    LRESULT OnBnClickedCancel(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
};

gui_wasapidlg::gui_wasapidlg(const control_wasapi_params_t& current_params) :
    current_params(current_params)
{
    assert_(this->current_params);
}

LRESULT gui_wasapidlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    this->combo_device.Attach(this->GetDlgItem(IDC_COMBO1));

    this->devices = control_wasapi::list_wasapi_devices();

    int selected = 0, i = 0;
    for(const auto& item : this->devices)
    {
        if(item.device_id == this->current_params->device_info.device_id &&
            item.capture == this->current_params->device_info.capture)
            selected = i;

        std::wstringstream sts;
        if(item.capture)
            sts << L"Capture Device: ";
        else
            sts << L"Render Device: ";
        sts << item.device_friendlyname;

        this->combo_device.AddString(sts.str().c_str());
        i++;
    }

    if(!this->devices.empty())
        this->combo_device.SetCurSel(selected);

    return 0;
}

LRESULT gui_wasapidlg::OnBnClickedOk(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
    const int cur_sel = this->combo_device.GetCurSel();

    if(cur_sel >= 0 && cur_sel < (int)this->devices.size())
    {
        this->new_params = std::make_shared<control_wasapi_params>();
        this->new_params->device_info = this->devices[cur_sel];
    }

    this->EndDialog(IDOK);
    return 0;
}

LRESULT gui_wasapidlg::OnBnClickedCancel(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
    this->EndDialog(IDCANCEL);
    return 0;
}


/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////


control_wasapi::control_wasapi(control_set_t& active_controls, control_pipeline& pipeline) :
    control_class(active_controls, pipeline.event_provider),
    audiomixer_params(new stream_audiomixer2_controller),
    pipeline(pipeline),
    reference(nullptr),
    params(new control_wasapi_params)
{
}

void control_wasapi::build_audio_topology(const media_stream_t& from,
    const media_stream_t& to, const media_topology_t& topology)
{
    if(!this->component)
        return;

    assert_(!this->disabled);

    stream_audiomixer2_base_t audiomixer_stream =
        std::dynamic_pointer_cast<stream_audiomixer2_base>(to);
    if(!audiomixer_stream)
        throw HR_EXCEPTION(E_UNEXPECTED);

    if(!this->reference)
    {
        media_stream_t wasapi_stream = this->component->create_stream(topology->get_message_generator());

        if(from)
            wasapi_stream->connect_streams(from, topology);
        audiomixer_stream->connect_streams(wasapi_stream, this->audiomixer_params, topology);

        this->stream = wasapi_stream;
    }
    else
    {
        // the topology branch for the referenced control must have been established beforehand
        assert_(this->reference->stream);
        assert_(!this->stream);

        // only connect from this stream to 'to' stream
        // (since this a duplicate control from the original)
        audiomixer_stream->connect_streams(
            this->reference->stream, this->audiomixer_params, topology);
    }
}

void control_wasapi::activate(const control_set_t& last_set, control_set_t& new_set)
{
    source_wasapi_t component;

    this->stream = nullptr;
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
            const control_wasapi* wasapi_control = (const control_wasapi*)control.get();
            this->reference = wasapi_control;
            component = wasapi_control->component;

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
                const control_wasapi* wasapi_control = (const control_wasapi*)control.get();
                component = wasapi_control->component;

                break;
            }
        }

        if(!component)
        {
            // create a new component since it was not found in the last or in the new set
            source_wasapi_t wasapi_source(new source_wasapi(this->pipeline.audio_session));

            wasapi_source->initialize(
                this->pipeline.shared_from_this<control_pipeline>(),
                this->params->device_info.device_id, 
                this->params->device_info.capture);

            component = wasapi_source;
        }
    }

    new_set.push_back(this->shared_from_this<control_wasapi>());

    if(this->new_params)
        this->new_params = nullptr;

out:
    this->component = component;

    if(this->component)
        this->event_provider.for_each([this](gui_event_handler* e) { e->on_activate(this, false); });
    else
        this->event_provider.for_each([this](gui_event_handler* e) { e->on_activate(this, true); });
}

control_configurable_class::params_t control_wasapi::on_show_config_dialog(
    HWND parent, control_configurable_class::tag_t&&)
{
    gui_wasapidlg dlg(this->params);
    const INT_PTR ret = dlg.DoModal(parent);

    // do not update parameters if they are the same
    if(dlg.new_params && this->is_same_device(dlg.new_params->device_info))
        return nullptr;

    return dlg.new_params;
}

void control_wasapi::set_params(const control_configurable_class::params_t& new_params)
{
    control_wasapi_params_t params =
        std::dynamic_pointer_cast<control_wasapi_params>(new_params);

    if(!params)
        throw HR_EXCEPTION(E_UNEXPECTED);

    this->new_params = params;
}

std::vector<control_wasapi_params::device_info_t> control_wasapi::list_wasapi_devices()
{
    std::vector<control_wasapi_params::device_info_t> devices;

    auto f = [&](EDataFlow flow)
    {
        HRESULT hr = S_OK;

        CComPtr<IMMDeviceEnumerator> enumerator;
        CComPtr<IMMDeviceCollection> collection;
        UINT count;

        CHECK_HR(hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator), (void**)&enumerator));
        CHECK_HR(hr = enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection));
        CHECK_HR(hr = collection->GetCount(&count));

        for(UINT i = 0; i < count; i++)
        {
            control_wasapi_params::device_info_t dev_info;
            CComPtr<IMMDevice> device;
            CComPtr<IPropertyStore> props;
            PROPVARIANT name;
            LPWSTR id;
            PropVariantInit(&name);

            CHECK_HR(hr = collection->Item(i, &device));
            CHECK_HR(hr = device->GetId(&id));
            CHECK_HR(hr = device->OpenPropertyStore(STGM_READ, &props));
            CHECK_HR(hr = props->GetValue(PKEY_Device_FriendlyName, &name));

            dev_info.device_friendlyname = name.pwszVal;
            dev_info.device_id = id;
            dev_info.capture = (flow == eCapture);

            CoTaskMemFree(id);
            PropVariantClear(&name);
            id = NULL;

            devices.push_back(std::move(dev_info));
        }

    done:
        // TODO: memory is leaked if this fails
        if(FAILED(hr))
            throw HR_EXCEPTION(hr);
    };

    f(eCapture);
    f(eRender);

    return devices;
}

bool control_wasapi::is_same_device(const control_wasapi_params::device_info_t& dev_info) const
{
    return (this->params->device_info.device_id == dev_info.device_id &&
        this->params->device_info.capture == dev_info.capture);
}

bool control_wasapi::is_identical_control(const control_class_t& control) const
{
    const control_wasapi* wasapi_control = dynamic_cast<const control_wasapi*>(control.get());

    // check that the control is of wasapi type and it stores a component
    if(!wasapi_control || !wasapi_control->component)
        return false;

    // check that the component isn't requesting a reinitialization
    if(wasapi_control->component->get_instance_type() ==
        media_component::INSTANCE_NOT_SHAREABLE)
        return false;

    // check that the component will reside in the same session
    if(wasapi_control->component->session != this->pipeline.audio_session)
        return false;

    // check that the control params match this control's params
    return this->is_same_device(wasapi_control->params->device_info);
}