#include "gui_newdlg.h"
#include <sstream>

gui_newdlg::gui_newdlg(const control_pipeline_t& pipeline) : ctrl_pipeline(pipeline)
{
}

LRESULT gui_newdlg::OnInitDialog(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& /*bHandled*/)
{
    this->new_item = (new_item_t)lParam;
    this->combobox.Attach(this->GetDlgItem(IDC_COMBO_NEW));
    this->editbox.Attach(this->GetDlgItem(IDC_EDIT_NEW));

    switch(new_item)
    {
    case NEW_SCENE:
        this->SetWindowTextW(L"Add New Scene");
        this->editbox.SetWindowTextW(L"New Scene");
        this->combobox.ShowWindow(SW_HIDE);
        break;
    default:
        this->SetWindowTextW(L"Add New Source");
        this->editbox.ShowWindow(SW_HIDE);

        this->combobox.AddString(L"Display Capture");
        this->combobox.AddString(L"Video Device");
        this->combobox.AddString(L"Audio Device");

        this->vidcap_sel_offset = 1;
        this->audio_sel_offset = 1 + 1;

        break;
    }

    this->combobox.SetCurSel(0);

    /*this->CenterWindow(this->GetParent());*/

    return TRUE;
}

LRESULT gui_newdlg::OnBnClickedOk(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
    if(this->new_item == NEW_SCENE)
    {
        ATL::CString str;
        this->editbox.GetWindowTextW(str);
        if(str.GetLength() == 0)
        {
            this->MessageBoxW(L"Cannot create a scene with empty name.", NULL, MB_ICONERROR);
            return 0;
        }
        this->new_scene_name = str;
    }
    else
        this->cursel = this->combobox.GetCurSel();

    this->EndDialog(0);
    return 0;
}

LRESULT gui_newdlg::OnBnClickedCancel(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
    this->EndDialog(1);
    return 0;
}


LRESULT gui_newdlg::OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/)
{
    return 0;
}
