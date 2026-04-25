// Copyright 2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "ColorPicker.h"
#include "PathVisualizerGui.h"
#include "RenderViewport.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QGridLayout>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTextStream>

#include <moonray/rendering/rndr/PathVisualizerManager.h>

namespace moonray_gui {

/// -----------------------  Defaults  ------------------------------

#define PIXEL_X_MIN 0
#define PIXEL_X_MAX 10000

#define PIXEL_Y_MIN 0
#define PIXEL_Y_MAX 10000

#define PIXEL_SAMPLES_MIN 0
#define PIXEL_SAMPLES_MAX 12

#define BSDF_SAMPLES_MIN 0
#define BSDF_SAMPLES_MAX 12

#define LIGHT_SAMPLES_MIN 0
#define LIGHT_SAMPLES_MAX 12

#define MAX_DEPTH_MIN 0
#define MAX_DEPTH_MAX 50

#define LINE_WIDTH_INTERVAL 1
#define LINE_WIDTH_MIN 1
#define LINE_WIDTH_MAX 8

/// --------------------------- Helpers ------------------------------

QColor convertToQColor(const scene_rdl2::math::Color& color)
{
    return QColor(color.r * 255, color.g * 255, color.b * 255);
}

scene_rdl2::math::Color convertFromQColor(const QColor& color)
{
    return scene_rdl2::math::Color(color.red() / 255.f, color.green() / 255.f, color.blue() / 255.f);
}

/// ------------------------------------------------------------------

PathVisualizerGui::PathVisualizerGui(QWidget* parent, moonray::rndr::PathVisualizerManager* manager) :
    QWidget(parent), 
    mPathVisualizerManager(manager),
    mCurrentRow(0)
{
    // Set up the layout
    QGridLayout* layout = new QGridLayout;
    layout->setContentsMargins(5, 5, 5, 5);
    
    // Set up the title
    QLabel* title = new QLabel("Path Visualizer", this);
    title->setStyleSheet("QLabel { font-size: 25px; border-bottom: 1px solid white; margin-bottom:5px; }");
    layout->addWidget(title, mCurrentRow++, 0, 1, 2);

    // Setup the rest of the path visualizer GUI
    setupOnBtn(layout);
    setupPixelUI(layout);
    setupSamplingUI(layout);
    setupDepthUI(layout);
    setupVisibilityUI(layout);
    setupStyleUI(layout);

    // Setup stylesheet
    QFile styleFile(":/PathVisualizerGui.qss");

    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        QTextStream ts(&styleFile);
        QString styleSheet = ts.readAll();
        this->setStyleSheet(styleSheet);
    }

    // Set the layout for this widget
    setLayout(layout);
}

PathVisualizerGui::~PathVisualizerGui() {}

void PathVisualizerGui::setupOnBtn(QGridLayout* layout)
{
    mOnBtn = new QPushButton("Turn On", this);
    mOnBtn->setProperty("class", "button");
    connect(mOnBtn, SIGNAL(clicked()), this, SLOT(slot_togglePathVisualizer()));

    mOnBtn->setCursor(Qt::PointingHandCursor);

    layout->addWidget(mOnBtn, mCurrentRow++, 0, 1, 3);
}

void PathVisualizerGui::setupPixelUI(QGridLayout* layout)
{
    QLabel* pixelTitle = new QLabel("Pixel: ", this);
    mPixelXSpinBox = new QSpinBox(this);
    mPixelYSpinBox = new QSpinBox(this);
    mPixelXSpinBox->setRange(PIXEL_X_MIN, PIXEL_X_MAX);
    mPixelYSpinBox->setRange(PIXEL_Y_MIN, PIXEL_Y_MAX);
    mPixelXSpinBox->setSingleStep(1);
    mPixelYSpinBox->setSingleStep(1);
    mPixelXSpinBox->setSuffix(" px");
    mPixelYSpinBox->setSuffix(" py");

    const uint32_t pixelXDefault = mPathVisualizerManager->getPixelX();
    const uint32_t pixelYDefault = mPathVisualizerManager->getPixelY();
    mPixelXSpinBox->setValue(pixelXDefault);
    mPixelYSpinBox->setValue(pixelYDefault);
    // only process the input once the user is done typing and presses enter
    mPixelXSpinBox->setKeyboardTracking(false);
    mPixelYSpinBox->setKeyboardTracking(false);    
    connect(mPixelXSpinBox, SIGNAL(valueChanged(int)), this, SLOT(slot_processPixelXValue(int)));
    connect(mPixelYSpinBox, SIGNAL(valueChanged(int)), this, SLOT(slot_processPixelYValue(int)));

    layout->addWidget(pixelTitle, mCurrentRow, 0, 1, 1);
    layout->addWidget(mPixelXSpinBox, mCurrentRow, 1, 1, 1);
    layout->addWidget(mPixelYSpinBox, mCurrentRow++, 2, 1, 1);
}

void PathVisualizerGui::setupSamplingUI(QGridLayout* layout)
{
    QLabel* samplingTitle = new QLabel("Sampling Settings", this);
    samplingTitle->setProperty("class", "header");

    /// ---------------------------------- Use Scene Samples Checkbox --------------------------------------------------

    QCheckBox* useSceneSamples = new QCheckBox("Use Scene Sampling Settings", this);
    useSceneSamples->setCheckState(mPathVisualizerManager->getUseSceneSamples() ? Qt::Checked : Qt::Unchecked);
    useSceneSamples->setCursor(Qt::PointingHandCursor);

    connect(useSceneSamples, SIGNAL(stateChanged(int)), this, SLOT(slot_processUseSceneSamples(int)));

    /// ---------------------------------- Sampling Spin Boxes ---------------------------------------------------------

    QLabel* pixelSamplesTitle = new QLabel("Pixel Samples:", this);
    QLabel* lightSamplesTitle = new QLabel("Light Samples:", this);
    QLabel* bsdfSamplesTitle = new QLabel("Bsdf Samples:", this);

    mPixelSamples = new QSpinBox(this);
    mLightSamples = new QSpinBox(this);
    mBsdfSamples = new QSpinBox(this);

    mPixelSamples->setRange(PIXEL_SAMPLES_MIN, PIXEL_SAMPLES_MAX);
    mLightSamples->setRange(LIGHT_SAMPLES_MIN, LIGHT_SAMPLES_MAX);
    mBsdfSamples->setRange(BSDF_SAMPLES_MIN, BSDF_SAMPLES_MAX);

    mPixelSamples->setValue(mPathVisualizerManager->getPixelSamples());
    mLightSamples->setValue(mPathVisualizerManager->getLightSamples());
    mBsdfSamples->setValue(mPathVisualizerManager->getBsdfSamples());

    connect(mPixelSamples, SIGNAL(valueChanged(int)), this, SLOT(slot_processPixelSamples(int)));
    connect(mLightSamples, SIGNAL(valueChanged(int)), this, SLOT(slot_processLightSamples(int)));
    connect(mBsdfSamples, SIGNAL(valueChanged(int)), this, SLOT(slot_processBsdfSamples(int)));

    /// ----------------------------------------------------------------------------------------------------------------

    layout->addWidget(samplingTitle, mCurrentRow++, 0, 1, 3);
    layout->addWidget(useSceneSamples, mCurrentRow++, 0, 1, 3);

    layout->addWidget(pixelSamplesTitle, mCurrentRow, 0, 1, 2);
    layout->addWidget(mPixelSamples, mCurrentRow++, 2, 1, 1);

    layout->addWidget(lightSamplesTitle, mCurrentRow, 0, 1, 2);
    layout->addWidget(mLightSamples, mCurrentRow++, 2, 1, 1);

    layout->addWidget(bsdfSamplesTitle, mCurrentRow, 0, 1, 2);
    layout->addWidget(mBsdfSamples, mCurrentRow++, 2, 1, 1);
}

void PathVisualizerGui::setupDepthUI(QGridLayout* layout)
{
    QLabel* depthTitle = new QLabel("Max Depth", this);
    depthTitle->setProperty("class", "header");

    QSpinBox* maxDepth = new QSpinBox(this);
    maxDepth->setValue(mPathVisualizerManager->getMaxDepth());
    maxDepth->setRange(MAX_DEPTH_MIN, MAX_DEPTH_MAX);
    maxDepth->setKeyboardTracking(false);
    connect(maxDepth, SIGNAL(valueChanged(int)), this, SLOT(slot_processMaxDepth(int)));

    layout->addWidget(depthTitle, mCurrentRow++, 0, 1, 3);
    layout->addWidget(maxDepth,   mCurrentRow++, 0, 1, 1);
}

void PathVisualizerGui::setupVisibilityUI(QGridLayout* layout)
{
    QLabel* visibilityTitle = new QLabel("Visibility Toggles", this);
    visibilityTitle->setProperty("class", "header");

    // Indirect rays (continuing rays)
    QLabel* indirectTitle = new QLabel("Indirect Rays (Continuing):", this);
    QCheckBox* indirectDiffuseRaysOn = new QCheckBox("Indirect Diffuse Rays", this);
    QCheckBox* indirectSpecularRaysOn = new QCheckBox("Indirect Specular Rays", this);

    // Direct rays (occlusion rays)
    QLabel* directTitle = new QLabel("Direct Rays (Occlusion):", this);
    QCheckBox* directDiffuseRaysOn = new QCheckBox("Direct Diffuse Rays", this);
    QCheckBox* directSpecularRaysOn = new QCheckBox("Direct Specular Rays", this);
    QCheckBox* directLightRaysOn = new QCheckBox("Direct Light Rays", this);

    // Samples
    QLabel* samplesTitle = new QLabel("Samples:", this);
    QCheckBox* diffuseSamplesOn = new QCheckBox("Diffuse Samples", this);
    QCheckBox* specularSamplesOn = new QCheckBox("Specular Samples", this);
    QCheckBox* lightSamplesOn = new QCheckBox("Light Samples", this);

    indirectDiffuseRaysOn->setCheckState(mPathVisualizerManager->getShowIndirectDiffuseRays() ? Qt::Checked : Qt::Unchecked);
    indirectSpecularRaysOn->setCheckState(mPathVisualizerManager->getShowIndirectSpecularRays() ? Qt::Checked : Qt::Unchecked);
    directDiffuseRaysOn->setCheckState(mPathVisualizerManager->getShowDirectDiffuseRays() ? Qt::Checked : Qt::Unchecked);
    directSpecularRaysOn->setCheckState(mPathVisualizerManager->getShowDirectSpecularRays() ? Qt::Checked : Qt::Unchecked);
    directLightRaysOn->setCheckState(mPathVisualizerManager->getShowDirectLightRays() ? Qt::Checked : Qt::Unchecked);
    diffuseSamplesOn->setCheckState(mPathVisualizerManager->getShowDiffuseSamples() ? Qt::Checked : Qt::Unchecked);
    specularSamplesOn->setCheckState(mPathVisualizerManager->getShowSpecularSamples() ? Qt::Checked : Qt::Unchecked);
    lightSamplesOn->setCheckState(mPathVisualizerManager->getShowLightSamples() ? Qt::Checked : Qt::Unchecked);

    indirectDiffuseRaysOn->setCursor(Qt::PointingHandCursor);
    indirectSpecularRaysOn->setCursor(Qt::PointingHandCursor);
    directDiffuseRaysOn->setCursor(Qt::PointingHandCursor);
    directSpecularRaysOn->setCursor(Qt::PointingHandCursor);
    directLightRaysOn->setCursor(Qt::PointingHandCursor);
    diffuseSamplesOn->setCursor(Qt::PointingHandCursor);
    specularSamplesOn->setCursor(Qt::PointingHandCursor);
    lightSamplesOn->setCursor(Qt::PointingHandCursor);

    /// NOTE: stateChanged will be deprecated starting with Qt v6.9
    connect(indirectDiffuseRaysOn,  SIGNAL(stateChanged(int)), this, SLOT(slot_processIndirectDiffuseRayFlag(int)));
    connect(indirectSpecularRaysOn, SIGNAL(stateChanged(int)), this, SLOT(slot_processIndirectSpecularRayFlag(int)));
    connect(directDiffuseRaysOn,    SIGNAL(stateChanged(int)), this, SLOT(slot_processDirectDiffuseRayFlag(int)));
    connect(directSpecularRaysOn,   SIGNAL(stateChanged(int)), this, SLOT(slot_processDirectSpecularRayFlag(int)));
    connect(directLightRaysOn,      SIGNAL(stateChanged(int)), this, SLOT(slot_processDirectLightRayFlag(int)));
    connect(diffuseSamplesOn,       SIGNAL(stateChanged(int)), this, SLOT(slot_processDiffuseSampleFlag(int)));
    connect(specularSamplesOn,      SIGNAL(stateChanged(int)), this, SLOT(slot_processSpecularSampleFlag(int)));
    connect(lightSamplesOn,         SIGNAL(stateChanged(int)), this, SLOT(slot_processLightSampleFlag(int)));
    
    layout->addWidget(visibilityTitle, mCurrentRow++, 0, 1, 3);
    layout->addWidget(indirectTitle, mCurrentRow++, 0, 1, 3);
    layout->addWidget(indirectDiffuseRaysOn,  mCurrentRow++, 0, 1, 3);
    layout->addWidget(indirectSpecularRaysOn, mCurrentRow++, 0, 1, 3);
    layout->addWidget(directTitle, mCurrentRow++, 0, 1, 3);
    layout->addWidget(directDiffuseRaysOn,  mCurrentRow++, 0, 1, 3);
    layout->addWidget(directSpecularRaysOn, mCurrentRow++, 0, 1, 3);
    layout->addWidget(directLightRaysOn,    mCurrentRow++, 0, 1, 3);
    layout->addWidget(samplesTitle, mCurrentRow++, 0, 1, 3);
    layout->addWidget(diffuseSamplesOn,  mCurrentRow++, 0, 1, 3);
    layout->addWidget(specularSamplesOn, mCurrentRow++, 0, 1, 3);
    layout->addWidget(lightSamplesOn,    mCurrentRow++, 0, 1, 3);
}

void PathVisualizerGui::setupStyleUI(QGridLayout* layout)
{
    // ----------------------------- Section title -------------------------------------------------
    
    QLabel* styleTitle = new QLabel("Style Options", this);
    styleTitle->setProperty("class", "header");

    // ----------------------------- Line Width Slider ---------------------------------------------

    QLabel* lineWidthLabel = new QLabel("Line Width: ", this);
    QSlider* lineWidthSlider = new QSlider(this);
    const uint32_t lineWidthDefault = static_cast<uint32_t>(mPathVisualizerManager->getLineWidth() + 0.5f);

    lineWidthSlider->setValue(lineWidthDefault);
    lineWidthSlider->setTickInterval(LINE_WIDTH_INTERVAL);
    lineWidthSlider->setMinimum(LINE_WIDTH_MIN);
    lineWidthSlider->setMaximum(LINE_WIDTH_MAX);
    lineWidthSlider->setOrientation(Qt::Horizontal);

    mLineWidthValue = new QLabel(QString::number(lineWidthDefault), this);
    mLineWidthValue->setAlignment(Qt::AlignHCenter);

    connect(lineWidthSlider, SIGNAL(sliderMoved(int)), this, SLOT(slot_setLineWidth(int)));

    // ----------------------------- Ray Color Pickers ---------------------------------------------

    const scene_rdl2::math::Color& cameraRayColorDefault = mPathVisualizerManager->getCameraRayColor();
    const scene_rdl2::math::Color& indirectDiffuseRayColorDefault = mPathVisualizerManager->getIndirectDiffuseRayColor();
    const scene_rdl2::math::Color& indirectSpecularRayColorDefault = mPathVisualizerManager->getIndirectSpecularRayColor();
    const scene_rdl2::math::Color& directDiffuseRayColorDefault = mPathVisualizerManager->getDirectDiffuseRayColor();
    const scene_rdl2::math::Color& directSpecularRayColorDefault = mPathVisualizerManager->getDirectSpecularRayColor();
    const scene_rdl2::math::Color& directLightRayColorDefault = mPathVisualizerManager->getDirectLightRayColor();

    mCameraRayColorPicker = new ColorPicker(this, "Camera Ray Color: ", convertToQColor(cameraRayColorDefault));
    mIndirectDiffuseRayColorPicker = new ColorPicker(this, "Indirect Diffuse Ray Color: ", convertToQColor(indirectDiffuseRayColorDefault));
    mIndirectSpecularRayColorPicker = new ColorPicker(this, "Indirect Specular Ray Color: ", convertToQColor(indirectSpecularRayColorDefault));
    mDirectDiffuseRayColorPicker = new ColorPicker(this, "Direct Diffuse Ray Color: ", convertToQColor(directDiffuseRayColorDefault));
    mDirectSpecularRayColorPicker = new ColorPicker(this, "Direct Specular Ray Color: ", convertToQColor(directSpecularRayColorDefault));
    mDirectLightRayColorPicker = new ColorPicker(this, "Direct Light Ray Color: ", convertToQColor(directLightRayColorDefault));

    connect(mCameraRayColorPicker, SIGNAL(sig_colorChanged(const QColor&)), 
            this, SLOT(slot_setCameraRayColor(const QColor&)));
    connect(mIndirectDiffuseRayColorPicker, SIGNAL(sig_colorChanged(const QColor&)), 
            this, SLOT(slot_setIndirectDiffuseRayColor(const QColor&)));
    connect(mIndirectSpecularRayColorPicker, SIGNAL(sig_colorChanged(const QColor&)), 
            this, SLOT(slot_setIndirectSpecularRayColor(const QColor&)));
    connect(mDirectDiffuseRayColorPicker, SIGNAL(sig_colorChanged(const QColor&)), 
            this, SLOT(slot_setDirectDiffuseRayColor(const QColor&)));
    connect(mDirectSpecularRayColorPicker, SIGNAL(sig_colorChanged(const QColor&)), 
            this, SLOT(slot_setDirectSpecularRayColor(const QColor&)));
    connect(mDirectLightRayColorPicker, SIGNAL(sig_colorChanged(const QColor&)), 
            this, SLOT(slot_setDirectLightRayColor(const QColor&)));

    // ---------------------------------------------------------------------------------------------
    
    layout->addWidget(styleTitle, mCurrentRow++, 0, 1, 3);
    layout->addWidget(lineWidthLabel, mCurrentRow++, 0, 1, 3);
    layout->addWidget(lineWidthSlider, mCurrentRow, 0, 1, 2);
    layout->addWidget(mLineWidthValue, mCurrentRow++, 2, 1, 1);
    layout->addWidget(mCameraRayColorPicker, mCurrentRow++, 0, 1, 3);
    layout->addWidget(mIndirectDiffuseRayColorPicker, mCurrentRow++, 0, 1, 3);
    layout->addWidget(mIndirectSpecularRayColorPicker, mCurrentRow++, 0, 1, 3);
    layout->addWidget(mDirectDiffuseRayColorPicker, mCurrentRow++, 0, 1, 3);
    layout->addWidget(mDirectSpecularRayColorPicker, mCurrentRow++, 0, 1, 3);
    layout->addWidget(mDirectLightRayColorPicker, mCurrentRow++, 0, 1, 3);
}

/// -------------------------------------------- SLOTS -----------------------------------------------------------------

void
PathVisualizerGui::slot_togglePathVisualizer()
{
    if (mPathVisualizerManager->isOn()) {
        // If the visualizer is on, turn it off, set the button text
        // to "Turn on", and refresh the frame
        mPathVisualizerManager->turnOff();
        mOnBtn->setText("Turn On");
        Q_EMIT sig_styleParamChanged();
    } else {
        // If the visualizer is off, turn it on, start the visualization
        // process, and set the button text to "Turn off"
        mPathVisualizerManager->turnOn();
        mOnBtn->setText("Turn Off");
    }
}

void
PathVisualizerGui::slot_processPixelXValue(const int x)
{
    mPathVisualizerManager->setPixelX(static_cast<uint32_t>(x));
    mPathVisualizerManager->startSimulation();
}

void
PathVisualizerGui::slot_processPixelYValue(const int y)
{
    mPathVisualizerManager->setPixelY(static_cast<uint32_t>(y));
    mPathVisualizerManager->startSimulation();
}

void
PathVisualizerGui::slot_processPixel(const int x, const int y)
{
    mPathVisualizerManager->setPixelX(static_cast<uint32_t>(x));
    mPathVisualizerManager->setPixelY(static_cast<uint32_t>(y));
    
    // We must temporarily block signals before making changes to 
    // avoid processing the pixel values twice
    mPixelXSpinBox->blockSignals(true);
    mPixelXSpinBox->setValue(x);
    mPixelXSpinBox->blockSignals(false);
    
    mPixelYSpinBox->blockSignals(true);
    mPixelYSpinBox->setValue(y);
    mPixelYSpinBox->blockSignals(false);
    
    mPathVisualizerManager->startSimulation();
}

void
PathVisualizerGui::slot_processUseSceneSamples(const int useSceneSamples)
{
    mPathVisualizerManager->setUseSceneSamples(static_cast<bool>(useSceneSamples));
    mPathVisualizerManager->startSimulation();
    
    // Toggle on/off the other sampling settings
    const bool enableSamplingSettings = !static_cast<bool>(useSceneSamples);
    
    mPixelSamples->setEnabled(enableSamplingSettings);
    mLightSamples->setEnabled(enableSamplingSettings);
    mBsdfSamples->setEnabled(enableSamplingSettings);
}

void
PathVisualizerGui::slot_processPixelSamples(const int samples)
{
    mPathVisualizerManager->setPixelSamples(samples); 
    mPathVisualizerManager->startSimulation();
}

void
PathVisualizerGui::slot_processLightSamples(const int samples)
{
    mPathVisualizerManager->setLightSamples(samples); 
    mPathVisualizerManager->startSimulation();
}

void
PathVisualizerGui::slot_processBsdfSamples(const int samples)
{
    mPathVisualizerManager->setBsdfSamples(samples); 
    mPathVisualizerManager->startSimulation();
}

void
PathVisualizerGui::slot_processMaxDepth(const int depth)
{
    mPathVisualizerManager->setMaxDepth(depth); 
    mPathVisualizerManager->startSimulation();
}

void
PathVisualizerGui::slot_processIndirectDiffuseRayFlag(const int flag)
{
    mPathVisualizerManager->setShowIndirectDiffuseRays(static_cast<bool>(flag));
    mPathVisualizerManager->startSimulation();
}

void
PathVisualizerGui::slot_processIndirectSpecularRayFlag(const int flag)
{
    mPathVisualizerManager->setShowIndirectSpecularRays(static_cast<bool>(flag));
    mPathVisualizerManager->startSimulation();
}

void
PathVisualizerGui::slot_processDirectDiffuseRayFlag(const int flag)
{
    mPathVisualizerManager->setShowDirectDiffuseRays(static_cast<bool>(flag));
    mPathVisualizerManager->startSimulation();
}

void
PathVisualizerGui::slot_processDirectSpecularRayFlag(const int flag)
{
    mPathVisualizerManager->setShowDirectSpecularRays(static_cast<bool>(flag));
    mPathVisualizerManager->startSimulation();
}

void
PathVisualizerGui::slot_processDirectLightRayFlag(const int flag)
{
    mPathVisualizerManager->setShowDirectLightRays(static_cast<bool>(flag));
    mPathVisualizerManager->startSimulation();
}

void
PathVisualizerGui::slot_processDiffuseSampleFlag(const int flag)
{
    mPathVisualizerManager->setShowDiffuseSamples(static_cast<bool>(flag));
    mPathVisualizerManager->startSimulation();
}

void
PathVisualizerGui::slot_processSpecularSampleFlag(const int flag)
{
    mPathVisualizerManager->setShowSpecularSamples(static_cast<bool>(flag));
    mPathVisualizerManager->startSimulation();
}

void
PathVisualizerGui::slot_processLightSampleFlag(const int flag)
{
    mPathVisualizerManager->setShowLightSamples(static_cast<bool>(flag));
    mPathVisualizerManager->startSimulation();
}

void
PathVisualizerGui::slot_setLineWidth(const int value)
{
    mPathVisualizerManager->setLineWidth(static_cast<float>(value));
    mLineWidthValue->setText(QString::number(value));
    Q_EMIT sig_styleParamChanged();
}

void
PathVisualizerGui::slot_setCameraRayColor(const QColor& color)
{
    mPathVisualizerManager->setCameraRayColor(convertFromQColor(color)); 
    Q_EMIT sig_styleParamChanged();
}

void
PathVisualizerGui::slot_setIndirectDiffuseRayColor(const QColor& color)
{
    mPathVisualizerManager->setIndirectDiffuseRayColor(convertFromQColor(color)); 
    Q_EMIT sig_styleParamChanged();
}

void
PathVisualizerGui::slot_setIndirectSpecularRayColor(const QColor& color)
{
    mPathVisualizerManager->setIndirectSpecularRayColor(convertFromQColor(color)); 
    Q_EMIT sig_styleParamChanged();
}

void
PathVisualizerGui::slot_setDirectDiffuseRayColor(const QColor& color)
{
    mPathVisualizerManager->setDirectDiffuseRayColor(convertFromQColor(color)); 
    Q_EMIT sig_styleParamChanged();
}

void
PathVisualizerGui::slot_setDirectSpecularRayColor(const QColor& color)
{
    mPathVisualizerManager->setDirectSpecularRayColor(convertFromQColor(color)); 
    Q_EMIT sig_styleParamChanged();
}

void
PathVisualizerGui::slot_setDirectLightRayColor(const QColor& color)
{
    mPathVisualizerManager->setDirectLightRayColor(convertFromQColor(color)); 
    Q_EMIT sig_styleParamChanged();
}

}
