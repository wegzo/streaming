#pragma once

#include "wtl.h"
#include "control_scene.h"
#include "control_pipeline.h"
#include "control_displaycapture.h"
#include "control_vidcap.h"
#include "control_wasapi.h"
#include <string>
#include <vector>

class gui_maindlg;

class gui_newdlg : public CDialogImpl<gui_newdlg>
{
public:
    enum new_item_t
    {
        NEW_SCENE, NEW_VIDEO, NEW_AUDIO
    };
private:
    control_pipeline_t ctrl_pipeline;
    CComboBox combobox;
    CEdit editbox;
public:
    enum {IDD = IDD_DIALOG_NEW};

    std::wstring new_scene_name;
    int cursel, audio_sel_offset, vidcap_sel_offset;
    int new_item;

    explicit gui_newdlg(const control_pipeline_t&);

    BEGIN_MSG_MAP(gui_newdlg)
        MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
        COMMAND_HANDLER(IDOK, BN_CLICKED, OnBnClickedOk)
        COMMAND_HANDLER(IDCANCEL, BN_CLICKED, OnBnClickedCancel)
        MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
    END_MSG_MAP()

    LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnBnClickedOk(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
    LRESULT OnBnClickedCancel(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
    LRESULT OnDestroy(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
};