#pragma once

#include "wtl.h"
#include "gui_event_handler.h"
#include "control_pipeline.h"
#include <string>

class gui_scenedlg final :
    public gui_event_handler,
    public CDialogImpl<gui_scenedlg>,
    public CDialogResize<gui_scenedlg>
{
private:
    int scene_counter;
    control_pipeline_t ctrl_pipeline;
    CContainedWindowT<CTreeViewCtrlEx> wnd_scenetree;
    CToolBarCtrl wnd_toolbar;
    CFont font_toolbar;
    bool selecting;

    // gui_event_handler
    void on_scene_activate(control_scene* activated_scene, bool deactivated) override;
    void on_control_added(control_class*, bool removed, control_scene* scene) override;
public:
    enum {IDD = IDD_SCENEDLG};

    explicit gui_scenedlg(const control_pipeline_t&);
    ~gui_scenedlg();

    // command handlers handle events from child windows
    BEGIN_MSG_MAP(gui_scenedlg)
        COMMAND_HANDLER(ID_BUTTON_ADD_SRC, BN_CLICKED, OnBnClickedAddscene)
        COMMAND_HANDLER(ID_BUTTON_REMOVE_SRC, BN_CLICKED, OnBnClickedRemovescene)
        MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
        NOTIFY_HANDLER(IDC_SCENETREE, TVN_SELCHANGED, OnTvnSelchangedScenetree)
        MSG_WM_SIZE(OnSceneDlgSize)
        CHAIN_MSG_MAP(CDialogResize<gui_scenedlg>)

    // alt message map for scene tree view
    ALT_MSG_MAP(1)
    END_MSG_MAP()

    BEGIN_DLGRESIZE_MAP(gui_scenedlg)
    END_DLGRESIZE_MAP()

    LRESULT OnBnClickedAddscene(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
    LRESULT OnBnClickedRemovescene(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
    LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTvnSelchangedScenetree(int /*idCtrl*/, LPNMHDR pNMHDR, BOOL& /*bHandled*/);
    void OnSceneDlgSize(UINT nType, CSize size);
};

class gui_sourcedlg final :
    public gui_event_handler,
    public CDialogImpl<gui_sourcedlg>,
    public CDialogResize<gui_sourcedlg>
{
private:
    int video_counter, audio_counter;

    control_pipeline_t ctrl_pipeline;

    CContainedWindowT<CTreeViewCtrlEx> wnd_sourcetree;
    CToolBarCtrl wnd_toolbar;
    CFont font_toolbar;
    CMenu menu_sources;

    bool do_not_reselect;
    control_scene* current_active_scene;
    bool update_source_list_on_scene_activate;

    // gui_event_handler
    void on_scene_activate(control_scene* activated_scene, bool deactivated) override;
    void on_activate(control_class*, bool deactivated) override;
    void on_control_added(control_class*, bool removed, control_scene* scene) override;
    void on_control_selection_changed() override;

    void set_selected_item(CTreeItem item);
    void set_source_tree(const control_scene*);
    void set_selected_item(const control_class*);
public:
    enum {IDD = IDD_SOURCEDLG};

    explicit gui_sourcedlg(const control_pipeline_t&);
    ~gui_sourcedlg();

    BEGIN_MSG_MAP(gui_sourcedlg)
        COMMAND_HANDLER(ID_BUTTON_ADD_SRC, BN_CLICKED, OnBnClickedAddsrc)
        COMMAND_HANDLER(ID_BUTTON_REMOVE_SRC, BN_CLICKED, OnBnClickedRemovesrc)
        MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
        MSG_WM_SIZE(OnSourceDlgSize)
        NOTIFY_HANDLER(IDC_SOURCETREE, TVN_SELCHANGED, OnTvnSelchangedSourcetree)
        NOTIFY_HANDLER(IDC_SOURCETREE, NM_KILLFOCUS, OnKillFocus)
        NOTIFY_HANDLER(IDC_SOURCETREE, NM_SETFOCUS, OnSetFocus)
        COMMAND_HANDLER(ID_BUTTON_MOVE_UP_SRC, BN_CLICKED, OnBnClickedSrcup)
        COMMAND_HANDLER(ID_BUTTON_MOVE_DOWN_SRC, BN_CLICKED, OnBnClickedSrcdown)
        COMMAND_HANDLER(ID_BUTTON_CONFIGURE_SRC, BN_CLICKED, OnBnClickedConfigure)
        CHAIN_MSG_MAP(CDialogResize<gui_sourcedlg>)

    // alt message map for source tree view
    ALT_MSG_MAP(1)
        NOTIFY_HANDLER(ID_SOURCE_TOOLBAR, NM_CUSTOMDRAW, OnSourceToolbarCustomDraw)
    END_MSG_MAP()

    BEGIN_DLGRESIZE_MAP(gui_sourcedlg)
    END_DLGRESIZE_MAP()

    LRESULT OnBnClickedAddsrc(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
    LRESULT OnBnClickedRemovesrc(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
    LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnTvnSelchangedSourcetree(int /*idCtrl*/, LPNMHDR pNMHDR, BOOL& /*bHandled*/);
    LRESULT OnKillFocus(int /*idCtrl*/, LPNMHDR pNMHDR, BOOL& /*bHandled*/);
    LRESULT OnSetFocus(int /*idCtrl*/, LPNMHDR pNMHDR, BOOL& /*bHandled*/);
    LRESULT OnBnClickedSrcup(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
    LRESULT OnBnClickedSrcdown(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
    LRESULT OnBnClickedConfigure(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
    LRESULT OnSourceToolbarCustomDraw(int /*wParam*/, LPNMHDR /*lParam*/, BOOL& /*bHandled*/);
    void OnSourceDlgSize(UINT /*nType*/, CSize /*size*/);
};

class gui_controldlg :
    public CDialogImpl<gui_controldlg>,
    public CDialogResize<gui_controldlg>,
    public CIdleHandler
{
private:
    control_pipeline_t ctrl_pipeline;
    CButton btn_start_recording, btn_start_streaming;

    void start_recording(bool streaming);
    void stop_recording(bool streaming);
public:
    enum {IDD = IDD_CTRLDLG};

    explicit gui_controldlg(const control_pipeline_t&);

    BEGIN_MSG_MAP(gui_controldlg)
        MSG_WM_DESTROY(OnDestroy)
        MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
        COMMAND_HANDLER(IDC_START_RECORDING, BN_CLICKED, OnBnClickedStartRecording)
        MESSAGE_HANDLER(RECORDING_STOPPED_MESSAGE, OnRecordingStopped)
        COMMAND_HANDLER(IDC_START_STREAMING, BN_CLICKED, OnBnClickedStartStreaming)
        CHAIN_MSG_MAP(CDialogResize<gui_controldlg>)
    END_MSG_MAP()

    BEGIN_DLGRESIZE_MAP(gui_controldlg)
        DLGRESIZE_CONTROL(IDC_START_STREAMING, DLSZ_SIZE_X | DLSZ_MOVE_Y)
        DLGRESIZE_CONTROL(IDC_START_RECORDING, DLSZ_SIZE_X | DLSZ_MOVE_Y)
    END_DLGRESIZE_MAP()

    void OnDestroy();
    LRESULT OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnBnClickedStartRecording(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
    LRESULT OnRecordingStopped(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
    LRESULT OnBnClickedStartStreaming(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);

    BOOL OnIdle();
};