# MonitorMage sample LUTs

In MonitorMage select **Imported 1D LUT**, choose **Import 1D LUT...**, and open a sample. The same curve is supplied in several containers so you can verify interchange; import one version, not several copies of the same look. Imported samples are embedded in your look preset.

## Original curves - CC0 1.0

These numerical samples were generated specifically for this project. The CC0 dedication applies only to the sample LUT data in `Original curves`, not to the app's source code or third-party files. See `Original curves/CC0-1.0.txt`.

- **00 - Identity:** unchanged RGB ramp, useful for comparison.
- **01 - Gentle contrast:** endpoint-preserving S-curve, amount 0.12, pivot 0.5.
- **02 - Warm balance:** linear-light RGB gains derived from warmth 0.18 and normalized so the strongest channel is one. It can lower overall brightness.
- **03 - Faded tone:** output black 0.03 / white 0.97 around an S-curve of amount 0.05; green multiplied by 0.99 and blue by 0.96. A simple tonal example, not a measured film-stock emulation.

Each has 1024 samples in `.cube`, `.spi1d`, `.cal`, `.csv`, 16-bit `.png` and 16-bit `.tif`. Images are horizontal strips. Image versions are quantized to 16-bit; text versions retain double precision. Reproduce with the CMake target `LutSampleMaker`, source `tools/make_lut_samples.cpp`. Its command-line argument is a new output directory; it refuses to replace existing sample files. The generator reads all its output back and checks the values.

## OpenColorIO technical - BSD-3-Clause

These original upstream files are redistributed with the OpenColorIO copyright and licence in `OpenColorIO technical/LICENSE`. `PROVENANCE.json` records the exact upstream commit, URLs and SHA-256 hashes. No affiliation or endorsement is implied.

- **lut1d_1.spi1d:** 512-sample identity curve. A compatibility test, not an artistic look.
- **sRGB_to_linear.spi1d:** the sRGB decoding transfer function, stored on an extended input domain with 4101 source samples. MonitorMage evaluates it on 0..1 and stores 4096 samples. This intentionally makes ordinary desktop midtones much darker; it is a technical transfer-function sample, not an HDR gamma fix or a ready-made monitor calibration.

Do not treat these technical files as measured calibrations for your monitor. Importing any file adds a look curve ahead of captured calibration; it does not replace that calibration or install a Windows colour profile.
