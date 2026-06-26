# Assets

## turbousd_logo.c

`ui_manager.h` and `shared_components.h` both reference `LV_IMG_DECLARE(turbousd_logo)`
— an LVGL image asset compiled directly into the firmware binary (no
filesystem/SD card needed for something this small and static).

This repo has the source vector (the eye-in-triangle logo, `TurboUSD.svg`,
already used in the earlier browser simulator) but LVGL needs it as a C
array, not an SVG. To generate `turbousd_logo.c`:

1. Rasterize the SVG to a PNG at the size you actually want on-device
   (48x48 was the size used throughout the simulator's header — start
   there, since every screen's layout was designed around that footprint).

   ```bash
   pip install cairosvg --break-system-packages
   python3 -c "import cairosvg; cairosvg.svg2png(url='TurboUSD.svg', write_to='turbousd_logo.png', output_width=48, output_height=48)"
   ```

2. Run the PNG through LVGL's official image converter:
   https://lvgl.io/tools/imageconverter

   Settings that match what this firmware expects:
   - Color format: `CF_TRUE_COLOR_ALPHA` (keeps the logo's transparency)
   - Output format: C array

3. Drop the resulting `turbousd_logo.c` in this folder, and add it to
   `platformio.ini`'s build (PlatformIO picks up `.c`/`.cpp` files anywhere
   under the project automatically, so no extra config needed beyond
   making sure this `assets/` folder is included — `src_dir` is `src` by
   default, but `build_src_filter` or just moving this file under `src/`
   works too if PlatformIO doesn't pick it up from here).

4. Confirm the generated array name matches what the code expects
   (`turbousd_logo`) — the converter names it after the source filename by
   default, so name the PNG `turbousd_logo.png` going in to get that for free.
