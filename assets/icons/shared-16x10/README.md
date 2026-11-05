# Shared 16:10 state icons

Production artwork for MonitorMage. A single code-native vector design replaces the independently generated monitor silhouettes, using the previously preferred graphite-frame direction.

- Outer body: full canvas width, height = width * 10/16; stand excluded from ratio.
- Shared frame, screen and stand geometry in all six states.
- Transparent exterior; near-black opaque screen. No exterior glow or shadow.
- Curve, pause, HDR sun and application diamond are separate state content.
- ICO sizes: 16, 20, 24, 32, 40, 48, 64, 96, 128 and 256. Each rendered directly, with minimum stroke widths and four-ray suns at 16/20 pixels.
- SVG/PNG masters are 512 pixels; ICO frames use their own size-aware source geometry.
- `measurements.json` records alpha >=128 silhouette bounds and nominal body height for every state and frame.
- `comparison.html` displays the actual frame artwork at native size; `comparison.png` is the overview.

Rebuild with Node and Sharp: `node tools/build_shared_icons.cjs <absolute path to sharp module>`. The app has no runtime dependency on either. The builder checks full-width occupancy and matching state bounds before producing the comparison. Earlier raster artwork remains untouched in the other icon folders.
