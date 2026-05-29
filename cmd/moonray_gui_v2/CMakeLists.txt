# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

find_package(glfw3 REQUIRED)

# This application is built with Dear ImGui, which is a popular GUI library.
# Because we only need a few headers and source files, we can get away with
# FetchContent's Populate() function instead of making ImGui a full CMake
# subproject with MakeAvailable(). Note that imgui_SOURCE_DIR will be defined
# after the Populate() call, and we can use it to reference the ImGui source
# files and headers.
include(FetchContent)
FetchContent_Populate(imgui
    QUIET
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.6.tar.gz # v1.92.6
    URL_HASH SHA256=5b17c01f69545bde732b14936d89ce0f508adb83e8b56fa82448371845172bc3
)

# If the target 'moonray_gui' already exists, create a new target 'moonray_gui_v2' so that
# users have the option to use either version.
#
# If the target 'moonray_gui' does NOT exist, it probably means we are using QT6 (refplat-vfx2025+),
# so we can just use 'moonray_gui' as the target for the new app.
if(TARGET moonray_gui)
    set(target moonray_gui_v2)
else()
    set(target moonray_gui)
endif()

add_executable(${target})

target_sources(${target}
    PRIVATE
        moonray_gui_v2.cc
        camera/FreeCam.cc
        camera/OrbitCam.cc
        ColorManager.cc
        DenoiserManager.cc
        FileUtil.cc
        GuiTypes.cc
        RenderGui.cc
        viewport/interface/AxisDisplay.cc
        viewport/interface/Dock.cc
        viewport/interface/DrawUtil.cc
        viewport/interface/ExposureWindow.cc
        viewport/interface/GammaWindow.cc
        viewport/interface/HelpWindow.cc
        viewport/interface/Interface.cc
        viewport/interface/ImageDisplay.cc
        viewport/interface/KeyBindingsWindow.cc
        viewport/interface/PathVisualizerWindow.cc
        viewport/interface/PixelInspector.cc
        viewport/interface/SceneInspector.cc
        viewport/interface/SnapshotWindow.cc
        viewport/interface/StatusBar.cc
        viewport/Keyboard.cc
        viewport/SnapshotManager.cc
        viewport/Viewport.cc

        # ImGui source files
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
)

target_include_directories(${target} PRIVATE ${imgui_SOURCE_DIR})

if (NOT IsDarwinPlatform)
    set(PlatformSpecificLibs atomic dl)
endif()

target_link_libraries(${target}
    PRIVATE
        McrtDenoise::denoiser
        ${MKL_target}
        ${OCIO}
        Moonray::application
        Moonray::rendering_rndr
        SceneRdl2::common_fb_util
        SceneRdl2::common_math
        SceneRdl2::common_platform
        SceneRdl2::render_logging
        SceneRdl2::render_util
        SceneRdl2::scene_rdl2
        OpenGL::GL
        glfw
        ${PlatformSpecificLibs}
)

# Set standard compile/link options
MoonrayGui_cxx_compile_definitions(${target})
MoonrayGui_cxx_compile_features(${target})
MoonrayGui_cxx_compile_options(${target})
MoonrayGui_link_options(${target})

# Disable OCIO if not found
if (NOT OpenColorIO_FOUND)
    target_compile_definitions(${target} PRIVATE DISABLE_OCIO)
endif()

install(TARGETS ${target}
    RUNTIME DESTINATION bin)

