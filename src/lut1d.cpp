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
namespace lut1d
{
namespace
{
using Microsoft::WRL::ComPtr;
constexpr size_t MAX_FILE_BYTES = 16 * 1024 * 1024, MAX_SOURCE_ENTRIES = 262144, MAX_STORED_ENTRIES = 4096,
				 MAX_PIXELS = 4 * 1024 * 1024;
[[noreturn]] void ThrowLutError(const char* text)
{
	throw std::runtime_error(text);
}

void Require(bool value, const char* text)
{
	if (!value)
		ThrowLutError(text);
}

std::string StripBomAndComments(const std::string& text)
{
	Require(text.size() <= MAX_FILE_BYTES && text.find('\0') == std::string::npos,
	        "LUT text exceeds 16 MiB or contains invalid text");
	std::istringstream in(text.substr(text.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0));
	std::string line, result;
	while (std::getline(in, line))
	{
		bool quoted = false;
		for (size_t i = 0; i < line.size(); ++i)
		{
			if (line[i] == '"')
				quoted = !quoted;
			if (line[i] == '#' && !quoted)
			{
				line.resize(i);
				break;
			}
		}
		result += line + '\n';
	}
	return result;
}

std::vector<std::string> SplitTokens(const std::string& line)
{
	std::istringstream in(line);
	in.imbue(std::locale::classic());
	std::vector<std::string> result;
	std::string value;
	while (in >> std::quoted(value))
		result.push_back(value);
	Require(in.eof(), "Invalid quoted LUT field");
	return result;
}

double ParseNumber(const std::string& text)
{
	std::istringstream in(text);
	in.imbue(std::locale::classic());
	double value;
	Require(static_cast<bool>(in >> value) && std::isfinite(value), "Invalid or nonfinite LUT number");
	in >> std::ws;
	Require(in.eof(), "Extra characters in LUT number");
	return value;
}

size_t ParseSampleCount(const std::string& text)
{
	const double value = ParseNumber(text);
	Require(value >= 2 && value <= MAX_SOURCE_ENTRIES && value == std::floor(value),
	        "LUT length must be 2 to 262144 samples");
	return static_cast<size_t>(value);
}

void ValidateSource(const Channels& values)
{
	const auto n = values[0].size();
	Require(n >= 2 && n <= MAX_SOURCE_ENTRIES, "Invalid LUT sample count");
	for (const auto& channel : values)
	{
		Require(channel.size() == n, "Unequal LUT channel lengths");
		double previous = -INFINITY;
		for (double value : channel)
		{
			Require(std::isfinite(value) && value >= previous, "LUT curves must be finite and nondecreasing");
			previous = value;
		}
	}
}

Channels NormalizeChannels(const Channels& values, double low = 0, double high = 1,
                           const std::vector<double>& positions = {})
{
	ValidateSource(values);
	Require(std::isfinite(low) && std::isfinite(high) && high > low, "Invalid LUT input range");
	const size_t n = values[0].size(), stored = std::min(n, MAX_STORED_ENTRIES);
	Channels out;
	if (!positions.empty())
	{
		Require(positions.size() == n && positions.front() == 0 && positions.back() == 1,
		        "Indexed LUT inputs must cover 0..1");
		for (size_t i = 0; i < n; ++i)
			Require(std::isfinite(positions[i]) && (i == 0 || positions[i] > positions[i - 1]),
			        "LUT input indices must be finite and strictly increasing");
	}
	for (size_t i = 0; i < stored; ++i)
	{
		const double x = i / static_cast<double>(stored - 1);
		size_t a, b;
		double fraction;
		if (positions.empty())
		{
			const double at = std::clamp((x - low) / (high - low), 0.0, 1.0) * (n - 1);
			a = static_cast<size_t>(at);
			b = std::min(a + 1, n - 1);
			fraction = at - a;
		}
		else
		{
			const auto upper = std::upper_bound(positions.begin(), positions.end(), x);
			b = std::min(static_cast<size_t>(upper - positions.begin()), n - 1);
			a = b ? b - 1 : 0;
			fraction = a == b ? 0 : (x - positions[a]) / (positions[b] - positions[a]);
		}
		for (size_t c = 0; c < 3; ++c)
		{
			double value = values[c][a] + fraction * (values[c][b] - values[c][a]);
			Require(
				value >= -1e-12 && value <= 1 + 1e-12,
				"This LUT produces out-of-range values on 0..1 input; it cannot be used as a normalized hardware curve");
			out[c].push_back(std::clamp(value, 0.0, 1.0));
		}
	}
	color::Adjustments check;
	check.Imported = out;
	color::Validate(check);
	return out;
}

std::string ToLower(std::string value)
{
	for (auto& c : value)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return value;
}

std::string ReadTextFile(const std::filesystem::path& path)
{
	std::ifstream input(path, std::ios::binary);
	Require(static_cast<bool>(input), "Cannot open LUT file");
	input.seekg(0, std::ios::end);
	const auto size = input.tellg();
	Require(size >= 0 && static_cast<uint64_t>(size) <= MAX_FILE_BYTES, "LUT file exceeds 16 MiB");
	input.seekg(0);
	std::string text(static_cast<size_t>(size), '\0');
	Require(static_cast<bool>(input.read(text.data(), static_cast<std::streamsize>(text.size()))),
	        "Cannot read complete LUT file");
	return text;
}

void ThrowIfFailed(HRESULT result, const char* text)
{
	if (FAILED(result))
		ThrowLutError(text);
}

struct Apartment
{
	HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	Apartment()
	{
		Require(SUCCEEDED(result) || result == RPC_E_CHANGED_MODE, "Cannot initialize Windows image decoder");
	}
	~Apartment()
	{
		if (SUCCEEDED(result))
			CoUninitialize();
	}
};

struct Image
{
	UINT width = 0, height = 0;
	std::vector<std::array<double, 3>> pixels;
};

Image ReadImageFile(const std::filesystem::path& path)
{
	Require(std::filesystem::file_size(path) <= MAX_FILE_BYTES, "Image LUT exceeds 16 MiB");
	Apartment apartment;
	ComPtr<IWICImagingFactory> factory;
	ThrowIfFailed(
		CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf())),
		"Windows image decoder is unavailable");
	ComPtr<IWICBitmapDecoder> decoder;
	ThrowIfFailed(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
	                                                 WICDecodeMetadataCacheOnDemand, &decoder),
	              "Cannot decode image LUT");
	GUID container {};
	ThrowIfFailed(decoder->GetContainerFormat(&container), "Cannot identify image LUT");
	Require(container == GUID_ContainerFormatPng || container == GUID_ContainerFormatTiff,
	        "Only PNG and TIFF image LUTs are supported");
	UINT frames = 0;
	ThrowIfFailed(decoder->GetFrameCount(&frames), "Cannot inspect image frames");
	Require(frames == 1, "Image LUT must contain exactly one frame/page");
	ComPtr<IWICBitmapFrameDecode> frame;
	ThrowIfFailed(decoder->GetFrame(0, &frame), "Cannot read image frame");
	Image image;
	ThrowIfFailed(frame->GetSize(&image.width, &image.height), "Cannot inspect image dimensions");
	const size_t pixels = static_cast<size_t>(image.width) * image.height;
	Require(image.width && image.height && image.width <= 262144 && image.height <= 262144 && pixels <= MAX_PIXELS,
	        "Image LUT exceeds four million pixels or has invalid dimensions");
	WICPixelFormatGUID format {};
	ThrowIfFailed(frame->GetPixelFormat(&format), "Cannot inspect image pixel format");
	ComPtr<IWICComponentInfo> component;
	ThrowIfFailed(factory->CreateComponentInfo(format, &component), "Cannot inspect image format");
	ComPtr<IWICPixelFormatInfo2> info;
	ThrowIfFailed(component.As(&info), "Cannot inspect image precision");
	WICPixelFormatNumericRepresentation numeric {};
	ThrowIfFailed(info->GetNumericRepresentation(&numeric), "Cannot inspect image numeric representation");
	const bool floating = numeric == WICPixelFormatNumericRepresentationFloat;
	Require(floating || numeric == WICPixelFormatNumericRepresentationUnsignedInteger ||
	            numeric == WICPixelFormatNumericRepresentationIndexed,
	        "Unsupported image numeric representation");
	ComPtr<IWICFormatConverter> converter;
	ThrowIfFailed(factory->CreateFormatConverter(&converter), "Cannot create image format converter");
	ThrowIfFailed(converter->Initialize(frame.Get(),
	                                    floating ? GUID_WICPixelFormat128bppRGBAFloat : GUID_WICPixelFormat64bppRGBA,
	                                    WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom),
	              "Cannot decode image samples without reducing precision");
	image.pixels.resize(pixels);
	if (floating)
	{
		std::vector<float> raw(pixels * 4);
		ThrowIfFailed(converter->CopyPixels(nullptr, image.width * 16, static_cast<UINT>(raw.size() * sizeof(float)),
		                                    reinterpret_cast<BYTE*>(raw.data())),
		              "Cannot read floating-point image samples");
		for (size_t i = 0; i < pixels; ++i)
		{
			Require(std::isfinite(raw[i * 4 + 3]) && raw[i * 4 + 3] == 1, "Image LUT must be fully opaque");
			for (size_t c = 0; c < 3; ++c)
			{
				const double value = raw[i * 4 + c];
				Require(std::isfinite(value) && value >= 0 && value <= 1,
				        "Image LUT values must be finite and normalized to 0..1");
				image.pixels[i][c] = value;
			}
		}
	}
	else
	{
		std::vector<uint16_t> raw(pixels * 4);
		ThrowIfFailed(converter->CopyPixels(nullptr, image.width * 8, static_cast<UINT>(raw.size() * sizeof(uint16_t)),
		                                    reinterpret_cast<BYTE*>(raw.data())),
		              "Cannot read image samples");
		for (size_t i = 0; i < pixels; ++i)
		{
			Require(raw[i * 4 + 3] == 65535, "Image LUT must be fully opaque");
			for (size_t c = 0; c < 3; ++c)
				image.pixels[i][c] = raw[i * 4 + c] / 65535.0;
		}
	}
	// No ICC colour transform, gamma correction, orientation transform or dithering:
	// a LUT image stores numerical data, rather than a photograph to display.
	return image;
}

Loaded LutFromImage(const std::filesystem::path& path)
{
	const auto image = ReadImageFile(path);
	const size_t w = image.width, h = image.height;
	Channels channels;
	const auto at = [&](size_t x, size_t y) -> const std::array<double, 3>&
	{
		return image.pixels[y * w + x];
	};
	bool horizontal = w >= 2, vertical = h >= 2;
	for (size_t y = 0; y < h && horizontal; ++y)
		for (size_t x = 0; x < w; ++x)
			if (at(x, y) != at(x, 0))
			{
				horizontal = false;
				break;
			}
	for (size_t y = 0; y < h && vertical; ++y)
		for (size_t x = 0; x < w; ++x)
			if (at(x, y) != at(0, y))
			{
				vertical = false;
				break;
			}
	std::wstring layout;
	if (horizontal || vertical)
	{
		const size_t n = horizontal ? w : h;
		for (size_t i = 0; i < n; ++i)
			for (size_t c = 0; c < 3; ++c)
				channels[c].push_back((horizontal ? at(i, 0) : at(0, i))[c]);
		layout = horizontal ? L"horizontal image ramp" : L"vertical image ramp";
	}
	else
	{
		size_t n = 0;
		bool hald = false;
		if (w == h * h && h >= 2)
			n = h;
		else if (w == h)
		{
			for (size_t level = 2; level <= 16; ++level)
				if (level * level * level == w)
				{
					n = level * level;
					hald = true;
					break;
				}
		}
		Require(n >= 2, "Image is not a 1D ramp or a recognized ReShade/Hald LUT layout");
		const auto pixel = [&](size_t r, size_t g, size_t b) -> const std::array<double, 3>&
		{
			const size_t index = r + g * n + b * n * n;
			return hald ? image.pixels[index] : at(r + b * n, g);
		};
		for (size_t i = 0; i < n; ++i)
		{
			channels[0].push_back(pixel(i, 0, 0)[0]);
			channels[1].push_back(pixel(0, i, 0)[1]);
			channels[2].push_back(pixel(0, 0, i)[2]);
		}
		for (size_t b = 0; b < n; ++b)
			for (size_t g = 0; g < n; ++g)
				for (size_t r = 0; r < n; ++r)
				{
					const auto& p = pixel(r, g, b);
					Require(
						p[0] == channels[0][r] && p[1] == channels[1][g] && p[2] == channels[2][b],
						"This image is a genuine 3D LUT with channel mixing. It cannot be represented by the current 1D hardware backend");
				}
		layout =
			hald ? L"Hald image (verified separable into 1D)" : L"ReShade tiled image (verified separable into 1D)";
	}
	const auto count = channels[0].size();
	auto out = NormalizeChannels(channels);
	return { std::move(out), layout + L", " + std::to_wstring(count) + L" source samples" +
		                         (count > MAX_STORED_ENTRIES ? L" (resampled to 4096)" : L"") };
}

void WriteImage(const std::filesystem::path& path, const Channels& values, bool tiff)
{
	Apartment apartment;
	ComPtr<IWICImagingFactory> factory;
	ThrowIfFailed(
		CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf())),
		"Cannot create Windows image encoder");
	ComPtr<IStream> stream;
	ThrowIfFailed(CreateStreamOnHGlobal(nullptr, TRUE, &stream), "Cannot create image output buffer");
	ComPtr<IWICBitmapEncoder> encoder;
	ThrowIfFailed(factory->CreateEncoder(tiff ? GUID_ContainerFormatTiff : GUID_ContainerFormatPng, nullptr, &encoder),
	              "Cannot create image encoder");
	ThrowIfFailed(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Cannot initialize image encoder");
	ComPtr<IWICBitmapFrameEncode> frame;
	ComPtr<IPropertyBag2> properties;
	ThrowIfFailed(encoder->CreateNewFrame(&frame, &properties), "Cannot create image frame");
	ThrowIfFailed(frame->Initialize(properties.Get()), "Cannot initialize image frame");
	const UINT n = static_cast<UINT>(values[0].size());
	ThrowIfFailed(frame->SetSize(n, 1), "Cannot set image dimensions");
	WICPixelFormatGUID format = GUID_WICPixelFormat64bppRGBA;
	ThrowIfFailed(frame->SetPixelFormat(&format), "Cannot set 16-bit image format");
	Require(format == GUID_WICPixelFormat64bppRGBA, "Image encoder does not support 16-bit RGBA");
	std::vector<uint16_t> pixels(n * 4);
	for (size_t i = 0; i < n; ++i)
	{
		for (size_t c = 0; c < 3; ++c)
			pixels[i * 4 + c] = static_cast<uint16_t>(std::lround(values[c][i] * 65535));
		pixels[i * 4 + 3] = 65535;
	}
	ThrowIfFailed(
		frame->WritePixels(1, n * 8, static_cast<UINT>(pixels.size() * 2), reinterpret_cast<BYTE*>(pixels.data())),
		"Cannot encode image pixels");
	ThrowIfFailed(frame->Commit(), "Cannot commit image frame");
	ThrowIfFailed(encoder->Commit(), "Cannot finish image encoding");
	STATSTG stat {};
	ThrowIfFailed(stream->Stat(&stat, STATFLAG_NONAME), "Cannot measure encoded image");
	Require(stat.cbSize.QuadPart <= MAX_FILE_BYTES, "Encoded image is too large");
	LARGE_INTEGER start {};
	ThrowIfFailed(stream->Seek(start, STREAM_SEEK_SET, nullptr), "Cannot rewind image output");
	std::string bytes(static_cast<size_t>(stat.cbSize.QuadPart), '\0');
	ULONG read = 0;
	ThrowIfFailed(stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read), "Cannot read encoded image");
	Require(read == bytes.size(), "Incomplete encoded image");
	presets::WriteBytes(path, bytes, true);
}
}

Channels DecodeCube(const std::string& text)
{
	std::istringstream input(StripBomAndComments(text));
	std::string line;
	size_t n = 0;
	Channels values;
	double low = 0, high = 1;
	std::set<std::string> headers;
	while (std::getline(input, line))
	{
		auto row = SplitTokens(line);
		if (row.empty())
			continue;
		const auto& key = row[0];
		if (key == "TITLE")
		{
			Require(values[0].empty(), "LUT metadata must precede samples");
			continue;
		}
		if (key == "LUT_3D_SIZE")
			ThrowLutError("This is a 3D LUT. The current hardware backend accepts 1D transforms only");
		if (key == "LUT_1D_SIZE")
		{
			Require(row.size() == 2 && !n && values[0].empty(), "Invalid or duplicate LUT size");
			n = ParseSampleCount(row[1]);
			continue;
		}
		if (key == "DOMAIN_MIN" || key == "DOMAIN_MAX" || key == "LUT_1D_INPUT_RANGE")
		{
			Require(values[0].empty() && headers.insert(key).second, "Duplicate or misplaced LUT range");
			if (key == "LUT_1D_INPUT_RANGE")
			{
				Require(row.size() == 3, "Invalid input range");
				low = ParseNumber(row[1]);
				high = ParseNumber(row[2]);
			}
			else
			{
				Require(row.size() == 4, "Invalid LUT domain");
				const double value = ParseNumber(row[1]);
				Require(ParseNumber(row[2]) == value && ParseNumber(row[3]) == value,
				        "Different channel domains are not supported");
				(key == "DOMAIN_MIN" ? low : high) = value;
			}
			continue;
		}
		Require(n && row.size() == 3, "A 1D cube needs a size followed by three numbers per sample");
		for (size_t c = 0; c < 3; ++c)
			values[c].push_back(ParseNumber(row[c]));
		Require(values[0].size() <= n, "Too many LUT samples");
	}
	Require(n && values[0].size() == n, "Incomplete 1D cube LUT");
	Require(!headers.count("LUT_1D_INPUT_RANGE") || (!headers.count("DOMAIN_MIN") && !headers.count("DOMAIN_MAX")),
	        "Conflicting LUT input ranges");
	return NormalizeChannels(values, low, high);
}

Channels DecodeSpi(const std::string& text)
{
	auto source = StripBomAndComments(text);
	for (size_t at = 0; (at = source.find_first_of("{}", at)) != std::string::npos; at += 3)
		source.replace(at, 1, " " + source.substr(at, 1) + " ");
	const auto words = SplitTokens(source);
	size_t i = 0, n = 0, components = 0;
	double low = 0, high = 1;
	std::set<std::string> seen;
	while (i < words.size() && words[i] != "{")
	{
		const auto key = words[i++];
		Require(seen.insert(key).second, "Duplicate spi1d header");
		if (key == "From")
		{
			Require(i + 1 < words.size(), "Incomplete spi1d range");
			low = ParseNumber(words[i++]);
			high = ParseNumber(words[i++]);
		}
		else
		{
			Require(i < words.size(), "Incomplete spi1d header");
			const auto value = words[i++];
			if (key == "Version")
				Require(value == "1", "Unsupported spi1d version");
			else if (key == "Length")
				n = ParseSampleCount(value);
			else if (key == "Components")
			{
				Require(value == "1" || value == "3", "Only one- and three-component spi1d LUTs are supported");
				components = value == "1" ? 1 : 3;
			}
			else
				ThrowLutError("Unknown spi1d header");
		}
	}
	Require(seen.count("Version") && seen.count("From") && n && components && i < words.size() && words[i++] == "{",
	        "Incomplete spi1d header");
	Channels values;
	for (size_t sample = 0; sample < n; ++sample)
	{
		std::array<double, 3> value {};
		for (size_t c = 0; c < components; ++c)
		{
			Require(i < words.size(), "Incomplete spi1d data");
			value[c] = ParseNumber(words[i++]);
		}
		for (size_t c = 0; c < 3; ++c)
			values[c].push_back(value[components == 1 ? 0 : c]);
	}
	Require(i < words.size() && words[i++] == "}" && i == words.size(), "Extra or incomplete spi1d data");
	return NormalizeChannels(values, low, high);
}

Channels DecodeCal(const std::string& text)
{
	std::istringstream input(StripBomAndComments(text));
	std::string line;
	bool identified = false, formatting = false, data = false, ended = false;
	size_t fields = 0, n = 0;
	std::vector<std::string> format;
	std::map<std::string, std::string> metadata;
	Channels values;
	std::vector<double> positions;
	while (std::getline(input, line))
	{
		const auto row = SplitTokens(line);
		if (row.empty())
			continue;
		const auto& key = row[0];
		if (!identified)
		{
			Require(row.size() == 1 && key == "CAL", "Not an Argyll CAL file");
			identified = true;
			continue;
		}
		if (key == "END_DATA_FORMAT")
		{
			Require(formatting, "Misplaced CAL format end");
			formatting = false;
			continue;
		}
		if (formatting)
		{
			format.insert(format.end(), row.begin(), row.end());
			continue;
		}
		if (key == "END_DATA")
		{
			Require(data, "Misplaced CAL data end");
			ended = true;
			break;
		}
		if (data)
		{
			Require(fields == 4 && format.size() == 4 && row.size() == 4, "CAL requires four RGB fields");
			const auto find = [&](const char* name)
			{
				const auto at = std::find(format.begin(), format.end(), name);
				Require(at != format.end(), "CAL is missing an RGB field");
				return static_cast<size_t>(at - format.begin());
			};
			positions.push_back(ParseNumber(row[find("RGB_I")]));
			values[0].push_back(ParseNumber(row[find("RGB_R")]));
			values[1].push_back(ParseNumber(row[find("RGB_G")]));
			values[2].push_back(ParseNumber(row[find("RGB_B")]));
			Require(positions.size() <= n, "Too many CAL samples");
			continue;
		}
		if (key == "BEGIN_DATA_FORMAT")
		{
			Require(format.empty() && !formatting, "Duplicate CAL format");
			formatting = true;
			continue;
		}
		if (key == "BEGIN_DATA")
		{
			Require(!data && n && format.size() == 4, "Incomplete CAL header");
			std::set<std::string> unique(format.begin(), format.end());
			Require(unique.size() == 4, "Duplicate CAL field");
			data = true;
			continue;
		}
		if (key == "NUMBER_OF_FIELDS")
		{
			Require(!fields && row.size() == 2 && row[1] == "4", "CAL must have exactly four fields");
			fields = 4;
			continue;
		}
		if (key == "NUMBER_OF_SETS")
		{
			Require(!n && row.size() == 2, "Invalid CAL sample count");
			n = ParseSampleCount(row[1]);
			continue;
		}
		if (row.size() >= 2)
			metadata[key] = row[1];
	}
	Require(identified && ended && positions.size() == n, "Incomplete CAL data");
	Require(metadata["DEVICE_CLASS"] == "DISPLAY" && metadata["COLOR_REP"] == "RGB",
	        "Only RGB display CAL files are supported");
	Require(metadata["TV_OUTPUT_ENCODING"] != "YES",
	        "Video-range CAL files need an output encoding mode not supported by this importer");
	return NormalizeChannels(values, 0, 1, positions);
}

Channels DecodeCsv(const std::string& text)
{
	std::istringstream input(StripBomAndComments(text));
	std::string line;
	Channels values;
	std::vector<double> positions;
	std::vector<std::string> header;
	size_t columns = 0;
	bool indexed = false;
	int model = -1;
	while (std::getline(input, line))
	{
		if (line.find_first_not_of(" \t\r") == std::string::npos)
			continue;
		const char delimiter = line.find(',') != std::string::npos ? ',' : '\t';
		std::vector<std::string> row;
		std::istringstream fields(line);
		std::string value;
		while (std::getline(fields, value, delimiter))
		{
			const auto a = value.find_first_not_of(" \t\r\"");
			const auto b = value.find_last_not_of(" \t\r\"");
			row.push_back(a == std::string::npos ? "" : value.substr(a, b - a + 1));
		}
		if (!columns)
		{
			columns = row.size();
			const auto first = ToLower(row[0]);
			const bool has_header = first == "input" || first == "x" || first == "red" || first == "r";
			if (has_header)
			{
				for (const auto& field : row)
					header.push_back(ToLower(field));
				indexed = first == "input" || first == "x";
				const size_t offset = indexed ? 1 : 0;
				Require(columns >= offset + 3, "CSV header is incomplete");
				for (size_t c = 0; c < 3; ++c)
				{
					const std::string short_name = c == 0   ? "r"
					                               : c == 1 ? "g"
					                                        : "b",
									  name = c == 0   ? "red"
					                         : c == 1 ? "green"
					                                  : "blue";
					Require(header[offset + c] == short_name || header[offset + c] == name ||
					            header[offset + c] == "output_" + name,
					        "CSV channels must be ordered red, green, blue");
				}
				for (size_t c = 0; c < header.size(); ++c)
					if (header[c] == "model")
						model = static_cast<int>(c);
				continue;
			}
			Require(columns == 3 || columns == 4, "CSV needs RGB or input/RGB columns");
			indexed = columns == 4;
		}
		Require(row.size() == columns, "Inconsistent CSV row length");
		if (model >= 0)
			Require(row[model].find("hdr") == std::string::npos,
			        "This CSV describes an HDR/PQ transform, rather than a normalized creative look");
		const size_t offset = indexed ? 1 : 0;
		if (indexed)
			positions.push_back(ParseNumber(row[0]));
		for (size_t c = 0; c < 3; ++c)
			values[c].push_back(ParseNumber(row[offset + c]));
		Require(values[0].size() <= MAX_SOURCE_ENTRIES, "Too many CSV samples");
	}
	return NormalizeChannels(values, 0, 1, positions);
}

Loaded Read(const std::filesystem::path& path)
{
	const auto extension = ToLower(path.extension().string());
	if (extension == ".png" || extension == ".tif" || extension == ".tiff")
		return LutFromImage(path);
	const auto text = ReadTextFile(path);
	Channels values;
	std::wstring format;
	if (extension == ".cube")
	{
		values = DecodeCube(text);
		format = L"1D cube";
	}
	else if (extension == ".spi1d")
	{
		values = DecodeSpi(text);
		format = L"OpenColorIO spi1d";
	}
	else if (extension == ".cal")
	{
		values = DecodeCal(text);
		format = L"Argyll RGB display calibration";
	}
	else if (extension == ".csv")
	{
		values = DecodeCsv(text);
		format = L"RGB CSV";
	}
	else
		ThrowLutError("Unsupported LUT format. Use .cube, .spi1d, .cal, .csv, .png or .tif/.tiff");
	format += L", " + std::to_wstring(values[0].size()) +
	          L" stored samples (source ranges respected; dense tables resampled to at most 4096)";
	return { std::move(values), format };
}

void Write(const std::filesystem::path& path, const Channels& channels)
{
	color::Adjustments check;
	check.Imported = channels;
	color::Validate(check);
	Require(!channels[0].empty(), "Cannot export an empty LUT");
	const auto extension = ToLower(path.extension().string());
	if (extension == ".png" || extension == ".tif" || extension == ".tiff")
	{
		WriteImage(path, channels, extension != ".png");
		return;
	}
	std::ostringstream out;
	out.imbue(std::locale::classic());
	out << std::setprecision(17);
	const size_t n = channels[0].size();
	if (extension == ".cube")
		out << "TITLE \"MonitorMage creative look\"\nLUT_1D_SIZE " << n << "\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 1 1 1\n";
	else if (extension == ".spi1d")
		out << "Version 1\nFrom 0 1\nLength " << n << "\nComponents 3\n{\n";
	else if (extension == ".cal")
		out << "CAL\nDESCRIPTOR \"MonitorMage creative curves (not measured calibration)\"\nDEVICE_CLASS \"DISPLAY\"\nCOLOR_REP \"RGB\"\nNUMBER_OF_FIELDS 4\nBEGIN_DATA_FORMAT\nRGB_I RGB_R RGB_G RGB_B\nEND_DATA_FORMAT\nNUMBER_OF_SETS "
			<< n << "\nBEGIN_DATA\n";
	else if (extension == ".csv")
		out << "input,red,green,blue\n";
	else
		ThrowLutError("Unsupported LUT export format");
	for (size_t i = 0; i < n; ++i)
	{
		const char separator = extension == ".csv" ? ',' : ' ';
		if (extension == ".csv" || extension == ".cal")
			out << i / static_cast<double>(n - 1) << separator;
		out << channels[0][i] << separator << channels[1][i] << separator << channels[2][i] << '\n';
	}
	if (extension == ".spi1d")
		out << "}\n";
	if (extension == ".cal")
		out << "END_DATA\n";
	presets::WriteBytes(path, out.str(), true);
}
}
