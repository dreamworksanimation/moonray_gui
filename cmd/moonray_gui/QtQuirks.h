// Copyright 2023 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

// Include this header before any Qt headers to compile with icc14/gcc48.

// These should totally not be necessary, but BaRT is black magic and I have
// absolutely no idea WTF it's doing.
#define slots Q_SLOTS
#define signals Q_SIGNALS

