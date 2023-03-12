// Copyright 2023 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "FrameUpdateEvent.h"

#include <QEvent>
namespace moonray_gui {

QEvent::Type FrameUpdateEvent::sEventType =
        static_cast<QEvent::Type>(QEvent::registerEventType());

} // namespace moonray_gui

