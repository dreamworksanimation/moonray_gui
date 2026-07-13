// Copyright 2026 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <QWidget>

class QGridLayout;
class QPushButton;
class QSpinBox;
class QLabel;

namespace moonray { namespace rndr { class PathVisualizerManager; }}

namespace moonray_gui {

class ColorPicker;
class RenderViewport;

class PathVisualizerGui : public QWidget
{
    Q_OBJECT

public:
    explicit PathVisualizerGui(QWidget* parent, moonray::rndr::PathVisualizerManager* manager);

    ~PathVisualizerGui();

public Q_SLOTS:

    void slot_togglePathVisualizer();

    void slot_processPixelXValue(const int x);
    void slot_processPixelYValue(const int y);
    void slot_processPixel(const int x, const int y);

    void slot_processMaxDepth(const int depth);

    void slot_processUseSceneSamples(const int useSceneSamples);
    void slot_processPixelSamples(const int samples);
    void slot_processLightSamples(const int samples);
    void slot_processBsdfSamples(const int samples);

    void slot_processIndirectDiffuseRayFlag(const int flag);
    void slot_processIndirectSpecularRayFlag(const int flag);
    void slot_processDirectDiffuseRayFlag(const int flag);
    void slot_processDirectSpecularRayFlag(const int flag);
    void slot_processDirectLightRayFlag(const int flag);
    void slot_processDiffuseSampleFlag(const int flag);
    void slot_processSpecularSampleFlag(const int flag);
    void slot_processLightSampleFlag(const int flag);

    void slot_setLineWidth(const int width);
    void slot_setCameraRayColor(const QColor& color);
    void slot_setIndirectDiffuseRayColor(const QColor& color);
    void slot_setIndirectSpecularRayColor(const QColor& color);
    void slot_setDirectDiffuseRayColor(const QColor& color);
    void slot_setDirectSpecularRayColor(const QColor& color);
    void slot_setDirectLightRayColor(const QColor& color);

Q_SIGNALS:
    void sig_styleParamChanged();

private:

    void setupOnBtn(QGridLayout* layout);
    void setupPixelUI(QGridLayout* layout);
    void setupSamplingUI(QGridLayout* layout);
    void setupDepthUI(QGridLayout* layout);
    void setupVisibilityUI(QGridLayout* layout);
    void setupStyleUI(QGridLayout* layout);

    moonray::rndr::PathVisualizerManager* mPathVisualizerManager;

    int mCurrentRow;

    QPushButton* mOnBtn;

    QSpinBox* mPixelXSpinBox;
    QSpinBox* mPixelYSpinBox;

    QSpinBox* mPixelSamples;
    QSpinBox* mLightSamples;
    QSpinBox* mBsdfSamples;

    QLabel* mLineWidthValue;

    ColorPicker* mCameraRayColorPicker;
    ColorPicker* mIndirectDiffuseRayColorPicker;
    ColorPicker* mIndirectSpecularRayColorPicker;
    ColorPicker* mDirectDiffuseRayColorPicker;
    ColorPicker* mDirectSpecularRayColorPicker;
    ColorPicker* mDirectLightRayColorPicker;
};

} // namespace moonray_gui
