idf_component_register(
    SRCS "astrolabe_ui.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(${LVGL_LIB} PRIVATE -DLV_LVGL_H_INCLUDE_SIMPLE)
