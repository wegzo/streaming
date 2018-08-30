#pragma once

#include "wtl.h"
#include "control_pipeline.h"
#include <string>

class gui_sourcedlg;

class gui_scenedlg :
    public CDialogImpl<gui_scenedlg>,
    public CDialogResize<gui_scenedlg>
{
private:
    control_pipeline_t ctrl_pipeline;
    gui_sourcedlg& dlg_sources;
    CButton btn_addscene, btn_removescene;
    CListBox wnd_scenelist;
public:
    enum {IDD = IDD_SCENEDLG};

    gui_scenedlg(gui_sourcedlg&, const control_pipeline_t&);

    void add_scene(const std::wstring& scene_name);

    // command handlers handle events from child windows
    BEGIN_MSG_MAP(gui_scenedlg)
        COMMAND_HANDLER(IDC_ADDSCENE, BN_CLICKED, OnBnClickedAddscene)
        COMMAND_HANDLER(IDC_REMOVESCENE, BN_CLICKED, OnBnClickedRemovescene)
        MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
        COMMAND_HANDLER(IDC_SCENELIST, LBN_SELCHANGE, OnLbnSelchangeScenelist)
        CHAIN_MSG_MAP(CDialogResize<gui_scenedlg>)
    END_MSG_MAP()

    BEGIN_DLGRESIZE_MAP(gui_scenedlg)
        DLGRESIZE_CONTROL(IDC_SCENELIST, DLSZ_SIZE_X | DLSZ_SIZE_Y)
        /*BEGIN_DLGRESIZE_GROUP()*/
            DLGRESIZE_CONTROL(IDC_ADDSCENE, DLSZ_MOVE_X | DLSZ_MOVE_Y)
            DLGRESIZE_CONTROL(IDC_REMOVESCENE, DLSZ_MOVE_X | DLSZ_MOVE_Y)
        /*END_DLGRESIZE_GROUP()*/
    END_DLGRESIZE_MAP()

    LRESULT OnBnClickedAddscene(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
    LRESULT OnBnClickedRemovescene(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
    LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnLbnSelchangeScenelist(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
};

class gui_sourcedlg :
    public CDialogImpl<gui_sourcedlg>,
    public CDialogResize<gui_sourcedlg>
{
private:
    control_pipeline_t ctrl_pipeline;
    gui_scenedlg& dlg_scenes;
    CButton btn_addsource, btn_removesource;
    CTreeViewCtrlEx wnd_sourcetree;
public:
    enum {IDD = IDD_SOURCEDLG};

    gui_sourcedlg(gui_scenedlg&, const control_pipeline_t&);

    void set_source_tree(const control_scene&);

    BEGIN_MSG_MAP(gui_sourcedlg)
        COMMAND_HANDLER(IDC_ADDSRC, BN_CLICKED, OnBnClickedAddsrc)
        COMMAND_HANDLER(IDC_REMOVESRC, BN_CLICKED, OnBnClickedRemovesrc)
        MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
        CHAIN_MSG_MAP(CDialogResize<gui_sourcedlg>)
    END_MSG_MAP()

    BEGIN_DLGRESIZE_MAP(gui_scenedlg)
        DLGRESIZE_CONTROL(IDC_SOURCETREE, DLSZ_SIZE_X | DLSZ_SIZE_Y)
        /*BEGIN_DLGRESIZE_GROUP()*/
            DLGRESIZE_CONTROL(IDC_ADDSRC, DLSZ_MOVE_X | DLSZ_MOVE_Y)
            DLGRESIZE_CONTROL(IDC_REMOVESRC, DLSZ_MOVE_X | DLSZ_MOVE_Y)
        /*END_DLGRESIZE_GROUP()*/
    END_DLGRESIZE_MAP()

    LRESULT OnBnClickedAddsrc(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
    LRESULT OnBnClickedRemovesrc(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
    LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
};

class gui_controldlg :
    public CDialogImpl<gui_controldlg>,
    public CDialogResize<gui_controldlg>
{
private:
    control_pipeline_t ctrl_pipeline;
    CButton btn_start_recording;
    CHandle stop_recording_event;
public:
    enum {IDD = IDD_CTRLDLG};

    explicit gui_controldlg(const control_pipeline_t&);

    BEGIN_MSG_MAP(gui_controldlg)
        MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
        COMMAND_HANDLER(IDC_START_RECORDING, BN_CLICKED, OnBnClickedStartRecording)
        CHAIN_MSG_MAP(CDialogResize<gui_controldlg>)
    END_MSG_MAP()

    BEGIN_DLGRESIZE_MAP(gui_controldlg)
        DLGRESIZE_CONTROL(IDC_START_RECORDING, DLSZ_SIZE_X | DLSZ_MOVE_Y)
    END_DLGRESIZE_MAP()

    LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnBnClickedStartRecording(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
};