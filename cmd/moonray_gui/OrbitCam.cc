// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "OrbitCam.h"
#include <moonray/rendering/rndr/RenderContext.h>
#include <scene_rdl2/common/math/Viewport.h>
#include <scene_rdl2/common/platform/Platform.h>
#include <QKeyEvent>
#include <QMouseEvent>

using namespace scene_rdl2::math;

namespace {

// Print out matrix in lua format so it can be pasted into an rdla file.
void printMatrix(const char *comment, const Mat4f &m)
{
    std::cout << "-- " << comment << "\n"
    << "[\"node xform\"] = Mat4("
    << m.vx.x << ", " << m.vx.y << ", " << m.vx.z << ", " << m.vx.w << ", "
    << m.vy.x << ", " << m.vy.y << ", " << m.vy.z << ", " << m.vy.w << ", "
    << m.vz.x << ", " << m.vz.y << ", " << m.vz.z << ", " << m.vz.w << ", "
    << m.vw.x << ", " << m.vw.y << ", " << m.vw.z << ", " << m.vw.w << "),\n"
    << std::endl;
}

}   // end of anon namespace

namespace moonray_gui {

enum
{
    ORBIT_FORWARD     = 0x0001,
    ORBIT_BACKWARD    = 0x0002,
    ORBIT_LEFT        = 0x0004,
    ORBIT_RIGHT       = 0x0008,
    ORBIT_UP          = 0x0010,
    ORBIT_DOWN        = 0x0020,
    ORBIT_SLOW_DOWN   = 0x0040,
    ORBIT_SPEED_UP    = 0x0080
};

// orbit camera (taken from embree sample code)
// This camera is in world space
struct Camera {

    Camera () :
        position(0.0f, 0.0f, -3.0f),
        viewDir(normalize(-position)),
        up(0.0f, 1.0f, 0.0f),
        focusDistance(1.0f) {}

    Xform3f camera2world () const {
        // Warning: this needs to be double precision.  If we use single then
        // there is slight imprecision introduced when computing the cross products
        // when orthonormalizing the vectors.
        // This normally wouldn't be a problem, but this camera2world matrix
        // gets fed into OrbitCam::resetTransform() when the scene is reloaded.
        // OrbitCam::resetTransform() then sets the vectors used for camera2world,
        // but those came from camera2world.  Thus camera2world is used to set
        // itself, and the old value might be identical to the new if the user
        // hasn't manipulated the camera.
        // The imprecision from the single-precision cross products causes
        // a slight difference in camera2world when there should be no change
        // at all when camera2world hasn't changed.  This causes nondeterminism
        // between successive renders in moonray_gui as this has a slight effect
        // on the ray directions each time.
        Vec3d vz = -viewDir;
        Vec3d vx = normalize(cross(Vec3d(up), vz));
        Vec3d vy = normalize(cross(vz, vx));
        return Xform3f(
            static_cast<float>(vx.x), static_cast<float>(vx.y),
            static_cast<float>(vx.z), static_cast<float>(vy.x),
            static_cast<float>(vy.y), static_cast<float>(vy.z),
            static_cast<float>(vz.x), static_cast<float>(vz.y),
            static_cast<float>(vz.z), position.x, position.y, position.z);
    }

    Xform3f world2camera () const { return rcp(camera2world());}

    Vec3f world2camera(const Vec3f& p) const { return transformPoint(world2camera(),p);}
    Vec3f camera2world(const Vec3f& p) const { return transformPoint(camera2world(),p);}

    void move (float dx, float dy, float dz)
    {
        float moveSpeed = 0.03f;
        dx *= -moveSpeed;
        dy *= moveSpeed;
        dz *= moveSpeed;
        Xform3f xfm = camera2world();
        Vec3f ds = transformVector(xfm, Vec3f(dx,dy,dz));
        position += ds;
    }

    void rotate (float dtheta, float dphi)
    {
        float rotateSpeed = 0.005f;
        // in camera local space, viewDir is always (0, 0, -1)
        // and its spherical coordinate is always (PI, 0)
        float theta = sPi - dtheta * rotateSpeed;
        float phi   = -dphi * rotateSpeed;

        float cosPhi, sinPhi;
        sincos(phi, &sinPhi, &cosPhi);
        float cosTheta, sinTheta;
        sincos(theta, &sinTheta, &cosTheta);

        float x = cosPhi*sinTheta;
        float y = sinPhi;
        float z = cosPhi*cosTheta;

        viewDir = transformVector(camera2world(), Vec3f(x, y, z));
    }

    void rotateOrbit (float dtheta, float dphi)
    {
        bool currentlyValid = false;
        if (scene_rdl2::math::abs(dot(up, viewDir)) < 0.999f) {
            currentlyValid = true;
        }

        float rotateSpeed = 0.005f;
        // in camera local space, viewDir is always (0, 0, -1)
        // and its spherical coordinate is always (PI, 0)
        float theta = sPi - dtheta * rotateSpeed;
        float phi   = -dphi * rotateSpeed;

        float cosPhi, sinPhi;
        sincos(phi, &sinPhi, &cosPhi);
        float cosTheta, sinTheta;
        sincos(theta, &sinTheta, &cosTheta);

        float x = cosPhi*sinTheta;
        float y = sinPhi;
        float z = cosPhi*cosTheta;

        Vec3f newViewDir = transformVector(camera2world(),Vec3f(x,y,z));
        Vec3f newPosition = position + focusDistance * (viewDir - newViewDir);

        // Don't update 'position' if dir is near parallel with the up vector
        // unless the current state of 'position' is already invalid.
        if (scene_rdl2::math::abs(dot(up, newViewDir)) < 0.999f || !currentlyValid) {
            position = newPosition;
            viewDir = newViewDir;
        }
    }

    void dolly (float ds)
    {
        float dollySpeed = 0.005f;
        float k = scene_rdl2::math::pow((1.0f-dollySpeed), ds);
        Vec3f focusPoint = position + viewDir * focusDistance;
        position += focusDistance * (1-k) * viewDir;
        focusDistance = length(focusPoint - position);
    }

    void roll (float ds)
    {
        float rollSpeed = 0.005f;
        const Vec3f& axis = viewDir;
        up = transform3x3(Mat4f::rotate(Vec4f(axis[0], axis[1], axis[2], 0.0f),
            -ds * rollSpeed), up);
    }

public:
    Vec3f position;   //!< position of camera
    Vec3f viewDir;    //!< lookat direction
    Vec3f up;         //!< up vector
    float focusDistance;
};

//----------------------------------------------------------------------------

OrbitCam::OrbitCam() :
    mRenderContext(nullptr),
    mCamera(new Camera),
    mSpeed(50.0f),
    mInputState(0),
    mMouseMode(NONE),
    mMouseX(-1),
    mMouseY(-1),
    mInitialTransformSet(false),
    mInitialFocusSet(false),
    mInitialFocusDistance(1.0f)
{
}

OrbitCam::~OrbitCam()
{
    delete mCamera;
}

void 
OrbitCam::setRenderContext(const moonray::rndr::RenderContext &context)
{
    mRenderContext = &context;
}

Mat4f
OrbitCam::resetTransform(const Mat4f &xform, bool makeDefault)
{
    MNRY_ASSERT(mCamera);

    mCamera->position = asVec3(xform.vw);
    mCamera->viewDir = normalize(asVec3(-xform.vz));
    mCamera->up = asVec3(xform.vy);
    mCamera->focusDistance = 1.0f;

    if (!mInitialTransformSet || makeDefault) {
        mInitialTransformSet = true;
        mInitialFocusSet = false;
        mInitialPosition = mCamera->position;
        mInitialViewDir = mCamera->viewDir;
        mInitialUp = mCamera->up;
        mInitialFocusDistance = mCamera->focusDistance;
    }

    return xform;
}

void
OrbitCam::pickFocusPoint()
{
    MNRY_ASSERT(mCamera);
    
    // Ensure the render context exists.
    // Key bindings can call this function before everything is fully ready.
    if (!mRenderContext) return;
    
    // Do this function only once every time we reset the default transform
    // Note: We can't do picking during resetTransform() because picking uses
    // the pbr Scene, which hasn't been initialized at that time.
    if (mInitialFocusSet) {
        return;
    }
    mInitialFocusSet = true;

    const scene_rdl2::math::HalfOpenViewport vp = mRenderContext->getRezedRegionWindow();
    int width = int(vp.width());
    int height = int(vp.height());
    Vec3f focusPoint;
    if (pick(width / 2, height / 2, &focusPoint)) {
        Vec3f hitVec = focusPoint - mCamera->position;
        mCamera->viewDir = normalize(hitVec);
        mCamera->focusDistance = length(hitVec);
    }
    mInitialViewDir = mCamera->viewDir;
    mInitialFocusDistance = mCamera->focusDistance;
}

Mat4f
OrbitCam::update(float dt)
{
    float movement = mSpeed * dt;

    // Process keyboard input.
    if (mInputState & ORBIT_FORWARD) {
        mCamera->move(0.0f, 0.0f, -movement);
    }
    if (mInputState & ORBIT_BACKWARD) {
        mCamera->move(0.0f, 0.0f, movement);
    }
    if (mInputState & ORBIT_LEFT) {
        mCamera->move(movement, 0.0f, 0.0f);
    }
    if (mInputState & ORBIT_RIGHT) {
        mCamera->move(-movement, 0.0f, 0.0f);
    }
    if (mInputState & ORBIT_UP) {
        mCamera->move(0.0f, movement, 0.0f);
    }
    if (mInputState & ORBIT_DOWN) {
        mCamera->move(0.0f, -movement, 0.0f);
    }
    if (mInputState & ORBIT_SLOW_DOWN) {
        mSpeed += -mSpeed * dt;
    }
    if (mInputState & ORBIT_SPEED_UP) {
        mSpeed += mSpeed * dt;
    }

    return makeMatrix(*mCamera);
}

bool
OrbitCam::processKeyboardEvent(QKeyEvent *event, bool pressed)
{
    bool used = false;

    if (event->modifiers() == Qt::NoModifier) {

        used = true;

        if (pressed) {
            pickFocusPoint();

            // Check for pressed keys.
            switch (event->key()) {
            case Qt::Key_W:     mInputState |= ORBIT_FORWARD;     break;
            case Qt::Key_S:     mInputState |= ORBIT_BACKWARD;    break;
            case Qt::Key_A:     mInputState |= ORBIT_LEFT;        break;
            case Qt::Key_D:     mInputState |= ORBIT_RIGHT;       break;
            case Qt::Key_Space: mInputState |= ORBIT_UP;          break;
            case Qt::Key_C:     mInputState |= ORBIT_DOWN;        break;
            case Qt::Key_Q:     mInputState |= ORBIT_SLOW_DOWN;   break;
            case Qt::Key_E:     mInputState |= ORBIT_SPEED_UP;    break;
            case Qt::Key_F:     recenterCamera();                 break;
            case Qt::Key_T:     printCameraMatrices();            break;
            case Qt::Key_U:     mCamera->up = Vec3f(0.0f, 1.0f, 0.0f); break;
            case Qt::Key_R:
                if(mInitialTransformSet) {
                    clearMovementState();
                    mCamera->position = mInitialPosition;
                    mCamera->viewDir = mInitialViewDir;
                    mCamera->up = mInitialUp;
                    mCamera->focusDistance = mInitialFocusDistance;
                }
                break;
            default: used = false;
            }
        } else {
            // Check for released keys.
            switch (event->key()) {
            case Qt::Key_W:     mInputState &= ~ORBIT_FORWARD;    break;
            case Qt::Key_S:     mInputState &= ~ORBIT_BACKWARD;   break;
            case Qt::Key_A:     mInputState &= ~ORBIT_LEFT;       break;
            case Qt::Key_D:     mInputState &= ~ORBIT_RIGHT;      break;
            case Qt::Key_Space: mInputState &= ~ORBIT_UP;         break;
            case Qt::Key_C:     mInputState &= ~ORBIT_DOWN;       break;
            case Qt::Key_Q:     mInputState &= ~ORBIT_SLOW_DOWN;  break;
            case Qt::Key_E:     mInputState &= ~ORBIT_SPEED_UP;   break;
            default: used = false;
            }
        }
    }

    return used;
}

bool
OrbitCam::processMousePressEvent(QMouseEvent *event, int key)
{
    pickFocusPoint();

    mMouseMode = NONE;
    auto buttons = event->buttons();
    auto modifiers = event->modifiers();

    mMouseX = event->x();
    mMouseY = event->y();

    bool used = false;

    if (modifiers == Qt::AltModifier) {
        if (buttons == Qt::LeftButton) {
            mMouseMode = ORBIT;
            used = true;
        } else if (buttons == Qt::MiddleButton) {
            mMouseMode = PAN;
            used = true;
        } else if (buttons == Qt::RightButton) {
            mMouseMode = DOLLY;
            used = true;
        } else if (buttons == (Qt::LeftButton | Qt::RightButton)) {
            mMouseMode = ROLL;
            used = true;
        }
    } else if (modifiers == Qt::ControlModifier) {
        if (buttons == Qt::LeftButton) {
            mMouseMode = NONE;
            recenterCamera();
            used = true;
        }
    }

    return used;
}

bool
OrbitCam::processMouseMoveEvent(QMouseEvent *event)
{
    if (mMouseX == -1 || mMouseY == -1) {
        return false;
    }

    int x = event->x(); 
    int y = event->y(); 
    float dClickX = float(x - mMouseX);
    float dClickY = float(y - mMouseY);
    mMouseX = x;
    mMouseY = y;

    switch (mMouseMode) {
    case ORBIT:         mCamera->rotateOrbit(dClickX, dClickY); break;
    case PAN:           mCamera->move(dClickX, dClickY, 0.0f); break;
    case DOLLY:         mCamera->dolly(dClickX + dClickY); break;
    case ROLL:          mCamera->roll(dClickX); break;
    case ROTATE_CAMERA: mCamera->rotate(dClickX, dClickY); break;
    default: return false;
    }

    return true;
}

void
OrbitCam::clearMovementState()
{
    mInputState = 0;
    mMouseMode = NONE;
    mMouseX = -1;
    mMouseY = -1;
}

void
OrbitCam::recenterCamera()
{
    if (mMouseX == -1 || mMouseY == -1) {
        return;
    }

    Vec3f newFocus;
    if (pick(mMouseX, mMouseY, &newFocus)) {
        Vec3f delta = newFocus -
            (mCamera->position + mCamera->viewDir * mCamera->focusDistance);
        mCamera->position += delta;
        mCamera->focusDistance = length(newFocus - mCamera->position);
    }

    // reset mouse positions so repeatedly pressing F does not result in
    // repeated recentering.
    mMouseX = mMouseY = -1;
}

bool
OrbitCam::pick(int x, int y, Vec3f *hitPoint) const
{
    MNRY_ASSERT(mRenderContext);

    // must use offset between center point of aperture window and center point
    // of region window so that the region window is centered on the pick point.
    const scene_rdl2::math::HalfOpenViewport avp = mRenderContext->getRezedApertureWindow();
    const scene_rdl2::math::HalfOpenViewport rvp = mRenderContext->getRezedRegionWindow();
    const int offsetX = (avp.max().x + avp.min().x) / 2 - (rvp.max().x + rvp.min().x) / 2;
    const int offsetY = (avp.max().y + avp.min().y) / 2 - (rvp.max().y + rvp.min().y) / 2;

    return mRenderContext->handlePickLocation(x + offsetX, y - offsetY, hitPoint);
}

Mat4f
OrbitCam::makeMatrix(const Camera &camera) const
{
    Xform3f c2w = camera.camera2world();
    return Mat4f( Vec4f(c2w.l.vx.x, c2w.l.vx.y, c2w.l.vx.z, 0.0f),
                  Vec4f(c2w.l.vy.x, c2w.l.vy.y, c2w.l.vy.z, 0.0f),
                  Vec4f(c2w.l.vz.x, c2w.l.vz.y, c2w.l.vz.z, 0.0f),
                  Vec4f(c2w.p.x,    c2w.p.y,    c2w.p.z,    1.0f) );
}

void
OrbitCam::printCameraMatrices() const
{
    Mat4f fullMat = makeMatrix(*mCamera);
    printMatrix("Full matrix containing rotation and position.", fullMat);
}

//----------------------------------------------------------------------------

} // namespace moonray_gui

