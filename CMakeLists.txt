cmake_minimum_required(VERSION 3.24)

project(MiraView VERSION 0.4.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(MiraView WIN32
    src/main.cpp
    src/App.cpp
    src/App.h
    src/FolderModel.cpp
    src/FolderModel.h
    src/ImageCache.cpp
    src/ImageCache.h
    src/ImageData.h
    src/WicDecoder.cpp
    src/WicDecoder.h
    src/EnhancementWorker.cpp
    src/EnhancementWorker.h
    src/ImageEnhancer.h
    src/NullImageEnhancer.cpp
    src/NullImageEnhancer.h
    resources/MiraView.rc
    resources/app.manifest
    resources/icon/MiraView.ico
    resources/icon/MiraViewIcon.png
)

target_compile_definitions(MiraView PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    target_compile_options(MiraView PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_options(MiraView PRIVATE /MANIFEST:NO)
endif()

target_link_libraries(MiraView PRIVATE
    d2d1
    dwrite
    windowscodecs
    shlwapi
    shell32
    ole32
    comdlg32
    user32
    gdi32
)

set(MIRAVIEW_RTX_SDK_ROOT "${CMAKE_SOURCE_DIR}/.sdk/rtx-video-1.1.0"
    CACHE PATH "Path to NVIDIA RTX Video SDK 1.1 root")

if(EXISTS "${MIRAVIEW_RTX_SDK_ROOT}/include/nvsdk_ngx_helpers_vsr.h"
   AND EXISTS "${MIRAVIEW_RTX_SDK_ROOT}/lib/Windows/x64/nvsdk_ngx_d.lib")
    message(STATUS "MiraView RTX backend: enabled (${MIRAVIEW_RTX_SDK_ROOT})")
    target_sources(MiraView PRIVATE
        src/RtxImageEnhancer.cpp
        src/RtxImageEnhancer.h
    )
    target_compile_definitions(MiraView PRIVATE MIRAVIEW_WITH_RTX=1)
    target_include_directories(MiraView SYSTEM PRIVATE "${MIRAVIEW_RTX_SDK_ROOT}/include")
    target_link_libraries(MiraView PRIVATE
        d3d11
        dxgi
        "$<IF:$<CONFIG:Debug>,${MIRAVIEW_RTX_SDK_ROOT}/lib/Windows/x64/nvsdk_ngx_d_dbg.lib,${MIRAVIEW_RTX_SDK_ROOT}/lib/Windows/x64/nvsdk_ngx_d.lib>"
    )
    add_custom_command(TARGET MiraView POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${MIRAVIEW_RTX_SDK_ROOT}/bin/Windows/x64/dev/nvngx_vsr.dll,${MIRAVIEW_RTX_SDK_ROOT}/bin/Windows/x64/rel/nvngx_vsr.dll>"
            "$<TARGET_FILE_DIR:MiraView>/nvngx_vsr.dll"
        VERBATIM
    )

    add_executable(MiraViewRtxProbe
        tests/RtxProbe.cpp
        src/RtxImageEnhancer.cpp
        src/RtxImageEnhancer.h
        src/ImageEnhancer.h
        src/ImageData.h
    )
    target_compile_definitions(MiraViewRtxProbe PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX MIRAVIEW_WITH_RTX=1)
    target_include_directories(MiraViewRtxProbe PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_include_directories(MiraViewRtxProbe SYSTEM PRIVATE "${MIRAVIEW_RTX_SDK_ROOT}/include")
    target_link_libraries(MiraViewRtxProbe PRIVATE
        d3d11 dxgi
        "$<IF:$<CONFIG:Debug>,${MIRAVIEW_RTX_SDK_ROOT}/lib/Windows/x64/nvsdk_ngx_d_dbg.lib,${MIRAVIEW_RTX_SDK_ROOT}/lib/Windows/x64/nvsdk_ngx_d.lib>"
    )
    if(MSVC)
        target_compile_options(MiraViewRtxProbe PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_custom_command(TARGET MiraViewRtxProbe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${MIRAVIEW_RTX_SDK_ROOT}/bin/Windows/x64/dev/nvngx_vsr.dll,${MIRAVIEW_RTX_SDK_ROOT}/bin/Windows/x64/rel/nvngx_vsr.dll>"
            "$<TARGET_FILE_DIR:MiraViewRtxProbe>/nvngx_vsr.dll"
        VERBATIM
    )

    add_executable(MiraViewHdrPreview WIN32
        src/HdrPreviewMain.cpp
        src/RtxHdrProcessor.cpp
        src/RtxHdrProcessor.h
        src/FolderModel.cpp
        src/FolderModel.h
        src/ImageCache.cpp
        src/ImageCache.h
        src/WicDecoder.cpp
        src/WicDecoder.h
        src/ImageData.h
        resources/MiraView.rc
        resources/app.manifest
        resources/icon/MiraView.ico
    )
    target_compile_definitions(MiraViewHdrPreview PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX MIRAVIEW_WITH_RTX=1
        MIRAVIEW_HDR_PREVIEW=1)
    target_include_directories(MiraViewHdrPreview PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_include_directories(MiraViewHdrPreview SYSTEM PRIVATE "${MIRAVIEW_RTX_SDK_ROOT}/include")
    target_link_libraries(MiraViewHdrPreview PRIVATE
        d3d11 dxgi windowscodecs ole32 comdlg32 shell32 shlwapi user32 gdi32
        "$<IF:$<CONFIG:Debug>,${MIRAVIEW_RTX_SDK_ROOT}/lib/Windows/x64/nvsdk_ngx_d_dbg.lib,${MIRAVIEW_RTX_SDK_ROOT}/lib/Windows/x64/nvsdk_ngx_d.lib>"
    )
    if(MSVC)
        target_compile_options(MiraViewHdrPreview PRIVATE /W4 /permissive- /utf-8 /EHsc)
        target_link_options(MiraViewHdrPreview PRIVATE /MANIFEST:NO)
    endif()
    add_custom_command(TARGET MiraViewHdrPreview POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${MIRAVIEW_RTX_SDK_ROOT}/bin/Windows/x64/dev/nvngx_truehdr.dll,${MIRAVIEW_RTX_SDK_ROOT}/bin/Windows/x64/rel/nvngx_truehdr.dll>"
            "$<TARGET_FILE_DIR:MiraViewHdrPreview>/nvngx_truehdr.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${MIRAVIEW_RTX_SDK_ROOT}/bin/Windows/x64/dev/nvngx_vsr.dll,${MIRAVIEW_RTX_SDK_ROOT}/bin/Windows/x64/rel/nvngx_vsr.dll>"
            "$<TARGET_FILE_DIR:MiraViewHdrPreview>/nvngx_vsr.dll"
        VERBATIM
    )

    add_executable(MiraViewHdrProbe
        tests/HdrProbe.cpp
        src/RtxHdrProcessor.cpp
        src/RtxHdrProcessor.h
        src/ImageData.h
    )
    target_compile_definitions(MiraViewHdrProbe PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX MIRAVIEW_WITH_RTX=1)
    target_include_directories(MiraViewHdrProbe PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_include_directories(MiraViewHdrProbe SYSTEM PRIVATE "${MIRAVIEW_RTX_SDK_ROOT}/include")
    target_link_libraries(MiraViewHdrProbe PRIVATE
        d3d11 dxgi
        "$<IF:$<CONFIG:Debug>,${MIRAVIEW_RTX_SDK_ROOT}/lib/Windows/x64/nvsdk_ngx_d_dbg.lib,${MIRAVIEW_RTX_SDK_ROOT}/lib/Windows/x64/nvsdk_ngx_d.lib>"
    )
    if(MSVC)
        target_compile_options(MiraViewHdrProbe PRIVATE /W4 /permissive- /utf-8 /EHsc)
    endif()
    add_custom_command(TARGET MiraViewHdrProbe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<IF:$<CONFIG:Debug>,${MIRAVIEW_RTX_SDK_ROOT}/bin/Windows/x64/dev/nvngx_truehdr.dll,${MIRAVIEW_RTX_SDK_ROOT}/bin/Windows/x64/rel/nvngx_truehdr.dll>"
            "$<TARGET_FILE_DIR:MiraViewHdrProbe>/nvngx_truehdr.dll"
        VERBATIM
    )
else()
    message(STATUS "MiraView RTX backend: disabled (set MIRAVIEW_RTX_SDK_ROOT)")
    target_compile_definitions(MiraView PRIVATE MIRAVIEW_WITH_RTX=0)
endif()

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT MiraView)

set_target_properties(MiraView PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/out"
)

if(TARGET MiraViewHdrPreview)
    set_target_properties(MiraViewHdrPreview PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/out"
    )
endif()
