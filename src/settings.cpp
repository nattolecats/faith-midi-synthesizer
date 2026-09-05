#include "common.h"
#include <commctrl.h>
#include <shellapi.h>

int runHost();
static HWND slider,label,typeCombo,statusLabel;
static HFONT font;
static void updateLabel() {
    auto text=std::to_wstring(SendMessageW(slider,TBM_GETPOS,0,0))+L" %";
    SetWindowTextW(label,text.c_str());
}
static void registration(HWND window,bool remove) {
    auto script=appDirectory()+L"install.ps1";
    auto args=L"-NoProfile -ExecutionPolicy Bypass -File \""+script+L"\""+(remove?L" -Uninstall":L"");
    SHELLEXECUTEINFOW info{sizeof(info)};
    info.hwnd=window;info.lpVerb=L"runas";info.lpFile=L"powershell.exe";info.lpParameters=args.c_str();info.nShow=SW_HIDE;
    if(!ShellExecuteExW(&info)) MessageBoxW(window,L"登録処理を開始できませんでした。",L"Faith MIDI",MB_ICONERROR);
}
static HWND control(HWND parent,const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id=0) {
    auto hwnd=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,parent,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);
    SendMessageW(hwnd,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return hwnd;
}
static LRESULT CALLBACK wndProc(HWND window,UINT message,WPARAM w,LPARAM l) {
    switch(message) {
    case WM_CREATE:
        font=CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Yu Gothic UI");
        control(window,L"STATIC",L"Faith MIDI Synthesizer",0,24,20,470,30);
        control(window,L"STATIC",L"音源タイプ（次回のMIDIデバイス接続時に反映）",0,24,64,480,28);
        typeCombo=control(window,L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,24,96,480,200,101);
        for(auto text:{L"Type 1 — rt_player_1.dll",L"Type 2 — rt_synth_2.dll",L"Type 3 — rt_player_3.dll",L"Type 4 — rt_synth_4.dll（標準）",L"Type 5 — rt_synth_5.dll"}) SendMessageW(typeCombo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));
        SendMessageW(typeCombo,CB_SETCURSEL,readType()-1,0);
        control(window,L"STATIC",L"音量",0,24,148,130,28);
        label=control(window,L"STATIC",L"",SS_RIGHT,380,148,120,28);
        slider=control(window,TRACKBAR_CLASSW,L"",TBS_AUTOTICKS|WS_TABSTOP,24,184,480,42,102);
        SendMessageW(slider,TBM_SETRANGE,TRUE,MAKELONG(0,maxVolume));SendMessageW(slider,TBM_SETTICFREQ,25,0);
        SendMessageW(slider,TBM_SETPOS,TRUE,readVolume());updateLabel();
        control(window,L"STATIC",L"0%                 100%                 200%                 300%",0,28,232,478,24);
        statusLabel=control(window,L"STATIC",L"音量は自動保存され、再生中にも反映されます。",0,24,278,490,26);
        control(window,L"STATIC",L"音源DLLはこのアプリと同じフォルダーに置いてください。",0,24,312,490,26);
        control(window,L"BUTTON",L"デバイスを登録",WS_TABSTOP,190,364,150,38,104);
        control(window,L"BUTTON",L"登録解除",WS_TABSTOP,356,364,150,38,105);
        return 0;
    case WM_HSCROLL:
        if(reinterpret_cast<HWND>(l)==slider) {
            updateLabel();
            if(!writeVolume(static_cast<DWORD>(SendMessageW(slider,TBM_GETPOS,0,0)))) SetWindowTextW(statusLabel,L"音量設定を保存できませんでした。");
        }return 0;
    case WM_COMMAND:
        if(LOWORD(w)==101&&HIWORD(w)==CBN_SELCHANGE) {
            DWORD type=static_cast<DWORD>(SendMessageW(typeCombo,CB_GETCURSEL,0,0))+1;
            if(!writeType(type)) SetWindowTextW(statusLabel,L"音源タイプを保存できませんでした。");
        }
        if(LOWORD(w)==104) registration(window,false);
        if(LOWORD(w)==105) registration(window,true);
        return 0;
    case WM_DESTROY:DeleteObject(font);PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(window,message,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command,int show) {
    if(wcscmp(command,L"--host")==0) return runHost();
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES};InitCommonControlsEx(&controls);
    WNDCLASSW cls{};cls.hInstance=instance;cls.lpszClassName=L"FaithMidiSettings";cls.lpfnWndProc=wndProc;
    cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_BTNFACE+1);
    RegisterClassW(&cls);
    HWND window=CreateWindowExW(0,cls.lpszClassName,L"Faith MIDI Synthesizer 設定",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,552,470,nullptr,nullptr,instance,nullptr);
    if(!window) return 1;
    ShowWindow(window,show);
    MSG msg{};
    while(GetMessageW(&msg,nullptr,0,0)>0) {if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
    return 0;
}
