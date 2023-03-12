// Copyright 2023 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "RenderGui.h"

#include <moonray/application/ChangeWatcher.h>
#include <moonray/application/RaasApplication.h>
#include <scene_rdl2/common/platform/Platform.h>
#include <scene_rdl2/render/util/Args.h>
#include <scene_rdl2/render/util/Strings.h>
#include <scene_rdl2/scene/rdl2/Camera.h>

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <memory>
#include <set>
#include <map>

#include <boost/regex.hpp>
#include <QtGui>
#include <QApplication>

namespace moonray_gui {

class RaasGuiApplication : public moonray::RaasApplication
{
public:
    RaasGuiApplication();
    ~RaasGuiApplication();

protected:
    void parseOptions();
    void run();

private:
    
    static void* startRenderThread(void* me);
    // Parses an rdla file and adds references to other
    // rdla files (via the lua language) to the
    // referencedRdlaFiles set.  Recursive.
    static void parseRdlaFileForReferences(
        const std::string &sceneFile,
        std::set<std::string> &referencedRdlaFiles,

        // Holds values of rdla file lua variables which are
        // possibly referenced in dofile() or other such type
        // file inclusion mechanisms
        std::map<std::string, std::string> &luaVariables
    );

    CameraType mInitialCamType;
    pthread_t mRenderThread;
    RenderGui* mRenderGui;
    std::exception_ptr mException;
};

RaasGuiApplication::RaasGuiApplication()
    : RaasApplication()
    , mInitialCamType(ORBIT_CAM)
    , mRenderThread(0)
    , mRenderGui(nullptr)
    , mException(nullptr)

{
}

RaasGuiApplication::~RaasGuiApplication()
{
}

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

void RaasGuiApplication::parseRdlaFileForReferences (
    const std::string &sceneFile,
    std::set<std::string> &referencedRdlaFiles,
    std::map<std::string, std::string> &luaVariables
){
    // Keep the newly found rdla files separate for recursion
    std::set<std::string> newReferencedRdlaFiles;

    // Open sceneFile
    std::ifstream fin;
    fin.open(sceneFile);
    if (!fin.good()) {
        std::cerr << "Failed to load scenefile: " << sceneFile
            << std::endl;
        return;
    }

    // Find rdla references with varialbe concatenation like the following:
    // dofile(asset_lib_dir .. "char/astrid/skin/rdla/astrid_skin.rdla")
    // TODO: Make this work with more than one variable preceding the path
    //boost::regex rdlaWithVariableRegex (R"(.*\((\w+)\s+\.\.\s+\"+(.*rdla)\"+.*)");
    boost::regex rdlaWithVariableRegex (R"([^-]*\((\w+)\s+\.\.\s+\"+(.*rdla)\"+.*)");

    // Find straigh rdla references with no variables lines like the following:
    // dofile("/work/gshad/moonshine/lib/char/astrid/skin/rdla/astrid_skin.rdla")
    boost::regex rdlaWithoutVariableRegex (R"([^-]*\"+(.*rdla)\"+.*)");

    // Find lua variable assignment lines like the following:
    // asset_lib_dir = "/work/gshad/moonshine/lib/"
    boost::regex variableAssignmentRegex (R"(^\s*(\w+)\s*=\s*\"+(.*)\"+)");

    std::string buf;
    while (std::getline(fin, buf)) {
        // Really long lines can cause issues for the regex parser.
        // Long lines are usually layer entries, or rdl mesh attributes, and
        // certainly do not contain rdla file references.
        const unsigned int maxLineSize = 1024;
        if (buf.size() > maxLineSize) continue;

        boost::cmatch cm;
        if (boost::regex_match(buf.c_str(), cm, rdlaWithVariableRegex)) {
            // Found line with .rdla file with variable used.
            // Try to find the lua variable in the map and then
            // add it to the found rdla path.
            std::string luaVariable = cm[1];
            std::string rdlaPath = cm[2];
            if (luaVariables.find(luaVariable) != luaVariables.end()) {
                std::string &luaVariableValue = luaVariables[luaVariable];
                if (luaVariableValue.back() == '/')
                    newReferencedRdlaFiles.insert(luaVariableValue + rdlaPath);
                else
                    newReferencedRdlaFiles.insert(luaVariableValue + '/' + rdlaPath);
            }
        } else if (boost::regex_match(buf.c_str(), cm, rdlaWithoutVariableRegex)) {
            // Found line with .rdla file and no variable used.
            // Just add the rdla file to the set.
            newReferencedRdlaFiles.insert(cm[1]);
        } else if (boost::regex_match(buf.c_str(), cm, variableAssignmentRegex)) {
            // Found line with variable assignment.
            // Add it to the map.
            luaVariables[cm[1]] = cm[2];
        }
        // Add other regex situations here
    }
    fin.close();

    // Parse refernced rdla files recusively
    for (auto &rdlaFile : newReferencedRdlaFiles) {
        parseRdlaFileForReferences (rdlaFile, referencedRdlaFiles, luaVariables);
    }

    // Concatenate the new set onto the set passed in
    referencedRdlaFiles.insert(newReferencedRdlaFiles.begin(), newReferencedRdlaFiles.end());
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
        moonray::ChangeWatcher changeWatcher;
        moonray::ChangeWatcher deltasWatcher;

        const auto& sceneFiles = self->mOptions.getSceneFiles();
        std::set<std::string> referencedRdlaFiles;
        std::map<std::string, std::string> luaVariables;
        for (const auto& sceneFile : sceneFiles) {
            changeWatcher.watchFile(sceneFile);

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
                changeWatcher.watchFile(rdlaFile);
                std::cout << "Watching file: " << rdlaFile << std::endl;
            }

            referencedRdlaFiles.clear();
            luaVariables.clear();
        }

        for (const std::string & deltasFile : self->mOptions.getDeltasFiles()) {
            deltasWatcher.watchFile(deltasFile);

            // Parse deltas file's referenced rdla files
            parseRdlaFileForReferences(
                deltasFile,
                referencedRdlaFiles,
                luaVariables
            );

            // Add delta file's referenced rdla files to watch list
            for (const auto& rdlaFile : referencedRdlaFiles) {
                changeWatcher.watchFile(rdlaFile);
            }

            referencedRdlaFiles.clear();
            luaVariables.clear();
        }

        bool hasCameraXform = false;
        scene_rdl2::math::Mat4f origCameraXform;
        scene_rdl2::math::Mat4f currCameraXform;

        do {
            std::unique_ptr<moonray::rndr::RenderContext> renderContext;

            // Loop until we have a successful load of main scene.
            do {
                try {
                    // Scene load happens in here.
                    renderContext.reset(new moonray::rndr::RenderContext(self->mOptions,
                                                                &self->mInitMessages));
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

                    changeWatcher.waitForChange();
                }
            } while(!renderContext);

            self->mRenderGui->setContext(renderContext.get());

            // Set up file watchers for all the shader DSOs.

            watchShaderDsos(changeWatcher, *renderContext);

            // Record primary camera location the first time around so that we can maintain
            // positioning between dso/shader changes.
            const rdl2::Camera *camera = renderContext->getCameras()[0];
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

            while (self->mRenderGui->isActive()) {

                // Execute startFrame() if renderContext has forceCallStartFrame condition
                renderContext->forceGuiCallStartFrameIfNeed();

                if (deltasWatcher.hasChanged(&changedDeltaFiles)) {
                    currCameraXform = self->mRenderGui->endInteractiveRendering();

                    // Apply the deltas to the scene objects
                    for (const std::string & filename : changedDeltaFiles) {
                        renderContext->updateScene(filename);
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
                        // need to snapshot all the required buffers, the
                        // render buffer might not have been snapshot at all if
                        // displaying alternate render outputs
                        renderContext->snapshotRenderBuffer(&outputBuffer, true, true);

                        const std::string outputFilename = renderContext->getSceneContext().getSceneVariables().get(rdl2::SceneVariables::sOutputFile);
                        const rdl2::SceneObject *metadata = renderContext->getSceneContext().getSceneVariables().getExrHeaderAttributes();
                        const scene_rdl2::math::HalfOpenViewport aperture = renderContext->getRezedApertureWindow();
                        const scene_rdl2::math::HalfOpenViewport region = renderContext->getRezedRegionWindow();

                        moonray::writeImageWithMessage(&outputBuffer, outputFilename, metadata, aperture, region);

                        // write any arbitrary RenderOutput objects
                        const moonray::pbr::DeepBuffer *deepBuffer = renderContext->getDeepBuffer();
                        moonray::pbr::CryptomatteBuffer *cryptomatteBuffer = renderContext->getCryptomatteBuffer();
                        renderContext->snapshotHeatMapBuffer(&heatMapBuffer, /*untile*/ true, /*parallel*/ true);
                        renderContext->snapshotWeightBuffer(&weightBuffer, /*untile*/ true, /*parallel*/ true);
                        std::vector<scene_rdl2::fb_util::VariablePixelBuffer> aovBuffers;
                        renderContext->snapshotAovBuffers(aovBuffers, /*untile*/ true, /*parallel*/ true);
                        renderContext->snapshotRenderBufferOdd(&renderBufferOdd, /*untile*/ true, /*parallel*/ true);
                        std::vector<scene_rdl2::fb_util::VariablePixelBuffer> displayFilterBuffers;
                        renderContext->snapshotDisplayFilterBuffers(displayFilterBuffers,
                            /*untile*/ true, /*parallel*/ true);
                        moonray::writeRenderOutputsWithMessages(renderContext->getRenderOutputDriver(),
                                                       deepBuffer, cryptomatteBuffer, &heatMapBuffer,
                                                       &weightBuffer, &renderBufferOdd, aovBuffers,
                                                       displayFilterBuffers);
                        frameSavedTimestamp = currFrameTimestamp;
                    }
                }

                // We're done, exit function.
                if (changeWatcher.hasChanged()) {

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

        } while (self->mRenderGui->isActive());
    } catch (...) {
        self->mException = std::current_exception();
        if (self->mRenderGui) {
            self->mRenderGui->close();
        }
    }

    moonray::rndr::cleanUpGlobalDriver();

    return nullptr;
}

void
RaasGuiApplication::run()
{
    // Fire up the Qt app and display the main window.
    QApplication app(mArgc, mArgv);

    std::string lut = mOptions.getColorRenderTransformOverrideLut();
    std::string snapPath = mOptions.getSnapshotPath();
    RenderGui renderGui(mInitialCamType, mOptions.getTileProgress(),
                        mOptions.getApplyColorRenderTransform(),
                        lut.empty() ? nullptr : lut.c_str(), snapPath);
    mRenderGui = &renderGui;

    // Spin off a thread for rendering.
    int retVal = pthread_create(&mRenderThread, nullptr, RaasGuiApplication::startRenderThread, this);
    MNRY_ASSERT_REQUIRE(retVal == 0, "Failed to create render thread.");

    app.exec();

    // Clean up thread
    retVal = pthread_join(mRenderThread, nullptr);
    MNRY_ASSERT_REQUIRE(retVal == 0, "Failed to join render thread.");

    if (mException) {
        std::rethrow_exception(mException);
    }
}

} // namespace moonray_gui


int main(int argc, char* argv[])
{
    moonray_gui::RaasGuiApplication app;
    try {
        return app.main(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

