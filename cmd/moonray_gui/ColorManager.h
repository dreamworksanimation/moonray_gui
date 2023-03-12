// Copyright 2023 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

#include "MainWindow.h"
#include <scene_rdl2/common/fb_util/PixelBufferUtilsGamma8bit.h>

#if !defined(DISABLE_OCIO)
    #include <OpenColorIO/OpenColorIO.h>

    namespace OCIO = OCIO_NAMESPACE;
#endif

namespace moonray_gui {
class ColorManager 
{
public:
    ColorManager();
    ~ColorManager();

    void applyCRT(const MainWindow* mainWindow, 
                  const bool useOCIO, 
                  int renderOutput, 
                  const fb_util::RenderBuffer& renderBuffer, 
                  const fb_util::VariablePixelBuffer& renderOutputBuffer,
                  fb_util::Rgb888Buffer* displayBuffer, 
                  fb_util::PixelBufferUtilOptions options, 
                  bool parallel) const;
    
    void setupConfig();

private:
    #if !defined(DISABLE_OCIO)

        OCIO::ConstConfigRcPtr mConfig;
        bool mConfigIsRaw;
    
        // read config, define OCIO transforms, initialize processors 
        void configureOcio(double exposure, 
                           double gamma, 
                           DebugMode mode, 
                           OCIO::ConstCPUProcessorRcPtr& cpuProcessor) const;

        // apply color transformations using OCIO
        static void applyCRT_Ocio(const OCIO::ConstCPUProcessorRcPtr& cpuProcessor, 
                                  void* srcData, 
                                  scene_rdl2::fb_util::Rgb888Buffer* destBuf,
                                  int w, int h, 
                                  int channels);
    #endif

    // apply color transformations using previous non-OCIO code
    static void applyCRT_Legacy(const fb_util::RenderBuffer& renderBuffer, 
                                const fb_util::VariablePixelBuffer& renderOutputBuffer,
                                fb_util::Rgb888Buffer* displayBuffer, 
                                int renderOutput,
                                double exposure, 
                                double gamma, 
                                DebugMode mode,
                                fb_util::PixelBufferUtilOptions options, 
                                bool parallel);
}; 
}

