#pragma once
#include <windows.h>
#include <algorithm>

namespace paint {
// Preserve the caller's logical coordinates, but allocate only the visible
// dirty rectangle. Shared WM_PRINT DCs and native BeginPaint DCs both work.
class Buffer {
    HDC target_=nullptr,memory_=nullptr;
    HBITMAP bitmap_=nullptr;
    HGDIOBJ previous_=nullptr;
    RECT area_{};
    int saved_=0;
    bool active_=false,committed_=false;
public:
    Buffer(HDC target,const RECT& bounds):target_(target){
        if(!target_||(saved_=SaveDC(target_))==0)return;
        IntersectClipRect(target_,bounds.left,bounds.top,bounds.right,bounds.bottom);
        RECT clip{};const int type=GetClipBox(target_,&clip);
        if(type==NULLREGION)return;
        if(type==ERROR)clip=bounds;
        if(!IntersectRect(&area_,&bounds,&clip))return;
        active_=true;
        const int w=area_.right-area_.left,h=area_.bottom-area_.top;
        memory_=CreateCompatibleDC(target_);
        if(memory_)bitmap_=CreateCompatibleBitmap(target_,w,h);
        if(bitmap_){previous_=SelectObject(memory_,bitmap_);
            if(previous_&&previous_!=HGDI_ERROR){SetViewportOrgEx(memory_,-area_.left,-area_.top,nullptr);IntersectClipRect(memory_,area_.left,area_.top,area_.right,area_.bottom);return;}
            DeleteObject(bitmap_);bitmap_=nullptr;
        }
        if(memory_){DeleteDC(memory_);memory_=nullptr;}
    }
    Buffer(const Buffer&)=delete;Buffer& operator=(const Buffer&)=delete;
    explicit operator bool()const{return active_;}
    bool buffered()const{return memory_!=nullptr;}
    HDC dc()const{return memory_?memory_:target_;}
    SIZE bitmapSize()const{BITMAP info{};if(bitmap_)GetObjectW(bitmap_,sizeof(info),&info);return {info.bmWidth,info.bmHeight};}
    size_t bytes()const{BITMAP info{};if(bitmap_)GetObjectW(bitmap_,sizeof(info),&info);return static_cast<size_t>(info.bmWidthBytes)*info.bmHeight;}
    void commit(){
        if(committed_)return;committed_=true;
        if(active_&&memory_)BitBlt(target_,area_.left,area_.top,area_.right-area_.left,area_.bottom-area_.top,memory_,area_.left,area_.top,SRCCOPY);
    }
    void finish(){
        commit();if(bitmap_){SelectObject(memory_,previous_);DeleteObject(bitmap_);bitmap_=nullptr;}
        if(memory_){DeleteDC(memory_);memory_=nullptr;}if(saved_){RestoreDC(target_,saved_);saved_=0;}
    }
    ~Buffer(){finish();}
};
}
