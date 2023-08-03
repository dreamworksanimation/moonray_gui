// Copyright 2023 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "FrameUpdateEvent.h"
#include "MainWindow.h"
#include "NavigationCam.h"
#include "RenderGui.h"
#include "RenderViewport.h"

#include <moonray/rendering/rndr/RenderOutputDriver.h>
#include <moonray/rendering/rndr/RenderStatistics.h>
#include <scene_rdl2/common/fb_util/PixelBufferUtilsGamma8bit.h>
#include <scene_rdl2/common/math/MathUtil.h>
#include <scene_rdl2/common/platform/Platform.h>
#include <scene_rdl2/scene/rdl2/Camera.h>
#include <scene_rdl2/scene/rdl2/RenderOutput.h>

#include <QApplication>

#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <pthread.h>

// Experimental:
// Set to non-zero to only draw the corners of overlaid quads instead of the
// full square. It may be slightly less distracting.
#define DRAW_PARTIAL_TILE_OUTLINE       0


namespace moonray_gui {
using namespace scene_rdl2::math;

inline uint8_t
convertToByteColor(float col)
{
    return uint8_t(math::saturate(col) * 255.f);
}

inline void
addSaturate(uint8_t &fb, uint8_t c)
{
    uint8_t result = static_cast<uint8_t>(fb + c);
    fb = (result >= fb) ? result : 255;
}

inline void
addSaturate(float &fb, float c)
{
    float result = fb + c;
    fb = math::saturate(result);
}

inline void
addSaturate(scene_rdl2::fb_util::ByteColor &bc, uint8_t c)
{
    addSaturate(bc.r, c);
    addSaturate(bc.g, c);
    addSaturate(bc.b, c);
}

inline void
addSaturate(scene_rdl2::fb_util::RenderColor &rc, float c)
{
    addSaturate(rc.x, c);
    addSaturate(rc.y, c);
    addSaturate(rc.z, c);
}

inline void
addSaturate(math::Vec3f &v, float c)
{
    addSaturate(v.x, c);
    addSaturate(v.y, c);
    addSaturate(v.z, c);
}

template<typename BufferType, typename ScalarType> void 
drawHorizontalLine(BufferType *buf, unsigned x0, unsigned x1, unsigned y, const ScalarType col)
{
    auto *row = buf->getRow(y);
    for (unsigned x = x0; x < x1; ++x) {
        addSaturate(row[x], col);
    }
}

template<typename BufferType, typename ScalarType> void
drawVerticalLine(BufferType *buf, unsigned x, unsigned y0, unsigned y1, const ScalarType col)
{
    for (unsigned y = y0; y < y1; ++y) {
        auto &pixel = buf->getPixel(x, y);
        addSaturate(pixel, col);
    }
}

template<typename BufferType, typename ScalarType> void
drawFullTileOutline(BufferType *buf, const scene_rdl2::fb_util::Tile &tile, const ScalarType col)
{
    drawHorizontalLine(buf, tile.mMinX,     tile.mMaxX,     tile.mMinY,     col);
    drawHorizontalLine(buf, tile.mMinX,     tile.mMaxX,     tile.mMaxY - 1, col);
    drawVerticalLine(buf,   tile.mMinX,     tile.mMinY + 1, tile.mMaxY - 1, col);
    drawVerticalLine(buf,   tile.mMaxX - 1, tile.mMinY + 1, tile.mMaxY - 1, col);
}

template<typename BufferType, typename ScalarType> void
drawPoint(BufferType *buf, unsigned x, unsigned y, const ScalarType col)
{
    auto &pixel = buf->getPixel(x, y);
    addSaturate(pixel, col);
}

inline uint8_t
fadeColor(uint8_t c)
{
    return static_cast<uint8_t>(c >> 1);
}

inline float
fadeColor(float c)
{
    return c * 0.5f;
}

template<typename BufferType, typename ScalarType> void
drawPartialTileOutline(BufferType *buf, const scene_rdl2::fb_util::Tile &tile, const ScalarType col)
{
    ScalarType fadeCol = fadeColor(col);

    drawPoint(buf, tile.mMinX,     tile.mMinY,     col);
    drawPoint(buf, tile.mMinX + 1, tile.mMinY,     fadeCol);
    drawPoint(buf, tile.mMinX,     tile.mMinY + 1, fadeCol);
    drawPoint(buf, tile.mMinX + 2, tile.mMinY,     col);
    drawPoint(buf, tile.mMinX,     tile.mMinY + 2, col);

    drawPoint(buf, tile.mMinX,     tile.mMaxY - 1, col);
    drawPoint(buf, tile.mMinX + 1, tile.mMaxY - 1, fadeCol);
    drawPoint(buf, tile.mMinX,     tile.mMaxY - 2, fadeCol);
    drawPoint(buf, tile.mMinX + 2, tile.mMaxY - 1, col);
    drawPoint(buf, tile.mMinX,     tile.mMaxY - 3, col);

    drawPoint(buf, tile.mMaxX - 1, tile.mMinY,     col);
    drawPoint(buf, tile.mMaxX - 2, tile.mMinY,     fadeCol);
    drawPoint(buf, tile.mMaxX - 1, tile.mMinY + 1, fadeCol);
    drawPoint(buf, tile.mMaxX - 3, tile.mMinY,     col);
    drawPoint(buf, tile.mMaxX - 1, tile.mMinY + 2, col);

    drawPoint(buf, tile.mMaxX - 1, tile.mMaxY - 1, col);
    drawPoint(buf, tile.mMaxX - 2, tile.mMaxY - 1, fadeCol);
    drawPoint(buf, tile.mMaxX - 1, tile.mMaxY - 2, fadeCol);
    drawPoint(buf, tile.mMaxX - 3, tile.mMaxY - 1, col);
    drawPoint(buf, tile.mMaxX - 1, tile.mMaxY - 3, col);
}

template <typename BufferType, typename ScalarType> void
drawClippedPoint(BufferType *buf, unsigned x, unsigned y, const ScalarType col)
{
    if (x < buf->getWidth() && y < buf->getHeight()) {
        drawPoint(buf, x, y, col);
    }
}

template<typename BufferType, typename ScalarType> void
drawPartialTileOutlineClipped(BufferType *buf, const scene_rdl2::fb_util::Tile &tile, const ScalarType col)
{
    ScalarType fadeCol = fadeColor(col);

    drawClippedPoint(buf, tile.mMinX,     tile.mMinY,     col);
    drawClippedPoint(buf, tile.mMinX + 1, tile.mMinY,     col);
    drawClippedPoint(buf, tile.mMinX,     tile.mMinY + 1, col);
    drawClippedPoint(buf, tile.mMinX + 2, tile.mMinY,     fadeCol);
    drawClippedPoint(buf, tile.mMinX,     tile.mMinY + 2, fadeCol);

    drawClippedPoint(buf, tile.mMinX,     tile.mMaxY - 1, col);
    drawClippedPoint(buf, tile.mMinX + 1, tile.mMaxY - 1, col);
    drawClippedPoint(buf, tile.mMinX,     tile.mMaxY - 2, col);
    drawClippedPoint(buf, tile.mMinX + 2, tile.mMaxY - 1, fadeCol);
    drawClippedPoint(buf, tile.mMinX,     tile.mMaxY - 3, fadeCol);

    drawClippedPoint(buf, tile.mMaxX - 1, tile.mMinY,     col);
    drawClippedPoint(buf, tile.mMaxX - 2, tile.mMinY,     col);
    drawClippedPoint(buf, tile.mMaxX - 1, tile.mMinY + 1, col);
    drawClippedPoint(buf, tile.mMaxX - 3, tile.mMinY,     fadeCol);
    drawClippedPoint(buf, tile.mMaxX - 1, tile.mMinY + 2, fadeCol);

    drawClippedPoint(buf, tile.mMaxX - 1, tile.mMaxY - 1, col);
    drawClippedPoint(buf, tile.mMaxX - 2, tile.mMaxY - 1, col);
    drawClippedPoint(buf, tile.mMaxX - 1, tile.mMaxY - 2, col);
    drawClippedPoint(buf, tile.mMaxX - 3, tile.mMaxY - 1, fadeCol);
    drawClippedPoint(buf, tile.mMaxX - 1, tile.mMaxY - 3, fadeCol);
}

template<typename BufferType, typename ScalarType> void
drawTileOutline(BufferType *buf, const scene_rdl2::fb_util::Tile &tile, const ScalarType col)
{
    if (DRAW_PARTIAL_TILE_OUTLINE) {
        if (tile.getArea() == 64) {
            drawPartialTileOutline(buf, tile, col);
        } else {
            drawPartialTileOutlineClipped(buf, tile, col);
        }
    } else {
        drawFullTileOutline(buf, tile, col);
    }
}

RenderGui::RenderGui(CameraType initialCamType,
                     bool showTileProgress,
                     bool applyCrt,
                     const char *crtOverride,
                     const std::string& snapPath)
    : mInitialCameraType(initialCamType)
    , mMainWindow(nullptr)
    , mRenderTimestamp(0)
    , mLastSnapshotTimestamp(0)
    , mLastSnapshotTime(0.0)
    , mLastFilmActivity(0)
    , mLastCameraUpdateTime(0)
    , mLastCameraXform()
    , mC12C0()
    , mLastRenderOutputGuiIndx(0)
    , mRenderOutput(-1)
    , mLastTotalRenderOutputs(0)
    , mLastRenderOutputName("")
    , mHandler(nullptr)
    , mOkToRenderTiles(false)
    , mColorManager()
{
    mMainWindow = new MainWindow(nullptr, mInitialCameraType, crtOverride, snapPath);
    mHandler = new Handler(nullptr);
    mHandler->connect(QApplication::instance(), SIGNAL(lastWindowClosed()), mHandler, SLOT(quitApp()));
    mMainWindow->getRenderViewport()->setShowTileProgress(showTileProgress);
    mMainWindow->getRenderViewport()->setApplyColorRenderTransform(applyCrt);
    mMainWindow->show();
    mHandler->mIsActive = true;
    mMasterTimestamp = 1;
    mColorManager.setupConfig();
}


RenderGui::~RenderGui()
{
    delete mMainWindow;
    delete mHandler;
}


bool
RenderGui::isActive()
{
    return mHandler->mIsActive;
}

bool
RenderGui::close()
{
    bool retVal = false;
    if (mHandler) {
        mHandler->quitApp();
    }
    if (mMainWindow) {
        QMetaObject::invokeMethod(mMainWindow, "close", Qt::BlockingQueuedConnection, Q_RETURN_ARG(bool, retVal));
    }
    return retVal;
}

void
RenderGui::updateFrame(const scene_rdl2::fb_util::RenderBuffer *renderBuffer,
                       const scene_rdl2::fb_util::VariablePixelBuffer *renderOutputBuffer,
                       bool showProgress,
                       bool parallel)
{
    const DebugMode mode = mMainWindow->getRenderViewport()->getDebugMode();
    const bool applyCrt = mMainWindow->getRenderViewport()->getApplyColorRenderTransform();
    const float exposure = mMainWindow->getRenderViewport()->getExposure();
    const float gamma = mMainWindow->getRenderViewport()->getGamma();
    const bool useOCIO = mMainWindow->getRenderViewport()->getUseOCIO();
    bool denoise = mMainWindow->getRenderViewport()->getDenoisingEnabled();

    // Apply denoising whilst frame is in linear HDR format.
    if (denoise && mode != NUM_SAMPLES && mRenderOutput < 0) {
        unsigned w = renderBuffer->getWidth();
        unsigned h = renderBuffer->getHeight();

        const moonray::rndr::RenderOutputDriver *rod = mRenderContext->getRenderOutputDriver();
        const int albedoIndx = rod->getDenoiserAlbedoInput();
        const int normalIndx = rod->getDenoiserNormalInput();

        moonray::denoiser::DenoiserMode mode = mMainWindow->getRenderViewport()->getDenoiserMode();

        DenoisingBufferMode bufferMode = mMainWindow->getRenderViewport()->getDenoisingBufferMode();
        bool useAlbedo = (albedoIndx >= 0 && bufferMode != DN_BUFFERS_BEAUTY);
        bool useNormals = (albedoIndx >= 0 && normalIndx >= 0) && bufferMode == DN_BUFFERS_BEAUTY_ALBEDO_NORMALS;

        // Recreate denoiser if not yet created or config has changed
        if (mDenoiser == nullptr ||
            mode != mDenoiser->mode() ||
            w != mDenoiser->imageWidth() || h != mDenoiser->imageHeight() ||
            useAlbedo != mDenoiser->useAlbedo() ||
            useNormals != mDenoiser->useNormals()) {
            std::string errorMsg;
            mDenoiser = std::make_unique<moonray::denoiser::Denoiser>(
                mode, w, h, useAlbedo, useNormals, &errorMsg);
            if (!errorMsg.empty()) {
                std::cout << "Error creating denoiser: " << errorMsg << std::endl;
                mDenoiser.release();
            }
            mDenoisedRenderBuffer.init(w, h);
        }

        if (mDenoiser) {
            if (useAlbedo) {
                mRenderContext->snapshotAovBuffer(&mAlbedoBuffer, rod->getAovBuffer(albedoIndx), true, false);
            }

            if (useNormals) {
                mRenderContext->snapshotAovBuffer(&mNormalBuffer, rod->getAovBuffer(normalIndx), true, false);
            }

            const scene_rdl2::fb_util::RenderColor *inputBeautyPixels = renderBuffer->getData();
            const scene_rdl2::fb_util::RenderColor *inputAlbedoPixels = useAlbedo ? mAlbedoBuffer.getData() : nullptr;
            const scene_rdl2::fb_util::RenderColor *inputNormalPixels = useNormals ? mNormalBuffer.getData() : nullptr;
            scene_rdl2::fb_util::RenderColor *denoisedPixels = mDenoisedRenderBuffer.getData();
            std::string errorMsg;

            mDenoiser->denoise(reinterpret_cast<const float*>(inputBeautyPixels),
                               reinterpret_cast<const float*>(inputAlbedoPixels),
                               reinterpret_cast<const float*>(inputNormalPixels),
                               reinterpret_cast<float*>(denoisedPixels),
                               &errorMsg);

            if (!errorMsg.empty()) {
                std::cout << "Error denoising: " << errorMsg << std::endl;
            }

            renderBuffer = &mDenoisedRenderBuffer;
        }
    }

    /// -------------------------------- Color Grading -------------------------------------------------

    // assumes user is directly applying lut instead of ocio config file
    if (// are we applying the color render transform?
        applyCrt
        // are we in RGB, RED, GREEN, or BLUE display mode?
        && (mode == RGB  || mode == RED || mode == GREEN || mode == BLUE)
        // are we showing a color?  The main render buffer is definitely color,
        // but we cheat a bit and apply the transform to any 3 or 4
        // channel aov
        && (mRenderOutput < 0
            || mRenderOutputBuffer.getFormat() == scene_rdl2::fb_util::VariablePixelBuffer::FLOAT3
            || mRenderOutputBuffer.getFormat() == scene_rdl2::fb_util::VariablePixelBuffer::FLOAT4)) {

        // draw the tile progress boxes into the approriate buffer
        if (showProgress) {
            DisplayBuffer buf = mRenderOutput < 0?
                DISPLAY_BUFFER_IS_RENDER_BUFFER:
                DISPLAY_BUFFER_IS_RENDER_OUTPUT_BUFFER;
            showTileProgress(buf);
        }

        FrameBuffer frame;
        FrameType frameType;
        if (mRenderOutput < 0) {
            frame.xyzw32 = renderBuffer;
            frameType = FRAME_TYPE_IS_XYZW32;
        } else {
            switch (mRenderOutputBuffer.getFormat()) {
            case scene_rdl2::fb_util::VariablePixelBuffer::FLOAT3:
                frame.xyz32 = &mRenderOutputBuffer.getFloat3Buffer();
                frameType = FRAME_TYPE_IS_XYZ32;
                break;
            case scene_rdl2::fb_util::VariablePixelBuffer::FLOAT4:
                frame.xyzw32 = &mRenderOutputBuffer.getFloat4Buffer();
                frameType = FRAME_TYPE_IS_XYZW32;
                break;
            default:
                MNRY_ASSERT(0 && "render output buffer unhandled");
                // Unknown behavour here.
                frame.xyzw32 = renderBuffer;
                frameType = FRAME_TYPE_IS_XYZW32;
           }
        }
        // QApplication::postEvent handles deleting the raw pointer later, no risk of memory leak
        FrameUpdateEvent *event = new FrameUpdateEvent(frame, frameType, mode, exposure, gamma);
        QApplication::postEvent(mMainWindow, event);
        return;
    }

    scene_rdl2::fb_util::PixelBufferUtilOptions options = parallel?
            scene_rdl2::fb_util::PIXEL_BUFFER_UTIL_OPTIONS_PARALLEL :
            scene_rdl2::fb_util::PIXEL_BUFFER_UTIL_OPTIONS_NONE;

    // Apply color render transform
    mColorManager.applyCRT(mMainWindow, 
                           useOCIO, 
                           mRenderOutput, 
                           *renderBuffer, 
                           *renderOutputBuffer,
                           &mDisplayBuffer, 
                           options, 
                           parallel);

    if (showProgress) {
        showTileProgress(DISPLAY_BUFFER_IS_DISPLAY_BUFFER);
    }

    // Post an event to the main window on the GUI thread. Thankfully,
    // QCoreApplication::postEvent() is thread-safe.
    FrameBuffer frame;
    frame.rgb8 = &mDisplayBuffer;
    FrameUpdateEvent* event = new FrameUpdateEvent(frame, FRAME_TYPE_IS_RGB8, mode, exposure, gamma);
    QApplication::postEvent(mMainWindow, event);
}

void
RenderGui::snapshotFrame(scene_rdl2::fb_util::RenderBuffer *renderBuffer,
                         scene_rdl2::fb_util::HeatMapBuffer *heatMapBuffer,
                         scene_rdl2::fb_util::FloatBuffer *weightBuffer,
                         scene_rdl2::fb_util::RenderBuffer *renderBufferOdd,
                         scene_rdl2::fb_util::VariablePixelBuffer *renderOutputBuffer,
                         bool untile, bool parallel)
{
    DebugMode mode = mMainWindow->getRenderViewport()->getDebugMode();

    // Special case if debug mode is set to NUM_SAMPLES, in which case we want to display
    // the weights buffer directly with some transform applied to aid visualization.
    if (mode == NUM_SAMPLES) {
        mRenderContext->snapshotWeightBuffer(renderOutputBuffer, untile, parallel);
        return;
    }

    if (mRenderOutput < 0) {
        // snapshot the plain old render buffer output
        mRenderContext->snapshotRenderBuffer(renderBuffer, untile, parallel);
        return;
    }

    // snapshot something other than the render buffer
    const auto *rod = mRenderContext->getRenderOutputDriver();

    // If we have had a scene change but have not yet started rendering, the
    // progressive update might call us anyway.  This works for the render
    // buffer, since the render driver referenced by RenderContext is
    // a singleton that persists across RenderContext tear-downs.  But
    // the render output driver does not - and it is only setup during
    // start frame based on scene data.  We should be called
    // again shortly after the frame is started.
    if (!rod) return;

    MNRY_ASSERT(mRenderOutput < static_cast<int>(rod->getNumberOfRenderOutputs()));

    if (rod->requiresRenderBuffer(mRenderOutput)) {
        mRenderContext->snapshotRenderBuffer(renderBuffer, untile, parallel);
    }
    if (rod->requiresHeatMap(mRenderOutput)) {
        mRenderContext->snapshotHeatMapBuffer(heatMapBuffer, untile, parallel);
    }
    if (rod->requiresWeightBuffer(mRenderOutput)) {
        mRenderContext->snapshotWeightBuffer(weightBuffer, untile, parallel);
    }
    if (rod->requiresRenderBufferOdd(mRenderOutput)) {
        mRenderContext->snapshotRenderBufferOdd(renderBufferOdd, untile, parallel);
    }

    mRenderContext->snapshotRenderOutput(renderOutputBuffer, mRenderOutput,
                                         renderBuffer, heatMapBuffer, weightBuffer, renderBufferOdd,
                                         untile, parallel);
}

void
RenderGui::beginInteractiveRendering(const Mat4f& cameraXform,
                                     bool makeDefaultXform)
{
    mRenderTimestamp = 0;
    mLastSnapshotTimestamp = 0;
    mLastSnapshotTime = 0.0;
    mLastFilmActivity = 0;
    mLastCameraUpdateTime = -1.0;
    mLastCameraXform = cameraXform;

    RenderViewport *vp = mMainWindow->getRenderViewport();

    // Give the navigation camera access to the scene in case it needs to run
    // collision checks.
    vp->setCameraRenderContext(*mRenderContext);

    if (makeDefaultXform) {
        vp->setDefaultCameraTransform(cameraXform);
    }

    // Update the camera.
    computeCameraMotionXformOffset();
    NavigationCam *cam = vp->getNavigationCam();
    if (cam) {
        Mat4f conditionedXform = cam->resetTransform(cameraXform, false);
        if (!isEqual(mLastCameraXform, conditionedXform)) {
            setCameraXform(conditionedXform);
        }
    }
}

uint32_t
RenderGui::updateInteractiveRendering()
{
    switch (mRenderContext->getRenderMode())
    {
    case moonray::rndr::RenderMode::PROGRESSIVE:
    case moonray::rndr::RenderMode::PROGRESSIVE_FAST:
    case moonray::rndr::RenderMode::PROGRESS_CHECKPOINT:
    case moonray::rndr::RenderMode::BATCH:
        return updateProgressiveRendering();

    case moonray::rndr::RenderMode::REALTIME:
        return updateRealTimeRendering();

    default:
        MNRY_ASSERT(0);
    }

    return 0;
}

Mat4f 
RenderGui::endInteractiveRendering()
{
    if (mRenderContext->isFrameRendering()) {
        mRenderContext->stopFrame();
    }

    return updateNavigationCam(util::getSeconds());
}

uint32_t
RenderGui::updateProgressiveRendering()
{
    const double currentTime = util::getSeconds();
    bool updated = false;

    RenderViewport* renderVp = MNRY_VERIFY(mMainWindow->getRenderViewport());

    // This block of code won't get executed on the first iteration after
    // beginInteractiveRendering is called but will be for all subsequent 
    // iterations.
    if (mRenderContext->isFrameRendering() || mRenderContext->isFrameComplete()) {

        // Throttle rendering to the specified frames per second.
        float fps = mRenderContext->getSceneContext().getSceneVariables().get(rdl2::SceneVariables::sFpsKey);
        if (fps < 0.000001f) {
            fps = 24.0f;
        }

        // Have we elapsed enough time to show another part of the frame?
        const bool snapshotIntervalElapsed = (currentTime - mLastSnapshotTime) >= ((1.0 / fps) - 0.001f);   // 1 ms slop

        const unsigned filmActivity = mRenderContext->getFilmActivity();
        const bool renderSamplesPending = (filmActivity != mLastFilmActivity);
        bool roChanged = updateRenderOutput();

        // In NORMAL view mode, we want to check if we have a complete frame.  In
        // SNOOP mode, we allow partial frames
        const bool readyForDisplay = mRenderContext->isFrameReadyForDisplay();

        // All these conditions must be met before we push another new frame up.
        if (readyForDisplay && ((snapshotIntervalElapsed && renderSamplesPending) || roChanged)) {

            mLastSnapshotTimestamp = mRenderTimestamp;
            mLastSnapshotTime = currentTime;
            mLastFilmActivity = filmActivity;

            snapshotFrame(&mRenderBuffer, &mHeatMapBuffer, &mWeightBuffer, &mRenderBufferOdd,
                          &mRenderOutputBuffer, true, false);

            updateFrame(&mRenderBuffer, &mRenderOutputBuffer,
                        !mRenderContext->isFrameComplete() &&
                        renderVp->getShowTileProgress(),
                        false);

            updated = true;
        }

    } 

    // Special case for when we want to resend the frame buffer even after it
    // has completed rendering. One current example is if you toggle the show
    // alpha mode after rendering has completed. Another is when the tile
    // overlays are fading out right after the frame completes.
    bool needsRefresh = renderVp->getNeedsRefresh();
    if (!updated && needsRefresh && mRenderContext->isFrameComplete()) {
        snapshotFrame(&mRenderBuffer, &mHeatMapBuffer, &mWeightBuffer,
                      &mRenderBufferOdd, &mRenderOutputBuffer, true, false);
        updateFrame(&mRenderBuffer, &mRenderOutputBuffer, false, true);
        renderVp->setNeedsRefresh(false);
    }

    // This check forces us to wait on the previous frame being displayed at least once
    // before triggering the next frame. If we didn't do this, we may never see
    // anything displayed, or motion may be jerky.
    if (mLastSnapshotTimestamp >= mRenderTimestamp) {

        // Check if there have been any scene changes since the last render.
        Mat4f cameraXform = updateNavigationCam(currentTime);
        const bool cameraChanged = !math::isEqual(mLastCameraXform, cameraXform);
        bool sceneChanged = cameraChanged || (mMasterTimestamp != mRenderTimestamp);

        // Check if the progressive mode changed
        moonray::rndr::RenderMode currentMode = renderVp->isFastProgressive() ?
            moonray::rndr::RenderMode::PROGRESSIVE_FAST :
            moonray::rndr::RenderMode::PROGRESSIVE;
        if (mRenderContext->getRenderMode() != currentMode) {
            mRenderContext->setRenderMode(currentMode);
            sceneChanged = true;
        }

        // Check if the fast progressive mode changed
        moonray::rndr::FastRenderMode currentFastMode = renderVp->getFastMode();
        if (mRenderContext->getFastRenderMode() != currentFastMode) {
            mRenderContext->setFastRenderMode(currentFastMode);
            sceneChanged = true;
        }   

        if (sceneChanged) {

            // Stop the previous frame (if we were rendering one).
            if (mRenderContext->isFrameRendering()) {
                mRenderContext->stopFrame();
            }

            mRenderTimestamp = ++mMasterTimestamp;
            mLastFilmActivity = 0;

            //
            // Here is the point in the frame where we've stopped all render threads
            // and it's safe to update the scene.
            //

            // Update the camera.
            setCameraXform(cameraXform);

            // Kick off a new frame with the updated camera/progressive mode
            mRenderContext->startFrame();

            // Update the tile progress rendering state.
            mOkToRenderTiles = false;
            unsigned numTiles = unsigned(mRenderContext->getTiles()->size());
            if (mFadeLevels[0].getNumBits() != numTiles) {
                for (unsigned i = 0; i < NUM_TILE_FADE_STEPS; ++i) {
                    mFadeLevels[i].init(numTiles);
                }
            }
        }
    }

    return mRenderContext->isFrameRendering() ? mRenderTimestamp : 0;
}

uint32_t
RenderGui::updateRealTimeRendering()
{
    const double currentTime = util::getSeconds();

    // This block of code won't get executed on the first iteration after
    // beginInteractiveRendering is called but will be for all subsequent 
    // iterations.
    if (mRenderContext->isFrameRendering()) {

        if (mRenderContext->isFrameReadyForDisplay()) {
            mRenderContext->stopFrame();

            mRenderTimestamp = ++mMasterTimestamp;

            updateRenderOutput();
            snapshotFrame(&mRenderBuffer, &mHeatMapBuffer, &mWeightBuffer, &mRenderBufferOdd,
                          &mRenderOutputBuffer, true, true);

            updateFrame(&mRenderBuffer, &mRenderOutputBuffer, false, true);

            // Here is the point in the frame where we've stopped all render
            // threads and it's safe to update the scene.

            // ...

            // Update realtime frame statistics.
            mRenderContext->commitCurrentRealtimeStats();

            // Update the camera.
            Mat4f cameraXform = updateNavigationCam(currentTime);
            setCameraXform(cameraXform);

            mRenderContext->startFrame();
        }

    } else {

        // Kick off the first frame.
         
        // Check if there have been any scene changes since the last render.
        Mat4f cameraXform = updateNavigationCam(currentTime);

        // Update the camera.
        setCameraXform(cameraXform);

        mRenderTimestamp = ++mMasterTimestamp;

        // Kick off a new frame with the updated camera.
        mRenderContext->startFrame();
    }

    return mRenderContext->isFrameRendering() ? mRenderTimestamp : 0;
}

void
RenderGui::computeCameraMotionXformOffset()
{
    const rdl2::Camera* camera = mRenderContext->getCameras()[0];  // primary camera
    MNRY_ASSERT(camera);
    MNRY_ASSERT(rdl2::Node::sNodeXformKey.isBlurrable());

    // To preserve any camera motion xform, we need to compute the existing
    // "offset" xform to go from TIMESTEP_END to TIMESTEP_BEGIN
    // We'll accept the double to float precision loss for gui maniupulations
    Mat4f c02w = toFloat(camera->get(rdl2::Node::sNodeXformKey, rdl2::TIMESTEP_BEGIN));
    Mat4f c12w = toFloat(camera->get(rdl2::Node::sNodeXformKey, rdl2::TIMESTEP_END));
    Mat4f w2c0 = c02w.inverse();
    mC12C0 = c12w * w2c0;
}

void
RenderGui::setCameraXform(const Mat4f& c2w)
{
    rdl2::Camera* camera = const_cast<rdl2::Camera*>(mRenderContext->getCameras()[0]);  // primary camera
    MNRY_ASSERT(camera);
    MNRY_ASSERT(rdl2::Node::sNodeXformKey.isBlurrable());

    // We then add the offset to the given camera xform to set the corresponding
    // motion transform
    camera->beginUpdate();
    camera->set(rdl2::Node::sNodeXformKey, toDouble(c2w), rdl2::TIMESTEP_BEGIN);
    camera->set(rdl2::Node::sNodeXformKey, toDouble(mC12C0 * c2w), rdl2::TIMESTEP_END);
    camera->endUpdate();
    mRenderContext->setSceneUpdated();

    mLastCameraXform = c2w;
}

Mat4f
RenderGui::updateNavigationCam(double currentTime)
{
    NavigationCam *cam = mMainWindow->getRenderViewport()->getNavigationCam();
    if (!cam) {
        return Mat4f(math::one);
    }

    double dt = (mLastCameraUpdateTime < 0.0) ? 0.0 : (currentTime - mLastCameraUpdateTime);
    mLastCameraUpdateTime = currentTime;

    return cam->update(static_cast<float>(dt));
}

void
RenderGui::drawTileOutlines(DisplayBuffer buf, const std::vector<scene_rdl2::fb_util::Tile> &tiles,
                            float tileColor, int fadeLevelIdx)
{
    switch (buf) {
    case DISPLAY_BUFFER_IS_DISPLAY_BUFFER:
        {
            const uint8_t byteColor = convertToByteColor(tileColor);
            mFadeLevels[fadeLevelIdx].forEachBitSet([&](unsigned idx) {
                MNRY_ASSERT(idx < tiles.size());
                drawTileOutline(&mDisplayBuffer, tiles[idx], byteColor);
            });
        }
        break;
    case DISPLAY_BUFFER_IS_RENDER_BUFFER:
        mFadeLevels[fadeLevelIdx].forEachBitSet([&](unsigned idx) {
            MNRY_ASSERT(idx < tiles.size());
            drawTileOutline(&mRenderBuffer, tiles[idx], tileColor);
        });
        break;
    case DISPLAY_BUFFER_IS_RENDER_OUTPUT_BUFFER:
        switch (mRenderOutputBuffer.getFormat()) {
        case scene_rdl2::fb_util::VariablePixelBuffer::FLOAT3:
            mFadeLevels[fadeLevelIdx].forEachBitSet([&](unsigned idx) {
                MNRY_ASSERT(idx < tiles.size());
                drawTileOutline(&mRenderOutputBuffer.getFloat3Buffer(), tiles[idx], tileColor);
            });
            break;
        case scene_rdl2::fb_util::VariablePixelBuffer::FLOAT4:
            mFadeLevels[fadeLevelIdx].forEachBitSet([&](unsigned idx) {
                MNRY_ASSERT(idx < tiles.size());
                drawTileOutline(&mRenderOutputBuffer.getFloat4Buffer(), tiles[idx], tileColor);
            });
            break;
        default:
            MNRY_ASSERT(0 && "tile progress in render output buffer unhandled");
        }
        break;
    default:
        MNRY_ASSERT(0 && "unknown display buffer");
    }
}

void
RenderGui::showTileProgress(DisplayBuffer buf)
{
    // Color of new tiles, additive on framebuffer.
    static const float refTileColor = 0.2f;

    // Initial passes essentially try and render something to all tiles as fast
    // as possible so we have an image to extrapolate. This is problematic if
    // rendering diagnostic tiles on top since they cover the entire image
    // making it harder to see, especially if the camera is in constant motion.
    // The solution here is to only start rendering tiles when less than a
    // certain percentage of the screen is covered with them.
    // Here we set that threshold at 10%.
    static const float tileRatioThreshold = 0.1f;

    // Render all the tiles which we are are currently submitting primary rays
    // for over all threads.

    const std::vector<scene_rdl2::fb_util::Tile> &tiles =
        *(mRenderContext->getTiles());
    mRenderContext->getTilesRenderedTo(mFadeLevels[0]);

    if (!mOkToRenderTiles) {
        auto totalTiles = tiles.size();
        float ratio = float(double(mFadeLevels[0].getNumBitsSet()) / double(totalTiles));

        if (ratio < tileRatioThreshold) {
            mOkToRenderTiles = true;
        } else {
            // Early return.
            return;
        }
    }

    // Render full bright tiles we've rendered this frame.
    drawTileOutlines(buf, tiles, refTileColor, 0);

    // Render the tiles for each different fade level.
    for (unsigned i = 1; i < NUM_TILE_FADE_STEPS; ++i) {

        // Ensure each bit is only set to on for a single list, with lower indexed
        // lists getting priority over higher indexed lists.
        mFadeLevels[i].combine(mFadeLevels[0], [](uint32_t &a, uint32_t b) {
            a &= ~b;
        });

        // Compute fade amount.
        float t = (1.f - (float(i) / float(NUM_TILE_FADE_STEPS))) * 0.6f;
        const float fadeColor = refTileColor * t;

        drawTileOutlines(buf, tiles, fadeColor, i);
    }

    // Do actual fade. (TODO: use std::move instead of the logic below)
    // Note: mFadeLevels[0] is cleared next time around.
    for (int i = NUM_TILE_FADE_STEPS - 1; i > 0; --i) {
        mFadeLevels[i].combine(mFadeLevels[i - 1], [](uint32_t &a, uint32_t b) {
            a = b;
        });
    }
}

bool
RenderGui::updateRenderOutput()
{
    bool updated = false;
    int guiIndx = mMainWindow->getRenderViewport()->getRenderOutputIndx();
    const auto *rod = mRenderContext->getRenderOutputDriver();

    // rod can be null if we have not yet called startFrame()
    // this will happen in progressive rendering when we have a scene change
    if (!rod) {
        return false;
    }
    const int numRenderOutputs = rod->getNumberOfRenderOutputs();

    if (guiIndx != mLastRenderOutputGuiIndx) {
        if (guiIndx > mLastRenderOutputGuiIndx) {
            // find next active output
            if (mRenderOutput + 1 < numRenderOutputs) {
                mRenderOutput++;
                updated = true;
            }
        } else if (guiIndx < mLastRenderOutputGuiIndx) {
            // find previous active output
            if (mRenderOutput >= 0) {
                mRenderOutput--;
                updated = true;
                // -1 means to use the render buffer
            }
        }
        mLastRenderOutputGuiIndx = guiIndx;
    }

    if (mLastTotalRenderOutputs != numRenderOutputs) {
        // the scene changed - our mRenderOutput index is potentially out
        // of range or invalid.  first try to match the render output name
        for (int i = 0; i < numRenderOutputs; ++i) {
            if (mLastRenderOutputName == rod->getRenderOutput(i)->getName()) {
                // found it, no update needed
                mRenderOutput = i;
                break;
            }
        }

        // if we didn't find it and we are out of range, put
        // us at the last render output - this implies an update
        if (!(mRenderOutput < numRenderOutputs)) {
            mRenderOutput = numRenderOutputs - 1;
        }

        // if we have some kind of change, but our index is
        // in range, just flag this as an update
        const std::string outputName = mRenderOutput > 0? rod->getRenderOutput(mRenderOutput)->getName() : "";
        if (mLastRenderOutputName != outputName) {
            updated = true;
        }

        mLastTotalRenderOutputs = numRenderOutputs;
    }

    if (updated) {
        if (mRenderOutput < 0) {
            std::cerr << "switch output to render buffer\n";
            mLastRenderOutputName = "";
        } else {
            const std::string outputName = rod->getRenderOutput(mRenderOutput)->getName();
            std::cerr << "switch output to "
                      << outputName
                      << '\n';
            mLastRenderOutputName = outputName;
        }
    }
    return updated;
}

bool
RenderGui::isFastProgressive() const
{
    return mMainWindow->getRenderViewport()->isFastProgressive();
}

moonray::rndr::FastRenderMode
RenderGui::getFastRenderMode() const
{
    return mMainWindow->getRenderViewport()->getFastMode();
}


} // namespace moonray_gui

