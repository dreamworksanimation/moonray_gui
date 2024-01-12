// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <scene_rdl2/common/fb_util/FbTypes.h>

using namespace scene_rdl2;

namespace moonray_gui {

class MainWindow;
class NavigationCam;
class RenderViewport;

enum CameraType
{
    ORBIT_CAM,
    FREE_CAM,
    NUM_CAMERA_TYPES,
};

enum DebugMode
{
    RGB,
    RED,
    GREEN,
    BLUE,
    ALPHA,
    LUMINANCE,
    SATURATION,
    RGB_NORMALIZED,
    NUM_SAMPLES,

    NUM_DEBUG_MODES,
};

enum InspectorMode
{
    INSPECT_NONE,
    INSPECT_LIGHT_CONTRIBUTIONS,
    INSPECT_GEOMETRY,
    INSPECT_GEOMETRY_PART,
    INSPECT_MATERIAL,
    NUM_INSPECTOR_MODES
};

enum FrameType
{
    FRAME_TYPE_IS_RGB8 = 0,
    FRAME_TYPE_IS_XYZW32,
    FRAME_TYPE_IS_XYZ32
};

// Which additional buffers do we want to use for denoising.
enum DenoisingBufferMode
{
    DN_BUFFERS_BEAUTY,
    DN_BUFFERS_BEAUTY_ALBEDO,
    DN_BUFFERS_BEAUTY_ALBEDO_NORMALS,
    NUM_DENOISING_BUFFER_MODES,
};

union FrameBuffer
{
    const fb_util::Rgb888Buffer *rgb8;
    const fb_util::RenderBuffer *xyzw32;
    const fb_util::Float3Buffer *xyz32;
};

} // namespace moonray_gui

