cmake_minimum_required(VERSION 3.16)
project(nh_zh_mod C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(winmm SHARED
    src/winmm_proxy_main.c
    src/cJSON.c
    minhook/src/buffer.c
    minhook/src/hook.c
    minhook/src/trampoline.c
    minhook/src/hde/hde32.c
    minhook/src/hde/hde64.c
    resources/winmm_zh_proxy.rc
    include/resource.h
    locales/zh-CN.json
)

target_include_directories(winmm PRIVATE
    include
    minhook/include
    minhook/src
    minhook/src/hde
)

target_link_libraries(winmm PRIVATE
    user32
    gdi32
    dbghelp
)

target_compile_definitions(winmm PRIVATE
    CJSON_HIDE_SYMBOLS
)

# Keep output name exactly as winmm.dll for drop-in loading.
set_target_properties(winmm PROPERTIES
    PREFIX ""
    OUTPUT_NAME "winmm"
)

# Optional: copy only winmm.dll to a drop-in directory after build.
set(WINMM_DROPIN_DIR "" CACHE PATH "Directory to copy built winmm.dll into")
if(WINMM_DROPIN_DIR)
    add_custom_command(TARGET winmm POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${WINMM_DROPIN_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:winmm>"
            "${WINMM_DROPIN_DIR}/$<TARGET_FILE_NAME:winmm>"
        COMMENT "Copying winmm.dll to ${WINMM_DROPIN_DIR}"
        VERBATIM
    )
endif()

set(RES_FILES "locales/zh-CN.json")

foreach(FILE ${RES_FILES})
    configure_file(${FILE} ${CMAKE_CURRENT_BINARY_DIR}/${FILE} COPYONLY)
endforeach()

# Keep stable export names across toolchains.
if(MSVC)
    target_link_options(winmm PRIVATE
        /DEF:${CMAKE_CURRENT_SOURCE_DIR}/resources/winmm_zh_proxy.def
    )
elseif(MINGW)
    target_link_options(winmm PRIVATE
        -Wl,--add-stdcall-alias
    )
endif()
