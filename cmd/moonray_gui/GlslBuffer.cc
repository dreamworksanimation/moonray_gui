// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

/// @file GlslBuffer.cc

#include "GlslBuffer.h"

#include <scene_rdl2/common/platform/Platform.h>

#include <fstream>

// It's outside the scope of moonray to do the conversion into the binary format
// we use, plus we want to avoid a run-time dependency on legacy folios.
//
// Alternate LUTs can however be passed into via the lutOverride parameter.
// The LUTs are assumed to contain 64*64*64 * RGB float OpenGL compatible
// volume texture data.
//
// The following program generates such data. For the default LUT, the .bin file
// is compiled and linked into the lib via objcopy.  It relies on the legacy
// color render transform library which is part of the dwaimagebase folio.
//
// #include <color_rt_base/GlslColorRenderTransform.h>
// const char *file = "/rel/folio/cs_legacy/cs_legacy-1.2.0-5/aux/ani/color_render_transform.cdf";
// const char *indx = "25";
// auto crt = color_rt_base::GlslColorRenderTransform::create(file, indx);
// // write the tables, there should be 3: a 1D pre, 1D post, and 3D lut
// for (int texId = 0; texId < crt->texCount(); ++texId) {
//     std::string filename = "moonray_rndr_gui_" + crt->samplerName(texId) + ".bin";
//     std::ofstream fstr(filename);
//     size_t nFloats = 0;
//     switch(crt->texDims(texId)) {
//     case 1:
//         nFloats = crt->texSize(texId, 0);
//         break;
//     case 2:
//         MNRY_ASSERT(0 && "unexpected tex dims");
//         break;
//     case 3:
//         nFloats = crt->texSize(texId, 0) * crt->texSize(texId, 1) * crt->texSize(texId, 2) * 3;
//         break;
//     default:
//         MNRY_ASSERT(0 && "unexpected tex dims");
//         break;
//     }
//     size_t nBytes = nFloats * sizeof(float);
//     fstr.write(reinterpret_cast<char *>(crt->texData(texId).get()), nBytes);
// }

// objcopy generates these symbols
extern "C" float _binary_cmd_moonray_gui_data_moonray_rndr_gui_tex_3dlut_3d_bin_start;
extern "C" float _binary_cmd_moonray_gui_data_moonray_rndr_gui_tex_3dlut_post1d_bin_start;
extern "C" float _binary_cmd_moonray_gui_data_moonray_rndr_gui_tex_3dlut_pre1d_bin_start;

namespace {
// LINEAR RGB32F -> Color render transform -> gamma
//   const char *file = "/rel/folio/cs_legacy/cs_legacy-1.2.0-5/aux/ani/color_render_transform.cdf";
//   const char *indx = "25";
//   auto crt = color_rt_base::GlslColorRenderTransform::create(file, indx);
//   crt->setEntryPoint("apply_transform");
//   crt->setSrcSpace(color_rt_base::GlslColorRenderTransform::LINEAR);
//   crt->setDstSpace(color_rt_base::GlslColorRenderTransform::GAMMA_2_2);
//   crt->header()
const char *sCrtGammaProgram = R"(
#version 330 core
uniform sampler1D tex_3dlut_pre1d;
uniform sampler1D tex_3dlut_post1d;
uniform sampler3D tex_3dlut_3d;
uniform float exposure;
uniform float gamma;

vec4 oddPow_3dlut(const in vec4 x, const in vec4 y)
{
    return vec4(pow(abs(x), y) * sign(x));
}
vec3 oddPow_3dlut(const in vec3 x, const in vec3 y)
{
    return vec3(pow(abs(x), y) * sign(x));
}

vec2 frac(const in vec2 v)
{
    return vec2(v.x - floor(v.x), v.y - floor(v.y));
}

vec3 apply_dither(const in vec3 srcColor, const in vec2 pos)
{
    float dither_matrix_8x8[64] = float[](
         1.f/65.f,   49.f/65.f,   13.f/65.f,   61.f/65.f,    4.f/65.f,   52.f/65.f,   16.f/65.f,   64.f/65.f,
        33.f/65.f,   17.f/65.f,   45.f/65.f,   29.f/65.f,   36.f/65.f,   20.f/65.f,   48.f/65.f,   32.f/65.f,
         9.f/65.f,   57.f/65.f,    5.f/65.f,   53.f/65.f,   12.f/65.f,   60.f/65.f,    8.f/65.f,   56.f/65.f,
        41.f/65.f,   25.f/65.f,   37.f/65.f,   21.f/65.f,   44.f/65.f,   28.f/65.f,   40.f/65.f,   24.f/65.f,
         3.f/65.f,   51.f/65.f,   15.f/65.f,   63.f/65.f,    2.f/65.f,   50.f/65.f,   14.f/65.f,   62.f/65.f,
        35.f/65.f,   19.f/65.f,   47.f/65.f,   31.f/65.f,   34.f/65.f,   18.f/65.f,   46.f/65.f,   30.f/65.f,
        11.f/65.f,   59.f/65.f,    7.f/65.f,   55.f/65.f,   10.f/65.f,   58.f/65.f,    6.f/65.f,   54.f/65.f,
        43.f/65.f,   27.f/65.f,   39.f/65.f,   23.f/65.f,   42.f/65.f,   26.f/65.f,   38.f/65.f,   22.f/65.f);

    vec2 idx = frac(pos.xy * 0.125f) * 8.f;
    int y = int(floor(idx.y));
    int x = int(floor(idx.x));
    float dither_val = dither_matrix_8x8[y * 8 + x];
    return floor(srcColor * 255.f + vec3(dither_val)) * (1.f / 255.f);
}

vec4 apply_transform(const in vec4 srcColor, const in vec2 pos)
{
    // Application of film lut: transforms linear color values into a space visible in theaters
    // Transform is implemented with 1-D pre-lookup array, followed by a 64x64x64 lookup, followed by a 1-D post-lookup

    // Setup scale + offset terms for the texture lookups
    vec4 scalePre   = vec4(0.311342);
    vec4 offsetPre  = vec4(0.000488281);
    vec4 scale3d    = vec4(0.984375);
    vec4 offset3d   = vec4(0.0078125);
    vec4 scalePost  = vec4(0.999023);
    vec4 offsetPost = vec4(0.000488281);

    // Setup to sample the preLUT in gamma 2.2 space
    // srcColor is assumed to be in linear space.
    vec4 fragColor = oddPow_3dlut(srcColor, vec4(.454545454545));

    // Scale and offset for the preLUT
    vec4 newTexCoord3d = fragColor * scalePre + offsetPre;
    newTexCoord3d      = clamp(newTexCoord3d, 0.0, 1.0);

    // Apply preLUT
    fragColor.r = texture( tex_3dlut_pre1d, newTexCoord3d.r).r;
    fragColor.g = texture( tex_3dlut_pre1d, newTexCoord3d.g).r;
    fragColor.b = texture( tex_3dlut_pre1d, newTexCoord3d.b).r;

    // Scale and offset for the 3d LUT
    newTexCoord3d = fragColor * scale3d + offset3d;
    newTexCoord3d = clamp(newTexCoord3d, 0.0, 1.0);

    // Apply 3d LUT
    fragColor.rgb = texture( tex_3dlut_3d, newTexCoord3d.rgb).rgb;

    // Scale and offset for the postLUT
    newTexCoord3d = fragColor * scalePost + offsetPost;
    newTexCoord3d = clamp(newTexCoord3d, 0.0, 1.0);

    // Apply postLUT
    fragColor.r = texture( tex_3dlut_post1d, newTexCoord3d.r).r;
    fragColor.g = texture( tex_3dlut_post1d, newTexCoord3d.g).r;
    fragColor.b = texture( tex_3dlut_post1d, newTexCoord3d.b).r;

    // Apply exposure
    float gain = pow(2.0, exposure);
    fragColor.r *= gain;
    fragColor.g *= gain;
    fragColor.b *= gain;

    // Output in gamma 2.2 space
    // Conversion to gamma2.2 space is necessary for the monitor response to a linear
    // increase to result in a linear increase in perceived brightness.
    fragColor.rgb = oddPow_3dlut(fragColor.rgb, vec3(.454545454545));

    // Apply user gamma
    fragColor.r = pow(fragColor.r, 1.0 / gamma);
    fragColor.g = pow(fragColor.g, 1.0 / gamma);
    fragColor.b = pow(fragColor.b, 1.0 / gamma);

    // Apply dithering: palletize the results into 8-bit values
    fragColor.rgb = apply_dither(fragColor.rgb, pos);

    return fragColor;
}
in vec2 uv;
out vec3 color;

uniform sampler2D textureSampler;
uniform int channel;
uniform int width;
uniform int height;

void main() {
    vec4 t = texture(textureSampler, uv);
    vec2 pos;
    pos.x = uv.x * (width - 1);
    pos.y = uv.y * (height - 1);
    vec4 res = apply_transform(t, pos);
    if (channel == 0) {
        color.rgb = res.rgb;
    } else if (channel == 1) {
        color.r = res.r;
        color.g = res.r;
        color.b = res.r;
    } else if (channel == 2) {
        color.r = res.g;
        color.g = res.g;
        color.b = res.g;
    } else if (channel == 3) {
        color.r = res.b;
        color.g = res.b;
        color.b = res.b;
    }
}
)";

static const GLuint INVALID_HANDLE = 0xFFFFFFFF;

} // anonymous namespace

namespace moonray_gui {

GlslBuffer::GlslBuffer(int width, int height, const float *lutOverride):
    mWidth(width),
    mHeight(height),
    mPixelBuffer(width, height),
    mVertexBuffer(INVALID_HANDLE),
    mUvBuffer(INVALID_HANDLE),
    mTexture(-1),
    mVertexShaderID(INVALID_HANDLE),
    mProgram(INVALID_HANDLE),
    mChannel(INVALID_HANDLE),
    mExposure(0.f),
    mGamma(1.f),
    mLutOverride(lutOverride)
{
    // all our programs require the same vertex shader,
    // so go ahead and define that now
    mPixelBuffer.makeCurrent();

    // define our full screen quad in screen space
    // 4 verts, 3 floats per vert
    const GLfloat quad[12] = { -1.f, -1.f, 0.f,
                                1.f, -1.f, 0.f,
                                1.f,  1.f, 0.f,
                               -1.f,  1.f, 0.f };

    GLuint vertexArrayID;
    glGenVertexArrays(1, &vertexArrayID);
    glBindVertexArray(vertexArrayID);
    glGenBuffers(1, &mVertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, mVertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

    // assign uvs to our quad
    const GLfloat quadUV[8] = { 0.f, 0.f,
                                1.f, 0.f,
                                1.f, 1.f,
                                0.f, 1.f };
    GLuint uvArrayID;
    glGenVertexArrays(1, &uvArrayID);
    glBindVertexArray(uvArrayID);
    glGenBuffers(1, &mUvBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, mUvBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadUV), quadUV, GL_STATIC_DRAW);

    // compile the vertex shader
    const char *vCode = R"(
        #version 330 core
        layout(location = 0) in vec3 vertexPos;
        layout(location = 1) in vec2 vertexUV;
        out vec2 uv;
        void main() {
            gl_Position.xyz = vertexPos;
            gl_Position.w = 1.0;
            uv = vertexUV;
        }
    )";

    mVertexShaderID = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(mVertexShaderID, 1, &vCode, nullptr);
    glCompileShader(mVertexShaderID);
    GLint vResult;
    glGetShaderiv(mVertexShaderID, GL_COMPILE_STATUS, &vResult);
    MNRY_ASSERT(vResult);

    glDisable(GL_DEPTH_TEST);

    mPixelBuffer.doneCurrent();
}

GlslBuffer::~GlslBuffer()
{
    // cleanup vertex shader
    // TODO: does this happen automatically when the context is destroyed?
    glDeleteShader(mVertexShaderID);
}

// LINEAR RGBA -> CRT -> GAMMA -> RGB
void
GlslBuffer::makeCrtGammaProgram()
{
    mPixelBuffer.makeCurrent();

    // cleanup any existing program
    if (mProgram != INVALID_HANDLE) {
        glDeleteProgram(mProgram);
        mProgram = INVALID_HANDLE;
    }

    // compile the fragment shader
    GLuint fShaderID = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fShaderID, 1, &sCrtGammaProgram, nullptr);
    glCompileShader(fShaderID);
    GLint fResult;
    glGetShaderiv(fShaderID, GL_COMPILE_STATUS, &fResult);

    if (!fResult) {
        int infoLength;
        glGetShaderiv(fShaderID, GL_INFO_LOG_LENGTH, &infoLength);
        std::vector<char> message(infoLength + 1);
        glGetShaderInfoLog(fShaderID, infoLength, nullptr, &message[0]);
        std::cerr << &message[0] << '\n';
    }

    // link the program
    mProgram = glCreateProgram();
    glAttachShader(mProgram, mVertexShaderID);
    glAttachShader(mProgram, fShaderID);
    glLinkProgram(mProgram);
    GLint pResult;
    glGetProgramiv(mProgram, GL_LINK_STATUS, &pResult);
    MNRY_ASSERT(pResult);

    // cleanup - a little
    glDetachShader(mProgram, mVertexShaderID);
    glDetachShader(mProgram, fShaderID);
    // we no longer need the fragment shader, we'll reuse the
    // vertex shader if we run a different program
    glDeleteShader(fShaderID);

    // assign texture maps
    // need to use the program for the remainder of our setup
    glUseProgram(mProgram);

    // texture mapping
    // define the luts as textures used by the crt program
    // pre 1d table
    {
        int textureUnit = 1;  // 0 = main image, 1 = pre1d, 2 = post1d, 3 = 3dlut
        glActiveTexture(GL_TEXTURE0 + textureUnit);
        GLint textureID = glGetUniformLocation(mProgram, "tex_3dlut_pre1d");
        MNRY_ASSERT(textureID != -1);
        glUniform1i(textureID, textureUnit);
        glBindTexture(GL_TEXTURE_1D, textureID);
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        const float *data = &_binary_cmd_moonray_gui_data_moonray_rndr_gui_tex_3dlut_pre1d_bin_start;
        const size_t size = 1024;
        glTexImage1D(GL_TEXTURE_1D, 0, GL_R32F, size, 0, GL_RED, GL_FLOAT, data);
    }

    // post 1d table
    {
        int textureUnit = 2;  // 0 = main image, 1 = pre1d, 2 = post1d, 3 = 3dlut
        glActiveTexture(GL_TEXTURE0 + textureUnit);
        GLint textureID = glGetUniformLocation(mProgram, "tex_3dlut_post1d");
        MNRY_ASSERT(textureID != -1);
        glUniform1i(textureID, textureUnit);
        glBindTexture(GL_TEXTURE_1D, textureID);
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        const float *data = &_binary_cmd_moonray_gui_data_moonray_rndr_gui_tex_3dlut_post1d_bin_start;
        const size_t size = 1024;
        glTexImage1D(GL_TEXTURE_1D, 0, GL_R32F, size, 0, GL_RED, GL_FLOAT, data);
    }

    // 3d lut
    {
        int textureUnit = 3;  // 0 = main image, 1 = pre1d, 2 = post1d, 3 = 3dlut
        glActiveTexture(GL_TEXTURE0 + textureUnit);
        GLint textureID = glGetUniformLocation(mProgram, "tex_3dlut_3d");
        MNRY_ASSERT(textureID != -1);
        glUniform1i(textureID, textureUnit);
        glBindTexture(GL_TEXTURE_3D, textureID);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        const float *data = mLutOverride ? mLutOverride :
            &_binary_cmd_moonray_gui_data_moonray_rndr_gui_tex_3dlut_3d_bin_start;
        const size_t size = 64;
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB32F, size, size, size, 0, GL_RGB,
                     GL_FLOAT, data);
    }
    
    // define our main texture - its the render buffer
    glActiveTexture(GL_TEXTURE0);
    mTexture = glGetUniformLocation(mProgram, "textureSampler");
    glUniform1i(mTexture, 0); // 0 = main image, 1 = pre1d, 2 = post1d, 3 = 3dlut
    glBindTexture(GL_TEXTURE_2D, mTexture);
    // since the texture aligns perfectly with the window dimensions,
    // we can use GL_NEAREST for the filter
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

    // bind the display channel
    mChannel = glGetUniformLocation(mProgram, "channel");
    glUniform1i(mChannel, 0); // rgb display is the default

    // bind the exposure factor
    mExposure = glGetUniformLocation(mProgram, "exposure");
    glUniform1f(mExposure, 0.f); // 0 is the default

    // bind gamma correction
    mGamma = glGetUniformLocation(mProgram, "gamma");
    glUniform1f(mGamma, 1.f); // no gamma correction is the default

    // provide width and height for dithering
    // note: that these values are constant since we do not support resizing
    GLint var = glGetUniformLocation(mProgram, "width");
    glUniform1i(var, mWidth);
    var = glGetUniformLocation(mProgram, "height");
    glUniform1i(var, mHeight);


    mPixelBuffer.doneCurrent();
}

void
GlslBuffer::render(const FrameBuffer &frame, FrameType frameType, DebugMode mode,
                   float exposure, float gamma)
{
    mPixelBuffer.makeCurrent();

    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, mVertexBuffer);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(1);
    glBindBuffer(GL_ARRAY_BUFFER, mUvBuffer);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, 0);

    // set debug mode
    MNRY_ASSERT(mode == RGB || mode == RED || mode == GREEN || mode == BLUE);
    glUniform1i(mChannel, mode);

    // set exposure
    glUniform1f(mExposure, exposure);

    // set gamma
    glUniform1f(mGamma, gamma);

    // send image to gpu
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mTexture);
    switch (frameType) {
    case FRAME_TYPE_IS_RGB8:
        MNRY_ASSERT(0 && "8 bit texture unsupported");
        break;
    case FRAME_TYPE_IS_XYZ32:
        {
            const fb_util::Float3Buffer *buf = frame.xyz32;
            MNRY_ASSERT(mWidth == int(buf->getWidth()) &&
                       mHeight == int(buf->getHeight()));
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, mWidth, mHeight, 0, GL_RGB, GL_FLOAT,
                         buf->getData());
        }
        break;
    case FRAME_TYPE_IS_XYZW32:
        {
            const fb_util::Float4Buffer *buf = frame.xyzw32;
            MNRY_ASSERT(mWidth == int(buf->getWidth()) &&
                       mHeight == int(buf->getHeight()));
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, mWidth, mHeight, 0, GL_RGBA, GL_FLOAT,
                         buf->getData());
        }
        break;
    }

    glUseProgram(mProgram);
    glDrawArrays(GL_QUADS, 0, 4);

    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);

    mPixelBuffer.doneCurrent();
}

QImage
GlslBuffer::asImage() const
{
    return mPixelBuffer.toImage();
}

} // namespace moonray_gui

