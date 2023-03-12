// Copyright 2023 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once
#include <scene_rdl2/common/math/Mat4.h>

class QKeyEvent;
class QMouseEvent;

namespace moonray { namespace rndr { class RenderContext; } }

namespace moonray_gui {
///
/// Pure virtual base class which further navigation models may be implemented
/// on top of.
///
class NavigationCam
{
public:
                        NavigationCam() {}
    virtual             ~NavigationCam() {}

    // Certain types of camera may want to intersect with the scene, in which
    // case they'll need more information about the scene. This function does
    // nothing by default.
    virtual void        setRenderContext(const moonray::rndr::RenderContext &context) {}

    // If this camera model imposes any constraints on the input matrix, then
    // the constrained matrix is returned, otherwise the output will equal in 
    // input.
    // If makeDefault is set to true then this xform is designated a the new
    // default transform when/if the camera is reset.
    virtual scene_rdl2::math::Mat4f resetTransform(const scene_rdl2::math::Mat4f &xform, bool makeDefault) = 0;

    // Returns the latest camera matrix.
    virtual scene_rdl2::math::Mat4f update(float dt) = 0;

    /// Returns true if the input was used, false to pass the input to a higher
    /// level handler.
    virtual bool        processKeyboardEvent(QKeyEvent *event, bool pressed) { return false; }
    virtual bool        processMousePressEvent(QMouseEvent *event, int key) { return false; }
    virtual bool        processMouseReleaseEvent(QMouseEvent *event) { return false; }
    virtual bool        processMouseMoveEvent(QMouseEvent *event) { return false; }
    virtual void        clearMovementState() {};
};

} // namespace moonray_gui


