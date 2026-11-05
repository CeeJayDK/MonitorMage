#include "lut1d.h"
#include "presets.h"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <fstream>
#include <sstream>
#include <locale>
#include <iomanip>
#include <map>
#include <set>
#include <cstring>
#include <cctype>
#include <cstdint>
namespace lut1d {
namespace {
using Microsoft::WRL::ComPtr;
constexpr size_t MaxBytes=16*1024*1024,MaxSource=262144,MaxStored=4096,MaxPixels=4*1024*1024;
[[noreturn]] void fail(const char* text){throw std::runtime_error(text);}
void ensure(bool value,const char* text){if(!value)fail(text);}
std::string clean(const std::string& text){
    ensure(text.size()<=MaxBytes&&text.find('\0')==std::string::npos,"LUT text exceeds 16 MiB or contains invalid text");
    std::istringstream in(text.substr(text.compare(0,3,"\xef\xbb\xbf")==0?3:0));std::string line,result;
    while(std::getline(in,line)){bool quoted=false;for(size_t i=0;i<line.size();++i){if(line[i]=='"')quoted=!quoted;if(line[i]=='#'&&!quoted){line.resize(i);break;}}result+=line+'\n';}return result;
}
std::vector<std::string> tokens(const std::string& line){std::istringstream in(line);in.imbue(std::locale::classic());std::vector<std::string> result;std::string value;while(in>>std::quoted(value))result.push_back(value);ensure(in.eof(),"Invalid quoted LUT field");return result;}
double number(const std::string& text){std::istringstream in(text);in.imbue(std::locale::classic());double value;ensure(static_cast<bool>(in>>value)&&std::isfinite(value),"Invalid or nonfinite LUT number");in>>std::ws;ensure(in.eof(),"Extra characters in LUT number");return value;}
size_t count(const std::string& text){const double v=number(text);ensure(v>=2&&v<=MaxSource&&v==std::floor(v),"LUT length must be 2 to 262144 samples");return static_cast<size_t>(v);}
void sourceValid(const Channels& values){
    const auto n=values[0].size();ensure(n>=2&&n<=MaxSource,"Invalid LUT sample count");
    for(const auto& channel:values){ensure(channel.size()==n,"Unequal LUT channel lengths");double previous=-INFINITY;for(double v:channel){ensure(std::isfinite(v)&&v>=previous,"LUT curves must be finite and nondecreasing");previous=v;}}
}
Channels normalized(const Channels& values,double low=0,double high=1,const std::vector<double>& positions={}){
    sourceValid(values);ensure(std::isfinite(low)&&std::isfinite(high)&&high>low,"Invalid LUT input range");
    const size_t n=values[0].size(),stored=std::min(n,MaxStored);Channels out;
    if(!positions.empty()){ensure(positions.size()==n&&positions.front()==0&&positions.back()==1,"Indexed LUT inputs must cover 0..1");for(size_t i=0;i<n;++i)ensure(std::isfinite(positions[i])&&(i==0||positions[i]>positions[i-1]),"LUT input indices must be finite and strictly increasing");}
    for(size_t i=0;i<stored;++i){const double x=i/static_cast<double>(stored-1);size_t a,b;double fraction;
        if(positions.empty()){const double at=std::clamp((x-low)/(high-low),0.0,1.0)*(n-1);a=static_cast<size_t>(at);b=std::min(a+1,n-1);fraction=at-a;}
        else {const auto upper=std::upper_bound(positions.begin(),positions.end(),x);b=std::min(static_cast<size_t>(upper-positions.begin()),n-1);a=b?b-1:0;fraction=a==b?0:(x-positions[a])/(positions[b]-positions[a]);}
        for(size_t c=0;c<3;++c){double value=values[c][a]+fraction*(values[c][b]-values[c][a]);ensure(value>=-1e-12&&value<=1+1e-12,"This LUT produces out-of-range values on 0..1 input; it cannot be used as a normalized hardware curve");out[c].push_back(std::clamp(value,0.0,1.0));}
    }
    color::Adjustments check;check.imported=out;color::validate(check);return out;
}
std::string lower(std::string value){for(auto& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return value;}
std::string textFile(const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);ensure(static_cast<bool>(input),"Cannot open LUT file");input.seekg(0,std::ios::end);const auto size=input.tellg();ensure(size>=0&&static_cast<uint64_t>(size)<=MaxBytes,"LUT file exceeds 16 MiB");input.seekg(0);std::string text(static_cast<size_t>(size),'\0');ensure(static_cast<bool>(input.read(text.data(),static_cast<std::streamsize>(text.size()))),"Cannot read complete LUT file");return text;}
void hr(HRESULT result,const char* text){if(FAILED(result))fail(text);}
struct Apartment {HRESULT result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);Apartment(){ensure(SUCCEEDED(result)||result==RPC_E_CHANGED_MODE,"Cannot initialize Windows image decoder");}~Apartment(){if(SUCCEEDED(result))CoUninitialize();}};
struct Image {UINT width=0,height=0;std::vector<std::array<double,3>> pixels;};
Image imageFile(const std::filesystem::path& path){
    ensure(std::filesystem::file_size(path)<=MaxBytes,"Image LUT exceeds 16 MiB");Apartment apartment;
    ComPtr<IWICImagingFactory> factory;hr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.GetAddressOf())),"Windows image decoder is unavailable");
    ComPtr<IWICBitmapDecoder> decoder;hr(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder),"Cannot decode image LUT");
    GUID container{};hr(decoder->GetContainerFormat(&container),"Cannot identify image LUT");ensure(container==GUID_ContainerFormatPng||container==GUID_ContainerFormatTiff,"Only PNG and TIFF image LUTs are supported");
    UINT frames=0;hr(decoder->GetFrameCount(&frames),"Cannot inspect image frames");ensure(frames==1,"Image LUT must contain exactly one frame/page");
    ComPtr<IWICBitmapFrameDecode> frame;hr(decoder->GetFrame(0,&frame),"Cannot read image frame");Image image;hr(frame->GetSize(&image.width,&image.height),"Cannot inspect image dimensions");
    const size_t pixels=static_cast<size_t>(image.width)*image.height;ensure(image.width&&image.height&&image.width<=262144&&image.height<=262144&&pixels<=MaxPixels,"Image LUT exceeds four million pixels or has invalid dimensions");
    WICPixelFormatGUID format{};hr(frame->GetPixelFormat(&format),"Cannot inspect image pixel format");
    ComPtr<IWICComponentInfo> component;hr(factory->CreateComponentInfo(format,&component),"Cannot inspect image format");ComPtr<IWICPixelFormatInfo2> info;hr(component.As(&info),"Cannot inspect image precision");
    WICPixelFormatNumericRepresentation numeric{};hr(info->GetNumericRepresentation(&numeric),"Cannot inspect image numeric representation");
    const bool floating=numeric==WICPixelFormatNumericRepresentationFloat;ensure(floating||numeric==WICPixelFormatNumericRepresentationUnsignedInteger||numeric==WICPixelFormatNumericRepresentationIndexed,"Unsupported image numeric representation");
    ComPtr<IWICFormatConverter> converter;hr(factory->CreateFormatConverter(&converter),"Cannot create image format converter");
    hr(converter->Initialize(frame.Get(),floating?GUID_WICPixelFormat128bppRGBAFloat:GUID_WICPixelFormat64bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom),"Cannot decode image samples without reducing precision");
    image.pixels.resize(pixels);
    if(floating){std::vector<float> raw(pixels*4);hr(converter->CopyPixels(nullptr,image.width*16,static_cast<UINT>(raw.size()*sizeof(float)),reinterpret_cast<BYTE*>(raw.data())),"Cannot read floating-point image samples");
        for(size_t i=0;i<pixels;++i){ensure(std::isfinite(raw[i*4+3])&&raw[i*4+3]==1,"Image LUT must be fully opaque");for(size_t c=0;c<3;++c){const double v=raw[i*4+c];ensure(std::isfinite(v)&&v>=0&&v<=1,"Image LUT values must be finite and normalized to 0..1");image.pixels[i][c]=v;}}}
    else {std::vector<uint16_t> raw(pixels*4);hr(converter->CopyPixels(nullptr,image.width*8,static_cast<UINT>(raw.size()*sizeof(uint16_t)),reinterpret_cast<BYTE*>(raw.data())),"Cannot read image samples");
        for(size_t i=0;i<pixels;++i){ensure(raw[i*4+3]==65535,"Image LUT must be fully opaque");for(size_t c=0;c<3;++c)image.pixels[i][c]=raw[i*4+c]/65535.0;}}
    // No ICC colour transform, gamma correction, orientation transform or dithering:
    // a LUT image stores numerical data, rather than a photograph to display.
    return image;
}
Loaded fromImage(const std::filesystem::path& path){
    const auto image=imageFile(path);const size_t w=image.width,h=image.height;Channels channels;
    const auto at=[&](size_t x,size_t y)->const std::array<double,3>&{return image.pixels[y*w+x];};
    bool horizontal=w>=2,vertical=h>=2;
    for(size_t y=0;y<h&&horizontal;++y)for(size_t x=0;x<w;++x)if(at(x,y)!=at(x,0)){horizontal=false;break;}
    for(size_t y=0;y<h&&vertical;++y)for(size_t x=0;x<w;++x)if(at(x,y)!=at(0,y)){vertical=false;break;}
    std::wstring layout;
    if(horizontal||vertical){const size_t n=horizontal?w:h;for(size_t i=0;i<n;++i)for(size_t c=0;c<3;++c)channels[c].push_back((horizontal?at(i,0):at(0,i))[c]);layout=horizontal?L"horizontal image ramp":L"vertical image ramp";}
    else {
        size_t n=0;bool hald=false;
        if(w==h*h&&h>=2)n=h;
        else if(w==h){for(size_t level=2;level<=16;++level)if(level*level*level==w){n=level*level;hald=true;break;}}
        ensure(n>=2,"Image is not a 1D ramp or a recognized ReShade/Hald LUT layout");
        const auto pixel=[&](size_t r,size_t g,size_t b)->const std::array<double,3>&{const size_t index=r+g*n+b*n*n;return hald?image.pixels[index]:at(r+b*n,g);};
        for(size_t i=0;i<n;++i){channels[0].push_back(pixel(i,0,0)[0]);channels[1].push_back(pixel(0,i,0)[1]);channels[2].push_back(pixel(0,0,i)[2]);}
        for(size_t b=0;b<n;++b)for(size_t g=0;g<n;++g)for(size_t r=0;r<n;++r){const auto& p=pixel(r,g,b);ensure(p[0]==channels[0][r]&&p[1]==channels[1][g]&&p[2]==channels[2][b],"This image is a genuine 3D LUT with channel mixing. It cannot be represented by the current 1D hardware backend");}
        layout=hald?L"Hald image (verified separable into 1D)":L"ReShade tiled image (verified separable into 1D)";
    }
    const auto count=channels[0].size();auto out=normalized(channels);return {std::move(out),layout+L", "+std::to_wstring(count)+L" source samples"+(count>MaxStored?L" (resampled to 4096)":L"")};
}
void writeImage(const std::filesystem::path& path,const Channels& values,bool tiff){
    Apartment apartment;ComPtr<IWICImagingFactory> factory;hr(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.GetAddressOf())),"Cannot create Windows image encoder");
    ComPtr<IStream> stream;hr(CreateStreamOnHGlobal(nullptr,TRUE,&stream),"Cannot create image output buffer");ComPtr<IWICBitmapEncoder> encoder;hr(factory->CreateEncoder(tiff?GUID_ContainerFormatTiff:GUID_ContainerFormatPng,nullptr,&encoder),"Cannot create image encoder");hr(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache),"Cannot initialize image encoder");
    ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> properties;hr(encoder->CreateNewFrame(&frame,&properties),"Cannot create image frame");hr(frame->Initialize(properties.Get()),"Cannot initialize image frame");const UINT n=static_cast<UINT>(values[0].size());hr(frame->SetSize(n,1),"Cannot set image dimensions");WICPixelFormatGUID format=GUID_WICPixelFormat64bppRGBA;hr(frame->SetPixelFormat(&format),"Cannot set 16-bit image format");ensure(format==GUID_WICPixelFormat64bppRGBA,"Image encoder does not support 16-bit RGBA");
    std::vector<uint16_t> pixels(n*4);for(size_t i=0;i<n;++i){for(size_t c=0;c<3;++c)pixels[i*4+c]=static_cast<uint16_t>(std::lround(values[c][i]*65535));pixels[i*4+3]=65535;}
    hr(frame->WritePixels(1,n*8,static_cast<UINT>(pixels.size()*2),reinterpret_cast<BYTE*>(pixels.data())),"Cannot encode image pixels");hr(frame->Commit(),"Cannot commit image frame");hr(encoder->Commit(),"Cannot finish image encoding");STATSTG stat{};hr(stream->Stat(&stat,STATFLAG_NONAME),"Cannot measure encoded image");ensure(stat.cbSize.QuadPart<=MaxBytes,"Encoded image is too large");
    LARGE_INTEGER start{};hr(stream->Seek(start,STREAM_SEEK_SET,nullptr),"Cannot rewind image output");std::string bytes(static_cast<size_t>(stat.cbSize.QuadPart),'\0');ULONG read=0;hr(stream->Read(bytes.data(),static_cast<ULONG>(bytes.size()),&read),"Cannot read encoded image");ensure(read==bytes.size(),"Incomplete encoded image");presets::writeBytes(path,bytes,true);
}
}
Channels decode(const std::string& text){
    std::istringstream input(clean(text));std::string line;size_t n=0;Channels values;double low=0,high=1;std::set<std::string> headers;
    while(std::getline(input,line)){auto row=tokens(line);if(row.empty())continue;const auto& key=row[0];
        if(key=="TITLE"){ensure(values[0].empty(),"LUT metadata must precede samples");continue;}
        if(key=="LUT_3D_SIZE")fail("This is a 3D LUT. The current hardware backend accepts 1D transforms only");
        if(key=="LUT_1D_SIZE"){ensure(row.size()==2&&!n&&values[0].empty(),"Invalid or duplicate LUT size");n=count(row[1]);continue;}
        if(key=="DOMAIN_MIN"||key=="DOMAIN_MAX"||key=="LUT_1D_INPUT_RANGE"){
            ensure(values[0].empty()&&headers.insert(key).second,"Duplicate or misplaced LUT range");
            if(key=="LUT_1D_INPUT_RANGE"){ensure(row.size()==3,"Invalid input range");low=number(row[1]);high=number(row[2]);}
            else {ensure(row.size()==4,"Invalid LUT domain");const double v=number(row[1]);ensure(number(row[2])==v&&number(row[3])==v,"Different channel domains are not supported");(key=="DOMAIN_MIN"?low:high)=v;}continue;
        }
        ensure(n&&row.size()==3,"A 1D cube needs a size followed by three numbers per sample");for(size_t c=0;c<3;++c)values[c].push_back(number(row[c]));ensure(values[0].size()<=n,"Too many LUT samples");
    }
    ensure(n&&values[0].size()==n,"Incomplete 1D cube LUT");ensure(!headers.count("LUT_1D_INPUT_RANGE")||(!headers.count("DOMAIN_MIN")&&!headers.count("DOMAIN_MAX")),"Conflicting LUT input ranges");return normalized(values,low,high);
}
Channels decodeSpi(const std::string& text){
    auto source=clean(text);for(size_t at=0;(at=source.find_first_of("{}",at))!=std::string::npos;at+=3)source.replace(at,1," "+source.substr(at,1)+" ");
    const auto words=tokens(source);size_t i=0,n=0,components=0;double low=0,high=1;std::set<std::string> seen;
    while(i<words.size()&&words[i]!="{"){const auto key=words[i++];ensure(seen.insert(key).second,"Duplicate spi1d header");
        if(key=="From"){ensure(i+1<words.size(),"Incomplete spi1d range");low=number(words[i++]);high=number(words[i++]);}
        else {ensure(i<words.size(),"Incomplete spi1d header");const auto value=words[i++];if(key=="Version")ensure(value=="1","Unsupported spi1d version");else if(key=="Length")n=count(value);else if(key=="Components"){ensure(value=="1"||value=="3","Only one- and three-component spi1d LUTs are supported");components=value=="1"?1:3;}else fail("Unknown spi1d header");}}
    ensure(seen.count("Version")&&seen.count("From")&&n&&components&&i<words.size()&&words[i++]=="{","Incomplete spi1d header");Channels values;
    for(size_t sample=0;sample<n;++sample){std::array<double,3> v{};for(size_t c=0;c<components;++c){ensure(i<words.size(),"Incomplete spi1d data");v[c]=number(words[i++]);}for(size_t c=0;c<3;++c)values[c].push_back(v[components==1?0:c]);}
    ensure(i<words.size()&&words[i++]=="}"&&i==words.size(),"Extra or incomplete spi1d data");return normalized(values,low,high);
}
Channels decodeCal(const std::string& text){
    std::istringstream input(clean(text));std::string line;bool identified=false,formatting=false,data=false,ended=false;size_t fields=0,n=0;std::vector<std::string> format;std::map<std::string,std::string> metadata;Channels values;std::vector<double> positions;
    while(std::getline(input,line)){const auto row=tokens(line);if(row.empty())continue;const auto& key=row[0];
        if(!identified){ensure(row.size()==1&&key=="CAL","Not an Argyll CAL file");identified=true;continue;}
        if(key=="END_DATA_FORMAT"){ensure(formatting,"Misplaced CAL format end");formatting=false;continue;}
        if(formatting){format.insert(format.end(),row.begin(),row.end());continue;}
        if(key=="END_DATA"){ensure(data,"Misplaced CAL data end");ended=true;break;}
        if(data){ensure(fields==4&&format.size()==4&&row.size()==4,"CAL requires four RGB fields");const auto find=[&](const char* name){const auto at=std::find(format.begin(),format.end(),name);ensure(at!=format.end(),"CAL is missing an RGB field");return static_cast<size_t>(at-format.begin());};
            positions.push_back(number(row[find("RGB_I")]));values[0].push_back(number(row[find("RGB_R")]));values[1].push_back(number(row[find("RGB_G")]));values[2].push_back(number(row[find("RGB_B")]));ensure(positions.size()<=n,"Too many CAL samples");continue;}
        if(key=="BEGIN_DATA_FORMAT"){ensure(format.empty()&&!formatting,"Duplicate CAL format");formatting=true;continue;}
        if(key=="BEGIN_DATA"){ensure(!data&&n&&format.size()==4,"Incomplete CAL header");std::set<std::string> unique(format.begin(),format.end());ensure(unique.size()==4,"Duplicate CAL field");data=true;continue;}
        if(key=="NUMBER_OF_FIELDS"){ensure(!fields&&row.size()==2&&row[1]=="4","CAL must have exactly four fields");fields=4;continue;}
        if(key=="NUMBER_OF_SETS"){ensure(!n&&row.size()==2,"Invalid CAL sample count");n=count(row[1]);continue;}
        if(row.size()>=2)metadata[key]=row[1];
    }
    ensure(identified&&ended&&positions.size()==n,"Incomplete CAL data");ensure(metadata["DEVICE_CLASS"]=="DISPLAY"&&metadata["COLOR_REP"]=="RGB","Only RGB display CAL files are supported");ensure(metadata["TV_OUTPUT_ENCODING"]!="YES","Video-range CAL files need an output encoding mode not supported by this importer");return normalized(values,0,1,positions);
}
Channels decodeCsv(const std::string& text){
    std::istringstream input(clean(text));std::string line;Channels values;std::vector<double> positions;std::vector<std::string> header;size_t columns=0;bool indexed=false;int model=-1;
    while(std::getline(input,line)){if(line.find_first_not_of(" \t\r")==std::string::npos)continue;const char delimiter=line.find(',')!=std::string::npos?',':'\t';std::vector<std::string> row;std::istringstream fields(line);std::string value;while(std::getline(fields,value,delimiter)){const auto a=value.find_first_not_of(" \t\r\"");const auto b=value.find_last_not_of(" \t\r\"");row.push_back(a==std::string::npos?"":value.substr(a,b-a+1));}
        if(!columns){columns=row.size();const auto first=lower(row[0]);const bool hasHeader=first=="input"||first=="x"||first=="red"||first=="r";
            if(hasHeader){for(const auto& field:row)header.push_back(lower(field));indexed=first=="input"||first=="x";const size_t offset=indexed?1:0;ensure(columns>=offset+3,"CSV header is incomplete");
                for(size_t c=0;c<3;++c){const std::string shortName=c==0?"r":c==1?"g":"b",name=c==0?"red":c==1?"green":"blue";ensure(header[offset+c]==shortName||header[offset+c]==name||header[offset+c]=="output_"+name,"CSV channels must be ordered red, green, blue");}
                for(size_t c=0;c<header.size();++c)if(header[c]=="model")model=static_cast<int>(c);continue;}
            ensure(columns==3||columns==4,"CSV needs RGB or input/RGB columns");indexed=columns==4;}
        ensure(row.size()==columns,"Inconsistent CSV row length");if(model>=0)ensure(row[model].find("hdr")==std::string::npos,"This CSV describes an HDR/PQ transform, rather than a normalized creative look");
        const size_t offset=indexed?1:0;if(indexed)positions.push_back(number(row[0]));for(size_t c=0;c<3;++c)values[c].push_back(number(row[offset+c]));ensure(values[0].size()<=MaxSource,"Too many CSV samples");
    }
    return normalized(values,0,1,positions);
}
Loaded read(const std::filesystem::path& path){
    const auto extension=lower(path.extension().string());if(extension==".png"||extension==".tif"||extension==".tiff")return fromImage(path);
    const auto text=textFile(path);Channels values;std::wstring format;
    if(extension==".cube"){values=decode(text);format=L"1D cube";}else if(extension==".spi1d"){values=decodeSpi(text);format=L"OpenColorIO spi1d";}else if(extension==".cal"){values=decodeCal(text);format=L"Argyll RGB display calibration";}else if(extension==".csv"){values=decodeCsv(text);format=L"RGB CSV";}else fail("Unsupported LUT format. Use .cube, .spi1d, .cal, .csv, .png or .tif/.tiff");
    format+=L", "+std::to_wstring(values[0].size())+L" stored samples (source ranges respected; dense tables resampled to at most 4096)";return {std::move(values),format};
}
void write(const std::filesystem::path& path,const Channels& channels){
    color::Adjustments check;check.imported=channels;color::validate(check);ensure(!channels[0].empty(),"Cannot export an empty LUT");const auto ext=lower(path.extension().string());
    if(ext==".png"||ext==".tif"||ext==".tiff"){writeImage(path,channels,ext!=".png");return;}
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17);const size_t n=channels[0].size();
    if(ext==".cube")out<<"TITLE \"MonitorMage creative look\"\nLUT_1D_SIZE "<<n<<"\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 1 1 1\n";
    else if(ext==".spi1d")out<<"Version 1\nFrom 0 1\nLength "<<n<<"\nComponents 3\n{\n";
    else if(ext==".cal")out<<"CAL\nDESCRIPTOR \"MonitorMage creative curves (not measured calibration)\"\nDEVICE_CLASS \"DISPLAY\"\nCOLOR_REP \"RGB\"\nNUMBER_OF_FIELDS 4\nBEGIN_DATA_FORMAT\nRGB_I RGB_R RGB_G RGB_B\nEND_DATA_FORMAT\nNUMBER_OF_SETS "<<n<<"\nBEGIN_DATA\n";
    else if(ext==".csv")out<<"input,red,green,blue\n";else fail("Unsupported LUT export format");
    for(size_t i=0;i<n;++i){const char separator=ext==".csv"?',':' ';if(ext==".csv"||ext==".cal")out<<i/static_cast<double>(n-1)<<separator;out<<channels[0][i]<<separator<<channels[1][i]<<separator<<channels[2][i]<<'\n';}
    if(ext==".spi1d")out<<"}\n";if(ext==".cal")out<<"END_DATA\n";presets::writeBytes(path,out.str(),true);
}
}
