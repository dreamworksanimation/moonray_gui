// Copyright 2023 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include "RenderViewport.h"
#include <moonray/rendering/rndr/rndr.h>
#include <moonray/rendering/rndr/RenderOutputDriver.h>

#include <scene_rdl2/common/platform/Platform.h>
#include <scene_rdl2/render/logging/logging.h>
#include <scene_rdl2/scene/rdl2/Geometry.h>
#include <scene_rdl2/scene/rdl2/Light.h>
#include <scene_rdl2/scene/rdl2/Material.h>

#include <sys/param.h>
#include <sys/stat.h>
#include <unistd.h>

#include <QtGui>
#include <QInputDialog>
#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>

using namespace moonray;
using namespace scene_rdl2::logging;

namespace moonray_gui {

namespace {

moonray::rndr::FastRenderMode
nextFastMode(moonray::rndr::FastRenderMode const &mode) {
    const int numModes = static_cast<int>(moonray::rndr::FastRenderMode::NUM_MODES);
    return static_cast<moonray::rndr::FastRenderMode>((static_cast<int>(mode) + 1) % numModes);
}

moonray::rndr::FastRenderMode
prevFastMode(moonray::rndr::FastRenderMode const &mode) {
    const int numModes = static_cast<int>(moonray::rndr::FastRenderMode::NUM_MODES);
    return static_cast<moonray::rndr::FastRenderMode>((static_cast<int>(mode) + numModes - 1) % numModes);
}

}

// Moonray GUI Controls:
const char *RenderViewport::mHelp = R"(
W: forward
S: backward
A: left
D: right
Space: up
C: down
Q: slow down
E: speed up
R: reset to original world-location
U: upright camera
T: print current camera matrix
I: cycle through pixel inspector modes
O: toggle between orbitcam and freecam
P: toggle show tiled progress
`: toggle RGB
1: toggle red
2: toggle green
3: toggle blue
4: toggle alpha
5: toggle luminance
7: toggle normalized RGB mode
,: move to previous render output
.: move to next render output
K: Take snapshot
L: Toogle fast progressive mode
Alt + Up/Down: Switch between fast render modes
X hold + LMB drag: start exposure update
Y hold + LMB drag: start gamma update
X + LMB tap: reset exposure
Y + LMB tap: reset gamma
X tap: set exposure
Y tap: set gamma
Shift + Up/Down: increment/decrement exposure by 1
H: hotkey guide
N: deNoising on/off
Shift + N: deNoising mode: Optix / Open Image Denoise
B: toggle Buffers to use for denoising
Z: toggle OCIO support on/off

Free Cam:
LMB drag: rotate around camera position
Alt + LMB + RMB: roll

Orbit Cam:
Alt + LMB: orbit around pivot point
Alt + MMB: pan
Alt + RMB: dolly
Alt + LMB + RMB: roll
Ctrl + LMB: refocus on point under mouse cursor
F: refocus on point under mouse cursor)";

RenderViewport::RenderViewport(QWidget* parent, CameraType intialType, const char *crtOverride, const std::string& snapPath) :
    QWidget(parent),
    mImageLabel(nullptr),
    mGlslBuffer(nullptr),
    mWidth(-1),
    mHeight(-1),
    mActiveCameraType(intialType),
    mShowTileProgress(true),
    mApplyColorRenderTransform(false),
    mDenoise(false),
    mDenoiserMode(moonray::denoiser::OPTIX),
    mDenoisingBufferMode(DN_BUFFERS_BEAUTY),
    mDebugMode(RGB),
    mRenderOutputIndx(0),
    mNeedsRefresh(true),
    mUpdateExposure(false),
    mUpdateGamma(false),
    mExposure(0.f),
    mGamma(1.f),
    mKey(-1),
    mKeyTime(0),
    mMouseTime(0),
    mSnapIdx(1),
    mSnapshotPath(snapPath),
    mInspectorMode(INSPECT_NONE),
    mRenderContext(nullptr),
    mProgressiveFast(false),
    mFastMode(moonray::rndr::FastRenderMode::NORMALS),
    mUseOCIO(true),
    mLutOverride(nullptr)
{
    // Load the color render transform override LUT if a path was specified.
    if (crtOverride) {
        size_t numFloats = 64 * 64 * 64 * 3;
        size_t numBytesRequired = numFloats * sizeof(float);
        FILE *file = fopen(crtOverride, "rb");
        if (file) {

            // Determine the size of the file.
            fseek(file, 0, SEEK_END);
            size_t length = ftell(file);
            fseek(file, 0, SEEK_SET);

            if (length == numBytesRequired) {
                mLutOverride = util::alignedMallocArray<float>(numFloats, CACHE_LINE_SIZE);
                size_t numBytesRead = fread(mLutOverride, 1, numBytesRequired, file);
                if (ferror(file) || numBytesRead != numBytesRequired) {
                    Logger::error("Error reading bytes from \"", crtOverride, "\".");
                    util::alignedFreeArray(mLutOverride);
                    mLutOverride = nullptr;
                } else {
                    Logger::info("\"", crtOverride, "\" LUT read successfully.");
                }

            } else {
                Logger::error("\"", crtOverride, "\" LUT is the wrong size. Size = ", length,
                          ", expected = ", numBytesRequired, ".");
            }

            fclose(file);

        } else {
            Logger::error("\"", crtOverride, "\" LUT not found.");
        }
    }

    setupUi();
    setFocusPolicy(Qt::StrongFocus);

    struct stat buffer;
    // check snapshot path validity
    if (stat(mSnapshotPath.c_str(), &buffer) != 0) {
        // let user know if no path or invalid path was passed
        if (mSnapshotPath.length() == 0) {
            std::cout << "No path input. Snapshot path set to current directory." << std::endl;
        } else {
            std::cout << "Invalid path " << mSnapshotPath << ". Snapshot path set to current directory." << std::endl;
        }
        char curPath[MAXPATHLEN];
        getcwd(curPath, MAXPATHLEN);
        // set path to current path
        mSnapshotPath = curPath;
    }
    mSnapshotPath.append("/");
    // get index for snapshot
    while (true) {
        std::stringstream ss;
        ss << mSnapshotPath << "snapshot.";
        ss << std::setw(4) << std::setfill('0') << mSnapIdx << ".exr";
        if (stat(ss.str().c_str(), &buffer) != 0) {
            break;
        }
        mSnapIdx++;
    }
}

RenderViewport::~RenderViewport()
{
    util::alignedFreeArray(mLutOverride);
    delete mGlslBuffer;
}

void
RenderViewport::setupUi()
{
    mImageLabel = new QLabel;

    QVBoxLayout* layout = new QVBoxLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(mImageLabel);

    setLayout(layout);

    mWidth = -1;
    mHeight = -1;
}

void
RenderViewport::setCameraRenderContext(const moonray::rndr::RenderContext &context)
{
    mOrbitCam.setRenderContext(context);
    mFreeCam.setRenderContext(context);

    // save it, we'll use it for picking
    mRenderContext = &context;
}

void
RenderViewport::setDefaultCameraTransform(const scene_rdl2::math::Mat4f &xform)
{
    mOrbitCam.resetTransform(xform, true);
    mFreeCam.resetTransform(xform, true);
}

NavigationCam *
RenderViewport::getNavigationCam()
{
    MNRY_ASSERT(mActiveCameraType < NUM_CAMERA_TYPES);
    return mActiveCameraType == ORBIT_CAM ? static_cast<NavigationCam *>(&mOrbitCam) :
                                            static_cast<NavigationCam *>(&mFreeCam);
}

void
RenderViewport::updateFrame(FrameUpdateEvent* event)
{
    int width;
    int height;

    switch (event->getFrameType()) {
    case FRAME_TYPE_IS_RGB8:
        {
            const fb_util::Rgb888Buffer& frame = *event->getFrame().rgb8;
            width = int(frame.getWidth());
            height = int(frame.getHeight());

            // Move the image over to Qt's format, and flip it vertically to display
            // it correctly.
            QImage::Format format = QImage::Format_RGB888;
            QImage image(reinterpret_cast<const uchar*>(frame.getData()), width,
                         height, width * 3, format);
            QImage mirror = image.mirrored(false, true);
            mImageLabel->setPixmap(QPixmap::fromImage(mirror));
        }
        break;

    case FRAME_TYPE_IS_XYZW32:
    case FRAME_TYPE_IS_XYZ32:
        {
            if (event->getFrameType() == FRAME_TYPE_IS_XYZW32) {
                width  = event->getFrame().xyzw32->getWidth();
                height = event->getFrame().xyzw32->getHeight();
            } else {
                width  = event->getFrame().xyz32->getWidth();
                height = event->getFrame().xyz32->getHeight();
            }

            // not sure why this isn't resizable
            if (width != mWidth || height != mHeight) {
                delete mGlslBuffer;
                mGlslBuffer = new GlslBuffer(width, height, mLutOverride);
                mGlslBuffer->makeCrtGammaProgram();
            }
            MNRY_VERIFY(mGlslBuffer)->render(event->getFrame(), event->getFrameType(), event->getDebugMode(),
                                            event->getExposure(), event->getGamma());

            // Move the image over to Qt's format
            QImage image = mGlslBuffer->asImage();
            mImageLabel->setPixmap(QPixmap::fromImage(image));
        }
        break;
    }

    // Resize the widget if the viewport changed.
    if (width != mWidth || height != mHeight) {
        mImageLabel->resize(width, height);
        mWidth = width;
        mHeight = height;
    }
}

void
RenderViewport::keyPressEvent(QKeyEvent *event)
{
    mKey = event->key();
    if (mKeyTime == 0) {
        mKeyTime = time(nullptr);
    }
    if (event->modifiers() == Qt::NoModifier) {
        if (event->key() == Qt::Key_O) {
            // freecam toggle
            if (mActiveCameraType == ORBIT_CAM) {
                // switch from orbit cam to free cam
                auto xform = mOrbitCam.update(0.0f);
                mOrbitCam.clearMovementState();
                mFreeCam.resetTransform(xform, false);
                mActiveCameraType = FREE_CAM;
                std::cout << "Using FreeCam mode." << std::endl;
            } else {
                // switch from free cam to orbit cam
                auto xform = mFreeCam.update(0.0f);
                mFreeCam.clearMovementState();
                mOrbitCam.resetTransform(xform, false);
                mActiveCameraType = ORBIT_CAM;
                std::cout << "Using OrbitCam mode." << std::endl;
            }

            mNeedsRefresh = true;
            return;
        }

        // Start exposure adjustment
        else if (event->key() == Qt::Key_X) {
            if (QGuiApplication::mouseButtons() == Qt::LeftButton && !mUpdateExposure) {
                mUpdateExposure = true;
            }
        }

        // Start gamma adjustment
        else if (event->key() == Qt::Key_Y) {
            if (QGuiApplication::mouseButtons() == Qt::LeftButton && !mUpdateGamma) {
                mUpdateGamma = true;
            }
        }

        // toggle rendered from tile display
        else if (event->key() == Qt::Key_P) {

            mShowTileProgress = !mShowTileProgress;

            std::cout << "Show tiled progress is " << (mShowTileProgress ? "on" : "off") << std::endl;

            mNeedsRefresh = true;
            return;
        }

        // toggle de(N)oising
        if (event->key() == Qt::Key_N) {
            mDenoise = !mDenoise;
            std::cout << "Denoising is " << (mDenoise ? "on" : "off") << std::endl;
            mNeedsRefresh = true;
            return;
        }

        // select which additional (B)uffers to use for optix denoising
        if (event->key() == Qt::Key_B) {

            // populate valid denoising modes based on AOVs in rdla file
            if (mValidDenoisingBufferModes.empty()) {
                mValidDenoisingBufferModes.push_back(DN_BUFFERS_BEAUTY);
                mDenoisingBufferMode = DN_BUFFERS_BEAUTY;

                const rndr::RenderOutputDriver *rod = mRenderContext->getRenderOutputDriver();
                const bool albedoValid = rod->getDenoiserAlbedoInput() >= 0;
                const bool normalsValid = rod->getDenoiserNormalInput() >= 0;

                if (albedoValid) {
                    mValidDenoisingBufferModes.push_back(DN_BUFFERS_BEAUTY_ALBEDO);
                }
                if (albedoValid && normalsValid) {
                    mValidDenoisingBufferModes.push_back(DN_BUFFERS_BEAUTY_ALBEDO_NORMALS);
                }
            }

            // find the next denoising mode    
            do {
                mDenoisingBufferMode = (DenoisingBufferMode)((int)(mDenoisingBufferMode + 1) % 
                                                             NUM_DENOISING_BUFFER_MODES);
            } while (std::find(mValidDenoisingBufferModes.begin(),
                               mValidDenoisingBufferModes.end(),
                               mDenoisingBufferMode ) == mValidDenoisingBufferModes.end());

            switch(mDenoisingBufferMode) {
                case DN_BUFFERS_BEAUTY:
                    std::cout << "Denoising buffer mode: Beauty\n";
                    break;
                case DN_BUFFERS_BEAUTY_ALBEDO:
                    std::cout << "Denoising buffer mode: Beauty+Albedo\n";
                    break;
                case DN_BUFFERS_BEAUTY_ALBEDO_NORMALS:
                    std::cout << "Denoising buffer mode: Beauty+Albedo+Normals\n";
                    break;
                default:
                    MNRY_ASSERT(0);
            }

            mNeedsRefresh = true;
            return;
        }

        // move to next pick mode
        else if (event->key() == Qt::Key_I) {
            mInspectorMode = (mInspectorMode + 1) % NUM_INSPECTOR_MODES;
            switch(mInspectorMode) {
            case INSPECT_NONE:
                std::cout << "Pixel Inspector Mode: None" << std::endl;
                break;
            case INSPECT_LIGHT_CONTRIBUTIONS:
                std::cout << "Pixel Inspector Mode: Light Contributions" << std::endl;
                break;
            case INSPECT_GEOMETRY:
                std::cout << "Pixel Inspector Mode: Geometry" << std::endl;
                break;
            case INSPECT_GEOMETRY_PART:
                std::cout << "Pixel Inspector Mode: Geometry Part" << std::endl;
                break;
            case INSPECT_MATERIAL:
                std::cout << "Pixel Inspector Mode: Material" << std::endl;
                break;
            default:
                std::cout << "Unknown pixel inspector mode" << std::endl;
            }

            mNeedsRefresh = true;
            return;
        }

        // move to previous render output
        else if (event->key() == Qt::Key_Comma) {
            --mRenderOutputIndx;
            mNeedsRefresh = true;
            return;
        }

        // move to next render output
        else if (event->key() == Qt::Key_Period) {
            ++mRenderOutputIndx;
            mNeedsRefresh = true;
            return;
        }

        // toggle fast progressive mode
        else if (event->key() == Qt::Key_L) {
            mProgressiveFast = !mProgressiveFast;
            if (mProgressiveFast) {
                std::cout << "Switched to fast mode" << std::endl;
            } else {
                std::cout << "Switched to regular mode" << std::endl;
            }
            mNeedsRefresh = true;
        }

        // take a snapshot
        else if (event->key() == Qt::Key_K) {
            // Ensure the render context exists and can be displayed.
            // Key bindings can call this function before everything is fully ready.
            if (!mRenderContext || !mRenderContext->isFrameReadyForDisplay()) return;
            
            std::stringstream ss;
            ss << "snapshot." << std::setw(4) << std::setfill('0') << mSnapIdx << ".exr";
            const std::string outputFilename = ss.str();
            // Write the image
            fb_util::RenderBuffer outputBuffer;
            const rdl2::SceneObject *metadata = mRenderContext->getSceneContext().getSceneVariables().getExrHeaderAttributes();
            const math::HalfOpenViewport aperture = mRenderContext->getRezedApertureWindow();
            const math::HalfOpenViewport region = mRenderContext->getRezedRegionWindow();
            mRenderContext->snapshotRenderBuffer(&outputBuffer, true, true);
            try {
                moonray::rndr::writePixelBuffer(outputBuffer, mSnapshotPath + outputFilename, metadata, aperture, region);
                std::cout << "Snapshot " << outputFilename << " taken and saved to " << mSnapshotPath << std::endl;
                mSnapIdx++;
            } catch (...) {
                Logger::error("Failed to write out");
            }
            return;
        }

        //
        // DebugMode support:
        //

        // RGB
        else if (event->key() == Qt::Key_QuoteLeft) {
            mDebugMode = RGB;
            mNeedsRefresh = true;
            return;
        }

        // RED
        else if (event->key() == Qt::Key_1) {
            mDebugMode = (mDebugMode == RED) ? RGB : RED;
            mNeedsRefresh = true;
            return;
        }

        // GREEN
        else if (event->key() == Qt::Key_2) {
            mDebugMode = (mDebugMode == GREEN) ? RGB : GREEN;
            mNeedsRefresh = true;
            return;
        }

        // BLUE
        else if (event->key() == Qt::Key_3) {
            mDebugMode = (mDebugMode == BLUE) ? RGB : BLUE;
            mNeedsRefresh = true;
            return;
        }

        // ALPHA
        else if (event->key() == Qt::Key_4) {
            mDebugMode = (mDebugMode == ALPHA) ? RGB : ALPHA;
            mNeedsRefresh = true;
            return;
        }

        // LUMINANCE
        else if (event->key() == Qt::Key_5) {
            mDebugMode = (mDebugMode == LUMINANCE) ? RGB : LUMINANCE;
            mNeedsRefresh = true;
            return;
        }

        // RGB_NORMALIZED
        else if (event->key() == Qt::Key_7) {
            mDebugMode = (mDebugMode == RGB_NORMALIZED)? RGB : RGB_NORMALIZED;
            mNeedsRefresh = true;
            return;
        }

        // NUM_SAMPLES
        else if (event->key() == Qt::Key_8) {
            mDebugMode = (mDebugMode == NUM_SAMPLES)? RGB : NUM_SAMPLES;
            mNeedsRefresh = true;
            return;
        }
    } else if (event->modifiers() == Qt::ShiftModifier) {

        // reset exposure
        if (event->key() == Qt::Key_X) {
            mExposure = 0.f;
            std::cout << "Exposure is reset." << std::endl;
            mNeedsRefresh = true;
            return;
        }

        // reset gamma
        else if (event->key() == Qt::Key_Y) {
            mGamma = 1.f;
            std::cout << "Gamma is reset." << std::endl;
            mNeedsRefresh = true;
            return;
        }

        // Increment exposure by 1
        else if (event->key() == Qt::Key_Up) {
            mExposure = math::floor(mExposure) + 1.f;
            mNeedsRefresh = true;
            return;
        }

        // Decrement exposure by 1
        else if (event->key() == Qt::Key_Down) {
            mExposure = math::floor(mExposure) - 1.f;
            mNeedsRefresh = true;
        }

        // toggle de(N)oising mode (Optix or OIDN default/cpu/cuda)
        if (event->key() == Qt::Key_N) {
            if (mDenoiserMode == moonray::denoiser::OPTIX) {
                std::cout << "Denoiser mode: Open Image Denoise (default/best device)" << std::endl;
                mDenoiserMode = moonray::denoiser::OPEN_IMAGE_DENOISE;
            } else if (mDenoiserMode == moonray::denoiser::OPEN_IMAGE_DENOISE) {
                std::cout << "Denoiser mode: Open Image Denoise (cpu device)" << std::endl;
                mDenoiserMode = moonray::denoiser::OPEN_IMAGE_DENOISE_CPU;
            } else if (mDenoiserMode == moonray::denoiser::OPEN_IMAGE_DENOISE_CPU) {
                std::cout << "Denoiser mode: Open Image Denoise (CUDA device)" << std::endl;
                mDenoiserMode = moonray::denoiser::OPEN_IMAGE_DENOISE_CUDA;
            } else {
                std::cout << "Denoiser mode: Optix" << std::endl;
                mDenoiserMode = moonray::denoiser::OPTIX;
            }
            mNeedsRefresh = true;
            return;
        }

    } else if (event->modifiers() == Qt::AltModifier) {
        if (event->key() == Qt::Key_Up) {
            if (isFastProgressive()) {
                mFastMode = nextFastMode(mFastMode);
                mNeedsRefresh = true;
            }
            return;
        }
        else if (event->key() == Qt::Key_Down) {
            if (isFastProgressive()) {
                mFastMode = prevFastMode(mFastMode);
                mNeedsRefresh = true;
            }
            return;
        }
    }
    if (event->key() == Qt::Key_Z) {
        mUseOCIO = !mUseOCIO;
        std::cout << "OCIO is " << (mUseOCIO ? "on" : "off") << std::endl;
        mNeedsRefresh = true;
        return;
    }

    if (!getNavigationCam()->processKeyboardEvent(event, true)) {
        QWidget::keyPressEvent(event);
    }
}

void
RenderViewport::keyReleaseEvent(QKeyEvent *event)
{
    if (!event->isAutoRepeat()) {
        mKeyTime = time(nullptr) - mKeyTime;
        // check for key tap vs long key hold event
        if (mKeyTime < 1) {
            // set exposure directly
            if (event->key() == Qt::Key_X && event->modifiers() == Qt::NoModifier) {
                bool ok;
                float exposure = static_cast<float>(QInputDialog::getDouble(this, tr("Set Exposure"), tr("Value:"),
                                                    0.0, -8.0, 8.0, 3, &ok, Qt::WindowFlags()));
                if (ok) {
                    mExposure = exposure;
                    std::cout << "Exposure updated." << std::endl;
                    mNeedsRefresh = true;
                }
            }
            // set gamma directly
            else if (event->key() == Qt::Key_Y && event->modifiers() == Qt::NoModifier) {
                bool ok;
                float gamma = static_cast<float>(QInputDialog::getDouble(this, tr("Set Gamma"), tr("Value:"),
                                                 1.0, 0.005, 8.0, 3, &ok, Qt::WindowFlags()));
                if (ok) {
                    mGamma = gamma;
                    std::cout << "Gamma updated." << std::endl;
                    mNeedsRefresh = true;
                }
            }
        } else {
            if (event->key() == Qt::Key_X && QGuiApplication::mouseButtons() == Qt::NoButton) {
                mUpdateExposure = false;
                mNeedsRefresh = true;
            }
            else if (event->key() == Qt::Key_Y && QGuiApplication::mouseButtons() == Qt::NoButton) {
                mUpdateGamma = false;
                mNeedsRefresh = true;
            }
        }
        mKeyTime = 0;
        mKey = -1;
    }
    if (!getNavigationCam()->processKeyboardEvent(event, false)) {
        QWidget::keyReleaseEvent(event);
    }
}

void
RenderViewport::mousePressEvent(QMouseEvent *event)
{
    // get mouse position
    mMousePos = event->pos().x();
    if (mMouseTime == 0) {
        mMouseTime = time(nullptr);
    }
    if (!getNavigationCam()->processMousePressEvent(event, mKey)) {
        const int x = event->x();
        const int y = mHeight - event->y();

        switch (mInspectorMode) {
        case INSPECT_LIGHT_CONTRIBUTIONS:
            {
                moonray::shading::LightContribArray rdlLights;
                mRenderContext->handlePickLightContributions(x, y, rdlLights);
                std::sort(rdlLights.begin(), rdlLights.end(),
                          [&](const moonray::shading::LightContrib &l0, const moonray::shading::LightContrib &l1) {
                              return l0.second < l1.second;
                          });
                std::cout << "Light Pick Results: (" << x << ", " << y << ")" << std::endl;
                for (unsigned int i = 0; i < rdlLights.size(); ++i) {
                    std::cout << "\t"
                              << rdlLights[i].first->getName() << ": "
                              << rdlLights[i].second << std::endl;
                }
            }
            break;
        case INSPECT_GEOMETRY:
            {
                const rdl2::Geometry *geometry = mRenderContext->handlePickGeometry(x, y);
                std::cout << "Geometry Pick Result: (" << x << ", " << y << ")" << std::endl;
                if (geometry) {
                    std::cout << "\t" << geometry->getName() << std::endl;
                }
                
            }
            break;
        case INSPECT_GEOMETRY_PART:
            {
                std::string parts;
                const rdl2::Geometry* geometry = mRenderContext->handlePickGeometryPart(x, y, parts);
                std::cout << "Geometry Part Pick Result: (" << x << ", " << y << ")" << std::endl;

                if (geometry) {
                    std::cout << "\t" << geometry->getName() << ", " << parts << std::endl;
                }
            }
            break;
        case INSPECT_MATERIAL:
            {
                const rdl2::Material *material = mRenderContext->handlePickMaterial(x, y);
                std::cout << "Material Pick Result: (" << x << ", " << y << ")" << std::endl;
                if (material) {
                    std::cout << "\t" << material->getName() << std::endl;
                }
            }
            break;
        case INSPECT_NONE:
        default:
            QWidget::mousePressEvent(event);
        }
    }
}

void
RenderViewport::mouseReleaseEvent(QMouseEvent *event)
{
    mMouseTime = time(nullptr) - mMouseTime;
    // mouse click release
    if (mMouseTime < 1) {
        // reset exposure
        if (event->button() == Qt::LeftButton && mKey == Qt::Key_X) {
            mExposure = 0.f;
            std::cout << "Exposure is reset." << std::endl;
            mNeedsRefresh = true;
        }
        if (event->button() == Qt::LeftButton && mKey == Qt::Key_Y) {
            mGamma = 1.f;
            std::cout << "Gamma is reset." << std::endl;
            mNeedsRefresh = true;
        }
    }
    if (event->button() == Qt::LeftButton && mKey == -1) {
        if (mUpdateExposure) {
            std::cout << "Exposure update finished." << std::endl;
            mUpdateExposure = false;
            mNeedsRefresh = true;
        }
        if (mUpdateGamma) {
            std::cout << "Gamma update finished." << std::endl;
            mUpdateGamma = false;
            mNeedsRefresh = true;
        }
    }
    mMouseTime = 0;
    if (!getNavigationCam()->processMouseReleaseEvent(event)) {
        QWidget::mouseReleaseEvent(event);
    }
}

void
RenderViewport::mouseMoveEvent(QMouseEvent *event)
{
    // Handle exposure/gamma adjustment by mouse drag
    if (QGuiApplication::mouseButtons() == Qt::LeftButton) {
        if (mUpdateExposure) {
            int currentPos = event->pos().x();
            mExposure += (0.01f * (currentPos - mMousePos));
            mMousePos = currentPos;
        }
        if (mUpdateGamma) {
            int currentPos = event->pos().x();
            mGamma += (0.005f * (currentPos - mMousePos));
            // set min gamma to 0.005
            if (mGamma <= 0.005f) {
                mGamma = 0.005f;
            }
            mMousePos = currentPos;
        }
        mNeedsRefresh = true;
    }
    if (!getNavigationCam()->processMouseMoveEvent(event)) {
        QWidget::mouseMoveEvent(event);
    }
}
} // namespace moonray_gui

