#include "lut1d.h"
#include <iostream>
int wmain(int argc, wchar_t** argv)
{
	try
	{
		if (argc != 2)
			throw std::runtime_error("Pass the Original curves output directory");
		const std::filesystem::path folder(argv[1]);
		std::filesystem::create_directories(folder);
		for (int look = 0; look < 4; ++look)
		{
			const wchar_t* names[] { L"00 - Identity", L"01 - Gentle contrast", L"02 - Warm balance",
				                     L"03 - Faded tone" };
			lut1d::Channels channels;
			color::Adjustments warmth;
			warmth.Balance[color::BALANCE_WARMTH] = 18;
			for (int i = 0; i < 1024; ++i)
			{
				const double x = i / 1023.0;
				for (int c = 0; c < 3; ++c)
				{
					double y = x;
					if (look == 1)
						y = color::ApplyContrast(x, .12, .5);
					if (look == 2)
						y = color::ApplyStageToSignal(x, c, 2, warmth);
					if (look == 3)
						y = (.03 + .94 * color::ApplyContrast(x, .05, .5)) * (c == 0 ? 1 : c == 1 ? .99 : .96);
					channels[c].push_back(y);
				}
			}
			for (const wchar_t* extension : { L".cube", L".spi1d", L".cal", L".csv", L".png", L".tif" })
			{
				const auto path = folder / (std::wstring(names[look]) + extension);
				if (std::filesystem::exists(path))
					throw std::runtime_error("Refusing to replace an existing sample file");
				lut1d::Write(path, channels);
				const auto decoded = lut1d::Read(path).channels;
				if (decoded[0].size() != 1024)
					throw std::runtime_error("Sample readback length changed");
				for (size_t c = 0; c < 3; ++c)
					for (size_t i = 0; i < 1024; ++i)
						if (std::abs(decoded[c][i] - channels[c][i]) > 0.50001 / 65535)
							throw std::runtime_error("Sample readback changed values");
			}
		}
		std::cout << "Created and read back 24 original curve samples in six formats.\n";
		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
