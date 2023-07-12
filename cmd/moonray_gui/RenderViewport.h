// Copyright 2023 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifndef Q_MOC_RUN
#include "QtQuirks.h"
#include "FrameUpdateEvent.h"
#include "FreeCam.h"
#include "GlslBuffer.h"
#include "GuiTypes.h"
#include "OrbitCam.h"

#include <mcrt_denoise/denoiser/Denoiser.h>
#endif

#include <QWidget>

class QLabel;

namespace moonray_gui {

/**
 * The RenderViewport class will just display a frame buffer.
 */
class RenderViewport : public QWidget
{
    Q_OBJECT

public:
    RenderViewport(QWidget* parent, CameraType initialType, const char *crtOverride, const std::string& snapPath);
    ~RenderViewport();

    /// Navigation camera access.
    void setCameraRenderContext(const moonray::rndr::RenderContext &context);
    void setDefaultCameraTransform(const scene_rdl2::math::Mat4f &xform);
    NavigationCam *getNavigationCam();

    void setShowTileProgress(bool tileProgress) { mShowTileProgress = tileProgress; }
    bool getShowTileProgress() const    { return mShowTileProgress; }
    void setApplyColorRenderTransform(bool applyCrt) { mApplyColorRenderTransform = applyCrt; }
    bool getApplyColorRenderTransform() const { return mApplyColorRenderTransform; }
    bool getDenoisingEnabled() const { return mDenoise; }
    moonray::denoiser::DenoiserMode getDenoiserMode() const { return mDenoiserMode; }
    DenoisingBufferMode getDenoisingBufferMode() const { return mDenoisingBufferMode; }
    DebugMode getDebugMode() const      { return mDebugMode; }
    int getRenderOutputIndx() const { return mRenderOutputIndx; }

    bool getUpdateExposure() const { return mUpdateExposure; }
    bool getUpdateGamma() const { return mUpdateGamma; }
    float getExposure() const { return mExposure; }
    float getGamma() const { return mGamma; }

    bool isFastProgressive() const { return mProgressiveFast; }

    moonray::rndr::FastRenderMode getFastMode() const { return mFastMode; }
    void setFastMode(moonray::rndr::FastRenderMode mode) { mFastMode = mode; }

    bool getNeedsRefresh() const { return mNeedsRefresh; }
    void setNeedsRefresh(bool refresh) { mNeedsRefresh = refresh; }

    int getKey() const { return mKey; }
    void setKey(int key) { mKey = key; }

    bool getUseOCIO() const { return mUseOCIO; }

    /// Called by the main application to update the frame which is displayed.
    void updateFrame(FrameUpdateEvent* event);

    // Get status string
    QString getSettings() const { return "Exposure: " + QString::number(mExposure) + 
                                         "\nGamma: " + QString::number(mGamma); }
    
    static const char* mHelp;

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    void setupUi();

    QLabel* mImageLabel;

    // OpenGL CRT
    GlslBuffer *mGlslBuffer;

    int mWidth;
    int mHeight;

    CameraType mActiveCameraType;
    OrbitCam mOrbitCam;
    FreeCam mFreeCam;

    bool mShowTileProgress;
    bool mApplyColorRenderTransform;
    bool mDenoise;
    moonray::denoiser::DenoiserMode mDenoiserMode;
    DenoisingBufferMode mDenoisingBufferMode;
    std::vector<DenoisingBufferMode> mValidDenoisingBufferModes;
    DebugMode mDebugMode;
    int mRenderOutputIndx;
    bool mNeedsRefresh;
    bool mUpdateExposure; // is exposure being updated?
    bool mUpdateGamma; // is gamma being updated?
    float mExposure;
    float mGamma;
    int mMousePos; // x position of the mouse
    int mKey; // index of current pressed key
    int mKeyTime; // elapsed time between key press and release
    int mMouseTime; // elapsed time between mouse button press and release
    int mSnapIdx;
    std::string mSnapshotPath;
    int mInspectorMode;
    const moonray::rndr::RenderContext *mRenderContext;
    bool mProgressiveFast;
    moonray::rndr::FastRenderMode mFastMode;
    bool mUseOCIO; // toggles on/off OCIO support

    // Color render override LUT. Set to nullptr if we aren't overriding
    // the LUT. This binary blob is assumed to contain 64*64*64 * RGB float
    // OpenGL compatible volume texture data.
    // This class owns this data and is responsible for deleting it.
    float * mLutOverride;
};

} // namespace moonray_gui

