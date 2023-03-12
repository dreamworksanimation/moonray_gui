// Copyright 2023 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

///
/// Controls:
///
/// alt + LMB                   - orbit around pivot point
/// alt + MMB                   - pan
/// alt + RMB                   - dolly (zoom in and out)
/// alt + LMB + RMB             - roll
/// ctrl + LMB                  - refocus on point under mouse cursor
/// 
/// W                           - forward
/// S                           - backward
/// A                           - left
/// D                           - right
/// Space                       - up
/// C                           - down
/// Q                           - slow down
/// E                           - speed up
/// R                           - reset to original startup location in world
/// U                           - upright camera (remove roll)
/// T                           - print current camera matrix to console in lua format
/// F                           - alternate key to refocus on point under mouse cursor
///

#pragma once
#include "NavigationCam.h"

namespace moonray_gui {

struct Camera;

class OrbitCam : public NavigationCam
{
public:
                        OrbitCam();
                        ~OrbitCam();

    void                setRenderContext(const moonray::rndr::RenderContext &context) override;

    /// The active render context should be set before calling this function.
    scene_rdl2::math::Mat4f  resetTransform(const scene_rdl2::math::Mat4f &xform, bool makeDefault) override;

    scene_rdl2::math::Mat4f  update(float dt) override;

    /// Returns true if the input was used, false if discarded.
    bool                processKeyboardEvent(QKeyEvent *event, bool pressed) override;
    bool                processMousePressEvent(QMouseEvent *event, int key) override;
    bool                processMouseMoveEvent(QMouseEvent *event) override;
    void                clearMovementState() override;

private:
    enum MouseMode
    {
        NONE,
        ORBIT,
        PAN,
        DOLLY,
        ROLL,
        ROTATE_CAMERA,
    };

    // Run a center-pixel "pick" operation to compute camera focus
    void                pickFocusPoint();

    void                recenterCamera();
    bool                pick(int x, int y, scene_rdl2::math::Vec3f *hitPoint) const;
    scene_rdl2::math::Mat4f  makeMatrix(const Camera &camera) const;
    void                printCameraMatrices() const;

    const moonray::rndr::RenderContext *mRenderContext;
    Camera *            mCamera;

    float               mSpeed;
    uint32_t            mInputState;
    MouseMode           mMouseMode;
    int                 mMouseX;
    int                 mMouseY;

    bool                mInitialTransformSet;
    bool                mInitialFocusSet;
    scene_rdl2::math::Vec3f  mInitialPosition;
    scene_rdl2::math::Vec3f  mInitialViewDir;
    scene_rdl2::math::Vec3f  mInitialUp;
    float               mInitialFocusDistance;
};

} // namespace moonray_gui

