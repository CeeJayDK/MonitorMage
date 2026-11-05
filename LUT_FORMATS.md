# LUT interchange research and support

Checked 22 October 2026. File extension identifies a container, not whether a transform is 1D or 3D.

## What established tools use

| Tool / ecosystem | Primary-source finding | Consequence for MonitorMage |
|---|---|---|
| ReShade's standard LUT.fx | PNG texture containing adjacent slices of an RGB cube; texture width is tile size times tile count. | It is normally a 3D transform. A tiled image can be reduced only when every red value depends on red input alone, and likewise for green/blue. |
| BX-Shade CubeLUT1D.fx, listed in ReShade's EffectPackages.ini | Loads a true 1D `.cube` into an RGBA32F texture1D and samples each output channel using its corresponding input channel. | Its underlying normalized, nondecreasing 1D tables fit the existing cube importer. The shader's separate chroma/luma blend controls are not part of the file and are not reproduced. |
| OtisFX MultiLUT.fx, also listed in EffectPackages.ini | Stacks several tiled 3D LUTs vertically in one PNG atlas and selects a band in the shader. | This atlas container is not currently supported. Individual extracted tiles would still require exact channel separability; the shader's chroma/luma controls are separate processing. |
| DaVinci Resolve | The official manual describes both cube LUTs and a 1D shaper followed by a 3D LUT. | Read true 1D cube data and its input-range metadata. A mixed shaper/3D file is not a single per-channel curve. |
| OpenColorIO / Nuke / Blender / Houdini / Fusion | OCIO documents spi1d and cube among its formats; the ecosystem integrates OCIO transforms. Fusion's manual also lists spi1d and other OCIO formats. | Add spi1d, including one-/three-component tables and explicit input domains, without bundling the full OCIO runtime. |
| ArgyllCMS / DisplayCAL calibration workflow | CAL contains RGB input coordinates and three normalized output curves; coordinates need not be uniformly spaced. | Import RGB DISPLAY calibration data with explicit input indexing. Adding it as a look does not replace captured calibration. |
| darktable | Its LUT 3D module accepts cube, 3dl, Hald PNG and gmz. | A Hald image is a 3D layout; only mathematically separable instances fit our hardware backend. |

Primary references:

- [ReShade LUT.fx source](https://github.com/crosire/reshade-shaders/blob/master/Shaders/LUT.fx)
- [ReShade effect package list](https://github.com/crosire/reshade-shaders/blob/list/EffectPackages.ini)
- [BX-Shade CubeLUT1D.fx source](https://github.com/liuxd17thu/BX-Shade/blob/main/Shaders/CubeLUT1D.fx)
- [OtisFX MultiLUT.fx source](https://github.com/FransBouma/OtisFX/blob/master/Shaders/MultiLUT.fx)
- [Resolve reference manual, LUT discussion](https://documents.blackmagicdesign.com/UserManuals/DaVinci_Resolve_11_Reference_Manual.pdf)
- [OCIO tools, supported LUT formats and integrations](https://opencolorio.readthedocs.io/en/stable/guides/using_ocio/using_ocio.html)
- [Fusion tool reference, OCIO formats](https://documents.blackmagicdesign.com/sa/UserManuals/Fusion7_Tool_Reference.pdf)
- [Argyll CAL specification](https://www.argyllcms.com/doc/cal_format.html)
- [darktable LUT 3D manual](https://docs.darktable.org/usermanual/development/en/module-reference/processing-modules/lut-3d/)
- [ColourSpace's ReShade PNG explanation](https://lightillusion.com/reshade_guide.html)

## Implemented in 0.15

- **cube 1D:** 2-262144 source samples, RGB rows; DOMAIN_MIN/MAX or Resolve LUT_1D_INPUT_RANGE metadata. Different per-channel domains, mixed range declarations, shaper-plus-3D and 3D cube files are rejected.
- **spi1d:** Version 1, Components 1 or 3, Length 2-262144, explicit From range. Single-component data applies to RGB. Extended input ranges are evaluated on the application's normalized 0..1 signal, rather than discarded or renormalized blindly.
- **Argyll CAL:** first RGB DISPLAY table, RGB_I/R/G/B fields in declared order, nonuniform input indices supported. Input indices must span 0..1. Video-range encoding is not supported. Extra internal tables after END_DATA are ignored per the format specification.
- **CSV:** RGB rows, or input/RGB rows with strictly increasing input coordinates spanning 0..1. Comma or tab separated. Optional common RGB header names; MonitorMage's SDR diagnostic CSV header is also recognized. Explicit HDR/PQ diagnostic CSV is rejected as a creative look.
- **PNG/TIFF:** horizontal or vertical RGB ramps; repeated identical rows/columns are accepted. Native Windows WIC decoding retains 8/16-bit data; floating-point TIFF data is retained and must be finite and normalized. Images must be opaque and single-frame. Embedded ICC/gamma metadata does not alter the numerical LUT samples. TIFF orientation metadata is not applied; input order is raw left-to-right or top-to-bottom.
- **ReShade/Hald PNG/TIFF:** recognized tiled/square layouts are checked at every entry. Exactly channel-separable transforms are extracted; actual cross-channel looks are rejected. This is a proof of representability, not sampling the gray diagonal and calling it equivalent.
- **Export:** cube, spi1d, CAL, CSV and 16-bit horizontal PNG/TIFF ramps. Exported CAL data is labelled as creative curves, not measured calibration.

All imported curves must remain nondecreasing and produce values in 0..1 over the operating input range. Dense tables are resampled to at most 4096 stored samples with linear interpolation; source monotonicity is validated first. The physical gamma ramp still has 256 entries per channel. Text/image source files are capped at 16 MiB; decoded images at four million pixels. Parsing, decoding and resampling happen on import, with no idle work.

Presets with more than 1024 imported samples save as version 4; earlier-size extended looks retain version 3. The reader supports versions 1-4, with a 1 MiB file limit. Data is embedded rather than retaining paths to source LUTs. Existing calibration remains composed last.

Other extensions such as `.lut`, `.look`, `.3dl`, `.csp` and ICC profiles have distinct or ambiguous structures and are not assumed to be generic curve data. A specific format should be implemented from its specification rather than guessed from the extension.

## Samples and redistribution

The bundle's `luts` directory contains four original CC0 curve examples in six formats, plus two unchanged upstream OpenColorIO technical LUTs under BSD-3-Clause. See `luts/README.md` for intent, limits, exact licence texts and provenance. Upstream files were pinned to commit `22dd9c4faa0d1b7cfb3d64d1a6e9cb454acc6cfd`; hashes are recorded locally. Free-to-download commercial film LUTs were not assumed to grant redistribution rights.

Tests use real upstream SPI files, including dense identity curves, extended-domain sRGB decoding and out-of-range transforms that must be refused. Independent PNG/TIFF fixtures cover 8/16/32-bit samples, raw gamma metadata, both ramp orientations, repeated rows, separable and coupled 3D layouts, opacity and multipage rejection. All six export formats are read back and compared numerically. Tests do not change display state.
