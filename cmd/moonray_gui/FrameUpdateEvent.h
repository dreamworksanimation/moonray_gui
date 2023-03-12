// Copyright 2023 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <moonray/rendering/rndr/rndr.h>

#include "GuiTypes.h"

#include <QEvent>
namespace moonray_gui {

class FrameUpdateEvent : public QEvent
{
public:
    FrameUpdateEvent(const FrameBuffer &frame, FrameType frameType, DebugMode mode, float exposure, float gamma):
        QEvent(FrameUpdateEvent::type()),
        mFrame(frame),
        mFrameType(frameType),
        mDebugMode(mode),
        mExposure(exposure),
        mGamma(gamma)
    {
    }

    const FrameBuffer &getFrame() const { return mFrame; }
    FrameType getFrameType() const { return mFrameType; }
    DebugMode getDebugMode() const { return mDebugMode; }
    float getExposure() const { return mExposure; }
    float getGamma() const { return mGamma; }
    static QEvent::Type type() { return sEventType; }

private:
    FrameBuffer mFrame;
    FrameType mFrameType;
    DebugMode mDebugMode;
    float mExposure;
    float mGamma;
    static QEvent::Type sEventType;
};

} // namespace moonray_gui

