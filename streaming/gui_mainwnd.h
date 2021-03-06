#pragma once

#include "wtl.h"
#include "gui_threadwnd.h"
#include "gui_previewwnd.h"
#include "gui_dlgs.h"
#include "gui_event_handler.h"
#include "control_pipeline.h"
#include <atlsplit.h>
#include <memory>
#include <string>

#define GUI_MAINWND_SHOW_MESSAGE WM_APP

class gui_mainwnd;

// hosts the dialogs
class gui_controlwnd : 
    public CWindowImpl<gui_controlwnd>,
    public CMessageFilter
{
    friend class gui_mainwnd;
private:
    control_pipeline_t ctrl_pipeline;
    gui_scenedlg dlg_scenes;
    gui_sourcedlg dlg_sources;
    gui_controldlg dlg_controls;

    bool dpi_changed;
public:
    DECLARE_WND_CLASS(L"gui_controlwnd")

    explicit gui_controlwnd(const control_pipeline_t&);

    BOOL PreTranslateMessage(MSG* pMsg);

    BEGIN_MSG_MAP(gui_controlwnd)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_DESTROY(OnDestroy)
        MSG_WM_SIZE(OnSize)
        MESSAGE_HANDLER(WM_DPICHANGED_BEFOREPARENT, OnDpiChanged)
    END_MSG_MAP()

    int OnCreate(LPCREATESTRUCT);
    void OnDestroy();
    void OnSize(UINT /*nType*/, CSize size);
    LRESULT OnDpiChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
};

// hosts the preview and control window using a splitter window
class gui_mainwnd final :
    public gui_event_handler,
    public CFrameWindowImpl<gui_mainwnd>,
    public CMessageFilter,
    public CIdleHandler
{
public:
private:
    control_pipeline_t ctrl_pipeline;

    BOOL was_minimized;
    CSplitterWindow wnd_splitter;
    /*std::unique_ptr<gui_previewwnd> wnd_preview;*/
    std::unique_ptr<gui_controlwnd> wnd_control;
    CWindow last_focus;
    CContainedWindowT<CStatusBarCtrl> wnd_statusbar;

    void set_statusbar_parts(CSize);

    // gui_event_handler
    void on_activate(control_class*, bool deactivated) override;
public:
    DECLARE_FRAME_WND_CLASS(L"streaming", IDR_MAINFRAME)

    gui_mainwnd();

    static void show_dump_file_dialog(
        LPEXCEPTION_POINTERS = nullptr,
        control_pipeline* ctrl_pipeline = nullptr,
        HWND parent = nullptr);

    BOOL PreTranslateMessage(MSG* pMsg);
    BOOL OnIdle();
    BEGIN_MSG_MAP(gui_mainwnd)
        MSG_WM_CREATE(OnCreate)
        MSG_WM_DESTROY(OnDestroy)
        MSG_WM_SETFOCUS(OnSetFocus)
        MSG_WM_ACTIVATE(OnActivate)
        MESSAGE_HANDLER(GUI_MAINWND_SHOW_MESSAGE, OnMainWndShowMessage)
        MESSAGE_HANDLER(WM_DPICHANGED, OnDpiChanged)
        COMMAND_ID_HANDLER(ID_HELP_ABOUT, OnAbout)
        COMMAND_ID_HANDLER(ID_DEBUG, OnDebug)
        COMMAND_ID_HANDLER(ID_FILE_SETTINGS, OnSettings)
        COMMAND_ID_HANDLER(ID_FILE_EXIT, OnExit)
        COMMAND_ID_HANDLER(ID_DEBUG_CREATE_DUMP_FILE, OnCreateDumpFile)

        CHAIN_MSG_MAP(CFrameWindowImpl<gui_mainwnd>)

        // alt msg map must be at the end
        ALT_MSG_MAP(1) // 1 is the id for the ccontainedwindow; the begin_msg_map is identified by 0
            MSG_WM_SIZE(OnStatusBarSize)
            MESSAGE_HANDLER(SB_SIMPLE, OnStatusBarSimple)
    END_MSG_MAP()

    int OnCreate(LPCREATESTRUCT);
    void OnDestroy();
    void OnSetFocus(CWindow /*old*/);
    void OnActivate(UINT /*nState*/, BOOL /*bMinimized*/, CWindow /*wndOther*/);
    void OnStatusBarSize(UINT /*nType*/, CSize size);
    LRESULT OnStatusBarSimple(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
    LRESULT OnMainWndShowMessage(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
    LRESULT OnDpiChanged(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& /*bHandled*/);
    LRESULT OnAbout(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
    LRESULT OnDebug(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
    LRESULT OnSettings(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
    LRESULT OnExit(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
    LRESULT OnCreateDumpFile(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
};