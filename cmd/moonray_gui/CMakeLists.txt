# Copyright 2023-2024 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

set(target moonray_gui)

set(CMAKE_AUTOMOC TRUE)

add_executable(${target})

# -----------------------------------------
# CRT objects
# color render transform tables are just large float arrays

set(crtFiles
    moonray_rndr_gui_tex_3dlut_post1d
    moonray_rndr_gui_tex_3dlut_pre1d
    moonray_rndr_gui_tex_3dlut_3d
)

set(crtBinDir ${CMAKE_CURRENT_SOURCE_DIR}/data)
file(RELATIVE_PATH relBinDir ${PROJECT_SOURCE_DIR} ${crtBinDir})

# TODO: actually check that the string produced by xxd matches the expectation. i.e. Test this beyond building it, which works
# translate ".bin" source files to ".o" files
foreach(crtFile ${crtFiles})
    if(IsDarwinPlatform)
        add_custom_command(
            OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/${crtFile}.o
            DEPENDS ${crtBinDir}/${crtFile}.bin
            COMMAND xxd -i -n _binary_${crtFile} "${crtBinDir}/${crtFile}.bin" "${crtBinDir}/${crtFile}.c" && cc -c "${crtBinDir}/${crtFile}.c" -o "${CMAKE_CURRENT_BINARY_DIR}/${crtFile}.o")
    else()
        add_custom_command(
            OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/${crtFile}.o
            DEPENDS ${crtBinDir}/${crtFile}.bin
            COMMAND cd ${PROJECT_SOURCE_DIR} &&
                        objcopy --input-target=binary --binary-architecture=i386
                        --output-target=elf64-x86-64
                        "${relBinDir}/${crtFile}.bin"
                        "${CMAKE_CURRENT_BINARY_DIR}/${crtFile}.o")
    endif()
    # build a list of the results
    list(APPEND crtObjs ${CMAKE_CURRENT_BINARY_DIR}/${crtFile}.o)
endforeach()

# --------------------------------------------------------------

target_sources(${target}
    PRIVATE
        ColorManager.cc
        FrameUpdateEvent.cc
        FreeCam.cc
        GlslBuffer.cc
        MainWindow.cc
        moonray_gui.cc
        OrbitCam.cc
        RenderGui.cc
        RenderViewport.cc
        ${crtObjs}
)

set(atomicLib "")
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(atomicLib atomic)
endif()

target_link_libraries(${target}
    PRIVATE
        Boost::regex
        McrtDenoise::denoiser
        ${MKL}
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
        Qt5::Core
        Qt5::Gui
        Qt5::OpenGL
        ${atomicLib}
)

# Set standard compile/link options
MoonrayGui_cxx_compile_definitions(${target})
MoonrayGui_cxx_compile_features(${target})
MoonrayGui_cxx_compile_options(${target})
MoonrayGui_link_options(${target})

# Disable OCIO if < v2
if (NOT DEFINED ENV{REZ_OPENCOLORIO_MAJOR_VERSION} OR ENV{REZ_OPENCOLORIO_MAJOR_VERSION} VERSION_LESS "2.0.0.0")
    target_compile_definitions(${target} PRIVATE DISABLE_OCIO)
endif()

install(TARGETS ${target}
    RUNTIME DESTINATION bin)

