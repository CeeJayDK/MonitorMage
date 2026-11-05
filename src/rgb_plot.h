#pragma once
#include <windows.h>
#include <array>
#include <vector>
#include <algorithm>
#include <cstdint>

namespace plot {
using Paths=std::array<std::vector<POINT>,3>;
// One bounded supersampling tile, reusable while the editor is open. A full
// graph bitmap is never retained. The owner releases this on hide/minimize.
class Scratch {
    HDC dc_=nullptr;HBITMAP bitmap_=nullptr;int width_=0,height_=0;
    void* pixels_=nullptr;
public:
    Paths points;
    Scratch()=default;Scratch(const Scratch&)=delete;Scratch& operator=(const Scratch&)=delete;
    void reset(){if(bitmap_)DeleteObject(bitmap_);if(dc_)DeleteDC(dc_);bitmap_=nullptr;dc_=nullptr;pixels_=nullptr;width_=height_=0;for(auto& p:points)std::vector<POINT>().swap(p);}
    ~Scratch(){reset();}
    bool ensure(HDC target,int width,int height){
        if(bitmap_&&width_==width&&height_==height)return true;
        reset();dc_=CreateCompatibleDC(target);if(!dc_)return false;
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        bitmap_=CreateDIBSection(target,&info,DIB_RGB_COLORS,&pixels_,nullptr,0);
        if(!bitmap_){reset();return false;}width_=width;height_=height;SetROP2(dc_,R2_MERGEPEN);return true;
    }
    HDC dc()const{return dc_;}HBITMAP bitmap()const{return bitmap_;}
    uint32_t* pixels()const{return static_cast<uint32_t*>(pixels_);}
    size_t bytes()const{return static_cast<size_t>(width_)*height_*4;}
};
// Additive RGB coverage with unchanged 2x supersampling and no glow. Each
// tile is downsampled in place, then blended before its storage is reused.
inline bool rgb(HDC target,int width,int height,const Paths& paths,int stroke,Scratch* reusable=nullptr) {
    if(width<=0||height<=0||stroke<=0)return false;
    RECT bounds{width,height,0,0};bool any=false;
    for(const auto& path:paths)if(path.size()>1)for(const auto& p:path){
        any=true;bounds.left=std::min(bounds.left,p.x);bounds.top=std::min(bounds.top,p.y);
        bounds.right=std::max(bounds.right,p.x);bounds.bottom=std::max(bounds.bottom,p.y);
    }
    if(!any)return true;
    const int pad=stroke+1;
    bounds.left=std::max(0L,bounds.left-pad);bounds.top=std::max(0L,bounds.top-pad);
    bounds.right=std::min(static_cast<LONG>(width),bounds.right+pad);
    bounds.bottom=std::min(static_cast<LONG>(height),bounds.bottom+pad);
    RECT clip{};const auto type=GetClipBox(target,&clip);
    if(type==NULLREGION)return true;
    if(type!=ERROR){RECT visible{};if(!IntersectRect(&visible,&bounds,&clip))return true;bounds=visible;}
    const int w=bounds.right-bounds.left,h=bounds.bottom-bounds.top;
    if(w<=0||h<=0)return true;
    // Four supersamples * four bytes per output pixel. Keep the tile at
    // most 1 MiB for ordinary widths, rather than retaining a full 2x image.
    const int rows=std::min({h,64,std::max(1,1048576/(w*16))});
    Scratch transient;auto& scratch=reusable?*reusable:transient;
    if(!scratch.ensure(target,w*2,rows*2))return false;
    HDC layer=scratch.dc();auto& scaled=scratch.points;
    for(size_t c=0;c<3;++c){scaled[c].clear();scaled[c].reserve(paths[c].size());
        for(const auto& p:paths[c])scaled[c].push_back({(p.x-bounds.left)*2,(p.y-bounds.top)*2});}
    constexpr COLORREF colours[]{RGB(255,0,0),RGB(0,255,0),RGB(0,0,255)};HPEN pens[3]{};
    for(int c=0;c<3;++c)if(!(pens[c]=CreatePen(PS_SOLID,stroke*2,colours[c]))){for(auto p:pens)if(p)DeleteObject(p);return false;}
    bool ok=true;const auto original=SelectObject(layer,scratch.bitmap());
    for(int start=0;start<h&&ok;start+=rows){const int count=std::min(rows,h-start);
        SetViewportOrgEx(layer,0,0,nullptr);PatBlt(layer,0,0,w*2,count*2,BLACKNESS);
        SetViewportOrgEx(layer,0,-start*2,nullptr);
        for(size_t c=0;c<3;++c)if(scaled[c].size()>1){const auto prior=SelectObject(layer,pens[c]);
            if(!Polyline(layer,scaled[c].data(),static_cast<int>(scaled[c].size())))ok=false;SelectObject(layer,prior);}
        SetViewportOrgEx(layer,0,0,nullptr);GdiFlush();
        auto output=scratch.pixels();const auto input=output;
        for(int y=0;y<count;++y)for(int x=0;x<w;++x){uint32_t value=0;int alpha=0;
            for(int c=0;c<3;++c){int sum=0;
                for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)sum+=(input[static_cast<size_t>(y*2+dy)*w*2+x*2+dx]>>(c*8))&255;
                const int coverage=(sum+2)/4;alpha=std::max(alpha,coverage);value|=static_cast<uint32_t>(coverage)<<(c*8);}
            output[static_cast<size_t>(y)*w*2+x]=value|(static_cast<uint32_t>(alpha)<<24);
        }
        if(ok){BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};ok=AlphaBlend(target,bounds.left,bounds.top+start,w,count,layer,0,0,w,count,blend)!=FALSE;}
        GdiFlush();
    }
    SelectObject(layer,original);for(auto pen:pens)DeleteObject(pen);return ok;
}
}
