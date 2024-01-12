// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file GlslBuffer.h

#pragma once

#include "GuiTypes.h"

#include <QGLPixelBuffer>

namespace moonray_gui {

class GlslBuffer
{
public:
    // create a glsl buffer for off-screen rendering
    GlslBuffer(int width, int height, const float *lutOverride);
    ~GlslBuffer();

    // LINEAR RGBA -> CRT -> GAMMA -> RGB
    void makeCrtGammaProgram();

    // render to pixel buffer, input should
    // be a linear RenderBuffer
    void render(const FrameBuffer &frame, FrameType frameType, DebugMode mode, float exposure, float gamma);

    // return pixel buffer as a QImage
    QImage asImage() const;

private:
    int            mWidth;
    int            mHeight;
    QGLPixelBuffer mPixelBuffer;
    GLuint         mVertexBuffer;
    GLuint         mUvBuffer;
    GLint          mTexture;
    GLuint         mVertexShaderID;
    GLuint         mProgram;
    GLuint         mChannel;
    GLfloat        mExposure;
    GLfloat        mGamma;

    // Color render override LUT. Set to nullptr if we aren't overriding
    // the LUT. This binary blob is assumed to contain 64*64*64 * RGB float
    // OpenGL compatible volume texture data.
    // This class doesn't own this data and isn't responsible for deleting it.
    const float *  mLutOverride;
};

} // namespace moonray_gui

