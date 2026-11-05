#include "tray_popup.h"
#include "ui_theme.h"
#include "app_version.h"
#include "paint_buffer.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <algorithm>
#include <cstring>

namespace ui {
namespace {
constexpr UINT Choose = WM_APP+40;
struct Popup {
    HWND window{}, list{}; HFONT font{}; HICON icon{}; UINT dpi=96, command=0;
    const std::vector<MenuItem>* items{};
    bool below=false,compact=false,ignoreOpeningRelease=false,paintingList=false;
};
int px(const Popup& state,int n) { return MulDiv(n,state.dpi,96); }
void choose(Popup& state) {
    const auto row=SendMessageW(state.list,LB_GETCURSEL,0,0);
    if(row<0||static_cast<size_t>(row)>=state.items->size()||!(*state.items)[row].enabled)return;
    state.command=(*state.items)[row].id;
    DestroyWindow(state.window);
}
void paintList(Popup& state,HDC dc) {
    RECT bounds{};GetClientRect(state.list,&bounds);paint::Buffer frame(dc,bounds);
    if(frame&&frame.buffered()){
        FillRect(frame.dc(),&bounds,background());state.paintingList=true;
        SendMessageW(state.list,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(frame.dc()),PRF_CLIENT);
        state.paintingList=false;
    }
}
LRESULT CALLBACK listProc(HWND list,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR ref) {
    auto& state=*reinterpret_cast<Popup*>(ref);
    if(msg==WM_ERASEBKGND)return 1; // The buffered paint supplies the background.
    if(msg==WM_PAINT){
        PAINTSTRUCT p{};HDC dc=BeginPaint(list,&p);paintList(state,dc);
        EndPaint(list,&p);return 0;
    }
    if(msg==WM_KEYDOWN && (wp==VK_RETURN || wp==VK_ESCAPE)) { if(wp==VK_RETURN) choose(state);else DestroyWindow(state.window);return 0; }
    if(msg==WM_KEYDOWN&&(wp==VK_UP||wp==VK_DOWN||wp==VK_HOME||wp==VK_END)){
        const int count=static_cast<int>(state.items->size());int row=static_cast<int>(SendMessageW(list,LB_GETCURSEL,0,0));
        const int direction=(wp==VK_UP||wp==VK_END)?-1:1;if(wp==VK_HOME)row=-1;else if(wp==VK_END)row=count;
        for(int i=0;i<count;++i){row=(row+count+direction)%count;if((*state.items)[row].enabled){SendMessageW(list,LB_SETCURSEL,row,0);break;}}return 0;
    }
    return DefSubclassProc(list,msg,wp,lp);
}
int pointerRow(Popup& state,LPARAM point) {
    POINT at{GET_X_LPARAM(point),GET_Y_LPARAM(point)};ClientToScreen(state.window,&at);ScreenToClient(state.list,&at);
    const auto hit=SendMessageW(state.list,LB_ITEMFROMPOINT,0,MAKELPARAM(at.x,at.y));
    return HIWORD(hit)?-1:LOWORD(hit);
}
void paint(Popup& state,HDC dc) {
    RECT rect{};GetClientRect(state.window,&rect);FillRect(dc,&rect,background());
    HPEN pen=CreatePen(PS_SOLID,1,RGB(60,64,71));auto oldPen=SelectObject(dc,pen);auto oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
    RoundRect(dc,0,0,rect.right,rect.bottom,px(state,16),px(state,16));
    SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);
    if(state.compact)return;
    if(state.icon) DrawIconEx(dc,px(state,18),px(state,17),state.icon,px(state,20),px(state,20),0,nullptr,DI_NORMAL);
    auto oldFont=SelectObject(dc,state.font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,Muted);
    RECT label{px(state,50),px(state,8),rect.right-px(state,12),px(state,47)};
    DrawTextW(dc,L"MonitorMage " MONITORMAGE_VERSION_W,-1,&label,DT_SINGLELINE|DT_VCENTER);SelectObject(dc,oldFont);
}
LRESULT CALLBACK proc(HWND window,UINT msg,WPARAM wp,LPARAM lp) {
    auto state=reinterpret_cast<Popup*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    if(msg==WM_NCCREATE) { state=static_cast<Popup*>(reinterpret_cast<LPCREATESTRUCTW>(lp)->lpCreateParams);state->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state)); }
    if(!state) return DefWindowProcW(window,msg,wp,lp);
    switch(msg) {
    case WM_CREATE: {
        state->dpi=GetDpiForWindow(window);state->font=makeFont(state->dpi);
        if(state->compact){LOGFONTW font{};GetObjectW(state->font,sizeof(font),&font);font.lfHeight=-px(*state,style::SecondaryTextSize);auto smaller=CreateFontIndirectW(&font);if(smaller){DeleteObject(state->font);state->font=smaller;}}
        state->list=CreateWindowW(L"LISTBOX",L"MonitorMage tray actions",WS_CHILD|WS_VISIBLE|WS_TABSTOP|LBS_OWNERDRAWVARIABLE|LBS_HASSTRINGS|LBS_NOINTEGRALHEIGHT,0,0,1,1,window,reinterpret_cast<HMENU>(10),GetModuleHandleW(nullptr),nullptr);
        SetWindowTheme(state->list,L"",L"");SendMessageW(state->list,WM_SETFONT,reinterpret_cast<WPARAM>(state->font),0);
        SetWindowSubclass(state->list,listProc,1,reinterpret_cast<DWORD_PTR>(state));
        int height=0;
        for(const auto& item:*state->items) {
            const bool stateful=item.glyph==Glyph::Sun||item.glyph==Glyph::Power||item.glyph==Glyph::Diamond||item.glyph==Glyph::ColorFilter;
            const auto text=item.text+(stateful?(item.checked?L" (on)":L" (off)"):item.checked?L" (on)":L"")+(item.enabled?L"":L" (unavailable)");const auto row=SendMessageW(state->list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));
            SendMessageW(state->list,LB_SETITEMDATA,row,item.id);
            const int rowHeight=px(*state,state->compact?(item.separatorBefore?30:24):(item.separatorBefore?44:36));SendMessageW(state->list,LB_SETITEMHEIGHT,row,rowHeight);height+=rowHeight;
        }
        HDC measure=GetDC(window);const auto oldFont=SelectObject(measure,state->font);int width=px(*state,state->compact?160:200);
        for(const auto& item:*state->items){SIZE text{},hint{};GetTextExtentPoint32W(measure,item.text.c_str(),static_cast<int>(item.text.size()),&text);GetTextExtentPoint32W(measure,item.shortcut.c_str(),static_cast<int>(item.shortcut.size()),&hint);width=std::max(width,static_cast<int>(text.cx+hint.cx)+px(*state,(state->compact?(item.shortcut.empty()?24:36):(item.shortcut.empty()?40:56))+(item.glyph!=Glyph::None?24:0)));}
        SelectObject(measure,oldFont);ReleaseDC(window,measure);
        const int margin=state->compact?4:6,top=state->compact?4:51,extra=state->compact?8:61;
        MoveWindow(state->list,px(*state,margin),px(*state,top),width-px(*state,margin*2),height,TRUE);
        SetWindowPos(window,nullptr,0,0,width,height+px(*state,extra),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        HRGN region=CreateRoundRectRgn(0,0,width+1,height+px(*state,extra)+1,px(*state,16),px(*state,16));if(!SetWindowRgn(window,region,FALSE))DeleteObject(region);
        for(size_t i=0;i<state->items->size();++i)if((*state->items)[i].enabled){SendMessageW(state->list,LB_SETCURSEL,i,0);break;}return 0;
    }
    case WM_CTLCOLORLISTBOX: SetBkColor(reinterpret_cast<HDC>(wp),Background);return reinterpret_cast<LRESULT>(background());
    case WM_MEASUREITEM: reinterpret_cast<MEASUREITEMSTRUCT*>(lp)->itemHeight=px(*state,36);return TRUE;
    case WM_DRAWITEM: {
        const auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);if(item->itemID>=state->items->size())return TRUE;
        // Selection changes can send WM_DRAWITEM outside WM_PAINT. Buffer those
        // rows too, so their erase and content reach the screen together.
        const auto& entry=(*state->items)[item->itemID];RECT rect=item->rcItem;
        paint::Buffer row(state->paintingList?nullptr:item->hDC,rect);
        if(!state->paintingList&&(!row||!row.buffered()))return TRUE;
        auto dc=state->paintingList?item->hDC:row.dc();FillRect(dc,&rect,background());
        if(entry.separatorBefore) { RECT line{rect.left+px(*state,6),rect.top+px(*state,3),rect.right-px(*state,6),rect.top+px(*state,3)+1};HBRUSH brush=CreateSolidBrush(Border);FillRect(dc,&line,brush);DeleteObject(brush);rect.top+=px(*state,state->compact?6:8); }
        if((item->itemState&ODS_SELECTED)&&entry.enabled) { HPEN pen=CreatePen(PS_SOLID,1,FocusBorder);auto oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH)),oldPen=SelectObject(dc,pen);RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,px(*state,8),px(*state,8));SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen); }
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,entry.enabled?Text:Muted);auto oldFont=SelectObject(dc,state->font);
        RECT label=rect;label.left+=px(*state,state->compact?6:10);label.right-=px(*state,state->compact?6:10);RECT hint=label;
        if(entry.glyph!=Glyph::None){const int size=px(*state,state->compact?16:20),mid=(rect.top+rect.bottom)/2;
            RECT icon{label.left,mid-size/2,label.left+size,mid+(size+1)/2};paintMenuGlyph(state->window,dc,icon,entry.glyph,entry.checked,entry.enabled);label.left+=px(*state,24);}
        if(!entry.shortcut.empty()){SIZE size{};GetTextExtentPoint32W(dc,entry.shortcut.c_str(),static_cast<int>(entry.shortcut.size()),&size);label.right-=size.cx+px(*state,12);}
        DrawTextW(dc,entry.text.c_str(),-1,&label,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX|DT_END_ELLIPSIS);
        if(!entry.shortcut.empty()) { SetTextColor(dc,Muted);DrawTextW(dc,entry.shortcut.c_str(),-1,&hint,DT_SINGLELINE|DT_VCENTER|DT_RIGHT|DT_NOPREFIX); }
        SelectObject(dc,oldFont);return TRUE;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PRINTCLIENT: paint(*state,reinterpret_cast<HDC>(wp));return 0;
    case WM_PAINT: {PAINTSTRUCT p{};HDC dc=BeginPaint(window,&p);RECT bounds{};GetClientRect(window,&bounds);
        {paint::Buffer frame(dc,bounds);if(frame&&frame.buffered())paint(*state,frame.dc());}EndPaint(window,&p);return 0;}
    case WM_MOUSEMOVE: {const int row=pointerRow(*state,lp);if(row!=SendMessageW(state->list,LB_GETCURSEL,0,0))SendMessageW(state->list,LB_SETCURSEL,row,0);return 0;}
    case WM_LBUTTONDOWN: if(pointerRow(*state,lp)<0)DestroyWindow(window);return 0;
    case WM_LBUTTONUP: {const int row=pointerRow(*state,lp);const bool opening=state->ignoreOpeningRelease;state->ignoreOpeningRelease=false;if(row>=0){SendMessageW(state->list,LB_SETCURSEL,row,0);choose(*state);}else if(!opening)DestroyWindow(window);return 0;}
    case WM_RBUTTONDOWN: case WM_CANCELMODE: DestroyWindow(window);return 0;
    case WM_ACTIVATE: if(LOWORD(wp)==WA_INACTIVE)DestroyWindow(window);return 0;
    case WM_COMMAND: if(LOWORD(wp)==IDOK)choose(*state);else if(LOWORD(wp)==IDCANCEL)DestroyWindow(window);return 0;
    case Choose: choose(*state);return 0;
    case WM_CLOSE: DestroyWindow(window);return 0;
    case WM_DESTROY: if(GetCapture()==window)ReleaseCapture();if(state->font){DeleteObject(state->font);state->font=nullptr;}return 0;
    }
    return DefWindowProcW(window,msg,wp,lp);
}
HWND create(HWND owner,Popup& state,POINT point) {
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=proc;cls.lpszClassName=L"MonitorMageTrayPopup";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=background();
    if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return nullptr;
    HWND popup=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,cls.lpszClassName,L"MonitorMage tray menu",WS_POPUP|WS_CLIPCHILDREN,point.x,point.y,1,1,owner,nullptr,cls.hInstance,&state);
    if(!popup)return nullptr;
    RECT rect{};GetWindowRect(popup,&rect);MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST),&monitor);
    const LONG x=std::clamp(point.x,monitor.rcWork.left,std::max(monitor.rcWork.left,monitor.rcWork.right-(rect.right-rect.left)));
    const LONG y=std::clamp(state.below?point.y:point.y-(rect.bottom-rect.top),monitor.rcWork.top,std::max(monitor.rcWork.top,monitor.rcWork.bottom-(rect.bottom-rect.top)));
    SetWindowPos(popup,nullptr,x,y,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);return popup;
}
}
UINT popup(HWND owner,HICON icon,const std::vector<MenuItem>& items,POINT anchor,bool below) {
    Popup state;state.icon=icon;state.items=&items;state.below=below;state.compact=below;state.ignoreOpeningRelease=(GetAsyncKeyState(VK_LBUTTON)&0x8000)!=0;HWND window=create(owner,state,anchor);if(!window)return 0;
    if(below){SetWindowTextW(window,L"MonitorMage window menu");SetWindowTextW(state.list,L"Window actions");}
    SetForegroundWindow(window);ShowWindow(window,SW_SHOWNORMAL);SetFocus(state.list);SetCapture(window);
    MSG message{};
    while(IsWindow(window)) {
        const int status=GetMessageW(&message,nullptr,0,0);
        if(status<=0) { if(IsWindow(window))DestroyWindow(window);if(status==0)PostQuitMessage(static_cast<int>(message.wParam));break; }
        if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}
    }
    PostMessageW(owner,WM_NULL,0,0);return state.command;
}
bool previewPopup(HWND owner,HICON icon,const std::vector<MenuItem>& items,std::function<bool(HWND)> render,bool compact) {
    Popup state;state.icon=icon;state.items=&items;state.compact=compact;POINT point{100,100};auto window=create(owner,state,point);if(!window)return false;
    const bool result=render(window);if(IsWindow(window))DestroyWindow(window);return result;
}
// Replay hover damage into an existing image; compare against a complete native
// list repaint. Erase requests must leave the previous image untouched.
bool popupPaintTest(Popup& state) {
    RECT bounds{};GetClientRect(state.list,&bounds);HDC screen=GetDC(state.list);
    HDC partial=CreateCompatibleDC(screen),full=CreateCompatibleDC(screen);
    BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),bounds.right,-bounds.bottom,1,32,BI_RGB};
    void *a=nullptr,*b=nullptr;HBITMAP first=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&a,nullptr,0),second=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&b,nullptr,0);
    ReleaseDC(state.list,screen);
    auto oldA=SelectObject(partial,first),oldB=SelectObject(full,second);
    bool ok=partial&&full&&first&&second;
    if(ok){
        paintList(state,partial);int previous=static_cast<int>(SendMessageW(state.list,LB_GETCURSEL,0,0));
        const size_t bytes=static_cast<size_t>(bounds.right)*bounds.bottom*4;
        for(int selected:{3,1,0,2,1}){
            RECT oldRow{},newRow{},damage{};SendMessageW(state.list,LB_GETITEMRECT,previous,reinterpret_cast<LPARAM>(&oldRow));
            SendMessageW(state.list,LB_SETCURSEL,selected,0);SendMessageW(state.list,LB_GETITEMRECT,selected,reinterpret_cast<LPARAM>(&newRow));UnionRect(&damage,&oldRow,&newRow);
            const int saved=SaveDC(partial);IntersectClipRect(partial,damage.left,damage.top,damage.right,damage.bottom);paintList(state,partial);RestoreDC(partial,saved);
            state.paintingList=true;SendMessageW(state.list,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(full),PRF_CLIENT);state.paintingList=false;
            GdiFlush();if(std::memcmp(a,b,bytes)!=0){ok=false;break;}
            SendMessageW(state.list,WM_ERASEBKGND,reinterpret_cast<WPARAM>(partial),0);
            SendMessageW(state.window,WM_ERASEBKGND,reinterpret_cast<WPARAM>(partial),0);
            GdiFlush();if(std::memcmp(a,b,bytes)!=0){ok=false;break;}previous=selected;
        }
    }
    SelectObject(partial,oldA);SelectObject(full,oldB);DeleteObject(first);DeleteObject(second);DeleteDC(partial);DeleteDC(full);return ok;
}
int popupSelfTest(HWND owner) {
    const std::vector<MenuItem> items{{10,L"Unavailable",L"",false,false,false},{11,L"Open editor",L""},{13,L"Unavailable",L"",false,false,false},{12,L"Enable corrections",L"",true}};
    Popup state;state.items=&items;POINT point{100,100};HWND window=create(owner,state,point);if(!window)return 1;
    if(!popupPaintTest(state)){DestroyWindow(window);return 11;}
    state.compact=true;DestroyWindow(window);window=create(owner,state,point);if(!window)return 12;
    if(!popupPaintTest(state)){DestroyWindow(window);return 13;}
    SendMessageW(state.list,LB_SETCURSEL,0,0);SendMessageW(state.list,WM_KEYDOWN,VK_RETURN,0);
    if(!IsWindow(window)||state.command)return 7;
    SendMessageW(state.list,WM_KEYDOWN,VK_HOME,0);if(SendMessageW(state.list,LB_GETCURSEL,0,0)!=1)return 8;
    SendMessageW(state.list,WM_KEYDOWN,VK_DOWN,0);if(SendMessageW(state.list,LB_GETCURSEL,0,0)!=3)return 9;
    SendMessageW(state.list,WM_KEYDOWN,VK_RETURN,0);
    if(IsWindow(window)||state.command!=12)return 2;
    state.command=0;window=create(owner,state,point);if(!window)return 3;
    SendMessageW(window,WM_COMMAND,IDCANCEL,0);if(IsWindow(window)||state.command)return 4;
    window=create(owner,state,point);if(!window)return 5;
    state.ignoreOpeningRelease=true;
    SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(-1,-1));if(!IsWindow(window)||state.command)return 10;
    SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(-1,-1));return !IsWindow(window)&&state.command==0?0:6;
}
}
