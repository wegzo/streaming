#include "gui_dlgs.h"
#include "gui_newdlg.h"
#include <iostream>
#include <sstream>

#pragma warning(push)
#pragma warning(disable: 4706) // assignment within conditional expression

gui_scenedlg::gui_scenedlg(const control_pipeline_t& ctrl_pipeline) :
    ctrl_pipeline(ctrl_pipeline),
    scene_counter(0),
    selecting(false),
    wnd_scenetree(this, 1)
{
    this->ctrl_pipeline->event_provider.register_event_handler(*this);
}

gui_scenedlg::~gui_scenedlg()
{
    this->ctrl_pipeline->event_provider.unregister_event_handler(*this);
}

void gui_scenedlg::on_scene_activate(control_scene* activated_scene, bool deactivated)
{
    // find the scene on the wnd_scenelist
    CTreeItem item;
    for(item = this->wnd_scenetree.GetRootItem();
        item && !deactivated;
        item = item.GetNextSibling())
    {
        CString text;
        item.GetText(text);
        if(!text.IsEmpty() && activated_scene->name.compare(text) == 0)
            break;
    }

    if(item && !deactivated)
    {
        if(!this->selecting)
            this->wnd_scenetree.SelectItem(item);
    }
}

void gui_scenedlg::on_control_added(control_class* new_control, bool removed, control_scene* /*scene*/)
{
    control_scene* new_scene = dynamic_cast<control_scene*>(new_control);
    if(!removed && new_scene)
    {
        CTreeItem new_item = this->wnd_scenetree.InsertItem(
            new_scene->name.c_str(), nullptr, this->wnd_scenetree.GetLastVisibleItem());
    }
}

LRESULT gui_scenedlg::OnBnClickedAddscene(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
    /*gui_newdlg dlg(NULL);
    const INT_PTR ret = dlg.DoModal(*this, gui_newdlg::NEW_SCENE);
    if(ret == 0)
        this->add_scene(dlg.new_scene_name);*/

    std::wostringstream sts;
    sts << "scene" << this->scene_counter;
    std::wstring&& str = sts.str();
    control_scene* scene = this->ctrl_pipeline->root_scene->add_scene(str);
    if(!scene)
        throw HR_EXCEPTION(E_UNEXPECTED);
    this->scene_counter++;

    this->ctrl_pipeline->root_scene->switch_scene(*scene);

    return 0;
}

LRESULT gui_scenedlg::OnBnClickedRemovescene(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
    CTreeItem item = this->wnd_scenetree.GetSelectedItem();
    if(item)
    {
        const int size = (int)this->wnd_scenetree.GetCount();
        if(size > 1)
        {
            CTreeItem next_item = item.GetNextSibling();
            if(!next_item)
                next_item = item.GetPrevSibling();

            CString name, old_name;
            this->wnd_scenetree.SelectItem(next_item);
            next_item.GetText(name);
            item.GetText(old_name);

            bool is_video_control, found;
            bool is_video_control2, found2;
            auto it = this->ctrl_pipeline->root_scene->find_control_iterator(
                std::wstring(name), is_video_control, found),
                old_it = this->ctrl_pipeline->root_scene->find_control_iterator(
                    std::wstring(old_name), is_video_control2, found2);
            control_scene* scene, *scene2;

            if(found && (scene = dynamic_cast<control_scene*>(it->get())))
            {
                // remove old scene
                if(found2 && is_video_control2 && 
                    (scene2 = dynamic_cast<control_scene*>(old_it->get())))
                {
                    this->ctrl_pipeline->root_scene->remove_control(is_video_control2, old_it);
                }

                // switch to new scene(activate is called here)
                this->ctrl_pipeline->root_scene->switch_scene(*scene);
            }

            this->wnd_scenetree.DeleteItem(item);
        }
        else if(size == 1)
        {
            CString old_name;
            item.GetText(old_name);

            bool is_video_control2, found2;
            auto old_it = this->ctrl_pipeline->root_scene->find_control_iterator(
                std::wstring(old_name), is_video_control2, found2);
            control_scene* scene2;

            // remove old scene
            if(found2 && is_video_control2 &&
                (scene2 = dynamic_cast<control_scene*>(old_it->get())))
            {
                this->ctrl_pipeline->root_scene->remove_control(is_video_control2, old_it);

                // unselect the selected scene, since this was the last scene
                this->ctrl_pipeline->root_scene->unselect_selected_scene();

                // deactive the pipeline
                this->ctrl_pipeline->deactivate();
            }

            this->wnd_scenetree.DeleteItem(item);
        }
    }

    return 0;
}

LRESULT gui_scenedlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    this->DlgResize_Init(false);

    this->font_toolbar = ::CreateFont(0, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        VARIABLE_PITCH, L"Segoe MDL2 Assets");

    this->wnd_scenetree.SubclassWindow(this->GetDlgItem(IDC_SCENETREE));

    CRect rc = {POINT{0, 0}, POINT{0, 0}};
    this->wnd_toolbar.Create(*this, rc, nullptr,
        CCS_NODIVIDER | CCS_BOTTOM | WS_CHILD | TBSTYLE_TOOLTIPS | TBSTYLE_LIST, 0,
        ID_SOURCE_TOOLBAR);
    this->wnd_toolbar.SetFont(this->font_toolbar);
    this->wnd_toolbar.SetDrawTextFlags((DWORD)~0, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    this->wnd_toolbar.SetExtendedStyle(TBSTYLE_EX_MIXEDBUTTONS /*| TBSTYLE_EX_DOUBLEBUFFER*/);
    this->wnd_toolbar.SetButtonStructSize();

    TBMETRICS metrics = {0};
    metrics.cbSize = sizeof(TBMETRICS);
    metrics.dwMask = TBMF_PAD | TBMF_BARPAD | TBMF_BUTTONSPACING;
    this->wnd_toolbar.GetMetrics(&metrics);

    metrics.cxPad = 0;
    metrics.cyPad = 13;
    metrics.cxButtonSpacing = 0;
    this->wnd_toolbar.SetMetrics(&metrics);

    this->wnd_toolbar.AddButton(
        ID_BUTTON_ADD_SRC, BTNS_BUTTON | BTNS_SHOWTEXT | BTNS_AUTOSIZE,
        TBSTATE_ENABLED, I_IMAGENONE, L" \uE109 ", NULL);
    this->wnd_toolbar.AddButton(
        ID_BUTTON_REMOVE_SRC, BTNS_BUTTON | BTNS_SHOWTEXT | BTNS_AUTOSIZE,
        TBSTATE_ENABLED, I_IMAGENONE, L" \uE108 ", NULL);

    this->wnd_toolbar.AutoSize();
    this->wnd_toolbar.ShowWindow(SW_SHOW);

    return TRUE;
}

LRESULT gui_scenedlg::OnTvnSelchangedScenetree(int /*idCtrl*/, LPNMHDR /*pNMHDR*/, BOOL& /*bHandled*/)
{
    CTreeItem selected_item = this->wnd_scenetree.GetSelectedItem();
    if(!selected_item)
        return 0;

    this->selecting = true;

    CString name;
    selected_item.GetText(name);

    bool is_video_control, found;
    auto it = this->ctrl_pipeline->root_scene->find_control_iterator(
        std::wstring(name), is_video_control, found);

    if(found && is_video_control)
        this->ctrl_pipeline->root_scene->switch_scene(dynamic_cast<control_scene&>(*it->get()));

    // set focus to the source dialog
    /*this->dlg_sources.SetFocus();*/

    this->selecting = false;

    return 0;
}

void gui_scenedlg::OnSceneDlgSize(UINT /*nType*/, CSize /*size*/)
{
    this->wnd_toolbar.AutoSize();

    RECT toolbar_rc, scenetree_client_rc, this_client_rc;
    this->GetClientRect(&this_client_rc);
    this->wnd_toolbar.GetWindowRect(&toolbar_rc);

    const LONG wnd_toolbar_width = toolbar_rc.right - toolbar_rc.left,
        wnd_toolbar_height = toolbar_rc.bottom - toolbar_rc.top;

    scenetree_client_rc.left = this_client_rc.left;
    scenetree_client_rc.top = this_client_rc.top + 23;
    scenetree_client_rc.right = this_client_rc.right;
    scenetree_client_rc.bottom = this_client_rc.bottom - wnd_toolbar_height;
    this->wnd_scenetree.SetWindowPos(HWND_BOTTOM, &scenetree_client_rc, SWP_NOACTIVATE);

    this->SetMsgHandled(FALSE);
}


/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////


gui_sourcedlg::gui_sourcedlg(const control_pipeline_t& ctrl_pipeline) :
    ctrl_pipeline(ctrl_pipeline),
    video_counter(0), audio_counter(0),
    do_not_reselect(false),
    current_active_scene(nullptr),
    update_source_list_on_scene_activate(false),
    wnd_sourcetree(this, 1)
{
    this->ctrl_pipeline->event_provider.register_event_handler(*this);
}

gui_sourcedlg::~gui_sourcedlg()
{
    this->ctrl_pipeline->event_provider.unregister_event_handler(*this);
}

void gui_sourcedlg::on_scene_activate(control_scene* activated_scene, bool deactivated)
{
    if(activated_scene == this->ctrl_pipeline->root_scene->get_selected_scene() && !deactivated &&
        this->current_active_scene != activated_scene || 
        (!deactivated && this->update_source_list_on_scene_activate))
    {
        // set the source tree only if switched to a new scene or if an explicit update is requested
        this->update_source_list_on_scene_activate = false;
        this->current_active_scene = activated_scene;
        this->set_source_tree(this->current_active_scene);
    }
}

void gui_sourcedlg::on_activate(control_class* activated_control, bool deactivated)
{
    // clear the source tree on ctrl pipeline deactivation
    if(deactivated && activated_control == this->ctrl_pipeline.get())
    {
        this->current_active_scene = nullptr;
        this->set_source_tree(NULL);
    }
}

void gui_sourcedlg::on_control_added(control_class* control, bool removed, control_scene* /*scene*/)
{
    if(!dynamic_cast<control_scene*>(control))
    {
        this->set_source_tree(this->current_active_scene);

        if(!removed)
            // select the new item
            this->ctrl_pipeline->set_selected_control(control);
    }
}

void gui_sourcedlg::on_control_selection_changed()
{
    this->set_selected_item(this->ctrl_pipeline->get_selected_controls().empty() ? nullptr : 
        this->ctrl_pipeline->get_selected_controls()[0]);
}

void gui_sourcedlg::set_source_tree(const control_scene* scene)
{
    this->do_not_reselect = true;
    this->wnd_sourcetree.DeleteAllItems();
    this->do_not_reselect = false;

    this->ctrl_pipeline->set_selected_control(nullptr, control_pipeline::CLEAR);

    if(!scene)
        return;

    const control_scene::controls_t& video_controls = scene->get_video_controls();
    const control_scene::controls_t& audio_controls = scene->get_audio_controls();

    for(auto&& elem : video_controls)
        this->wnd_sourcetree.InsertItem(elem->name.c_str(), TVI_ROOT, TVI_LAST);
    for(auto&& elem : audio_controls)
        this->wnd_sourcetree.InsertItem(elem->name.c_str(), TVI_ROOT, TVI_LAST);
}

void gui_sourcedlg::set_selected_item(CTreeItem item)
{
    if(!item.IsNull())
    {
        CString str;
        item.GetText(str);

        bool is_video_control, found;
        control_scene* active_scene = this->ctrl_pipeline->root_scene->get_selected_scene();
        if(!active_scene)
            return;
        auto it = active_scene->find_control_iterator(str.GetBuffer(), is_video_control, found);

        if(found)
            this->ctrl_pipeline->set_selected_control(it->get());
    }
    else
        this->ctrl_pipeline->set_selected_control(NULL, control_pipeline::CLEAR);
}

void gui_sourcedlg::set_selected_item(const control_class* control)
{
    // pipeline lock is assumed when the control is passed as an argument
    CTreeItem first_item = this->wnd_sourcetree.GetNextItem(NULL, TVGN_ROOT);
    CTreeItem item = first_item;
    if(!first_item || !control)
        goto unselect;
    
    do
    {
        CString text;
        item.GetText(text);

        if(control->name.compare(text) == 0)
        {
            // triggers the OnTvnSelchangedSourcetree event handler
            this->do_not_reselect = true;
            item.Select();
            this->do_not_reselect = false;

            this->wnd_sourcetree.SetFocus();

            return;
        }

        item = this->wnd_sourcetree.GetNextSiblingItem(item);
    }
    while(item != first_item);

unselect:
    this->do_not_reselect = true;
    this->wnd_sourcetree.Select(NULL, TVGN_CARET);
    this->do_not_reselect = false;
}

LRESULT gui_sourcedlg::OnBnClickedAddsrc(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
    control_scene* scene;
    // add the first scene if there wasn't a scene before
    {
        scene = this->ctrl_pipeline->root_scene->get_selected_scene();
        if(!scene)
        {
            scene = this->ctrl_pipeline->root_scene->add_scene(L"first scene");
            if(!scene)
                throw HR_EXCEPTION(E_UNEXPECTED);

            this->ctrl_pipeline->root_scene->switch_scene(*scene);
        }
    }

    gui_newdlg dlg(this->ctrl_pipeline);
    const INT_PTR ret = dlg.DoModal(*this, gui_newdlg::NEW_VIDEO);
    if(ret == 0)
    {
        // set the focus to the source list
        this->wnd_sourcetree.SetFocus();

        if(dlg.cursel < dlg.vidcap_sel_offset)
        {
            // add the displaycapture and set its params
            std::wostringstream sts;
            sts << L"displaycapture" << this->video_counter;
            control_displaycapture& displaycapture = *scene->add_displaycapture(sts.str());
            this->video_counter++;

            displaycapture.set_displaycapture_params(dlg.displaycaptures[dlg.cursel]);
            // displaycapture params must be set before setting video params
            displaycapture.apply_default_video_params();
        }
        else if(dlg.cursel < dlg.audio_sel_offset)
        {
            std::wostringstream sts;
            sts << L"vidcap" << this->video_counter;
            control_vidcap* vidcap = scene->add_vidcap(sts.str());
            this->video_counter++;

            // TODO: the vidcap control should probably host a dialog where the parameters
            // can be chosen
            const int index = dlg.cursel - dlg.vidcap_sel_offset;
            vidcap->set_vidcap_params(dlg.vidcaps[index]);
            /*vidcap->apply_default_video_params();*/
        }
        else
        {
            // audio
            const int index = dlg.cursel - dlg.audio_sel_offset;

            std::wostringstream sts;
            sts << L"wasapi" << this->audio_counter;
            control_wasapi& wasapi = *scene->add_wasapi(sts.str());
            this->audio_counter++;

            wasapi.set_wasapi_params(dlg.audios[index]);
        }

        ((control_class*)this->ctrl_pipeline.get())->activate();
    }

    return 0;
}

LRESULT gui_sourcedlg::OnBnClickedRemovesrc(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
    CTreeItem item = this->wnd_sourcetree.GetSelectedItem();

    if(!item.IsNull())
    {
        CString str;
        item.GetText(str);

        bool is_video_control, found;
        control_scene* active_scene = this->ctrl_pipeline->root_scene->get_selected_scene();
        auto it = active_scene->find_control_iterator(str.GetBuffer(), is_video_control, found);

        if(found)
        {
            active_scene->remove_control(is_video_control, it);
            ((control_class*)this->ctrl_pipeline.get())->activate();
        }
    }

    return 0;
}

LRESULT gui_sourcedlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    this->DlgResize_Init(false);

    this->font_toolbar = ::CreateFont(0, 0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        VARIABLE_PITCH, L"Segoe MDL2 Assets");

    this->wnd_sourcetree.SubclassWindow(this->GetDlgItem(IDC_SOURCETREE));

    CRect rc = {POINT{0, 0}, POINT{0, 0}};
    this->wnd_toolbar.Create(*this, rc, nullptr,
        CCS_NODIVIDER | CCS_BOTTOM | WS_CHILD | TBSTYLE_TOOLTIPS | TBSTYLE_LIST, 0,
        ID_SOURCE_TOOLBAR);
    this->wnd_toolbar.SetFont(this->font_toolbar);
    this->wnd_toolbar.SetDrawTextFlags((DWORD)~0, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    this->wnd_toolbar.SetExtendedStyle(TBSTYLE_EX_MIXEDBUTTONS /*| TBSTYLE_EX_DOUBLEBUFFER*/);
    this->wnd_toolbar.SetButtonStructSize();

    TBMETRICS metrics = {0};
    metrics.cbSize = sizeof(TBMETRICS);
    metrics.dwMask = TBMF_PAD | TBMF_BARPAD | TBMF_BUTTONSPACING;
    this->wnd_toolbar.GetMetrics(&metrics);

    metrics.cxPad = 0;
    metrics.cyPad = 13;
    metrics.cxButtonSpacing = 0;
    this->wnd_toolbar.SetMetrics(&metrics);

    this->wnd_toolbar.AddButton(
        ID_BUTTON_ADD_SRC, BTNS_BUTTON | BTNS_SHOWTEXT | BTNS_AUTOSIZE, 
        TBSTATE_ENABLED, I_IMAGENONE, L" \uE109 ", NULL);
    this->wnd_toolbar.AddButton(
        ID_BUTTON_REMOVE_SRC, BTNS_BUTTON | BTNS_SHOWTEXT | BTNS_AUTOSIZE, 
        TBSTATE_ENABLED, I_IMAGENONE, L" \uE108 ", NULL);
    this->wnd_toolbar.AddButton(0, BTNS_SEP, 0, 10, nullptr, 0);
    this->wnd_toolbar.AddButton(
        ID_BUTTON_MOVE_UP_SRC, BTNS_BUTTON | BTNS_SHOWTEXT | BTNS_AUTOSIZE, 
        TBSTATE_ENABLED, I_IMAGENONE, L" \uE018 ", 0);
    this->wnd_toolbar.AddButton(
        ID_BUTTON_MOVE_DOWN_SRC, BTNS_BUTTON | BTNS_SHOWTEXT | BTNS_AUTOSIZE, 
        TBSTATE_ENABLED, I_IMAGENONE, L" \uE019 ", 0);
    this->wnd_toolbar.AddButton(0, BTNS_SEP, 0, 10, nullptr, 0);
    this->wnd_toolbar.AddButton(
        ID_BUTTON_CONFIGURE_SRC, BTNS_BUTTON | BTNS_SHOWTEXT | BTNS_AUTOSIZE, 
        TBSTATE_INDETERMINATE, I_IMAGENONE, L" \uE115 ", 0);

    this->wnd_toolbar.AutoSize();
    this->wnd_toolbar.ShowWindow(SW_SHOW);

    return TRUE;
}

LRESULT gui_sourcedlg::OnTvnSelchangedSourcetree(int /*idCtrl*/, LPNMHDR pNMHDR, BOOL& /*bHandled*/)
{
    if(this->do_not_reselect)
        return 0;

    LPNMTREEVIEW pNMTreeView = reinterpret_cast<LPNMTREEVIEW>(pNMHDR);
    this->set_selected_item(CTreeItem(pNMTreeView->itemNew.hItem, &this->wnd_sourcetree));

    return 0;
}

LRESULT gui_sourcedlg::OnKillFocus(int /*idCtrl*/, LPNMHDR /*pNMHDR*/, BOOL& /*bHandled*/)
{
    /*this->ctrl_pipeline->set_selected_control(NULL, control_pipeline::CLEAR);*/



    /*if(this->ctrl_pipeline->get_preview_window())*/
        /*this->ctrl_pipeline->selected_items.clear();*/
        /*this->ctrl_pipeline->get_preview_window()->set_size_box(NULL);*/
    return 0;
}

LRESULT gui_sourcedlg::OnSetFocus(int /*idCtrl*/, LPNMHDR /*pNMHDR*/, BOOL& /*bHandled*/)
{
    /*control_class* selected_item = NULL;
    if(!this->ctrl_pipeline->selected_items.empty())
        selected_item = this->ctrl_pipeline->selected_items[0];

    this->set_selected_item(selected_item);*/
    return 0;
}

LRESULT gui_sourcedlg::OnBnClickedSrcup(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
    CTreeItem item = this->wnd_sourcetree.GetSelectedItem();

    if(!item.IsNull())
    {
        CString str;
        item.GetText(str);

        bool is_video_control, found;
        control_scene* active_scene = this->ctrl_pipeline->root_scene->get_selected_scene();
        control_scene::controls_t::const_iterator it = 
            active_scene->find_control_iterator(str.GetBuffer(), is_video_control, found);
        control_video* video_control;

        if(found && (video_control = dynamic_cast<control_video*>(it->get())))
        {
            const control_scene::controls_t& video_controls = active_scene->get_video_controls();
            if(std::distance(video_controls.begin(), it) > 0)
            {
                auto jt = it;
                active_scene->reorder_controls(it, --jt);

                // update the source list and reselect the old selection
                this->update_source_list_on_scene_activate = true;
                control_class* selected_control =
                    this->ctrl_pipeline->get_selected_controls().empty() ? nullptr :
                    this->ctrl_pipeline->get_selected_controls()[0];
                ((control_class*)this->ctrl_pipeline.get())->activate();
                if(selected_control)
                    this->ctrl_pipeline->set_selected_control(selected_control);
            }
        }
    }

    return 0;
}

LRESULT gui_sourcedlg::OnBnClickedSrcdown(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
    CTreeItem item = this->wnd_sourcetree.GetSelectedItem();

    if(!item.IsNull())
    {
        CString str;
        item.GetText(str);

        bool is_video_control, found;
        control_scene* active_scene = this->ctrl_pipeline->root_scene->get_selected_scene();
        control_scene::controls_t::const_iterator it =
            active_scene->find_control_iterator(str.GetBuffer(), is_video_control, found);
        control_video* video_control;

        if(found && (video_control = dynamic_cast<control_video*>(it->get())))
        {
            const control_scene::controls_t& video_controls = active_scene->get_video_controls();
            if(std::distance(it, video_controls.end()) > 1)
            {
                auto jt = it;
                active_scene->reorder_controls(it, ++(++jt));

                // update the source list and reselect the old selection
                this->update_source_list_on_scene_activate = true;
                control_class* selected_control =
                    this->ctrl_pipeline->get_selected_controls().empty() ? nullptr :
                    this->ctrl_pipeline->get_selected_controls()[0];
                ((control_class*)this->ctrl_pipeline.get())->activate();
                if(selected_control)
                    this->ctrl_pipeline->set_selected_control(selected_control);
            }
        }
    }

    return 0;
}

LRESULT gui_sourcedlg::OnSourceToolbarCustomDraw(int /*wParam*/, LPNMHDR /*lParam*/, BOOL& /*bHandled*/)
{
    return CDRF_DODEFAULT;
}

void gui_sourcedlg::OnSourceDlgSize(UINT /*nType*/, CSize /*size*/)
{
    this->wnd_toolbar.AutoSize();

    RECT toolbar_rc, sourcetree_client_rc, this_client_rc;
    this->GetClientRect(&this_client_rc);
    this->wnd_toolbar.GetWindowRect(&toolbar_rc);

    const LONG wnd_toolbar_width = toolbar_rc.right - toolbar_rc.left,
        wnd_toolbar_height = toolbar_rc.bottom - toolbar_rc.top;
    
    sourcetree_client_rc.left = this_client_rc.left;
    sourcetree_client_rc.top = this_client_rc.top + 23;
    sourcetree_client_rc.right = this_client_rc.right;
    sourcetree_client_rc.bottom = this_client_rc.bottom - wnd_toolbar_height;
    this->wnd_sourcetree.SetWindowPos(HWND_BOTTOM, &sourcetree_client_rc, SWP_NOACTIVATE);

    this->SetMsgHandled(FALSE);
}



/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////



gui_controldlg::gui_controldlg(const control_pipeline_t& ctrl_pipeline) :
    ctrl_pipeline(ctrl_pipeline)
{
}

void gui_controldlg::start_recording(bool streaming)
{
    if(!this->ctrl_pipeline->root_scene->get_selected_scene())
    {
        this->MessageBoxW(L"Add some sources first", NULL, MB_ICONINFORMATION);
        return;
    }

    if(!this->ctrl_pipeline->is_recording())
    {
        try
        {
            if(streaming)
                this->ctrl_pipeline->start_recording(L"", *this, true);
            else
                this->ctrl_pipeline->start_recording(L"test.mp4", *this);
        }
        catch(control_pipeline_recording_activate_exception err)
        {
            // TODO: use wstring
            std::string error_msg =
                "Could not start recording.\n"
                "Make sure that the video device and video encoder settings are valid and compatible.\n\n";
            error_msg += err.what();

            ::MessageBoxA(*this, error_msg.c_str(), nullptr, MB_ICONERROR);
        }

        if(this->ctrl_pipeline->is_recording())
        {
            if(this->ctrl_pipeline->is_streaming())
            {
                this->btn_start_streaming.SetWindowTextW(L"Stop Streaming");
                this->btn_start_recording.EnableWindow(FALSE);
            }
            else
            {
                this->btn_start_recording.SetWindowTextW(L"Stop Recording");
                this->btn_start_streaming.EnableWindow(FALSE);
            }
        }
    }
    else
    {
        const bool was_streaming = this->ctrl_pipeline->is_streaming();

        this->ctrl_pipeline->stop_recording();

        if(was_streaming)
        {
            this->btn_start_streaming.EnableWindow(FALSE);
            this->btn_start_streaming.SetWindowTextW(L"Stopping...");
        }
        else
        {
            this->btn_start_recording.EnableWindow(FALSE);
            this->btn_start_recording.SetWindowTextW(L"Stopping...");
        }
    }
}

void gui_controldlg::stop_recording(bool streaming)
{
    if(!streaming)
    {
        this->btn_start_recording.SetWindowTextW(L"Start Recording");
        this->btn_start_recording.EnableWindow(TRUE);
        this->btn_start_streaming.EnableWindow(TRUE);
    }
    else
    {
        this->btn_start_streaming.SetWindowTextW(L"Start Streaming");
        this->btn_start_streaming.EnableWindow(TRUE);
        this->btn_start_recording.EnableWindow(TRUE);
    }
}

void gui_controldlg::OnDestroy()
{
    extern CAppModule module_;
    CMessageLoop* msg_loop = module_.GetMessageLoop();
    msg_loop->RemoveIdleHandler(this);
}

LRESULT gui_controldlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    extern CAppModule module_;
    CMessageLoop* msg_loop = module_.GetMessageLoop();
    msg_loop->AddIdleHandler(this);

    this->DlgResize_Init(false);

    this->btn_start_recording.Attach(this->GetDlgItem(IDC_START_RECORDING));
    this->btn_start_streaming.Attach(this->GetDlgItem(IDC_START_STREAMING));

    return TRUE;
}

LRESULT gui_controldlg::OnBnClickedStartRecording(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
    this->start_recording(false);
    return 0;
}

LRESULT gui_controldlg::OnRecordingStopped(UINT /*uMsg*/, WPARAM wParam, LPARAM /*lParam*/, BOOL& bHandled)
{
    bHandled = TRUE;

    if(wParam == 1)
        this->stop_recording(false);
    else if(wParam == 0)
        this->stop_recording(true);

    return 0;
}

LRESULT gui_controldlg::OnBnClickedStartStreaming(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
    this->start_recording(true);
    return 0;
}

BOOL gui_controldlg::OnIdle()
{
    return FALSE;
}


#pragma warning(pop)