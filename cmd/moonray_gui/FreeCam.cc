// Copyright 2023 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include "FreeCam.h"
#include <QKeyEvent>
#include <QMouseEvent>

// must be between 0 and 1
#define FREECAM_MAX_DAMPENING   0.1f
#define NO_KEY                  -1

using namespace scene_rdl2::math;

namespace {

Mat4f
makeMatrix(float yaw, float pitch, float roll, const Vec3f &pos)
{
    Mat4f rotYaw, rotPitch, rotRoll;
    rotYaw.setToRotation(Vec4f(0.0f, 1.0f, 0.0f, 0.0f), yaw);
    rotPitch.setToRotation(Vec4f(1.0f, 0.0f, 0.0f, 0.0f), pitch);
    rotRoll.setToRotation(Vec4f(0.0f, 0.0f, 1.0f, 0.0f), roll);
    Mat4f rotation = rotRoll * rotPitch * rotYaw;

    return rotation * Mat4f::translate(Vec4f(pos.x, pos.y, pos.z, 1.0f));
}

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
    FREECAM_FORWARD     = 0x0001,
    FREECAM_BACKWARD    = 0x0002,
    FREECAM_LEFT        = 0x0004,
    FREECAM_RIGHT       = 0x0008,
    FREECAM_UP          = 0x0010,
    FREECAM_DOWN        = 0x0020,
    FREECAM_SLOW_DOWN   = 0x0040,
    FREECAM_SPEED_UP    = 0x0080
};

//----------------------------------------------------------------------------

FreeCam::FreeCam() :
    mPosition(0.0f, 0.0f, 0.0f),
    mVelocity(0.0f, 0.0f, 0.0f),
    mYaw(0.0f),
    mPitch(0.0f),
    mRoll(0.0f),
    mSpeed(10.0f),
    mDampening(1.0f),
    mMouseSensitivity(0.004f),
    mInputState(0),
    mMouseMode(NONE),
    mMouseX(0),
    mMouseY(0),
    mMouseDeltaX(0),
    mMouseDeltaY(0),
    mInitialTransformSet(false)
{
}

FreeCam::~FreeCam()
{
}

Mat4f
FreeCam::resetTransform(const Mat4f &xform, bool makeDefault)
{
    if (!mInitialTransformSet || makeDefault) {
        mInitialTransform = xform;
        mInitialTransformSet = true;
    }

    mPosition = asVec3(xform.row3());
    mVelocity = Vec3f(zero);

    Vec3f viewDir = -normalize(asVec3(xform.row2()));

    mYaw = 0.0f;
    if (viewDir.x * viewDir.x + viewDir.z * viewDir.z > 0.00001f) {
        mYaw = atan2(-viewDir.x, -viewDir.z);
    }

    // We aren't extracting the entire range of possible pitches here, just the
    // ones which the freecam can natively handle. Because of this, not all camera
    // orientations are supported.
    mPitch = asin(viewDir.y);

    // Compute a matrix which only contains the roll so we can extract it out.
    Mat4f noRoll = makeMatrix(mYaw, mPitch, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    Mat4f rollOnly = xform * noRoll.transposed();
    Vec3f xAxis = normalize(asVec3(rollOnly.row0()));
    mRoll = atan2(xAxis.y, xAxis.x);

    mInputState = 0;
    mMouseMode = NONE; 
    mMouseX = mMouseY = 0;    
    mMouseDeltaX = mMouseDeltaY = 0;

    return makeMatrix(mYaw, mPitch, mRoll, mPosition);
}

Mat4f
FreeCam::update(float dt)
{
    // Compute some amount to change our current velocity.
    Vec3f deltaVelocity = Vec3f(zero);
    float movement = mSpeed * 0.5f;

    // Process keyboard input.
    if (mInputState & FREECAM_FORWARD) {
        deltaVelocity += Vec3f(0.0f, 0.0f, -movement);
    }
    if (mInputState & FREECAM_BACKWARD) {
        deltaVelocity += Vec3f(0.0f, 0.0f, movement);
    }
    if (mInputState & FREECAM_LEFT) {
        deltaVelocity += Vec3f(-movement, 0.0f, 0.0f);
    }
    if (mInputState & FREECAM_RIGHT) {
        deltaVelocity += Vec3f(movement, 0.0f, 0.0f);
    }
    if (mInputState & FREECAM_UP) {
        deltaVelocity += Vec3f(0.0f, movement, 0.0f);
    }
    if (mInputState & FREECAM_DOWN) {
        deltaVelocity += Vec3f(0.0f, -movement, 0.0f);
    }
    if (mInputState & FREECAM_SLOW_DOWN) {
        mSpeed += -mSpeed * dt;
    }
    if (mInputState & FREECAM_SPEED_UP) {
        mSpeed += mSpeed * dt;
    }

    // Update the camera angles by the rotation amounts (ignore dt for this
    // since it should be instant).
    if (mMouseMode == MOVE) {

        // rotate mouse movement by roll before updating yaw and pitch
        float c, s;
        sincos(-mRoll, &s, &c);

        float dx = float(mMouseDeltaX) * c - float(mMouseDeltaY) * s;
        float dy = float(mMouseDeltaY) * c + float(mMouseDeltaX) * s;

        mYaw -= dx * mMouseSensitivity;
        mPitch -= dy * mMouseSensitivity;

    } else if (mMouseMode == ROLL) {
        mRoll += float(mMouseDeltaX) * mMouseSensitivity;
    }
    mMouseDeltaX = mMouseDeltaY = 0;

    // Clip camera pitch to prevent Gimbal Lock.
    const float halfPi = sHalfPi;
    mPitch = clamp(mPitch, -halfPi, halfPi);

    // Transform deltaVelocity into current camera coordinate system.
    Mat4f rotation = makeMatrix(mYaw, mPitch, mRoll, zero);
    deltaVelocity = transform3x3(rotation, deltaVelocity);

    mVelocity += deltaVelocity;

    // Scale back velocity to mSpeed if too big.
    float len = mVelocity.length();
    if (len > mSpeed) {
        mVelocity *= (mSpeed / len);
    }

    // Integrate position.
    mPosition += mVelocity * dt;

    // Apply dampening to velocity.
    mVelocity *= min(mDampening * dt, FREECAM_MAX_DAMPENING);

    return makeMatrix(mYaw, mPitch, mRoll, mPosition);
}

bool
FreeCam::processKeyboardEvent(QKeyEvent *event, bool pressed)
{
    bool used = false;

    if (event->modifiers() == Qt::NoModifier) {

        used = true;

        if (pressed) {
            // Check for pressed keys.
            switch (event->key()) {
            case Qt::Key_W:     mInputState |= FREECAM_FORWARD;     break;
            case Qt::Key_S:     mInputState |= FREECAM_BACKWARD;    break;
            case Qt::Key_A:     mInputState |= FREECAM_LEFT;        break;
            case Qt::Key_D:     mInputState |= FREECAM_RIGHT;       break;
            case Qt::Key_Space: mInputState |= FREECAM_UP;          break;
            case Qt::Key_C:     mInputState |= FREECAM_DOWN;        break;
            case Qt::Key_Q:     mInputState |= FREECAM_SLOW_DOWN;   break;
            case Qt::Key_E:     mInputState |= FREECAM_SPEED_UP;    break;
            case Qt::Key_T:     printCameraMatrices();              break;
            case Qt::Key_U:     mRoll = 0.0f;                       break;
            case Qt::Key_R:
                if(mInitialTransformSet) {
                    clearMovementState();
                    resetTransform(mInitialTransform, false);
                }
                break;
            default: used = false;
            }
        } else {
            // Check for released keys.
            switch (event->key()) {
            case Qt::Key_W:     mInputState &= ~FREECAM_FORWARD;    break;
            case Qt::Key_S:     mInputState &= ~FREECAM_BACKWARD;   break;
            case Qt::Key_A:     mInputState &= ~FREECAM_LEFT;       break;
            case Qt::Key_D:     mInputState &= ~FREECAM_RIGHT;      break;
            case Qt::Key_Space: mInputState &= ~FREECAM_UP;         break;
            case Qt::Key_C:     mInputState &= ~FREECAM_DOWN;       break;
            case Qt::Key_Q:     mInputState &= ~FREECAM_SLOW_DOWN;  break;
            case Qt::Key_E:     mInputState &= ~FREECAM_SPEED_UP;   break;
            default: used = false;
            }
        }
    }

    return used;
}

bool
FreeCam::processMousePressEvent(QMouseEvent *event, int key)
{
    mMouseMode = NONE;
    if (event->buttons() == Qt::LeftButton &&
        event->modifiers() == Qt::NoModifier && key == NO_KEY) {
        mMouseMode = MOVE;
    } else if (event->buttons() == (Qt::LeftButton | Qt::RightButton) &&
               event->modifiers() == Qt::AltModifier) {
        mMouseMode = ROLL;
    }

    if (mMouseMode != NONE) {
        mMouseX = event->x();
        mMouseY = event->y();
        mMouseDeltaX = mMouseDeltaY = 0;
        return true;
    }

    return false;
}

bool
FreeCam::processMouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        mMouseMode = NONE;
        return true;
    }
    return false;
}

bool
FreeCam::processMouseMoveEvent(QMouseEvent *event)
{
    if (mMouseMode == MOVE || mMouseMode == ROLL) {
        mMouseDeltaX += (event->x() - mMouseX); 
        mMouseDeltaY += (event->y() - mMouseY); 
        mMouseX = event->x();
        mMouseY = event->y();
        return true;
    }
    return false;
}

void
FreeCam::clearMovementState()
{
    mVelocity = Vec3f(0.0f, 0.0f, 0.0f);
    mInputState = 0;
    mMouseMode = NONE;
    mMouseX = 0;
    mMouseY = 0;
}

void
FreeCam::printCameraMatrices() const
{
    Mat4f fullMat = makeMatrix(mYaw, mPitch, mRoll, mPosition);
    Mat4f zeroPitchMat = makeMatrix(mYaw, 0.0f, 0.0f, mPosition);

    printMatrix("Full matrix containing rotation and position.", fullMat);
    printMatrix("Matrix containing world xz rotation and position.", zeroPitchMat);
}

//----------------------------------------------------------------------------

} // namespace moonray_gui

