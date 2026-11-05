#include "rgb_plot.h"
#include "paint_buffer.h"
#include "window_geometry.h"
#include <iostream>

int main(){
    // Desktop sizes and scaling factors without changing Windows settings.
    for(int dpi:{96,120,144,168,192,240,288})for(SIZE screen:{SIZE{800,600},SIZE{1024,768},SIZE{1280,720},SIZE{1920,1080},SIZE{3840,2160}}){
        const SIZE preferred{MulDiv(776,dpi,96)+2,MulDiv(576,dpi,96)+2};
        const RECT work{-screen.cx,-120,0,screen.cy-160};
        const auto fit=ui::fitWindowRect({100,100,200,200},preferred,work);
        if(fit.left<work.left||fit.top<work.top||fit.right>work.right||fit.bottom>work.bottom||fit.right<=fit.left||fit.bottom<=fit.top)return 4;
        const RECT large{-4000,-200,4000,4000};const auto restored=ui::fitWindowRect(fit,preferred,large);
        if(restored.right-restored.left!=preferred.cx||restored.bottom-restored.top!=preferred.cy)return 5;
    }
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=64;info.bmiHeader.biHeight=-64;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    HDC dc=CreateCompatibleDC(nullptr);void* data=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&data,nullptr,0);
    if(!dc||!bitmap)return 1;
    const auto old=SelectObject(dc,bitmap);PatBlt(dc,0,0,64,64,BLACKNESS);
    plot::Paths paths;for(auto& path:paths)path={{8,10},{56,39}};
    bool ok=plot::rgb(dc,64,64,paths,2);GdiFlush();
    const auto pixels=static_cast<const uint32_t*>(data);bool partial=false,white=false;
    for(int y=0;y<64;++y)for(int x=0;x<64;++x){
        const auto p=pixels[y*64+x];const int r=(p>>16)&255,g=(p>>8)&255,b=p&255;
        if(r!=g||g!=b)ok=false;
        partial=partial||(r>0&&r<255);white=white||r==255;
        if((x<5||x>59||y<7||y>42)&&r)ok=false; // No distant glow/halo.
    }
    PatBlt(dc,0,0,64,64,BLACKNESS);paths[2].clear();ok=plot::rgb(dc,64,64,paths,2)&&ok;GdiFlush();
    bool yellow=false;for(int i=0;i<64*64;++i){const auto p=pixels[i]&0xffffff;if((p&255)!=0)ok=false;yellow=yellow||p==0xffff00;}
    // A partial repaint must match the corresponding pixels from a full AA
    // render, while leaving everything outside the caller's clip untouched.
    std::vector<uint32_t> reference(pixels,pixels+64*64);
    PatBlt(dc,0,0,64,64,BLACKNESS);const int saved=SaveDC(dc);IntersectClipRect(dc,20,18,40,29);
    ok=plot::rgb(dc,64,64,paths,2)&&ok;GdiFlush();
    for(int y=0;y<64;++y)for(int x=0;x<64;++x){const auto actual=pixels[y*64+x]&0xffffff;
        if(actual!=((x>=20&&x<40&&y>=18&&y<29)?reference[y*64+x]&0xffffff:0))ok=false;}
    RestoreDC(dc,saved);PatBlt(dc,0,0,64,64,BLACKNESS);
    const int clipped=SaveDC(dc);IntersectClipRect(dc,10,12,21,19);
    {
        paint::Buffer buffer(dc,{0,0,64,64});const auto size=buffer.bitmapSize();
        if(!buffer||size.cx!=11||size.cy!=7)ok=false;
        RECT all{0,0,64,64};FillRect(buffer.dc(),&all,static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    }
    RestoreDC(dc,clipped);GdiFlush();
    for(int y=0;y<64;++y)for(int x=0;x<64;++x)
        if((pixels[y*64+x]&0xffffff)!=((x>=10&&x<21&&y>=12&&y<19)?0xffffffu:0u))ok=false;
    const int empty=SaveDC(dc);IntersectClipRect(dc,0,0,0,0);
    {paint::Buffer buffer(dc,{0,0,64,64});if(buffer||buffer.bitmapSize().cx)ok=false;}
    RestoreDC(dc,empty);
    // A tall stroke crosses several tile boundaries without seams. Reusing
    // the workspace must also retain no old color, and remain bounded.
    {
        BITMAPINFO tallInfo=info;tallInfo.bmiHeader.biWidth=128;tallInfo.bmiHeader.biHeight=-256;void* tallPixels=nullptr;
        HDC tallDC=CreateCompatibleDC(dc);HBITMAP tall=CreateDIBSection(tallDC,&tallInfo,DIB_RGB_COLORS,&tallPixels,nullptr,0);
        if(!tallDC||!tall)return 3;const auto prior=SelectObject(tallDC,tall);PatBlt(tallDC,0,0,128,256,BLACKNESS);
        plot::Scratch scratch;plot::Paths vertical;vertical[0]={{64,4},{64,252}};vertical[1]=vertical[0];
        if(!plot::rgb(tallDC,128,256,vertical,2,&scratch)||!scratch.bytes()||scratch.bytes()>1048576)ok=false;
        GdiFlush();const auto values=static_cast<const uint32_t*>(tallPixels);
        for(int y=6;y<250;++y)if((values[y*128+64]&0xffffff)!=0xffff00)ok=false;
        vertical[1].clear();PatBlt(tallDC,0,0,128,256,BLACKNESS);
        if(!plot::rgb(tallDC,128,256,vertical,2,&scratch))ok=false;GdiFlush();
        for(int y=6;y<250;++y)if((values[y*128+64]&0xffffff)!=0xff0000)ok=false;
        scratch.reset();if(scratch.bytes())ok=false;
        SelectObject(tallDC,prior);DeleteObject(tall);DeleteDC(tallDC);
    }
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
    if(!ok||!partial||!white||!yellow)return 2;
    std::cout<<"Curve AA, clipped/full pixel equivalence and dirty-sized paint buffers passed\n";return 0;
}
