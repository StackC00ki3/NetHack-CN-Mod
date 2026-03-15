cmake_minimum_required(VERSION 3.16)
project(nh_zh_mod C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(winmm SHARED
    src/winmm_proxy_main.c
    src/cJSON.c
    resources/winmm_zh_proxy.rc
    include/resource.h
    locales/zh-CN.json
)

target_include_directories(winmm PRIVATE
    include
)

target_link_libraries(winmm PRIVATE
    user32
    gdi32
)

target_compile_definitions(winmm PRIVATE
    CJSON_HIDE_SYMBOLS
)

# Keep output name exactly as winmm.dll for drop-in loading.
set_target_properties(winmm PROPERTIES
    PREFIX ""
    OUTPUT_NAME "winmm"
)

# Keep DEF-based exports for MSVC if desired; MinGW uses dllexport symbols.
if(MSVC)
    target_link_options(winmm PRIVATE
        /DEF:${CMAKE_CURRENT_SOURCE_DIR}/resources/winmm_zh_proxy.def
    )
endif()
