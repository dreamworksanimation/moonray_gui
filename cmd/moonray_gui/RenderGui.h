// Copyright 2023 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ColorManager.h"
#include "GuiTypes.h"

#include <mcrt_denoise/denoiser/Denoiser.h>
#include <moonray/rendering/rndr/rndr.h>

#define NUM_TILE_FADE_STEPS  4

namespace moonray_gui {

class Handler;

/**
 * The RenderGui class handles spinning off a thread for Qt, as well as booting
 * up the GUI interface and communicating updates from the renderer to the GUI.
 */
class RenderGui
{
public:
    RenderGui(CameraType initialCamType, bool showTileProgress, bool applyCrt,
              const char *crtOverride, const std::string& snapPath);
    ~RenderGui();

    void setContext(moonray::rndr::RenderContext *ctx) { mRenderContext = ctx; }

    /// Submits a new frame to the GUI for display.
    void updateFrame(const scene_rdl2::fb_util::RenderBuffer *renderBuffer,
                     const scene_rdl2::fb_util::VariablePixelBuffer *renderOutputBuffer,
                     bool showTileProgress,
                     bool parallel);

    /// Snapshots the current output buffers based on the
    /// user's mRenderOutput selection.
    /// heatMapBuffer is a scratch buffer. Final results
    /// will be in either renderBuffer or renderOutputBuffer
    void snapshotFrame(scene_rdl2::fb_util::RenderBuffer *renderBuffer,
                       scene_rdl2::fb_util::HeatMapBuffer *heatMapBuffer,
                       scene_rdl2::fb_util::FloatBuffer *weightBuffer,
                       scene_rdl2::fb_util::RenderBuffer *renderBufferOdd,
                       scene_rdl2::fb_util::VariablePixelBuffer *renderOutputBuffer,
                       bool untile, bool parallel);

    /// APIs to handle interactive rendering logic.
    /// All calls to updateInteractiveRendering should only be done inside of
    /// a beginInteractiveRendering/endInteractiveRendering pair. 
    /// A call to beginInteractiveRendering takes an initial camera transform.
    /// A call to endInteractiveRendering returns to latest camera transform
    /// in case you later want to continue interactive rendering at that same
    /// location.
    /// updateInteractiveRendering returns the current "render frame" we're in
    /// the process of rendering.
    void beginInteractiveRendering(const scene_rdl2::math::Mat4f& cameraXform,
                                   bool makeDefaultXform);
    uint32_t updateInteractiveRendering();
    scene_rdl2::math::Mat4f endInteractiveRendering();

    /// Once we are within a beginInteractiveRendering/endInteractiveRendering,
    /// This function may be called from anywhere or any thread to discard the
    /// current interactive frame being rendered and to kick off another one.
    void forceNewInteractiveFrame();

    /// True: fast progressive, False: regular progressive
    bool isFastProgressive() const;

    /// Get the current fast progressive render mode
    moonray::rndr::FastRenderMode getFastRenderMode() const;

    bool isActive();

    bool close();

private:
    uint32_t updateProgressiveRendering();
    uint32_t updateRealTimeRendering();

    void computeCameraMotionXformOffset();
    void setCameraXform(const scene_rdl2::math::Mat4f& cameraXform);

    scene_rdl2::math::Mat4f updateNavigationCam(double currentTime);

    enum DisplayBuffer {
        DISPLAY_BUFFER_IS_DISPLAY_BUFFER = 0,
        DISPLAY_BUFFER_IS_RENDER_BUFFER,
        DISPLAY_BUFFER_IS_RENDER_OUTPUT_BUFFER
    };

    void drawTileOutlines(DisplayBuffer buf, const std::vector<scene_rdl2::fb_util::Tile> &tiles,
                          float tileColor, int fadeLevelIdx);
    void showTileProgress(DisplayBuffer buf);
    bool updateRenderOutput();

    CameraType mInitialCameraType;

    MainWindow* mMainWindow;

    moonray::rndr::RenderContext         *mRenderContext = nullptr;
    scene_rdl2::fb_util::RenderBuffer        mRenderBuffer;
    scene_rdl2::fb_util::RenderBuffer        mDenoisedRenderBuffer;
    scene_rdl2::fb_util::RenderBuffer        mAlbedoBuffer;
    scene_rdl2::fb_util::RenderBuffer        mNormalBuffer;
    scene_rdl2::fb_util::HeatMapBuffer       mHeatMapBuffer;
    scene_rdl2::fb_util::FloatBuffer         mWeightBuffer;
    scene_rdl2::fb_util::RenderBuffer        mRenderBufferOdd;
    scene_rdl2::fb_util::VariablePixelBuffer mRenderOutputBuffer;
    scene_rdl2::fb_util::Rgb888Buffer        mDisplayBuffer;

    //
    // Interactive rendering related members:
    //

    /// Increment whenever any inputs which will affect the render change.
    /// The renderering code will strive to render this frame. If it's rendering
    /// a frame with a lower timestamp then we know the frame it's currently
    /// rendering is old.
    std::atomic<uint32_t>   mMasterTimestamp;

    /// The timestamp of the frame the renderer is currently processing.
    uint32_t                mRenderTimestamp;

    /// This variable contains the timestamp of the most recent frame
    /// snap-shotted for display.
    uint32_t                mLastSnapshotTimestamp;

    /// This variable contains the absolute time of the most recent frame
    /// snap-shotted for display.
    double                  mLastSnapshotTime;

    /// Used to check if the Film has changed since the last time we
    /// checked. Only touched on the main thread.
    unsigned                mLastFilmActivity;

    /// This variable contains the absolute time of the most recent call to
    /// NavigationCam::update.
    double                  mLastCameraUpdateTime;

    /// The most recent camera transform. Stored to avoid kicking off new frame
    /// if the camera hasn't moved.
    scene_rdl2::math::Mat4f      mLastCameraXform;

    /// The "offset" camera xform to go from TIMESTEP_END to TIMESTEP_BEGIN
    /// See computeCameraMotionXformOffset() for details
    scene_rdl2::math::Mat4f      mC12C0;

    /// The viewport maintains an integer (renderOutputIndex) that increases
    /// when the user presses the '.' key and decreases when he presses
    /// the ',' key.  This member keeps track of the last value read from
    /// the viewport - so we can increment to the next render output or
    /// decrement to the previous.
    int                     mLastRenderOutputGuiIndx;

    /// < 0 mean show the main render buffer, otherwise this
    /// value stores a RenderOutput indx for the RenderOutputDriver
    int                     mRenderOutput;
    int                     mLastTotalRenderOutputs;
    std::string             mLastRenderOutputName;

    /// Small class for handling interactions between Qt Widgets and the Render GUI
    Handler*                mHandler;

    /// Tile progress:
    bool                    mOkToRenderTiles;
    util::BitArray          mFadeLevels[NUM_TILE_FADE_STEPS];

    /// Denoiser
    std::unique_ptr<moonray::denoiser::Denoiser> mDenoiser;

    /// Color Manager
    ColorManager mColorManager;
};

} // namespace moonray_gui

