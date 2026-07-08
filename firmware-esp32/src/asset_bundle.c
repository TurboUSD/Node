// asset_bundle.c — brings the generated LVGL assets into the compiled source set.
//
// The assets live in ../assets/ (kept separate from hand-written code) and used
// to be added to the build via platformio.ini's `build_src_filter`. But the
// pioarduino Hybrid Compile path (the from-source IDF build that lets
// custom_sdkconfig take effect) IGNORES build_src_filter — PlatformIO even warns
// "the 'src_filter' option cannot be used with ESP-IDF". So the assets have to
// reach the compiler through a file that is actually inside src/.
//
// #including the generated .c files here compiles them exactly ONCE, as part of
// this translation unit. The matching `+<../assets/...>` entries were removed
// from build_src_filter so nothing tries to compile them a second time (which
// would be a duplicate-symbol link error). Each asset only defines its own
// uniquely-named data + descriptor, so combining them in one unit is safe.
#include "../assets/turbousd_logo.c"
#include "../assets/montserrat_clock.c"
