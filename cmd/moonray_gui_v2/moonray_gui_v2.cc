// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "DenoiserManager.h"
#include "FileUtil.h"
#include "RenderGui.h"
#include "viewport/Viewport.h"

#include <moonray/application/ChangeWatcher.h>
#include <moonray/application/RaasApplication.h>
#include <moonray/rendering/rndr/PathVisualizerManager.h>
#include <scene_rdl2/render/util/Args.h>
#include <scene_rdl2/scene/rdl2/Camera.h>

#include <algorithm>
#include <string>
#include <vector>

namespace moonray_gui_v2 {

class RaasGuiApplication : public moonray::RaasApplication
{
public:
    RaasGuiApplication() {};
    ~RaasGuiApplication() override {};

protected:
    void parseOptions() override;
    void run() override;

private:
    
    static void* startRenderThread(void* me);

    CameraType mInitialCamType {ORBIT_CAM};
    pthread_t mRenderThread {0};
    std::exception_ptr mException {nullptr};
    std::unique_ptr<RenderGui> mRenderGui {nullptr};
    std::unique_ptr<Viewport> mViewport {nullptr};
    std::unique_ptr<DenoiserManager> mDenoiserManager {nullptr};
};

void
RaasGuiApplication::parseOptions()
{
    using scene_rdl2::util::Args;
    Args args(mArgc, mArgv);
    Args::StringArray values;

    if (args.getFlagValues("-free_cam", 0, values) >= 0) {
        mInitialCamType = FREE_CAM;
        auto newLast = std::remove_if(mArgv, mArgv + mArgc, [](char *str) {
            return strcmp(str, "-free_cam") == 0;
        });
        mArgc = static_cast<int>(newLast - mArgv);
    }

    RaasApplication::parseOptions(true);
}

static bool
isRdla(const std::string &sceneFile)
{
    const std::string rdlaExt = ".rdla";
    if (rdlaExt.size() > sceneFile.size()) return false;
    return std::equal(rdlaExt.rbegin(), rdlaExt.rend(), sceneFile.rbegin());
}

// static method for a thread starting point //
void*
RaasGuiApplication::startRenderThread(void* me)
{
    // Because we are multithreading, we need to keep a pointer to the original RaasGuiApplication instance 
    // that called startRenderThread in RaasGuiApplication::run()
    RaasGuiApplication* self = static_cast<RaasGuiApplication*>(me);

    // Run global init (creates a RenderDriver) This *must* be called on the same thread we
    // intend to call RenderContext::startFrame from.
    moonray::rndr::initGlobalDriver(self->mOptions);

    self->logInitMessages();

    scene_rdl2::fb_util::RenderBuffer        outputBuffer;
    scene_rdl2::fb_util::HeatMapBuffer       heatMapBuffer;
    scene_rdl2::fb_util::FloatBuffer         weightBuffer;
    scene_rdl2::fb_util::RenderBuffer        renderBufferOdd;
    scene_rdl2::fb_util::VariablePixelBuffer renderOutputBuffer;

    try {
        // Create the change watchers if applicable
        std::unique_ptr<moonray::ChangeWatcher> changeWatcher(moonray::ChangeWatcher::CreateChangeWatcher());
        std::unique_ptr<moonray::ChangeWatcher> deltasWatcher(moonray::ChangeWatcher::CreateChangeWatcher());

        const auto& sceneFiles = self->mOptions.getSceneFiles();
        std::set<std::string> referencedRdlaFiles;
        std::map<std::string, std::string> luaVariables;
        for (const auto& sceneFile : sceneFiles) {
            changeWatcher->watchFile(sceneFile);

            // avoid parsing rdlb files
            if (!isRdla(sceneFile)) continue;

            // Parse referenced rdla files
            parseRdlaFileForReferences(
                sceneFile,
                referencedRdlaFiles,
                luaVariables
            );

            // Add referenced rdla files to watch list
            for (const auto& rdlaFile : referencedRdlaFiles) {
                changeWatcher->watchFile(rdlaFile);
                std::cout << "Watching file: " << rdlaFile << std::endl;
            }

            referencedRdlaFiles.clear();
            luaVariables.clear();
        }

        for (const std::string & deltasFile : self->mOptions.getDeltasFiles()) {
            deltasWatcher->watchFile(deltasFile);

            // Parse deltas file's referenced rdla files
            parseRdlaFileForReferences(
                deltasFile,
                referencedRdlaFiles,
                luaVariables
            );

            // Add delta file's referenced rdla files to watch list
            for (const auto& rdlaFile : referencedRdlaFiles) {
                changeWatcher->watchFile(rdlaFile);
            }

            referencedRdlaFiles.clear();
            luaVariables.clear();
        }

        bool hasCameraXform = false;
        scene_rdl2::math::Mat4f origCameraXform;
        scene_rdl2::math::Mat4f currCameraXform;

        // Deltas applied since the base scene load. When a delta requires a full
        // reload we rebuild the RenderContext from the base scene files, so these
        // must be replayed into the fresh context to preserve their state (e.g.
        // visibility or other attribute changes). We intentionally do NOT clear
        // this list after a reload: each full reload always starts from the
        // original base scene files, so every accumulated delta must be replayed
        // again to reconstruct the current state.
        std::vector<std::string> appliedDeltas;

        do {
            std::unique_ptr<moonray::rndr::RenderContext> renderContext;

            // Loop until we have a successful load of main scene.
            do {
                try {
                    // Scene load happens in here.
                    renderContext.reset(new moonray::rndr::RenderContext(self->mOptions,
                                                                &self->mInitMessages));

                    // Replay any previously applied deltas so a full reload
                    // preserves their state. Queued before initialize() so they
                    // are baked into the full scene load, which rebuilds all
                    // geometry with complete primitive attribute tables (e.g.
                    // ref_P).
                    for (const std::string& deltasFile : appliedDeltas) {
                        renderContext->updateScene(deltasFile);
                    }

                    constexpr auto loggingConfig = moonray::rndr::RenderContext::LoggingConfiguration::ATHENA_DISABLED;
                    renderContext->initialize(self->mInitMessages, loggingConfig);

                    // Ensure we are either in progressive or fast progressive mode
                    if (self->mRenderGui->isFastProgressive()) {
                        renderContext->setRenderMode(moonray::rndr::RenderMode::PROGRESSIVE_FAST);
                        renderContext->setFastRenderMode(self->mRenderGui->getFastRenderMode());
                    } else {
                        renderContext->setRenderMode(moonray::rndr::RenderMode::PROGRESSIVE);
                        // Set to default fast render mode - doesn't matter in regular progressive
                        renderContext->setFastRenderMode(moonray::rndr::FastRenderMode::NORMALS);
                    }
                } catch(const std::exception& e) {
                    std::cerr << "Load failed! Fix the file and resave!\n"
                            << "ERROR: " << e.what() << std::endl;
                    renderContext.reset();

                    changeWatcher->waitForChange();
                }
            } while(!renderContext);


            self->mRenderGui->setContext(renderContext.get());

            // Set up file watchers for all the shader DSOs.

            watchShaderDsos(*changeWatcher, *renderContext);

            // Record camera location the first time around so that we can maintain
            // positioning between dso/shader changes.
            const rdl2::Camera *camera = renderContext->getCamera();
            MNRY_ASSERT(camera);

            // Tolerate a double to float precision loss in the gui
            scene_rdl2::math::Mat4f rdlaCameraXform = toFloat(camera->get(rdl2::Node::sNodeXformKey));

            // Typically we want to preserve the current camera location on reload.
            // The exception is if it has been manually changed in the rdla file.
            bool makeDefaultXform = false;
            if (!hasCameraXform || rdlaCameraXform != origCameraXform) {
                origCameraXform = currCameraXform = rdlaCameraXform;
                hasCameraXform = true;
                makeDefaultXform = true;
            }

            self->mRenderGui->beginInteractiveRendering(currCameraXform, makeDefaultXform);

            uint32_t prevFrameTimestamp = 0;
            uint32_t frameSavedTimestamp = 0;

            std::set<std::string> changedDeltaFiles;

            moonray::rndr::PathVisualizerManager* visualizer = renderContext->getPathVisualizerManager().get();

            while (self->mViewport->isWindowOpen()) {

                // Execute startFrame() if renderContext has forceCallStartFrame condition
                renderContext->forceGuiCallStartFrameIfNeed();

                if (deltasWatcher->hasChanged(&changedDeltaFiles)) {
                    currCameraXform = self->mRenderGui->endInteractiveRendering();

                    // Apply the deltas to the scene objects
                    bool geometryChanged = false;
                    for (const std::string & filename : changedDeltaFiles) {
                        // Remember this delta so it can be replayed if we later
                        // need to rebuild the RenderContext from scratch.
                        if (std::find(appliedDeltas.begin(), appliedDeltas.end(), filename) == appliedDeltas.end()) {
                            appliedDeltas.push_back(filename);
                        }
                        if (renderContext->updateScene(filename)) {
                            // Geometry has changed, trigger a full reload by breaking out of this loop
                            geometryChanged = true;
                            break;
                        }
                    }
                    
                    if (geometryChanged) {
                        break;  // Exit the main loop to trigger full reload
                    }
                    
                    changedDeltaFiles.clear();
                    
                    // Tolerate a double to float precision loss in the gui
                    rdlaCameraXform = toFloat(camera->get(rdl2::Node::sNodeXformKey));

                    // The same logic we do for full reloads applies for camera xforms
                    // applies to delta updates also.
                    makeDefaultXform = false;
                    if (rdlaCameraXform != origCameraXform) {
                        origCameraXform = currCameraXform = rdlaCameraXform;
                        makeDefaultXform = true;
                    }

                    self->mRenderGui->beginInteractiveRendering(currCameraXform, makeDefaultXform);
                }

                // This is the timestamp of the last frame we kicked off.
                uint32_t currFrameTimestamp = self->mRenderGui->updateInteractiveRendering();

                // Don't dump out text or save the file if rendering in real-time mode
                // since there will be many frames rendered per second.
                if (renderContext->getRenderMode() == moonray::rndr::RenderMode::REALTIME) {

                    // This effectively caps the max framerate to 500fps.
                    usleep(2000);

                } else {
                    bool frameComplete = false;

                    // when the visualizer is done recording ray info, we need to stop the frame,
                    // if necessary, then request that drawing begin
                    if (visualizer->isInStopRecordState()) {
                        if (renderContext->isFrameRendering()) {
                            renderContext->stopFrame(/*simulationMode*/ true);
                        }
                        /// TODO: By calling startFrame here, it ensures that, even
                        /// if rendering is complete, it will force a full render
                        /// restart. Ideally, if we are finished rendering, we would only
                        /// run the simulation (see moonray::RenderContext::forceCameraUpdates())
                        /// TODO: Also, ideally we wouldn't do heavy renderPrep work inside the event loop
                        renderContext->startFrame();
                        visualizer->generateLines();

                        /// TODO: This section could be improved for efficiency. This conditional section will rarely
                        /// (if ever) be entered because the first snapshot likely will not be ready before
                        /// generateLines is complete. However, we do want to take a snapshot as soon as possible.
                        if (renderContext->isFrameReadyForDisplay()) {
                            const bool parallel = renderContext->getRenderMode() == moonray::rndr::RenderMode::REALTIME;
                            self->mRenderGui->snapshotFrame(&outputBuffer, &heatMapBuffer, &weightBuffer, 
                                                            &renderBufferOdd, &renderOutputBuffer,
                                                            true, parallel);
                            self->mRenderGui->updateFrame(&outputBuffer, &renderOutputBuffer, false, parallel);
                        }
                    }

                    if (currFrameTimestamp > prevFrameTimestamp) {
                        // We've hit a brand new frame, do any new frame logic here...
                        self->mNextLogProgressTime = 0.0;
                        self->mNextLogProgressPercentage = 0.0;

                    } else if (currFrameTimestamp == prevFrameTimestamp &&
                            frameSavedTimestamp != currFrameTimestamp &&
                            renderContext->isFrameRendering() &&
                            renderContext->isFrameComplete()) {
                        // We've finished rendering so get the latest version (there may have
                        // been more samples rendered since the last snapshot), and save it.
                        frameComplete = true;
                        self->printStatusLine(*renderContext, renderContext->getLastFrameMcrtStartTime(), frameComplete);
                        renderContext->stopFrame();

                        // If we're in realtime mode then all rendering should have stopped by this
                        // point, so use all threads for the snapshot.
                        bool parallel = renderContext->getRenderMode() == moonray::rndr::RenderMode::REALTIME;
                        self->mRenderGui->snapshotFrame(&outputBuffer, &heatMapBuffer, &weightBuffer, &renderBufferOdd,
                                                        &renderOutputBuffer,
                                                        true, parallel);
                        self->mRenderGui->updateFrame(&outputBuffer, &renderOutputBuffer,
                                                      false, parallel);
                    }

                    // This effectively caps the max framerate to 200fps.
                    usleep(5000);

                    // Display progress bar if we're actively rendering.
                    if (frameSavedTimestamp != currFrameTimestamp && renderContext->isFrameRendering()) {
                        self->printStatusLine(*renderContext, renderContext->getLastFrameMcrtStartTime(), frameComplete);
                    }

                    // Save out file to disk (if not in real-time mode).
                    if (frameComplete) {
                        saveEXR(renderContext.get());
                        frameSavedTimestamp = currFrameTimestamp;
                    }
                }

                // We're done, exit function.
                if (changeWatcher->hasChanged()) {

                    // Grab most recent camera transform.
                    currCameraXform = self->mRenderGui->endInteractiveRendering();

                    // Get out of this loop to pick up changes.
                    std::cout << "Scene change detected." << std::endl;
                    break;
                }

                prevFrameTimestamp = currFrameTimestamp;
            }

            // not strictly necessary, but just to be thorough:
            self->mRenderGui->setContext(nullptr);

        } while (self->mViewport->isWindowOpen());
    } catch (...) {
        self->mException = std::current_exception();
    }

    moonray::rndr::cleanUpGlobalDriver();

    return nullptr;
}

void
RaasGuiApplication::run()
{
    const std::string snapPath = mOptions.getSnapshotPath();
    const bool showTileProgress = mOptions.getTileProgress();

    // Create the denoiser manager
    mDenoiserManager = std::make_unique<DenoiserManager>();

    // Create the main viewport and GUI
    mViewport = std::make_unique<Viewport>(mInitialCamType, snapPath, showTileProgress, mDenoiserManager.get());

    // Create the rendering manager for moonray_gui_v2
    mRenderGui = std::make_unique<RenderGui>(mViewport.get(), mDenoiserManager.get());

    // Spin off a thread for rendering.
    int retVal = pthread_create(&mRenderThread, nullptr, RaasGuiApplication::startRenderThread, this);
    MNRY_ASSERT_REQUIRE(retVal == 0, "Failed to create render thread.");

    // Start the GUI event loop
    mViewport->exec();

    // Clean up thread
    retVal = pthread_join(mRenderThread, nullptr);
    MNRY_ASSERT_REQUIRE(retVal == 0, "Failed to join render thread.");

    if (mException) {
        std::rethrow_exception(mException);
    }
}
} // end namespace moonray_gui_v2

int main(int argc, char* argv[])
{
    moonray_gui_v2::RaasGuiApplication app;
    try {
        return app.main(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

