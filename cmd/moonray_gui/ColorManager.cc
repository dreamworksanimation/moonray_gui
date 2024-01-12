// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include "ColorManager.h"
#include "RenderViewport.h"

#if !defined(DISABLE_OCIO)
    #include <OpenColorIO/OpenColorIO.h>
#endif
#include <QMessageBox>

#include <scene_rdl2/render/util/GetEnv.h>

constexpr double DEFAULT_GAMMA = 2.2;

constexpr double SRGB_LUMA_COEF1 = 0.2126;
constexpr double SRGB_LUMA_COEF2 = 0.7152;
constexpr double SRGB_LUMA_COEF3 = 0.0722;

namespace moonray_gui {
using namespace scene_rdl2::math;
using namespace scene_rdl2::fb_util;

/// -------------------------------- OCIO Helpers --------------------------------------

#if !defined(DISABLE_OCIO)
    OCIO::ExposureContrastTransformRcPtr createExposureTransform(double exposure) 
    {
        OCIO::ExposureContrastTransformRcPtr exposureTransform = OCIO::ExposureContrastTransform::Create();
        exposureTransform->setStyle(OCIO::EXPOSURE_CONTRAST_LINEAR);
        exposureTransform->setExposure(exposure);
        return exposureTransform;
    }

    OCIO::ExponentTransformRcPtr createGammaTransform(double gamma)
    {
        OCIO::ExponentTransformRcPtr gammaTransform = OCIO::ExponentTransform::Create();
        const double gammaExponent = 1.0 / gamma;
        MNRY_ASSERT(gamma > 0.0f);
        const double gammaArr[4] = { gammaExponent, gammaExponent, gammaExponent, gammaExponent };
        gammaTransform->setValue(gammaArr);
        return gammaTransform;
    }

    OCIO::RangeTransformRcPtr createClampTransform(double minClamp, double maxClamp) 
    {
        OCIO::RangeTransformRcPtr rangeTransform = OCIO::RangeTransform::Create();
        rangeTransform->setStyle(OCIO::RANGE_CLAMP);
        rangeTransform->setMinInValue(minClamp);
        rangeTransform->setMaxInValue(maxClamp);
        rangeTransform->setMinOutValue(minClamp);
        rangeTransform->setMaxOutValue(maxClamp);
        return rangeTransform;
    }

    OCIO::MatrixTransformRcPtr createChannelViewTransform(const OCIO::ConstConfigRcPtr& config, std::array<int, 4>& channel) 
    {
        // Channel swizzling
        double lumaCoef1 = scene_rdl2::util::getenv<double>("LUMA_COEF1", SRGB_LUMA_COEF1);
        double lumaCoef2 = scene_rdl2::util::getenv<double>("LUMA_COEF2", SRGB_LUMA_COEF2);
        double lumaCoef3 = scene_rdl2::util::getenv<double>("LUMA_COEF3", SRGB_LUMA_COEF3);
        double lumacoef[3] = { lumaCoef1, lumaCoef2, lumaCoef3 };
        double m44[16];
        double offset[4];

        OCIO::MatrixTransform::View(m44, offset, channel.data(), lumacoef);
        OCIO::MatrixTransformRcPtr swizzle = OCIO::MatrixTransform::Create();
        swizzle->setMatrix(m44);
        swizzle->setOffset(offset);
        return swizzle;
    }

    OCIO::DisplayViewTransformRcPtr createDisplayViewTransform(const OCIO::ConstConfigRcPtr config, bool configIsRaw) 
    {
        // Lookup the display ColorSpace
        const char* display = config->getDefaultDisplay();
        const char* view = config->getDefaultView(display);

        // Create a DisplayViewTransform, and set the input and display ColorSpaces
        OCIO::DisplayViewTransformRcPtr transform = OCIO::DisplayViewTransform::Create();
        transform->setSrc( configIsRaw ? OCIO::ROLE_DEFAULT : OCIO::ROLE_SCENE_LINEAR );
        transform->setDisplay( display );
        transform->setView( view ); 
        return transform;
    }
#endif

/// ------------------------------------ General Helpers -----------------------------------------------

void setHotChannel(DebugMode mode, std::array<int, 4>& channelHot) 
{
    switch (mode) {
    case RGB:
        channelHot = {1, 1, 1, 1};
        break;
    case RED:
        channelHot = {1, 0, 0, 0};
        break;
    case GREEN:
        channelHot = {0, 1, 0, 0};
        break;
    case BLUE:
        channelHot = {0, 0, 1, 0};
        break;
    case ALPHA:
        channelHot = {0, 0, 0, 1};
        break;
    case LUMINANCE:
        channelHot = {1, 1, 1, 0};
        break;
    default:
        channelHot = {1, 1, 1, 1};
        break;
    }
}

void floatBufferToRgb888(const float* src, int w, int h, Rgb888Buffer* dst, int channels) 
{
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            ByteColor col8;
            col8.r = static_cast<uint8_t>(src[y * w * channels + x * channels] * 255);
            col8.g = static_cast<uint8_t>(src[y * w * channels + x * channels + 1] * 255);
            col8.b = static_cast<uint8_t>(src[y * w * channels + x * channels + 2] * 255);
            dst->setPixel(x, y, col8);
        }
    }
}

/// --------------------------------- ColorManager Class -------------------------------------------

#if !defined(DISABLE_OCIO)
    ColorManager::ColorManager() : mConfig(nullptr), mConfigIsRaw(false) {};  
#else
    ColorManager::ColorManager() = default;  
#endif
ColorManager::~ColorManager() = default;

/// Applies color render transform to a render buffer, performing the following ops (not necessarily in order):
///     - transforms from scene-referred space to display-referred
///     - applies other pre-defined transforms, like exposure and user gamma
///     - allows for swizzling between debug modes
///     - quantizes the data to 8-bit (RGB888)
///
/// This function decides whether to use OpenColorIO for these operations, or the code path we were using prior
///
void ColorManager::applyCRT(const MainWindow* mainWindow, 
                            const bool useOCIO, 
                            int renderOutput, 
                            const RenderBuffer& renderBuffer, 
                            const VariablePixelBuffer& renderOutputBuffer,
                            Rgb888Buffer* displayBuffer, 
                            PixelBufferUtilOptions options, 
                            bool parallel) const 
{
    const double exposure = mainWindow->getRenderViewport()->getExposure();
    const double gamma =    mainWindow->getRenderViewport()->getGamma();
    const DebugMode mode =  mainWindow->getRenderViewport()->getDebugMode();

    #if !defined(DISABLE_OCIO)

        OCIO::ConstCPUProcessorRcPtr cpuProcessor;
        if (useOCIO) {
            configureOcio(exposure, gamma, mode, cpuProcessor);
        }

        int numChannels = renderOutputBuffer.getFormat() == VariablePixelBuffer::FLOAT4 || renderOutput < 0 ? 4 : 
                          renderOutputBuffer.getFormat() == VariablePixelBuffer::FLOAT3 ? 3 : -1;  

        if (useOCIO && mode != RGB_NORMALIZED && mode != NUM_SAMPLES && numChannels >= 3) {
            // OCIO code path for RenderBuffer
            if (renderOutput < 0) {

                Vec4<float>* bufConst = const_cast<Vec4<float>*>(renderBuffer.getData());
                applyCRT_Ocio(cpuProcessor, 
                              bufConst, 
                              displayBuffer, 
                              renderBuffer.getWidth(), 
                              renderBuffer.getHeight(), 4);
            }
            // OCIO code path for VariablePixelBuffer
            else if ((renderOutputBuffer.getFormat() == VariablePixelBuffer::FLOAT3 || 
                      renderOutputBuffer.getFormat() == VariablePixelBuffer::FLOAT4)) {

                unsigned char* bufConst = const_cast<unsigned char*>(renderOutputBuffer.getData());
                applyCRT_Ocio(cpuProcessor, 
                              bufConst, 
                              displayBuffer, 
                              renderOutputBuffer.getWidth(), 
                              renderOutputBuffer.getHeight(), 
                              numChannels);
            } 
        }
        // Applies the old color management code if useOCIO is false, mode is RGB_NORMALIZED or NUM_SAMPLES OR
        // if VariablePixelBuffer number of channels < 3 (I haven't found a case where this is true)
        else {
            applyCRT_Legacy(renderBuffer, 
                            renderOutputBuffer,
                            displayBuffer, 
                            renderOutput,
                            exposure, 
                            gamma, 
                            mode,
                            options, 
                            parallel);
        }
    // if < OCIO v2, just use old code path
    #else
        applyCRT_Legacy(renderBuffer, 
                        renderOutputBuffer,
                        displayBuffer, 
                        renderOutput,
                        exposure, 
                        gamma, 
                        mode,
                        options, 
                        parallel);
    #endif
}

void ColorManager::setupConfig() 
{
    #if !defined(DISABLE_OCIO)
        try {
            mConfig = OCIO::Config::CreateFromEnv();
            mConfigIsRaw = scene_rdl2::util::getenv<std::string>("OCIO", "").empty();
        } 
        catch (const OCIO::Exception & exception) {
            std::cerr << "OpenColorIO Error: Invalid filepath provided. A default color profile will be used. " <<
                         "\nMore Info: " << exception.what() << "\n";
            mConfig = OCIO::Config::CreateRaw();
            mConfigIsRaw = true;
        }
    #endif
}

#if !defined(DISABLE_OCIO)

    /// Configures the OpenColorIO transforms to be applied in the following order:
    ///     1. Exposure
    ///     2. User-defined gamma
    ///     3. Swizzle between debug modes
    ///     4. Transforms from scene-referred to display-referred by either:
    ///         - Applying the default display/view provided in an ocio config file
    ///         - Applying a 1/2.2 default gamma if no config file provided
    ///     5. Clamp [0,1]
    /// 
    void ColorManager::configureOcio(double exposure, 
                                     double gamma, 
                                     DebugMode mode, 
                                     OCIO::ConstCPUProcessorRcPtr& cpuProcessor) const
    {
        OCIO::ExposureContrastTransformRcPtr exposureTransform = createExposureTransform(exposure);
        OCIO::ExponentTransformRcPtr userGammaTransform =        createGammaTransform(gamma);
        OCIO::RangeTransformRcPtr rangeTransform =               createClampTransform(0.0, 1.0);

        // Configure the color channel toggle transform
        std::array<int, 4> channelHot;
        setHotChannel(mode, channelHot);
        OCIO::MatrixTransformRcPtr channelViewTransform =        createChannelViewTransform(mConfig, channelHot);
        // Create a DisplayViewTransform, and set the input and display ColorSpaces
        OCIO::DisplayViewTransformRcPtr transform =              createDisplayViewTransform(mConfig, mConfigIsRaw);

        // Create group transform to wrap all of the transforms
        OCIO::GroupTransformRcPtr groupTransform = OCIO::GroupTransform::Create();
        groupTransform->appendTransform(exposureTransform);
        groupTransform->appendTransform(userGammaTransform);
        groupTransform->appendTransform(channelViewTransform);
        groupTransform->appendTransform(transform);
        if (mConfigIsRaw) {
            OCIO::ExponentTransformRcPtr gammaTransform = createGammaTransform(DEFAULT_GAMMA);
            groupTransform->appendTransform(gammaTransform);
        }
        groupTransform->appendTransform(rangeTransform);

        // Create processor for view transform
        OCIO::ConstProcessorRcPtr processor = mConfig->getProcessor(groupTransform);
        cpuProcessor = processor->getDefaultCPUProcessor();   
    }       

    void ColorManager::applyCRT_Ocio(const OCIO::ConstCPUProcessorRcPtr& cpuProcessor, 
                                     void* srcData, 
                                     Rgb888Buffer* destBuf, 
                                     int w, int h, 
                                     int channels)
    {
        // Apply color transforms
        OCIO::PackedImageDesc img(srcData, w, h, channels);
        cpuProcessor->apply(img);
        float* imgOutput = reinterpret_cast<float*>(img.getData());

        destBuf->init(w, h);
        floatBufferToRgb888(imgOutput, w, h, destBuf, channels); 
    }
#endif

void ColorManager::applyCRT_Legacy(const RenderBuffer& renderBuffer, 
                                    const VariablePixelBuffer& renderOutputBuffer,
                                    Rgb888Buffer* displayBuffer, 
                                    int renderOutput,
                                    double exposure, 
                                    double gamma, 
                                    DebugMode mode,
                                    PixelBufferUtilOptions options, 
                                    bool parallel)
{ 
    switch (mode) {
    case RGB:
        // Convert the frame to RGB888 on the current thread (this is called from
        // the main rendering thread). This ensures that the renderer doesn't
        // start writing into this RenderBuffer before we've finished prepping it
        // for display.
        options |= scene_rdl2::fb_util::PIXEL_BUFFER_UTIL_OPTIONS_APPLY_GAMMA;
        renderOutput < 0?
            scene_rdl2::fb_util::gammaAndQuantizeTo8bit(*displayBuffer, renderBuffer,       options, exposure, gamma) :
            scene_rdl2::fb_util::gammaAndQuantizeTo8bit(*displayBuffer, renderOutputBuffer, options, exposure, gamma);
        break;

    case RED:
        renderOutput < 0?
            scene_rdl2::fb_util::extractRedChannel(*displayBuffer, renderBuffer,       options, exposure, gamma) :
            scene_rdl2::fb_util::extractRedChannel(*displayBuffer, renderOutputBuffer, options, exposure, gamma);
        break;

    case GREEN:
        renderOutput < 0?
            scene_rdl2::fb_util::extractGreenChannel(*displayBuffer, renderBuffer,       options, exposure, gamma) :
            scene_rdl2::fb_util::extractGreenChannel(*displayBuffer, renderOutputBuffer, options, exposure, gamma);
        break;

    case BLUE:
        renderOutput < 0?
            scene_rdl2::fb_util::extractBlueChannel(*displayBuffer, renderBuffer,       options, exposure, gamma) :
            scene_rdl2::fb_util::extractBlueChannel(*displayBuffer, renderOutputBuffer, options, exposure, gamma);
        break;

    case ALPHA:
        renderOutput < 0?
            scene_rdl2::fb_util::extractAlphaChannel(*displayBuffer, renderBuffer,       options, exposure, gamma) :
            scene_rdl2::fb_util::extractAlphaChannel(*displayBuffer, renderOutputBuffer, options);
        break;

    case LUMINANCE:
        renderOutput < 0?
            scene_rdl2::fb_util::extractLuminance(*displayBuffer, renderBuffer,       options, exposure, gamma) :
            scene_rdl2::fb_util::extractLuminance(*displayBuffer, renderOutputBuffer, options, exposure, gamma);
        break;

    case RGB_NORMALIZED:
        options |= scene_rdl2::fb_util::PIXEL_BUFFER_UTIL_OPTIONS_APPLY_GAMMA;
        options |= scene_rdl2::fb_util::PIXEL_BUFFER_UTIL_OPTIONS_NORMALIZE;
        renderOutput < 0?
            scene_rdl2::fb_util::gammaAndQuantizeTo8bit(*displayBuffer, renderBuffer,       options, exposure, gamma):
            scene_rdl2::fb_util::gammaAndQuantizeTo8bit(*displayBuffer, renderOutputBuffer, options, exposure, gamma);
        break;

    case NUM_SAMPLES:
        scene_rdl2::fb_util::visualizeSamplesPerPixel(*displayBuffer, renderOutputBuffer.getFloatBuffer(), parallel);
        break;

    default:
        MNRY_ASSERT(0);
    }
} 

} // end moonray_gui namespace

