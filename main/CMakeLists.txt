idf_component_register(
    SRCS "main.c" "smf.c" "midi_parse.c" "recorder.c" "midi_usb.c" "midi_din.c"
         "storage.c" "config.c" "net.c" "web.c" "archive.c" "ota.c"
         "led.c" "led_pattern.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "www/index.html" "www/setup.html")
# Paths are built with bounded snprintf; truncation is the intended behaviour for odd names.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format-truncation)
