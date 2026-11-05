#include "ui_theme.h"
#include "tray_popup.h"
#include "paint_buffer.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <string>
#include <windowsx.h>
#include <cmath>
#include <vector>
#include <algorithm>
#include <memory>

namespace ui {
namespace {
HRGN tooltipShape(HWND tip,int inset=0){
    RECT bounds{};GetWindowRect(tip,&bounds);const int d=MulDiv(style::CornerRadius*2,GetDpiForWindow(tip),96);
    return CreateRoundRectRgn(inset,inset,bounds.right-bounds.left-inset,bounds.bottom-bounds.top-inset,std::max(1,d-inset*2),std::max(1,d-inset*2));
}
void tooltipBounds(HWND tip){
    HRGN region=tooltipShape(tip);
    HRGN previous=CreateRectRgn(0,0,0,0);const bool same=GetWindowRgn(tip,previous)!=ERROR&&EqualRgn(region,previous);DeleteObject(previous);
    if(same||!SetWindowRgn(tip,region,TRUE))DeleteObject(region);
}
void tooltipBorder(HWND tip,HDC supplied=nullptr){
        HDC dc=supplied?supplied:GetWindowDC(tip);RECT bounds{};GetWindowRect(tip,&bounds);
        if(!dc)return;
        const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
        const int d=MulDiv(style::CornerRadius*2,GetDpiForWindow(tip),96);
        // Remove the native bevel, including its corner remnants. The padding
        // keeps text outside these small corner squares.
        HBRUSH fill=CreateSolidBrush(Surface);const int corner=d/2+1;
        const RECT clear[]{{0,0,width,2},{0,height-2,width,height},{0,0,2,height},{width-2,0,width,height},
            {0,0,corner,corner},{width-corner,0,width,corner},{0,height-corner,corner,height},{width-corner,height-corner,width,height}};
        for(const auto& rect:clear)FillRect(dc,&rect,fill);DeleteObject(fill);
        // Fill the difference between two masks rather than stroking another
        // rounded rectangle. Window clipping and the edge now share exactly
        // the same rasterized contour, including the bottom/right pixels.
        HRGN edge=tooltipShape(tip),inside=tooltipShape(tip,1);CombineRgn(edge,edge,inside,RGN_DIFF);
        fill=CreateSolidBrush(Border);FillRgn(dc,edge,fill);
        DeleteObject(fill);DeleteObject(inside);DeleteObject(edge);if(!supplied)ReleaseDC(tip,dc);
}
LRESULT CALLBACK tooltipStyleProc(HWND tip,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(msg==WM_NCDESTROY){
        auto font=reinterpret_cast<HFONT>(RemovePropW(tip,L"MonitorMageTipFont"));
        const auto result=DefSubclassProc(tip,msg,wp,lp);if(font)DeleteObject(font);return result;
    }
    if(msg==WM_NCPAINT){tooltipBorder(tip);return 0;}
    const auto result=DefSubclassProc(tip,msg,wp,lp);
    if(msg==WM_PAINT)tooltipBorder(tip);
    if(msg==WM_WINDOWPOSCHANGED)tooltipBounds(tip);
    if((msg==WM_SHOWWINDOW&&wp)||msg==WM_THEMECHANGED)styleTooltip(tip);
    return result;
}
struct SliderClicks {POINT point{};DWORD time=0;bool candidate=false,tracking=false,suppressUp=false;};
LRESULT CALLBACK sliderResetProc(HWND slider,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR ref){
    auto& clicks=*reinterpret_cast<SliderClicks*>(ref);
    if(msg==WM_NCDESTROY){delete &clicks;return DefSubclassProc(slider,msg,wp,lp);}
    const POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
    const auto close=[&](){return std::abs(point.x-clicks.point.x)<=GetSystemMetrics(SM_CXDOUBLECLK)/2&&std::abs(point.y-clicks.point.y)<=GetSystemMetrics(SM_CYDOUBLECLK)/2;};
    bool doubleClick=msg==WM_LBUTTONDBLCLK;
    if(msg==WM_LBUTTONDOWN){
        doubleClick=clicks.candidate&&GetTickCount()-clicks.time<=GetDoubleClickTime()&&close();
        clicks.candidate=false;clicks.point=point;clicks.time=GetTickCount();clicks.tracking=!doubleClick;
    }
    if(msg==WM_MOUSEMOVE&&clicks.tracking&&!close())clicks.tracking=false;
    if(msg==WM_LBUTTONUP){
        if(clicks.suppressUp){clicks.suppressUp=false;return 0;}
        clicks.candidate=clicks.tracking;clicks.tracking=false;clicks.point=point;
    }
    if(msg==WM_CANCELMODE||msg==WM_KILLFOCUS)clicks.candidate=clicks.tracking=false;
    if(msg==WM_RBUTTONDOWN)return 0;
    if(doubleClick||msg==WM_RBUTTONUP||msg==WM_CONTEXTMENU){
        clicks.candidate=clicks.tracking=false;clicks.suppressUp=doubleClick;
        if(IsWindowEnabled(slider))if(const auto marker=reinterpret_cast<INT_PTR>(GetPropW(slider,L"LutSliderDefault"))){
            SendMessageW(slider,TBM_SETPOS,TRUE,marker-32769);
            SendMessageW(GetParent(slider),WM_HSCROLL,TB_ENDTRACK,reinterpret_cast<LPARAM>(slider));
        }
        return 0;
    }
    return DefSubclassProc(slider,msg,wp,lp);
}
}
void styleTooltip(HWND tip){
    if(!tip)return;
    auto old=reinterpret_cast<HFONT>(GetPropW(tip,L"MonitorMageTipFont"));
    if(!old){SetWindowTheme(tip,L"",L"");SetWindowSubclass(tip,tooltipStyleProc,10,0);}
    const UINT dpi=GetDpiForWindow(tip);auto base=makeFont(dpi);LOGFONTW desired{},current{};
    GetObjectW(base,sizeof(desired),&desired);DeleteObject(base);
    desired.lfHeight=-MulDiv(style::SecondaryTextSize,dpi,96);
    if(old)GetObjectW(old,sizeof(current),&current);
    if(!old||desired.lfHeight!=current.lfHeight||wcscmp(desired.lfFaceName,current.lfFaceName)){
        if(auto font=CreateFontIndirectW(&desired)){
            SendMessageW(tip,WM_SETFONT,reinterpret_cast<WPARAM>(font),FALSE);
            SetPropW(tip,L"MonitorMageTipFont",font);if(old)DeleteObject(old);old=font;
        }
    }
    if(old)SendMessageW(tip,WM_SETFONT,reinterpret_cast<WPARAM>(old),FALSE);
    const int horizontal=MulDiv(6,dpi,96),vertical=MulDiv(3,dpi,96);
    RECT margin{horizontal,vertical,horizontal,vertical};
    SendMessageW(tip,TTM_SETMARGIN,0,reinterpret_cast<LPARAM>(&margin));
    SendMessageW(tip,TTM_SETTIPBKCOLOR,Surface,0);SendMessageW(tip,TTM_SETTIPTEXTCOLOR,Text,0);
}
bool tooltipEdgeSelfTest(HWND tip){
    RECT bounds{};GetWindowRect(tip,&bounds);const int w=bounds.right-bounds.left,h=bounds.bottom-bounds.top;
    HDC screen=GetDC(tip),dc=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,w,h);ReleaseDC(tip,screen);
    if(!dc||!bitmap){if(dc)DeleteDC(dc);if(bitmap)DeleteObject(bitmap);return false;}
    auto old=SelectObject(dc,bitmap);RECT all{0,0,w,h},bottom{0,h/2,w,h};
    FillRect(dc,&all,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));FillRect(dc,&bottom,reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    HRGN shape=tooltipShape(tip);SelectClipRgn(dc,shape);tooltipBorder(tip,dc);GdiFlush();bool ok=true;
    // Every visible outermost pixel must have the same edge color, regardless
    // of a bright or dark native frame underneath. Check all four directions.
    for(int y=0;y<h;++y){int left=0,right=w-1;while(left<w&&!PtInRegion(shape,left,y))++left;while(right>=left&&!PtInRegion(shape,right,y))--right;
        if(left<=right&&(GetPixel(dc,left,y)!=Border||GetPixel(dc,right,y)!=Border))ok=false;}
    for(int x=0;x<w;++x){int top=0,last=h-1;while(top<h&&!PtInRegion(shape,x,top))++top;while(last>=top&&!PtInRegion(shape,x,last))--last;
        if(top<=last&&(GetPixel(dc,x,top)!=Border||GetPixel(dc,x,last)!=Border))ok=false;}
    DeleteObject(shape);SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);return ok;
}
namespace {
struct Brush { HBRUSH value; explicit Brush(COLORREF color) : value(CreateSolidBrush(color)) {} ~Brush() { DeleteObject(value); } };
Brush bg(Background), surface(Surface);
std::wstring family=L"Bahnschrift";
int scale(HWND window, int n) { return MulDiv(n, GetDpiForWindow(window), 96); }
void rounded(HDC dc, RECT rect, COLORREF fill, COLORREF border, int radius) {
    HBRUSH brush = CreateSolidBrush(fill); HPEN pen = CreatePen(PS_SOLID, 1, border);
    const auto oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius*2, radius*2);
    SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(brush); DeleteObject(pen);
}
void roundBounds(HWND window,int radius=style::CornerRadius){
    RECT rect{};GetWindowRect(window,&rect);const int w=rect.right-rect.left,h=rect.bottom-rect.top;
    if(w<=0||h<=0)return;
    HRGN shape=CreateRoundRectRgn(0,0,w+1,h+1,scale(window,radius*2),scale(window,radius*2));
    if(!shape)return;
    HRGN previous=CreateRectRgn(0,0,0,0);const bool same=GetWindowRgn(window,previous)!=ERROR&&EqualRgn(shape,previous);DeleteObject(previous);
    if(same||!SetWindowRgn(window,shape,TRUE))DeleteObject(shape);
}
void outline(HWND window,HDC dc,RECT rect){
    HPEN pen=CreatePen(PS_SOLID,1,(GetFocus()==window||GetPropW(window,L"LutThemeHover"))?FocusBorder:Border);
    const auto oldPen=SelectObject(dc,pen),oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
    RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,scale(window,8),scale(window,8));
    SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);
}
void gear(HDC dc,int x,int y,int radius,COLORREF colour,int stroke) {
    POINT points[32]{};for(int i=0;i<32;++i){const double angle=i*6.283185307179586/32;const double r=(i%4<2?radius:radius*.77);points[i]={x+static_cast<LONG>(std::lround(std::cos(angle)*r)),y+static_cast<LONG>(std::lround(std::sin(angle)*r))};}
    HPEN pen=CreatePen(PS_SOLID,stroke,colour);auto op=SelectObject(dc,pen),ob=SelectObject(dc,GetStockObject(NULL_BRUSH));Polygon(dc,points,32);
    Ellipse(dc,x-radius/3,y-radius/3,x+radius/3+1,y+radius/3+1);SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(pen);
}
// Keep the one-pixel button edge out of the icon's supersampling pass.
// The fill and border share one native raster contour, like popup edges.
void glyphFrame(HWND window,HDC dc,RECT rect,int kind,bool hover,bool focus,COLORREF base,bool menu) {
    const bool toolbar=!menu&&kind>=4&&kind<=8;
    if(!toolbar&&!hover&&!focus)return;
    const int diameter=scale(window,8);
    HRGN shape=CreateRoundRectRgn(rect.left,rect.top,rect.right,rect.bottom,diameter,diameter);
    HRGN outside=CreateRectRgn(rect.left,rect.top,rect.right,rect.bottom);
    if(shape&&outside){
        CombineRgn(outside,outside,shape,RGN_DIFF);
        HBRUSH fill=CreateSolidBrush(base);FillRgn(dc,outside,fill);DeleteObject(fill);
        const COLORREF edgeColor=hover||focus?FocusBorder:Border;
        HBRUSH edge=CreateSolidBrush(edgeColor);FrameRgn(dc,shape,edge,1,1);DeleteObject(edge);
        // Approved middle variant: keep the rounded corners bright, then
        // smoothly darken each straight edge to 76.5% of its corner color.
        // This runs only when generating a cached icon/button image.
        const int radius=diameter/2;
        const double transition=std::max(1.0,scale(window,16)/3.0);
        const auto colorAt=[&](int distance){
            double t=std::clamp(distance/transition,0.0,1.0);t=t*t*(3-2*t);
            const double gain=1.0-.235*t;
            return RGB(static_cast<int>(std::lround(GetRValue(edgeColor)*gain)),
                       static_cast<int>(std::lround(GetGValue(edgeColor)*gain)),
                       static_cast<int>(std::lround(GetBValue(edgeColor)*gain)));
        };
        const int left=rect.left+radius,right=rect.right-radius-2;
        for(int x=left;x<=right;++x){const auto color=colorAt(std::min(x-left,right-x));SetPixelV(dc,x,rect.top,color);SetPixelV(dc,x,rect.bottom-2,color);}
        const int top=rect.top+radius,bottom=rect.bottom-radius-2;
        for(int y=top;y<=bottom;++y){const auto color=colorAt(std::min(y-top,bottom-y));SetPixelV(dc,rect.left,y,color);SetPixelV(dc,rect.right-2,y,color);}
    }
    if(outside)DeleteObject(outside);if(shape)DeleteObject(shape);
}
void glyphRaster(HWND window,HDC dc,RECT rect,int kind,bool on,bool enabled,bool /*hover*/,bool /*focus*/,COLORREF base,bool menu=false) {
    const bool toolbar=!menu&&kind>=4&&kind<=8;
    HBRUSH back=CreateSolidBrush(toolbar?Surface:base);FillRect(dc,&rect,back);DeleteObject(back);
    const COLORREF ink=menu?(kind==16?CloseRed:kind==1?(on?RGB(255,207,69):Muted):kind==2||kind==3||kind==19?(on?Accent:Muted):enabled?Text:Border):toolbar?(enabled?Text:Border):kind==1?(on?RGB(255,207,69):Muted):on?Accent:Muted;
    // Small line icons share the caption controls' one-DIP stroke weight.
    // Keep their button bounds unchanged so pointer and keyboard targets stay roomy.
    const auto unit=[&](int value){return MulDiv(scale(window,value),3,4);};
    const int x=(rect.left+rect.right)/2,y=(rect.top+rect.bottom)/2,r=unit(8);
    HPEN pen=CreatePen(PS_SOLID,scale(window,1),!enabled&&!on?Border:ink);auto op=SelectObject(dc,pen),ob=SelectObject(dc,GetStockObject(NULL_BRUSH));
    if(kind==1){
        Ellipse(dc,x-r/2,y-r/2,x+r/2+1,y+r/2+1);
        for(int i=0;i<8;++i){const double a=i*6.283185307179586/8;MoveToEx(dc,x+static_cast<int>(std::lround(std::cos(a)*r*.7)),y+static_cast<int>(std::lround(std::sin(a)*r*.7)),nullptr);LineTo(dc,x+static_cast<int>(std::lround(std::cos(a)*r)),y+static_cast<int>(std::lround(std::sin(a)*r)));}
    }
    else if(kind==2){Arc(dc,x-r,y-r,x+r,y+r,x-r,y-r,x+r,y-r);MoveToEx(dc,x,y-r,nullptr);LineTo(dc,x,y-unit(1));}
    else if(kind==3){POINT points[]{{x,y-r},{x+r,y},{x,y+r},{x-r,y}};Polygon(dc,points,4);}
    else if(kind==9){gear(dc,x,y,r,ink,scale(window,1));}
    else if(kind==10){
        Ellipse(dc,x-r,y-r,x+r+1,y+r+1);
        MoveToEx(dc,x,y-unit(1),nullptr);LineTo(dc,x,y+unit(4));
        // A device pixel disappears when the supersampled icon is reduced.
        // Draw the dot in logical units, with a visible gap above the stem.
        const int diameter=std::max(2,scale(window,1)),left=x-diameter/2,top=y-unit(4)-diameter/2;
        HBRUSH dot=CreateSolidBrush(!enabled&&!on?Border:ink);
        const auto priorBrush=SelectObject(dc,dot),priorPen=SelectObject(dc,GetStockObject(NULL_PEN));
        Ellipse(dc,left,top,left+diameter,top+diameter);
        SelectObject(dc,priorPen);SelectObject(dc,priorBrush);DeleteObject(dot);
    }
    else if(kind>=11){
        const auto line=[&](int a,int b,int c,int d){MoveToEx(dc,x+unit(a),y+unit(b),nullptr);LineTo(dc,x+unit(c),y+unit(d));};
        if(kind==11){Rectangle(dc,x-r,y-r,x+r+1,y+unit(4));line(0,4,0,8);line(-4,8,4,8);line(-5,1,5,-4);}
        else if(kind==12){line(-8,0,8,0);line(0,-8,0,8);line(-8,0,-4,-4);line(-8,0,-4,4);line(8,0,4,-4);line(8,0,4,4);line(0,-8,-4,-4);line(0,-8,4,-4);line(0,8,-4,4);line(0,8,4,4);}
        else if(kind==13){line(-7,7,7,-7);line(-7,1,-7,7);line(-7,7,-1,7);line(1,-7,7,-7);line(7,-7,7,-1);}
        else if(kind==14)line(-7,4,7,4);
        else if(kind==15)Rectangle(dc,x-r,y-r,x+r+1,y+r+1);
        else if(kind==16){line(-6,-6,6,6);line(-6,6,6,-6);}
        else if(kind==17){Ellipse(dc,x-r,y-r,x+r+1,y+r+1);line(-6,6,6,-6);}
        else if(kind==18){Rectangle(dc,x-unit(4),y-r,x+r+1,y+unit(4));Rectangle(dc,x-r,y-unit(4),x+unit(4),y+r+1);}
        else if(kind==19){POINT filter[]{{x-r,y-r},{x+r,y-r},{x+unit(2),y+unit(1)},{x+unit(2),y+r},{x-unit(2),y+r},{x-unit(2),y+unit(1)},{x-r,y-r}};Polyline(dc,filter,7);}
    }
    else {
        const auto line=[&](int a,int b,int c,int d){MoveToEx(dc,x+unit(a),y+unit(b),nullptr);LineTo(dc,x+unit(c),y+unit(d));};
        if(kind==4 || kind==5){
            POINT edge[]{{x-r,y-r},{x+unit(5),y-r},{x+r,y-unit(5)},{x+r,y+r},{x-r,y+r},{x-r,y-r}};Polyline(dc,edge,6);
            Rectangle(dc,x-unit(4),y-r,x+unit(4),y-unit(1));
            Rectangle(dc,x-unit(4),y+unit(3),x+unit(4),y+r);
            if(kind==5){RECT badge{x+unit(2),y+unit(1),x+unit(11),y+unit(11)};HBRUSH brush=CreateSolidBrush(Surface);FillRect(dc,&badge,brush);DeleteObject(brush);line(3,6,10,6);line(6,3,6,10);}
        } else if(kind==6){line(-7,-3,-7,8);line(-7,8,5,8);line(5,8,5,3);line(-3,2,8,-8);line(1,-8,8,-8);line(8,-8,8,-1);}
        else if(kind==7){
            POINT arc[25]{};for(int i=0;i<25;++i){const double angle=-.7853981633974483+i*4.71238898038469/24;arc[i]={x+static_cast<LONG>(std::lround(std::cos(angle)*r)),y+static_cast<LONG>(std::lround(std::sin(angle)*r))};}
            Polyline(dc,arc,25);const auto tip=arc[24];
            MoveToEx(dc,tip.x-unit(5),tip.y,nullptr);LineTo(dc,tip.x,tip.y);LineTo(dc,tip.x,tip.y+unit(5));
        }
        else {line(-2,-8,7,-8);line(7,-8,7,8);line(7,8,-2,8);line(-8,0,3,0);line(-8,0,-3,-5);line(-8,0,-3,5);}
    }
    SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(pen);
}
struct GlyphCache {
    struct Entry{int width,height,kind;UINT dpi;unsigned state;COLORREF base;HBITMAP bitmap;};
    std::vector<Entry> entries;
    ~GlyphCache(){for(const auto& entry:entries)DeleteObject(entry.bitmap);}
} glyphCache;
void glyph(HWND window,HDC dc,RECT rect,int kind,bool on,bool enabled,bool hover,bool focus,COLORREF base,bool menu=false){
    // A normal symbol needs no cached padding. Keep full-size images only
    // when they include a toolbar frame or hover/focus outline.
    if(!hover&&!focus&&(menu||kind<4||kind>8)){
        HBRUSH background=CreateSolidBrush(base);FillRect(dc,&rect,background);DeleteObject(background);
        const int cx=(rect.left+rect.right)/2,cy=(rect.top+rect.bottom)/2;
        const int w=std::min<int>(rect.right-rect.left,scale(window,20)),h=std::min<int>(rect.bottom-rect.top,scale(window,20));
        rect={cx-w/2,cy-h/2,cx-w/2+w,cy-h/2+h};
    }
    const int w=rect.right-rect.left,h=rect.bottom-rect.top;
    if(w<=0||h<=0)return;
    const UINT dpi=GetDpiForWindow(window);
    const unsigned state=(on?1:0)|(enabled?2:0)|(hover?4:0)|(focus?8:0)|(menu?16:0);
    HBITMAP image=nullptr;
    for(const auto& entry:glyphCache.entries)
        if(entry.width==w&&entry.height==h&&entry.kind==kind&&entry.dpi==dpi&&entry.state==state&&entry.base==base){image=entry.bitmap;break;}
    HDC memory=CreateCompatibleDC(dc);
    if(!memory){glyphRaster(window,dc,rect,kind,on,enabled,hover,focus,base,menu);glyphFrame(window,dc,rect,kind,hover,focus,base,menu);return;}
    if(!image){
        // Supersample once per size/state. Cached opaque images include the
        // exact surface colour, preserving hover/focus borders and thin strokes.
        HBITMAP high=CreateCompatibleBitmap(dc,w*4,h*4),low=CreateCompatibleBitmap(dc,w,h);
        if(high&&low){
            const auto original=SelectObject(memory,high);const int saved=SaveDC(memory);
            SetMapMode(memory,MM_ANISOTROPIC);SetWindowExtEx(memory,w,h,nullptr);SetViewportExtEx(memory,w*4,h*4,nullptr);
            glyphRaster(window,memory,{0,0,w,h},kind,on,enabled,hover,focus,base,menu);RestoreDC(memory,saved);
            HDC reduced=CreateCompatibleDC(dc);
            if(reduced){const auto old=SelectObject(reduced,low);SetStretchBltMode(reduced,HALFTONE);SetBrushOrgEx(reduced,0,0,nullptr);
                if(StretchBlt(reduced,0,0,w,h,memory,0,0,w*4,h*4,SRCCOPY)){
                    glyphFrame(window,reduced,{0,0,w,h},kind,hover,focus,base,menu);image=low;
                }
                SelectObject(reduced,old);DeleteDC(reduced);}
            SelectObject(memory,original);
        }
        if(high)DeleteObject(high);
        if(image){
            // Bound retained GDI/memory use even after many DPI/state changes.
            if(glyphCache.entries.size()==32){DeleteObject(glyphCache.entries.front().bitmap);glyphCache.entries.erase(glyphCache.entries.begin());}
            glyphCache.entries.push_back({w,h,kind,dpi,state,base,image});
        }else if(low)DeleteObject(low);
    }
    if(image){const auto old=SelectObject(memory,image);BitBlt(dc,rect.left,rect.top,w,h,memory,0,0,SRCCOPY);SelectObject(memory,old);}
    else {glyphRaster(window,dc,rect,kind,on,enabled,hover,focus,base,menu);glyphFrame(window,dc,rect,kind,hover,focus,base,menu);}
    DeleteDC(memory);
}
using Actions=std::array<CaptionAction,3>;
Actions* actions(HWND window){return reinterpret_cast<Actions*>(GetPropW(window,L"LutCaptionActions"));}
CaptionAction* action(HWND window,int hit){auto list=actions(window);return list&&hit>=100&&hit<103?&(*list)[hit-100]:nullptr;}
RECT actionRect(HWND window,int width,int i){return {width-scale(window,i==2?140:260-i*40),scale(window,3),width-scale(window,i==2?96:228-i*40),scale(window,33)};}
RECT primaryRect(HWND window,int width){const bool settings=GetPropW(window,L"LutCaptionSettings")!=nullptr,swapped=settings&&actions(window);return {width-scale(window,swapped?180:settings?140:184),scale(window,3),width-scale(window,swapped?148:96),scale(window,33)};}
std::vector<MenuItem> captionItems(HWND window){
    HMENU menu=GetSystemMenu(window,FALSE);const auto style=GetWindowLongPtrW(window,GWL_STYLE);
    const bool iconic=IsIconic(window)!=FALSE,zoomed=IsZoomed(window)!=FALSE;
    const auto enable=[&](UINT command,bool on){EnableMenuItem(menu,command,MF_BYCOMMAND|(on?MF_ENABLED:MF_GRAYED));};
    enable(SC_RESTORE,iconic||zoomed||windowNeedsSizeRepair(window));enable(SC_MOVE,!zoomed);
    enable(SC_SIZE,!iconic&&!zoomed&&(style&WS_THICKFRAME));
    enable(SC_MINIMIZE,!iconic&&(style&WS_MINIMIZEBOX));enable(SC_MAXIMIZE,!zoomed&&(style&WS_MAXIMIZEBOX));
    std::vector<MenuItem> items;bool separator=false;
    for(int i=0;i<GetMenuItemCount(menu);++i){
        wchar_t text[512]{};MENUITEMINFOW item{sizeof(item)};item.fMask=MIIM_FTYPE|MIIM_STATE|MIIM_ID|MIIM_STRING;item.dwTypeData=text;item.cch=static_cast<UINT>(std::size(text));
        if(!GetMenuItemInfoW(menu,i,TRUE,&item))continue;
        if(item.fType&MFT_SEPARATOR){separator=!items.empty();continue;}
        std::wstring label;for(size_t j=0;text[j];++j){if(text[j]==L'&'){if(text[j+1]==L'&')++j;else continue;}label+=text[j];}
        std::wstring shortcut;const auto tab=label.find(L'\t');if(tab!=std::wstring::npos){shortcut=label.substr(tab+1);label.resize(tab);}
        const bool checked=(item.fState&MFS_CHECKED)!=0;
        Glyph icon=Glyph::None;
        switch(item.wID){
        case SC_RESTORE:icon=Glyph::Restore;break;case SC_MOVE:icon=Glyph::Move;break;
        case SC_SIZE:icon=Glyph::Resize;break;case SC_MINIMIZE:icon=Glyph::Minimize;break;
        case SC_MAXIMIZE:icon=Glyph::Maximize;break;case SC_CLOSE:icon=Glyph::Close;break;
        case 0x1F00:icon=Glyph::Settings;break;case 0x1F10:icon=Glyph::Sun;break;
        case 0x1F20:icon=Glyph::Diamond;break;case 0x1F30:icon=Glyph::Power;break;
        }
        items.push_back({item.wID,label,shortcut,checked,separator,(item.fState&(MFS_DISABLED|MFS_GRAYED))==0,icon});separator=false;
    }
    return items;
}
void showCaptionMenu(HWND window,POINT anchor){
    const HWND focus=GetFocus();
    const auto items=captionItems(window);const auto icon=reinterpret_cast<HICON>(SendMessageW(window,WM_GETICON,ICON_SMALL,0));
    const auto command=popup(window,icon,items,anchor,true);
    if(IsWindow(window)&&IsWindowVisible(window)){SetForegroundWindow(window);if(IsWindow(focus)&&(focus==window||IsChild(window,focus)))SetFocus(focus);}
    if(command)PostMessageW(window,WM_SYSCOMMAND,command,0);
}
constexpr UINT_PTR GeometryTimer=0x434d4752;
constexpr UINT_PTR CaptionTipTimer=0x43546970;
HWND captionTip(HWND window){return reinterpret_cast<HWND>(GetPropW(window,L"LutCaptionTooltip"));}
void hideCaptionTip(HWND window){
    KillTimer(window,CaptionTipTimer);RemovePropW(window,L"LutCaptionTipHit");
    if(auto tip=captionTip(window)){TOOLINFOW item{sizeof(item)};item.hwnd=window;item.uId=1;SendMessageW(tip,TTM_TRACKACTIVATE,FALSE,reinterpret_cast<LPARAM>(&item));}
}
void trackCaptionTip(HWND window,int hit){
    const auto text=captionTooltip(window,hit);
    if(!text){hideCaptionTip(window);return;}
    if(reinterpret_cast<INT_PTR>(GetPropW(window,L"LutCaptionTipHit"))==hit)return;
    hideCaptionTip(window);
    auto tip=captionTip(window);
    if(!tip){
        tip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_NOPREFIX|TTS_ALWAYSTIP,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,window,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!tip)return;
        SetPropW(window,L"LutCaptionTooltip",tip);styleTooltip(tip);
        TOOLINFOW item{sizeof(item)};item.uFlags=TTF_TRACK|TTF_ABSOLUTE;item.hwnd=window;item.uId=1;item.lpszText=const_cast<wchar_t*>(text);
        SendMessageW(tip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&item));
    }
    SetPropW(window,L"LutCaptionTipHit",reinterpret_cast<HANDLE>(static_cast<INT_PTR>(hit)));
    SetTimer(window,CaptionTipTimer,GetDoubleClickTime(),nullptr);
}
void showCaptionTip(HWND window){
    KillTimer(window,CaptionTipTimer);
    const auto hit=static_cast<int>(reinterpret_cast<INT_PTR>(GetPropW(window,L"LutCaptionTipHit")));
    const auto text=captionTooltip(window,hit);auto tip=captionTip(window);
    if(!tip||!text||!IsWindowVisible(window))return;
    TOOLINFOW item{sizeof(item)};item.hwnd=window;item.uId=1;item.lpszText=const_cast<wchar_t*>(text);
    SendMessageW(tip,TTM_UPDATETIPTEXTW,0,reinterpret_cast<LPARAM>(&item));
    styleTooltip(tip);
    POINT cursor{};GetCursorPos(&cursor);SendMessageW(tip,TTM_TRACKPOSITION,0,MAKELPARAM(cursor.x+scale(window,8),cursor.y+scale(window,18)));
    SendMessageW(tip,TTM_TRACKACTIVATE,TRUE,reinterpret_cast<LPARAM>(&item));
}
void popupBorder(HWND list,HDC supplied=nullptr){
    RECT rect{};GetWindowRect(list,&rect);OffsetRect(&rect,-rect.left,-rect.top);
    HDC dc=supplied?supplied:GetWindowDC(list);if(!dc)return;
    // Use the same silhouette as the window region, rather than mixing a
    // native rectangular frame with a differently rasterized rounded stroke.
    HRGN edge=CreateRectRgn(0,0,0,0);
    if(GetWindowRgn(list,edge)==ERROR){DeleteObject(edge);const int diameter=scale(list,style::CornerRadius*2);edge=CreateRoundRectRgn(0,0,rect.right+1,rect.bottom+1,diameter,diameter);}
    if(edge){HBRUSH ink=CreateSolidBrush(FocusBorder);FrameRgn(dc,edge,ink,1,1);DeleteObject(ink);DeleteObject(edge);}
    if(!supplied)ReleaseDC(list,dc);
}
LRESULT CALLBACK popupProc(HWND list,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(msg==WM_NCDESTROY){RemoveWindowSubclass(list,popupProc,4);return DefSubclassProc(list,msg,wp,lp);}
    if(msg==WM_NCPAINT){const auto result=DefSubclassProc(list,msg,wp,lp);popupBorder(list);return result;}
    const bool trackSelection=msg==WM_MOUSEMOVE||msg==WM_KEYDOWN||msg==WM_LBUTTONDOWN||msg==WM_LBUTTONUP||msg==WM_VSCROLL;
    const auto selection=trackSelection?DefSubclassProc(list,LB_GETCURSEL,0,0):LB_ERR;
    int saved=0;
    if(msg==WM_PRINT){HDC dc=reinterpret_cast<HDC>(wp);saved=SaveDC(dc);HRGN shape=CreateRectRgn(0,0,0,0);
        if(GetWindowRgn(list,shape)!=ERROR){POINT origin{};GetViewportOrgEx(dc,&origin);OffsetRgn(shape,origin.x,origin.y);ExtSelectClipRgn(dc,shape,RGN_AND);}DeleteObject(shape);}
    const auto result=DefSubclassProc(list,msg,wp,lp);
    if(msg==WM_WINDOWPOSCHANGED||msg==WM_SHOWWINDOW||msg==WM_DPICHANGED)roundBounds(list);
    // Native item drawing reaches into the rounded corners. Restore the
    // complete border after painting or changing the selected row.
    if(msg==WM_PAINT||msg==WM_NCACTIVATE||msg==LB_SETCURSEL||(trackSelection&&selection!=DefSubclassProc(list,LB_GETCURSEL,0,0)))popupBorder(list);
    if(msg==WM_PRINT&&(lp&PRF_NONCLIENT))popupBorder(list,reinterpret_cast<HDC>(wp));
    if(saved)RestoreDC(reinterpret_cast<HDC>(wp),saved);
    return result;
}
// Keep native edit behavior (selection, caret and accessibility) and draw only
// its frame. Removing the native square frame avoids a second border.
bool singleLineEdit(HWND window){wchar_t cls[32]{};GetClassNameW(window,cls,32);return wcscmp(cls,L"Edit")==0&&!(GetWindowLongPtrW(window,GWL_STYLE)&ES_MULTILINE);}
RECT inputBounds(HWND window){RECT rect{};GetWindowRect(window,&rect);return {0,0,rect.right-rect.left,rect.bottom-rect.top};}
POINT inputOrigin(HWND window){RECT outer{};GetWindowRect(window,&outer);POINT origin{};ClientToScreen(window,&origin);return {origin.x-outer.left,origin.y-outer.top};}
void paintInputFrame(HWND window){
    HDC dc=GetWindowDC(window);if(!dc)return;const auto bounds=inputBounds(window);const auto origin=inputOrigin(window);
    RECT client{};GetClientRect(window,&client);const int saved=SaveDC(dc);ExcludeClipRect(dc,origin.x,origin.y,origin.x+client.right,origin.y+client.bottom);
    FillRect(dc,&bounds,surface.value);RestoreDC(dc,saved);outline(window,dc,bounds);ReleaseDC(window,dc);
}
LRESULT CALLBACK inputProc(HWND window,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(msg==WM_NCDESTROY){RemovePropW(window,L"LutThemeHover");RemoveWindowSubclass(window,inputProc,5);return DefSubclassProc(window,msg,wp,lp);}
    const bool centered=singleLineEdit(window);
    bool hoverChanged=false;
    if(centered&&msg==WM_NCHITTEST)return HTCLIENT;
    if(centered&&msg==WM_NCCALCSIZE){
        const auto result=DefSubclassProc(window,msg,wp,lp);auto rect=wp?&reinterpret_cast<NCCALCSIZE_PARAMS*>(lp)->rgrc[0]:reinterpret_cast<RECT*>(lp);
        HDC dc=GetDC(window);if(!dc)return result;auto old=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(window,WM_GETFONT,0,0)));
        TEXTMETRICW metrics{};GetTextMetricsW(dc,&metrics);SelectObject(dc,old);ReleaseDC(window,dc);
        const int spare=std::max(0,static_cast<int>(rect->bottom-rect->top-metrics.tmHeight));
        rect->top+=spare/2;rect->bottom-=spare-spare/2;return result;
    }
    if(centered&&msg==WM_NCPAINT){paintInputFrame(window);return 0;}
    if(centered&&msg==WM_PRINT){
        const auto bounds=inputBounds(window);HDC dc=reinterpret_cast<HDC>(wp);const int saved=SaveDC(dc);
        IntersectClipRect(dc,0,0,bounds.right,bounds.bottom);FillRect(dc,&bounds,surface.value);
        const auto origin=inputOrigin(window);RECT client{};GetClientRect(window,&client);const int content=SaveDC(dc);
        IntersectClipRect(dc,origin.x,origin.y,origin.x+client.right,origin.y+client.bottom);OffsetViewportOrgEx(dc,origin.x,origin.y,nullptr);
        SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(window,WM_GETFONT,0,0)));
        DefSubclassProc(window,WM_PRINTCLIENT,wp,PRF_CLIENT);RestoreDC(dc,content);outline(window,dc,bounds);RestoreDC(dc,saved);return 0;
    }
    if(msg==WM_MOUSEMOVE&&!GetPropW(window,L"LutThemeHover")){
        hoverChanged=true;
        SetPropW(window,L"LutThemeHover",reinterpret_cast<HANDLE>(1));TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0};TrackMouseEvent(&track);InvalidateRect(window,nullptr,FALSE);
    }
    if(msg==WM_MOUSELEAVE){hoverChanged=true;RemovePropW(window,L"LutThemeHover");InvalidateRect(window,nullptr,FALSE);}
    const auto result=DefSubclassProc(window,msg,wp,lp);
    if(centered&&msg==WM_SETFONT)SetWindowPos(window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    if(msg==WM_SIZE||msg==WM_DPICHANGED)roundBounds(window);
    if(msg==WM_SETFOCUS||msg==WM_KILLFOCUS||msg==WM_ENABLE)InvalidateRect(window,nullptr,FALSE);
    if(centered){if(msg==WM_PAINT||msg==WM_SETFOCUS||msg==WM_KILLFOCUS||msg==WM_ENABLE||hoverChanged)paintInputFrame(window);return result;}
    if(msg==WM_PAINT||msg==WM_PRINT||msg==WM_PRINTCLIENT){
        HDC dc=msg==WM_PAINT?GetDC(window):reinterpret_cast<HDC>(wp);RECT rect{};GetClientRect(window,&rect);
        if(dc)outline(window,dc,rect);if(msg==WM_PAINT&&dc)ReleaseDC(window,dc);
    }
    return result;
}
int captionHit(HWND window, POINT point) {
    RECT rect{}; GetWindowRect(window, &rect); point.x -= rect.left; point.y -= rect.top;
    if (point.y < 0 || point.y >= scale(window,36)) return HTCLIENT;
    if (point.x >= rect.right-rect.left-scale(window,44)) return HTCLOSE;
    if ((GetWindowLongPtrW(window,GWL_STYLE)&WS_MINIMIZEBOX) && point.x >= rect.right-rect.left-scale(window,88)) return HTMINBUTTON;
    if(actions(window))for(int i=0;i<3;++i){const auto button=actionRect(window,rect.right-rect.left,i);if(PtInRect(&button,point))return 100+i;}
    if(GetPropW(window,L"LutPowerCommand")){const auto button=primaryRect(window,rect.right-rect.left);if(PtInRect(&button,point))return HTHELP;}
    if (point.x < scale(window,34)) return HTSYSMENU;
    return HTCAPTION;
}
void paintCaption(HWND window, HDC dc) {
    RECT outer{}; GetWindowRect(window,&outer); RECT cap{0,0,outer.right-outer.left,scale(window,36)};
    FillRect(dc,&cap,bg.value); SetBkMode(dc,TRANSPARENT);
    auto font = reinterpret_cast<HFONT>(GetPropW(window,L"LutCaptionFont"));
    const auto oldFont = SelectObject(dc,font ? font : GetStockObject(DEFAULT_GUI_FONT));
    SetTextColor(dc,GetForegroundWindow()==window ? Text : Muted);
    const auto icon = reinterpret_cast<HICON>(SendMessageW(window,WM_GETICON,ICON_SMALL,0));
    if (icon) DrawIconEx(dc,scale(window,13),scale(window,10),icon,scale(window,16),scale(window,16),0,nullptr,DI_NORMAL);
    wchar_t text[1024]{}; GetWindowTextW(window,text,static_cast<int>(std::size(text)));
    RECT label{scale(window,38),0,cap.right-scale(window,(GetWindowLongPtrW(window,GWL_STYLE)&WS_MINIMIZEBOX)?98:54),cap.bottom};
    if(GetPropW(window,L"LutPowerCommand"))label.right=cap.right-scale(window,GetPropW(window,L"LutCaptionSettings")?150:194);
    if(actions(window))label.right=cap.right-scale(window,270);
    DrawTextW(dc,text,-1,&label,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    const auto hover = reinterpret_cast<INT_PTR>(GetPropW(window,L"LutCaptionHover"));
    if(auto list=actions(window))for(int i=0;i<3;++i){const auto& entry=(*list)[i];glyph(window,dc,actionRect(window,cap.right,i),i==0?1:i==1?3:2,entry.on,entry.enabled,hover==100+i,false,Background);}
    if(GetPropW(window,L"LutPowerCommand")) {
        const auto state=reinterpret_cast<INT_PTR>(GetPropW(window,L"LutPowerState"))-1;
        const bool on=(state&1)!=0,enabled=(state&2)!=0;
        const bool settings=GetPropW(window,L"LutCaptionSettings")!=nullptr;
        RECT button=primaryRect(window,cap.right);
        const COLORREF ink=!enabled?Border:on?Accent:Text;
        const bool pressed=GetPropW(window,L"LutCaptionPressed")==reinterpret_cast<HANDLE>(HTHELP);
        if(settings){glyph(window,dc,button,static_cast<int>(Glyph::Settings),on,enabled,hover==HTHELP||pressed,false,Background);}
        else {
        rounded(dc,button,enabled&&pressed?RGB(57,80,94):enabled&&hover==HTHELP?RGB(66,71,79):Surface,on?Accent:Border,scale(window,6));
        const int cx=settings?(button.left+button.right)/2:button.left+scale(window,20),cy=(button.top+button.bottom)/2,r=scale(window,9);
        HPEN pen=CreatePen(PS_SOLID,scale(window,2),ink);const auto previous=SelectObject(dc,pen);
        Arc(dc,cx-r,cy-r,cx+r,cy+r,cx-r,cy-r,cx+r,cy-r);
        MoveToEx(dc,cx,cy-scale(window,11),nullptr);LineTo(dc,cx,cy-scale(window,1));
        SelectObject(dc,previous);DeleteObject(pen);
        RECT powerLabel=button;powerLabel.left+=scale(window,37);SetTextColor(dc,ink);
        DrawTextW(dc,on?L"ON":L"OFF",-1,&powerLabel,DT_SINGLELINE|DT_VCENTER|DT_CENTER|DT_NOPREFIX);
        }
    }
    for (int hit : {HTMINBUTTON,HTCLOSE}) {
        if (hit==HTMINBUTTON && !(GetWindowLongPtrW(window,GWL_STYLE)&WS_MINIMIZEBOX)) continue;
        RECT button{cap.right-scale(window,hit==HTCLOSE?44:88),scale(window,4),cap.right-scale(window,hit==HTCLOSE?5:49),cap.bottom-scale(window,4)};
        if (hover==hit) rounded(dc,button,hit==HTCLOSE?CloseRed:Surface,hit==HTCLOSE?CloseRed:Surface,scale(window,4));
        const int cx=(button.left+button.right)/2, cy=(button.top+button.bottom)/2, arm=scale(window,5);
        HPEN pen=CreatePen(PS_SOLID,scale(window,1),hit==HTCLOSE&&hover!=HTCLOSE?CloseRed:Text);const auto oldPen=SelectObject(dc,pen);
        if (hit==HTCLOSE) { MoveToEx(dc,cx-arm,cy-arm,nullptr);LineTo(dc,cx+arm,cy+arm);MoveToEx(dc,cx+arm,cy-arm,nullptr);LineTo(dc,cx-arm,cy+arm); }
        else { MoveToEx(dc,cx-arm,cy+scale(window,3),nullptr);LineTo(dc,cx+arm,cy+scale(window,3)); }
        SelectObject(dc,oldPen);DeleteObject(pen);
    }
    HPEN pen=CreatePen(PS_SOLID,1,RGB(60,64,71));const auto oldPen=SelectObject(dc,pen);const auto oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
    RoundRect(dc,0,0,cap.right,outer.bottom-outer.top,scale(window,16),scale(window,16));
    SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);SelectObject(dc,oldFont);
}
void repaintCaption(HWND window) {
    HDC dc=GetWindowDC(window);if(!dc)return;
    RECT outer{};GetWindowRect(window,&outer);const int width=outer.right-outer.left,height=scale(window,36);
    HDC buffer=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,width,height);
    if(buffer&&bitmap){const auto old=SelectObject(buffer,bitmap);paintCaption(window,buffer);BitBlt(dc,0,0,width,height,buffer,0,0,SRCCOPY);SelectObject(buffer,old);}
    else paintCaption(window,dc);
    if(bitmap)DeleteObject(bitmap);if(buffer)DeleteDC(buffer);
    // Preserve the frame outside the short caption buffer without clearing it.
    HPEN pen=CreatePen(PS_SOLID,1,RGB(60,64,71));auto oldPen=SelectObject(dc,pen),oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
    RoundRect(dc,0,0,width,outer.bottom-outer.top,scale(window,16),scale(window,16));
    SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);ReleaseDC(window,dc);
}
void printWindow(HWND window,HDC dc,LPARAM flags) {
    RECT outer{};GetWindowRect(window,&outer);RECT whole{0,0,outer.right-outer.left,outer.bottom-outer.top};FillRect(dc,&whole,bg.value);
    if(flags&PRF_NONCLIENT) paintCaption(window,dc);
    POINT origin{};ClientToScreen(window,&origin);origin.x-=outer.left;origin.y-=outer.top;
    const int saved=SaveDC(dc);OffsetViewportOrgEx(dc,origin.x,origin.y,nullptr);
    // Scrolled children can have negative coordinates. Keep their print path
    // inside the client area, matching native window clipping at runtime.
    RECT clientClip{};GetClientRect(window,&clientClip);IntersectClipRect(dc,clientClip.left,clientClip.top,clientClip.right,clientClip.bottom);
    if(flags&PRF_CLIENT) { RECT client{};GetClientRect(window,&client);FillRect(dc,&client,bg.value);SendMessageW(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT); }
    if(flags&PRF_CHILDREN) for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
        if(!(GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE)) continue;
        RECT rect{};GetWindowRect(child,&rect);POINT at{rect.left,rect.top};ScreenToClient(window,&at);
        const int childSaved=SaveDC(dc);OffsetViewportOrgEx(dc,at.x,at.y,nullptr);
        IntersectClipRect(dc,0,0,rect.right-rect.left,rect.bottom-rect.top);
        HRGN shape=CreateRectRgn(0,0,0,0);
        if(GetWindowRgn(child,shape)!=ERROR){POINT viewport{};GetViewportOrgEx(dc,&viewport);OffsetRgn(shape,viewport.x,viewport.y);ExtSelectClipRgn(dc,shape,RGN_AND);}
        DeleteObject(shape);
        SendMessageW(child,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_NONCLIENT|PRF_CLIENT|PRF_CHILDREN|PRF_ERASEBKGND);RestoreDC(dc,childSaved);
    }
    RestoreDC(dc,saved);
}
void paintControl(HWND window, HDC dc) {
    RECT rect{}; GetClientRect(window, &rect);
    // WM_PRINTCLIENT can supply a shared parent DC. Keep borders and focus
    // outlines inside this control just as WM_PAINT's window DC does.
    struct Clip {HDC dc;int saved;Clip(HDC value,const RECT& r):dc(value),saved(SaveDC(value)){if(saved)IntersectClipRect(dc,r.left,r.top,r.right,r.bottom);}~Clip(){if(saved)RestoreDC(dc,saved);}} clip(dc,rect);
    FillRect(dc, &rect, bg.value);
    auto font = reinterpret_cast<HFONT>(SendMessageW(window, WM_GETFONT, 0, 0));
    const auto oldFont = SelectObject(dc, font); SetBkMode(dc, TRANSPARENT);
    const bool enabled = IsWindowEnabled(window) != FALSE, focused = GetFocus() == window;
    const bool hover = GetPropW(window, L"LutThemeHover") != nullptr;
    SetTextColor(dc, enabled ? Text : Muted);
    if(GetPropW(window,L"LutToggle")) {
        const bool on=SendMessageW(window,BM_GETCHECK,0,0)==BST_CHECKED;
        const bool powered=GetPropW(window,L"LutToggleUnpowered")==nullptr;
        paintToggle(window,dc,rect,on,powered,hover||focused);
        if(focused&&!(SendMessageW(window,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS)){RECT focus=rect;InflateRect(&focus,-1,-1);DrawFocusRect(dc,&focus);}
        SelectObject(dc,oldFont);return;
    }
    if(auto kind=reinterpret_cast<INT_PTR>(GetPropW(window,L"LutMonitorGlyph"))) {
        const bool captioned=kind>=4&&kind<=8;RECT icon=rect;
        if(captioned)icon.bottom=std::min(rect.bottom,rect.top+scale(window,28));
        glyph(window,dc,icon,static_cast<int>(kind),GetPropW(window,L"LutMonitorOn")!=nullptr,enabled,hover,focused,captioned?Background:RGB(7,9,12));
        if(captioned && rect.bottom>icon.bottom){
            const wchar_t* captions[]{L"Save",L"Save as",L"Export",L"Reset",L"Revert"};
            LOGFONTW labelStyle{};GetObjectW(font,sizeof(labelStyle),&labelStyle);labelStyle.lfHeight=-scale(window,style::CaptionTextSize);
            HFONT labelFont=CreateFontIndirectW(&labelStyle);SelectObject(dc,labelFont?labelFont:font);
            RECT label=rect;label.top=icon.bottom;SetTextColor(dc,enabled?Muted:Border);
            DrawTextW(dc,captions[kind-4],-1,&label,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
            SelectObject(dc,font);if(labelFont)DeleteObject(labelFont);
        }
        SelectObject(dc,oldFont);return;
    }
    wchar_t cls[40]{}; GetClassNameW(window, cls, static_cast<int>(std::size(cls)));
    if (wcscmp(cls, HOTKEY_CLASSW) == 0) {
        rounded(dc,rect,Surface,(focused||hover)?FocusBorder:Border,scale(window,4));
        const WORD shortcut=static_cast<WORD>(SendMessageW(window,HKM_GETHOTKEY,0,0));std::wstring text;
        if(HIBYTE(shortcut)&HOTKEYF_CONTROL) text+=L"Ctrl+";
        if(HIBYTE(shortcut)&HOTKEYF_ALT) text+=L"Alt+";
        if(HIBYTE(shortcut)&HOTKEYF_SHIFT) text+=L"Shift+";
        if(LOBYTE(shortcut)) { wchar_t key[128]{};LONG scan=static_cast<LONG>(MapVirtualKeyW(LOBYTE(shortcut),MAPVK_VK_TO_VSC)<<16);if(HIBYTE(shortcut)&HOTKEYF_EXT)scan|=1<<24;GetKeyNameTextW(scan,key,128);text+=key; }
        else text=L"Press shortcut";
        RECT label=rect;label.left+=scale(window,8);DrawTextW(dc,text.c_str(),-1,&label,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    } else if (wcscmp(cls, L"ComboBox") == 0) {
        rounded(dc, rect, enabled ? Surface : Background, (focused||hover) ? FocusBorder : Border, scale(window, 4));
        wchar_t text[1024]{}; GetWindowTextW(window, text, static_cast<int>(std::size(text)));
        RECT label{rect.left+scale(window, 9), rect.top, rect.right-scale(window, 25), rect.bottom};
        DrawTextW(dc, text, -1, &label, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        const int x = rect.right-scale(window, 13), y = (rect.top+rect.bottom)/2;
        HPEN pen = CreatePen(PS_SOLID, scale(window, 1), enabled ? Text : Muted); const auto oldPen = SelectObject(dc, pen);
        MoveToEx(dc, x-scale(window, 4), y-scale(window, 2), nullptr); LineTo(dc, x, y+scale(window, 2)); LineTo(dc, x+scale(window, 4), y-scale(window, 2));
        SelectObject(dc, oldPen); DeleteObject(pen);
    } else {
        const auto style = GetWindowLongPtrW(window, GWL_STYLE) & BS_TYPEMASK;
        const bool radio = style == BS_RADIOBUTTON || style == BS_AUTORADIOBUTTON;
        const bool check = radio || style == BS_CHECKBOX || style == BS_AUTOCHECKBOX;
        wchar_t text[1024]{}; GetWindowTextW(window, text, static_cast<int>(std::size(text)));
        RECT label = rect;
        if (check) {
            const int size = scale(window, 14), top = (rect.bottom-size)/2;
            RECT box{rect.left+1, top, rect.left+1+size, top+size};
            const bool checked = SendMessageW(window, BM_GETCHECK, 0, 0) == BST_CHECKED;
            const COLORREF ink = enabled ? Accent : Muted;
            if (radio) {
                HBRUSH brush = CreateSolidBrush(Surface); HPEN pen = CreatePen(PS_SOLID, 1, (focused||hover) ? FocusBorder : Border);
                auto oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, pen);
                Ellipse(dc, box.left, box.top, box.right, box.bottom);
                SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(brush); DeleteObject(pen);
                if (checked) { InflateRect(&box, -scale(window, 4), -scale(window, 4)); brush = CreateSolidBrush(ink); oldBrush = SelectObject(dc, brush); oldPen = SelectObject(dc, GetStockObject(NULL_PEN)); Ellipse(dc, box.left, box.top, box.right, box.bottom); SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(brush); }
            } else {
                rounded(dc, box, checked ? ink : Surface, (focused||hover) ? FocusBorder : Border, scale(window, 2));
                if (checked) {
                    HPEN pen = CreatePen(PS_SOLID, scale(window, 2), Background); const auto oldPen = SelectObject(dc, pen);
                    MoveToEx(dc, box.left+scale(window, 3), box.top+scale(window, 7), nullptr);
                    LineTo(dc, box.left+scale(window, 6), box.top+scale(window, 10)); LineTo(dc, box.left+scale(window, 11), box.top+scale(window, 4));
                    SelectObject(dc, oldPen); DeleteObject(pen);
                }
            }
            label.left += size+scale(window, 7);
            DrawTextW(dc, text, -1, &label, DT_SINGLELINE | DT_VCENTER);
        } else {
            const auto fill = enabled ? Surface : Background;
            rounded(dc, rect, fill, (focused||hover) ? FocusBorder : Border, scale(window, 4));
            DrawTextW(dc, text, -1, &label, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        }
        if (focused && !(SendMessageW(window, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
            RECT focus = label; InflateRect(&focus, -scale(window, 4), -scale(window, 4)); DrawFocusRect(dc, &focus);
        }
    }
    SelectObject(dc, oldFont);
}
LRESULT CALLBACK sliderPaintProc(HWND window,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_PAINT){
        PAINTSTRUCT paint{};HDC target=BeginPaint(window,&paint);RECT bounds{};GetClientRect(window,&bounds);
        // Native custom draw clears the track before drawing its thumb.
        // Present the completed slider together, with only a transient dirty buffer.
        paint::Buffer frame(target,bounds);
        if(frame&&frame.buffered())DefSubclassProc(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(frame.dc()),PRF_CLIENT);
        frame.finish();EndPaint(window,&paint);return 0;
    }
    if(msg==WM_NCDESTROY)RemoveWindowSubclass(window,sliderPaintProc,82);
    return DefSubclassProc(window,msg,wp,lp);
}
LRESULT CALLBACK proc(HWND window, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    switch (msg) {
    case WM_SIZE: {
        wchar_t cls[40]{};GetClassNameW(window,cls,40);if(wcscmp(cls,HOTKEY_CLASSW)==0)roundBounds(window);break;
    }
    case WM_SETCURSOR:
        if(LOWORD(lp)==HTCLIENT&&IsWindowEnabled(window)){
            wchar_t cls[40]{};GetClassNameW(window,cls,40);
            if(wcscmp(cls,L"Button")==0||wcscmp(cls,L"ComboBox")==0){SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;}
        }break;
    case WM_PAINT: {
        PAINTSTRUCT paint{}; auto dc = BeginPaint(window, &paint);
        RECT rect{}; GetClientRect(window, &rect);
        {paint::Buffer buffer(dc,rect);if(buffer)paintControl(window,buffer.dc());}
        EndPaint(window, &paint); return 0;
    }
    case WM_PRINT: case WM_PRINTCLIENT: paintControl(window, reinterpret_cast<HDC>(wp)); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE:
        if (!GetPropW(window, L"LutThemeHover")) { SetPropW(window, L"LutThemeHover", reinterpret_cast<HANDLE>(1)); TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0}; TrackMouseEvent(&tracking); InvalidateRect(window, nullptr, FALSE); }
        break;
    case WM_MOUSELEAVE: RemovePropW(window, L"LutThemeHover"); InvalidateRect(window, nullptr, FALSE); break;
    case WM_NCDESTROY: RemovePropW(window, L"LutThemeHover");RemovePropW(window,L"LutMonitorGlyph");RemovePropW(window,L"LutMonitorOn");RemovePropW(window,L"LutToggle");RemovePropW(window,L"LutToggleUnpowered"); RemoveWindowSubclass(window, proc, 1); break;
    }
    const auto result = DefSubclassProc(window, msg, wp, lp);
    if(msg==WM_SETFONT){wchar_t cls[32]{};GetClassNameW(window,cls,32);if(wcscmp(cls,L"ComboBox")==0){SendMessageW(window,CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),scale(window,18));SendMessageW(window,CB_SETITEMHEIGHT,0,scale(window,18));}}
    if (msg == BM_SETCHECK || msg == BM_SETSTATE || msg == CB_SETCURSEL || msg == HKM_SETHOTKEY || msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_CHAR || msg == WM_ENABLE || msg == WM_SETTEXT || msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == WM_UPDATEUISTATE)
        InvalidateRect(window, nullptr, FALSE);
    return result;
}
}
namespace {
struct HelpItem {HWND control;UINT_PTR id;bool whole;std::wstring text;};
struct HelpWindow {HWND tip=nullptr;std::wstring family;UINT dpi=0;std::vector<std::unique_ptr<HelpItem>> items;};
void helpFont(HWND owner,HelpWindow& state){
    const UINT dpi=GetDpiForWindow(owner);if(state.family==fontFamily()&&state.dpi==dpi)return;
    styleTooltip(state.tip);state.family=fontFamily();state.dpi=dpi;
    SendMessageW(state.tip,TTM_SETMAXTIPWIDTH,0,scale(owner,360));
}
LRESULT CALLBACK helpOwner(HWND window,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data) {
    if(msg==WM_NCDESTROY){auto state=reinterpret_cast<HelpWindow*>(data);RemovePropW(window,L"MonitorMageHelp");
        if(IsWindow(state->tip))DestroyWindow(state->tip);RemoveWindowSubclass(window,helpOwner,9);delete state;}
    return DefSubclassProc(window,msg,wp,lp);
}
void setHelp(HWND control,UINT_PTR id,const RECT* region,const wchar_t* text) {
    if(!control)return;const HWND owner=GetAncestor(control,GA_ROOT);
    auto state=reinterpret_cast<HelpWindow*>(GetPropW(owner,L"MonitorMageHelp"));
    if(!state){auto created=std::make_unique<HelpWindow>();
        created->tip=CreateWindowExW(WS_EX_TOPMOST,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_ALWAYSTIP|TTS_NOPREFIX,0,0,0,0,owner,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!created->tip)return;
        if(!SetWindowSubclass(owner,helpOwner,9,reinterpret_cast<DWORD_PTR>(created.get()))){DestroyWindow(created->tip);return;}
        state=created.release();SetPropW(owner,L"MonitorMageHelp",state);styleTooltip(state->tip);
        SendMessageW(state->tip,TTM_SETMAXTIPWIDTH,0,scale(owner,340));SendMessageW(state->tip,TTM_SETDELAYTIME,TTDT_AUTOPOP,15000);
    }
    helpFont(owner,*state);
    HelpItem* item=nullptr;for(const auto& candidate:state->items)if(candidate->control==control&&candidate->id==id&&candidate->whole==!region){item=candidate.get();break;}
    TOOLINFOW tool{sizeof(tool)};tool.hwnd=region?control:GetParent(control);tool.uId=region?id:reinterpret_cast<UINT_PTR>(control);
    tool.uFlags=TTF_SUBCLASS|(region?0:TTF_IDISHWND);if(region)tool.rect=*region;
    if(!item){auto created=std::make_unique<HelpItem>(HelpItem{control,id,!region,text});item=created.get();state->items.push_back(std::move(created));
        tool.lpszText=item->text.data();if(!SendMessageW(state->tip,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool))){state->items.pop_back();return;}}
    else if(item->text!=text){item->text=text;tool.lpszText=item->text.data();SendMessageW(state->tip,TTM_UPDATETIPTEXTW,0,reinterpret_cast<LPARAM>(&tool));}
    if(region)SendMessageW(state->tip,TTM_NEWTOOLRECTW,0,reinterpret_cast<LPARAM>(&tool));
}
}
void help(HWND control,const wchar_t* text){setHelp(control,0,nullptr,text);}
void helpRegion(HWND control,UINT_PTR id,const RECT& rect,const wchar_t* text){setHelp(control,id,&rect,text);}
const wchar_t* helpText(HWND control,UINT_PTR region){auto state=reinterpret_cast<HelpWindow*>(GetPropW(GetAncestor(control,GA_ROOT),L"MonitorMageHelp"));
    if(state)for(const auto& item:state->items)if(item->whole==!region&&item->id==region&&item->control==control)return item->text.c_str();return L"";}
void fixedWindowSize(HWND window,int width,int height){
    auto size=reinterpret_cast<SIZE*>(GetPropW(window,L"MonitorMagePreferredSize"));
    if(!size){size=new SIZE{width,height};if(!SetPropW(window,L"MonitorMagePreferredSize",size))delete size;}
    else *size={width,height};
}
namespace {
bool repairedBounds(HWND window,RECT& current,RECT& target){
    const auto size=reinterpret_cast<SIZE*>(GetPropW(window,L"MonitorMagePreferredSize"));
    if(!size||IsIconic(window)||IsZoomed(window)||!GetWindowRect(window,&current))return false;
    const UINT dpi=GetDpiForWindow(window);RECT preferred{0,0,MulDiv(size->cx,dpi,96),MulDiv(size->cy,dpi,96)};adjustWindowRect(preferred,dpi);
    MONITORINFO monitor{sizeof(monitor)};if(!GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor))return false;
    target=fitWindowRect(current,{preferred.right-preferred.left,preferred.bottom-preferred.top},monitor.rcWork);return true;
}
}
bool windowNeedsSizeRepair(HWND window){RECT current{},target{};return repairedBounds(window,current,target)&&!EqualRect(&current,&target);}
void repairWindowSize(HWND window){RECT current{},target{};
    if(repairedBounds(window,current,target)&&!EqualRect(&current,&target))SetWindowPos(window,nullptr,target.left,target.top,target.right-target.left,target.bottom-target.top,SWP_NOZORDER|SWP_NOACTIVATE);
    frame(window);
}
HBRUSH background() { return bg.value; }
const wchar_t* hdrTooltip(bool on,bool available){return !available?L"Windows HDR unavailable":on?L"Turn Windows HDR off":L"Turn Windows HDR on";}
const wchar_t* captionTooltip(HWND window,int hit){
    if(hit==100&&actions(window))return hdrTooltip((*actions(window))[0].on,(*actions(window))[0].enabled);
    if(hit==101&&actions(window))return wording::ApplicationProfiles;
    if(hit==102&&actions(window))return (*actions(window))[2].on?L"Turn off (Alt+P)":L"Turn on (Alt+P)";
    if(hit==HTHELP&&GetPropW(window,L"LutPowerCommand"))return GetPropW(window,L"LutCaptionSettings")?wording::Settings:L"Master power on/off";
    if(hit==HTMINBUTTON&&(GetWindowLongPtrW(window,GWL_STYLE)&WS_MINIMIZEBOX))return L"Minimize";
    if(hit==HTCLOSE)return L"Close";
    if(hit==HTSYSMENU)return L"Window menu";
    return nullptr;
}
bool previewCaptionMenu(HWND window,std::function<bool(HWND)> render){
    return previewPopup(window,reinterpret_cast<HICON>(SendMessageW(window,WM_GETICON,ICON_SMALL,0)),captionItems(window),std::move(render),true);
}
void setFontFamily(const wchar_t* value) {family=value;}
const wchar_t* fontFamily() {return family.c_str();}
void refreshCaptionFont(HWND window) {if(auto help=reinterpret_cast<HelpWindow*>(GetPropW(window,L"MonitorMageHelp")))helpFont(window,*help);hideCaptionTip(window);auto font=reinterpret_cast<HFONT>(RemovePropW(window,L"LutCaptionFont"));if(font)DeleteObject(font);SetPropW(window,L"LutCaptionFont",makeFont(GetDpiForWindow(window)));repaintCaption(window);}
void captionPower(HWND window,int command,bool on,bool enabled) {
    const auto state=static_cast<INT_PTR>(1+(on?1:0)+(enabled?2:0));
    if(GetPropW(window,L"LutPowerCommand")==reinterpret_cast<HANDLE>(static_cast<INT_PTR>(command)) && GetPropW(window,L"LutPowerState")==reinterpret_cast<HANDLE>(state))return;
    const bool first=!GetPropW(window,L"LutPowerCommand");
    SetPropW(window,L"LutPowerCommand",reinterpret_cast<HANDLE>(static_cast<INT_PTR>(command)));
    SetPropW(window,L"LutPowerState",reinterpret_cast<HANDLE>(state));
    // The standard system menu exposes the custom caption action to keyboard
    // users and accessibility clients as well as the visible pointer target.
    if(first){HMENU menu=GetSystemMenu(window,FALSE);AppendMenuW(menu,MF_SEPARATOR,0,nullptr);const auto label=GetPropW(window,L"LutCaptionSettings")?std::wstring(wording::Settings)+L"\tAlt+S":L"Power on/off\tAlt+P";AppendMenuW(menu,MF_STRING,0x1F00,label.c_str());}
    EnableMenuItem(GetSystemMenu(window,FALSE),0x1F00,MF_BYCOMMAND|(enabled?MF_ENABLED:MF_GRAYED));
    repaintCaption(window);
}
void captionSettings(HWND window,int command,bool selected){SetPropW(window,L"LutCaptionSettings",reinterpret_cast<HANDLE>(1));captionPower(window,command,selected,true);}
void iconButton(HWND window,Glyph glyph,bool on,bool enabled) {
    const auto value=reinterpret_cast<HANDLE>(static_cast<INT_PTR>(glyph));
    const bool changed=GetPropW(window,L"LutMonitorGlyph")!=value || (GetPropW(window,L"LutMonitorOn")!=nullptr)!=on;
    SetPropW(window,L"LutMonitorGlyph",value);if(on)SetPropW(window,L"LutMonitorOn",reinterpret_cast<HANDLE>(1));else RemovePropW(window,L"LutMonitorOn");
    if((IsWindowEnabled(window)!=FALSE)!=enabled)EnableWindow(window,enabled);
    if(changed)InvalidateRect(window,nullptr,FALSE);
}
void paintMenuGlyph(HWND window,HDC dc,RECT rect,Glyph icon,bool on,bool enabled,COLORREF base) {
    glyph(window,dc,rect,static_cast<int>(icon),on,enabled,false,false,base,true);
}
void sliderDetails(HWND window,int defaultValue,wchar_t thumbLetter) {
    DWORD_PTR clicks=0;if(!GetWindowSubclass(window,sliderResetProc,11,&clicks)){
        auto state=std::make_unique<SliderClicks>();if(SetWindowSubclass(window,sliderResetProc,11,reinterpret_cast<DWORD_PTR>(state.get())))state.release();
    }
    const auto marker=reinterpret_cast<HANDLE>(static_cast<INT_PTR>(defaultValue)+32769);
    const auto letter=reinterpret_cast<HANDLE>(static_cast<INT_PTR>(thumbLetter));
    if(GetPropW(window,L"LutSliderDefault")==marker&&GetPropW(window,L"LutSliderLetter")==letter)return;
    SetPropW(window,L"LutSliderDefault",marker);
    if(letter)SetPropW(window,L"LutSliderLetter",letter);else RemovePropW(window,L"LutSliderLetter");
    InvalidateRect(window,nullptr,FALSE);
}
void captionActions(HWND window,const std::array<CaptionAction,3>& value) {
    auto list=actions(window);bool changed=!list;
    if(!list){list=new Actions;SetPropW(window,L"LutCaptionActions",list);const wchar_t* names[]{L"Windows HDR",wording::ApplicationProfiles,L"Master power"};for(int i=0;i<3;++i)AppendMenuW(GetSystemMenu(window,FALSE),MF_STRING,0x1F10+i*16,names[i]);}
    else for(int i=0;i<3;++i)if((*list)[i].command!=value[i].command||(*list)[i].on!=value[i].on||(*list)[i].enabled!=value[i].enabled)changed=true;
    *list=value;
    if(changed){for(int i=0;i<3;++i){EnableMenuItem(GetSystemMenu(window,FALSE),0x1F10+i*16,MF_BYCOMMAND|(value[i].enabled?MF_ENABLED:MF_GRAYED));CheckMenuItem(GetSystemMenu(window,FALSE),0x1F10+i*16,MF_BYCOMMAND|(value[i].on?MF_CHECKED:MF_UNCHECKED));}repaintCaption(window);}
}
void paintToggle(HWND window,HDC dc,RECT bounds,bool on,bool powered,bool highlighted,const style::Toggle& theme){
    const int width=scale(window,theme.width),height=scale(window,theme.height);
    RECT pill{bounds.left+(bounds.right-bounds.left-width)/2,bounds.top+(bounds.bottom-bounds.top-height)/2,0,0};pill.right=pill.left+width;pill.bottom=pill.top+height;
    const auto fill=on?(powered?theme.onTrack:theme.unpoweredTrack):theme.offTrack;
    rounded(dc,pill,fill,highlighted?theme.highlight:on&&powered?theme.onTrack:theme.border,height/2);
    const int inset=scale(window,theme.inset),knob=scale(window,theme.knob),x=on?pill.right-inset-knob:pill.left+inset;
    HBRUSH brush=CreateSolidBrush(on?theme.onKnob:theme.offKnob);auto oldBrush=SelectObject(dc,brush),oldPen=SelectObject(dc,GetStockObject(NULL_PEN));
    Ellipse(dc,x,pill.top+inset,x+knob,pill.top+inset+knob);
    SelectObject(dc,oldPen);SelectObject(dc,oldBrush);DeleteObject(brush);
}
void toggleButton(HWND window,bool on,bool powered){
    SetPropW(window,L"LutToggle",reinterpret_cast<HANDLE>(1));
    const bool changed=(GetPropW(window,L"LutToggleUnpowered")==nullptr)!=powered;
    if(powered)RemovePropW(window,L"LutToggleUnpowered");else SetPropW(window,L"LutToggleUnpowered",reinterpret_cast<HANDLE>(1));
    const auto check=on?BST_CHECKED:BST_UNCHECKED;
    if(SendMessageW(window,BM_GETCHECK,0,0)!=check)SendMessageW(window,BM_SETCHECK,check,0);
    if(changed)InvalidateRect(window,nullptr,FALSE);
}
void sliderColour(HWND window,COLORREF colour){const auto value=reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(colour)+1);if(GetPropW(window,L"LutSliderColour")!=value){SetPropW(window,L"LutSliderColour",value);InvalidateRect(window,nullptr,FALSE);}}
HFONT makeFont(UINT dpi) { return CreateFontW(-MulDiv(style::TextSize,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,family.c_str()); }
int contentWidth(HWND window,HFONT font,int minimum,int maximum){
    if(!window)return minimum;
    wchar_t cls[32]{};GetClassNameW(window,cls,32);const bool combo=wcscmp(cls,L"ComboBox")==0;
    HDC dc=GetDC(window);if(!dc)return minimum;auto old=SelectObject(dc,font);int widest=0;
    const int count=combo?static_cast<int>(SendMessageW(window,CB_GETCOUNT,0,0)):1;
    for(int i=0;i<count;++i){
        const int length=combo?static_cast<int>(SendMessageW(window,CB_GETLBTEXTLEN,i,0)):GetWindowTextLengthW(window);
        if(length<0||length>32767)continue;std::wstring text(static_cast<size_t>(length)+1,L'\0');
        if(combo)SendMessageW(window,CB_GETLBTEXT,i,reinterpret_cast<LPARAM>(text.data()));else GetWindowTextW(window,text.data(),length+1);
        SIZE size{};GetTextExtentPoint32W(dc,text.c_str(),length,&size);widest=std::max(widest,static_cast<int>(size.cx));
    }
    SelectObject(dc,old);ReleaseDC(window,dc);const UINT dpi=std::max(96u,GetDpiForWindow(window));
    // Dropdown painting reserves nine DIPs on the left and 25 for the arrow.
    return std::clamp(static_cast<int>((widest*96+dpi-1)/dpi)+(combo?36:24),minimum,maximum);
}
void adjustWindowRect(RECT& rect,UINT dpi) { rect.left-=1;rect.right+=1;rect.top-=MulDiv(36,dpi,96);rect.bottom+=1; }
void control(HWND window) {
    wchar_t cls[40]{}; GetClassNameW(window, cls, static_cast<int>(std::size(cls)));
    if(wcscmp(cls,TRACKBAR_CLASSW)==0)SetWindowSubclass(window,sliderPaintProc,82,0);
    if (wcscmp(cls, L"Button") == 0 || wcscmp(cls, L"ComboBox") == 0 || wcscmp(cls,HOTKEY_CLASSW)==0) SetWindowSubclass(window, proc, 1, 0);
    const bool framedList=wcscmp(cls,L"ListBox")==0&&(GetWindowLongPtrW(window,GWL_STYLE)&WS_BORDER);
    if(wcscmp(cls,L"Edit")==0||wcscmp(cls,HOTKEY_CLASSW)==0||framedList){
        SetWindowLongPtrW(window,GWL_STYLE,GetWindowLongPtrW(window,GWL_STYLE)&~WS_BORDER);
        SetWindowLongPtrW(window,GWL_EXSTYLE,GetWindowLongPtrW(window,GWL_EXSTYLE)&~WS_EX_CLIENTEDGE);
        SetWindowPos(window,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
        if(wcscmp(cls,L"Edit")==0||framedList)SetWindowSubclass(window,inputProc,5,0);
        if(wcscmp(cls,L"Edit")==0)SendMessageW(window,EM_SETMARGINS,EC_LEFTMARGIN|EC_RIGHTMARGIN,MAKELPARAM(scale(window,8),scale(window,8)));
        roundBounds(window);
    }
    if (wcscmp(cls, L"ComboBox") == 0 || wcscmp(cls, L"ListBox") == 0 || wcscmp(cls, L"Edit") == 0) SetWindowTheme(window, L"", L"");
    if(wcscmp(cls,L"ComboBox")==0){COMBOBOXINFO info{sizeof(info)};if(GetComboBoxInfo(window,&info)){SetWindowTheme(info.hwndList,L"",L"");SetWindowSubclass(info.hwndList,popupProc,4,0);}}
}
void frame(HWND window) {
    // Best effort: unsupported DWM title attributes leave the system frame intact.
    const BOOL dark = TRUE; DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    if (IsIconic(window)) return;
    if (IsZoomed(window)) { SetWindowRgn(window, nullptr, TRUE); return; }
    RECT outer{}; GetWindowRect(window, &outer); RECT visible{0,0,outer.right-outer.left,outer.bottom-outer.top};
    const int diameter = scale(window, 16); // 8 DIP radius: restrained, nearly square corners.
    HRGN shape = CreateRoundRectRgn(visible.left, visible.top, visible.right+1, visible.bottom+1, diameter, diameter);
    if (!shape) return;
    HRGN previous = CreateRectRgn(0,0,0,0);
    const bool same = GetWindowRgn(window, previous) != ERROR && EqualRgn(shape, previous);
    DeleteObject(previous);
    if (same || !SetWindowRgn(window, shape, TRUE)) DeleteObject(shape); // Windows owns a successful region.
}
bool message(HWND window, UINT msg, WPARAM wp, LPARAM lp, LRESULT& result) {
    switch(msg) {
    case WM_NCRBUTTONUP:
        if(wp==HTCAPTION||wp==HTSYSMENU){hideCaptionTip(window);showCaptionMenu(window,{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});result=0;return true;}break;
    case WM_CONTEXTMENU:
        if(reinterpret_cast<HWND>(wp)==window){POINT at{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            if(at.x==-1&&at.y==-1){RECT bounds{};GetWindowRect(window,&bounds);at={bounds.left+scale(window,8),bounds.top+scale(window,36)};}
            else if(captionHit(window,at)!=HTCAPTION&&captionHit(window,at)!=HTSYSMENU)break;
            hideCaptionTip(window);showCaptionMenu(window,at);result=0;return true;
        }break;
    case WM_SETCURSOR: {
        const int hit=LOWORD(lp);auto entry=action(window,hit);
        if(IsWindowEnabled(window)&&((entry&&entry->enabled)||hit==HTCLOSE||hit==HTMINBUTTON||hit==HTSYSMENU||(hit==HTHELP&&GetPropW(window,L"LutPowerCommand")))){
            SetCursor(LoadCursorW(nullptr,IDC_HAND));result=TRUE;return true;
        }
        if(hit==HTCLIENT){wchar_t cls[40]{};auto target=reinterpret_cast<HWND>(wp);GetClassNameW(target,cls,40);
            if(IsWindowEnabled(target)&&wcscmp(cls,TRACKBAR_CLASSW)==0){SetCursor(LoadCursorW(nullptr,IDC_SIZEWE));result=TRUE;return true;}}
        break;
    }
    case WM_MEASUREITEM:
        if(auto item=reinterpret_cast<MEASUREITEMSTRUCT*>(lp);item->CtlType==ODT_COMBOBOX){item->itemHeight=scale(window,18);result=TRUE;return true;}break;
    case WM_DRAWITEM:
        if(auto item=reinterpret_cast<DRAWITEMSTRUCT*>(lp);item->CtlType==ODT_COMBOBOX){
            const auto combo=item->hwndItem;HDC dc=item->hDC;
            const bool selected=(item->itemState&ODS_SELECTED)!=0;
            HBRUSH fill=CreateSolidBrush(selected?RGB(66,71,79):Surface);FillRect(dc,&item->rcItem,fill);DeleteObject(fill);
            const auto old=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(combo,WM_GETFONT,0,0)));SetBkMode(dc,TRANSPARENT);SetTextColor(dc,IsWindowEnabled(combo)?Text:Muted);
            if(item->itemID!=static_cast<UINT>(-1)){
                const auto length=SendMessageW(combo,CB_GETLBTEXTLEN,item->itemID,0);
                if(length>=0){std::wstring text(static_cast<size_t>(length)+1,L'\0');SendMessageW(combo,CB_GETLBTEXT,item->itemID,reinterpret_cast<LPARAM>(text.data()));RECT area=item->rcItem;area.left+=scale(window,5);area.right-=scale(window,5);DrawTextW(dc,text.c_str(),-1,&area,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);}
            }
            if((item->itemState&ODS_FOCUS)&&!(SendMessageW(combo,WM_QUERYUISTATE,0,0)&UISF_HIDEFOCUS)){RECT focus=item->rcItem;InflateRect(&focus,-1,-1);DrawFocusRect(dc,&focus);}
            SelectObject(dc,old);result=TRUE;return true;
        }break;
    case WM_NCCALCSIZE: { RECT* rect=wp?&reinterpret_cast<NCCALCSIZE_PARAMS*>(lp)->rgrc[0]:reinterpret_cast<RECT*>(lp);rect->left+=1;rect->right-=1;rect->top+=scale(window,36);rect->bottom-=1;result=0;return true; }
    case WM_CREATE: SetPropW(window,L"LutCaptionFont",makeFont(GetDpiForWindow(window)));break;
    case WM_NCPAINT: repaintCaption(window);result=0;return true;
    case WM_NCACTIVATE: if(!wp)hideCaptionTip(window);repaintCaption(window);result=TRUE;return true;
    case WM_NCHITTEST: { POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};result=captionHit(window,point);return true; }
    case WM_NCMOUSEMOVE: {
        trackCaptionTip(window,static_cast<int>(wp));const auto previous=GetPropW(window,L"LutCaptionHover");
        if(!previous){TRACKMOUSEEVENT tracking{sizeof(tracking),TME_NONCLIENT|TME_LEAVE,window,0};TrackMouseEvent(&tracking);}
        if(previous!=reinterpret_cast<HANDLE>(wp)){SetPropW(window,L"LutCaptionHover",reinterpret_cast<HANDLE>(wp));repaintCaption(window);}
        result=0;return true;
    }
    case WM_NCMOUSELEAVE: hideCaptionTip(window);RemovePropW(window,L"LutCaptionHover");repaintCaption(window);result=0;return true;
    case WM_DISPLAYCHANGE:if(GetPropW(window,L"MonitorMagePreferredSize"))SetTimer(window,GeometryTimer,250,nullptr);break;
    case WM_SETTINGCHANGE:if(wp==SPI_SETWORKAREA&&GetPropW(window,L"MonitorMagePreferredSize"))SetTimer(window,GeometryTimer,250,nullptr);break;
    case WM_TIMER: if(wp==GeometryTimer){KillTimer(window,GeometryTimer);repairWindowSize(window);result=0;return true;}if(wp==CaptionTipTimer){showCaptionTip(window);result=0;return true;}break;
    case WM_SHOWWINDOW: if(!wp)hideCaptionTip(window);else if(GetPropW(window,L"MonitorMagePreferredSize"))SetTimer(window,GeometryTimer,250,nullptr);break;
    case WM_MOUSEMOVE:
        hideCaptionTip(window);
        if(GetPropW(window,L"LutCaptionPressed")) { POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ClientToScreen(window,&point);const auto hit=reinterpret_cast<HANDLE>(static_cast<INT_PTR>(captionHit(window,point)));if(GetPropW(window,L"LutCaptionHover")!=hit){SetPropW(window,L"LutCaptionHover",hit);repaintCaption(window);}result=0;return true; }break;
    case WM_NCLBUTTONDBLCLK:
        if(!action(window,static_cast<int>(wp)) && (wp!=HTHELP || !GetPropW(window,L"LutPowerCommand")))break;
        [[fallthrough]];
    case WM_NCLBUTTONDOWN:
        hideCaptionTip(window);
        if(wp==HTSYSMENU){RECT bounds{};GetWindowRect(window,&bounds);showCaptionMenu(window,{bounds.left+scale(window,8),bounds.top+scale(window,36)});result=0;return true;}
        if(auto entry=action(window,static_cast<int>(wp))){if(entry->enabled){SetPropW(window,L"LutCaptionPressed",reinterpret_cast<HANDLE>(wp));SetCapture(window);repaintCaption(window);}result=0;return true;}
        if(wp==HTHELP && GetPropW(window,L"LutPowerCommand")) {
            if((reinterpret_cast<INT_PTR>(GetPropW(window,L"LutPowerState"))-1)&2){SetPropW(window,L"LutCaptionPressed",reinterpret_cast<HANDLE>(wp));SetCapture(window);repaintCaption(window);}result=0;return true;
        }
        if(wp==HTCLOSE||wp==HTMINBUTTON) { SetPropW(window,L"LutCaptionPressed",reinterpret_cast<HANDLE>(wp));SetCapture(window);result=0;return true; }break;
    case WM_LBUTTONUP:
        if(const auto hit=reinterpret_cast<INT_PTR>(RemovePropW(window,L"LutCaptionPressed"))) { POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ClientToScreen(window,&point);ReleaseCapture();if(captionHit(window,point)==hit){if(auto entry=action(window,static_cast<int>(hit))){if(entry->enabled)PostMessageW(window,WM_COMMAND,entry->command,0);}else if(hit==HTHELP){if((reinterpret_cast<INT_PTR>(GetPropW(window,L"LutPowerState"))-1)&2)PostMessageW(window,WM_COMMAND,reinterpret_cast<WPARAM>(GetPropW(window,L"LutPowerCommand")),0);}else PostMessageW(window,WM_SYSCOMMAND,hit==HTCLOSE?SC_CLOSE:SC_MINIMIZE,0);}result=0;return true; }break;
    case WM_SYSCOMMAND: case WM_SYSCHAR:
        if(msg==WM_SYSCOMMAND&&(wp&0xFFF0)==SC_RESTORE&&GetPropW(window,L"MonitorMagePreferredSize")){DefWindowProcW(window,msg,wp,lp);repairWindowSize(window);result=0;return true;}
        if(msg==WM_SYSCOMMAND&&((wp&0xFFF0)==SC_KEYMENU||(wp&0xFFF0)==SC_MOUSEMENU)){
            // Alt+Space and the window icon use the same menu as caption right-click.
            if((wp&0xFFF0)==SC_MOUSEMENU||lp==VK_SPACE||lp==0){RECT bounds{};GetWindowRect(window,&bounds);hideCaptionTip(window);showCaptionMenu(window,{bounds.left+scale(window,8),bounds.top+scale(window,36)});result=0;return true;}
        }
        if(msg==WM_SYSCOMMAND && (wp&0xFFF0)>=0x1F10 && (wp&0xFFF0)<=0x1F30){if(auto entry=action(window,100+static_cast<int>(((wp&0xFFF0)-0x1F10)/16))){if(entry->enabled)PostMessageW(window,WM_COMMAND,entry->command,0);result=0;return true;}}
        if(GetPropW(window,L"LutPowerCommand") && ((msg==WM_SYSCOMMAND && (wp&0xFFF0)==0x1F00)||(msg==WM_SYSCHAR && (GetPropW(window,L"LutCaptionSettings")?(wp==L's'||wp==L'S'):(wp==L'p'||wp==L'P'))))) {
            if((reinterpret_cast<INT_PTR>(GetPropW(window,L"LutPowerState"))-1)&2)PostMessageW(window,WM_COMMAND,reinterpret_cast<WPARAM>(GetPropW(window,L"LutPowerCommand")),0);
            result=0;return true;
        }break;
    case WM_CAPTURECHANGED: RemovePropW(window,L"LutCaptionPressed");RemovePropW(window,L"LutCaptionHover");repaintCaption(window);break;
    case WM_CANCELMODE:
        if(RemovePropW(window,L"LutCaptionPressed")){if(GetCapture()==window)ReleaseCapture();repaintCaption(window);}break;
    case WM_SETTEXT: result=DefWindowProcW(window,msg,wp,lp);repaintCaption(window);return true;
    case WM_SETICON: result=DefWindowProcW(window,msg,wp,lp);repaintCaption(window);return true;
    case WM_PRINT: printWindow(window,reinterpret_cast<HDC>(wp),lp);result=0;return true;
    case WM_DPICHANGED: {if(GetPropW(window,L"MonitorMagePreferredSize"))SetTimer(window,GeometryTimer,250,nullptr);hideCaptionTip(window);auto font=reinterpret_cast<HFONT>(RemovePropW(window,L"LutCaptionFont"));if(font)DeleteObject(font);SetPropW(window,L"LutCaptionFont",makeFont(HIWORD(wp)));break; }
    case WM_NCDESTROY: {KillTimer(window,GeometryTimer);delete reinterpret_cast<SIZE*>(RemovePropW(window,L"MonitorMagePreferredSize"));hideCaptionTip(window);if(auto tip=reinterpret_cast<HWND>(RemovePropW(window,L"LutCaptionTooltip")))DestroyWindow(tip);auto font=reinterpret_cast<HFONT>(RemovePropW(window,L"LutCaptionFont"));if(font)DeleteObject(font);RemovePropW(window,L"LutCaptionHover");RemovePropW(window,L"LutCaptionPressed");RemovePropW(window,L"LutPowerCommand");RemovePropW(window,L"LutPowerState");RemovePropW(window,L"LutCaptionSettings");delete reinterpret_cast<Actions*>(RemovePropW(window,L"LutCaptionActions"));break; }
    }
    if (msg >= WM_CTLCOLORMSGBOX && msg <= WM_CTLCOLORSTATIC) {
        auto dc = reinterpret_cast<HDC>(wp); const auto child = reinterpret_cast<HWND>(lp);
        wchar_t cls[40]{};GetClassNameW(child,cls,40);
        const bool field = msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX || wcscmp(cls,L"Edit")==0;
        SetTextColor(dc, IsWindowEnabled(child) ? Text : Muted); SetBkColor(dc, field ? Surface : Background);
        result = reinterpret_cast<LRESULT>(field ? surface.value : bg.value); return true;
    }
    if (msg == WM_ERASEBKGND) { RECT rect{}; GetClientRect(window, &rect); FillRect(reinterpret_cast<HDC>(wp), &rect, bg.value); result = 1; return true; }
    if (msg == WM_NOTIFY) {
        auto draw = reinterpret_cast<NMCUSTOMDRAW*>(lp);
        if (draw->hdr.code != NM_CUSTOMDRAW) return false;
        wchar_t cls[40]{}; GetClassNameW(draw->hdr.hwndFrom, cls, static_cast<int>(std::size(cls)));
        // Native buttons also draw synchronously for text, enabled and pressed
        // state changes. Route that path through the same dark painter.
        if (wcscmp(cls, L"Button") == 0 && draw->dwDrawStage == CDDS_PREPAINT) {
            paintControl(draw->hdr.hwndFrom, draw->hdc); result = CDRF_SKIPDEFAULT; return true;
        }
        if (wcscmp(cls, TRACKBAR_CLASSW) != 0) return false;
        if (draw->dwDrawStage == CDDS_PREPAINT) { FillRect(draw->hdc, &draw->rc, bg.value); result = CDRF_NOTIFYITEMDRAW; return true; }
        if (draw->dwDrawStage == CDDS_ITEMPREPAINT) {
            const auto stored=reinterpret_cast<UINT_PTR>(GetPropW(draw->hdr.hwndFrom,L"LutSliderColour"));const COLORREF colour=stored?static_cast<COLORREF>(stored-1):Accent;
            if (draw->dwItemSpec == TBCD_CHANNEL) { RECT rect = draw->rc; const int mid = (rect.top+rect.bottom)/2; rect.top = mid-scale(window, 2); rect.bottom = mid+scale(window, 2); rounded(draw->hdc, rect, Border, Border, scale(window, 2)); }
            if(draw->dwItemSpec==TBCD_CHANNEL && stored){RECT rect=draw->rc;const int mid=(rect.top+rect.bottom)/2;rect.top=mid-scale(window,2);rect.bottom=mid+scale(window,2);const auto muted=RGB((GetRValue(colour)+2*GetRValue(Background))/3,(GetGValue(colour)+2*((Background>>8)&255))/3,(GetBValue(colour)+2*GetBValue(Background))/3);rounded(draw->hdc,rect,muted,muted,scale(window,2));}
            if(draw->dwItemSpec==TBCD_CHANNEL)if(const auto value=reinterpret_cast<INT_PTR>(GetPropW(draw->hdr.hwndFrom,L"LutSliderDefault"))) {
                const int low=static_cast<int>(SendMessageW(draw->hdr.hwndFrom,TBM_GETRANGEMIN,0,0)),high=static_cast<int>(SendMessageW(draw->hdr.hwndFrom,TBM_GETRANGEMAX,0,0));
                if(high>low){const int x=draw->rc.left+MulDiv(static_cast<int>(value-32769)-low,draw->rc.right-draw->rc.left-1,high-low),mid=(draw->rc.top+draw->rc.bottom)/2;
                    HPEN tick=CreatePen(PS_SOLID,1,Muted);const auto old=SelectObject(draw->hdc,tick);MoveToEx(draw->hdc,x,mid-scale(window,5),nullptr);LineTo(draw->hdc,x,mid+scale(window,5)+1);SelectObject(draw->hdc,old);DeleteObject(tick);}
            }
            if (draw->dwItemSpec == TBCD_THUMB) {
                rounded(draw->hdc, draw->rc, IsWindowEnabled(draw->hdr.hwndFrom) ? colour : Muted, Border, scale(window, 3));
                if(const wchar_t letter=static_cast<wchar_t>(reinterpret_cast<INT_PTR>(GetPropW(draw->hdr.hwndFrom,L"LutSliderLetter")))){
                    LOGFONTW labelStyle{};GetObjectW(reinterpret_cast<HFONT>(SendMessageW(draw->hdr.hwndFrom,WM_GETFONT,0,0)),sizeof(labelStyle),&labelStyle);labelStyle.lfHeight=-scale(window,style::CaptionTextSize);
                    HFONT labelFont=CreateFontIndirectW(&labelStyle);const auto old=SelectObject(draw->hdc,labelFont);SetBkMode(draw->hdc,TRANSPARENT);SetTextColor(draw->hdc,Background);
                    RECT label=draw->rc;DrawTextW(draw->hdc,&letter,1,&label,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);SelectObject(draw->hdc,old);DeleteObject(labelFont);
                }
            }
            result = CDRF_SKIPDEFAULT; return true;
        }
    }
    return false;
}
}
