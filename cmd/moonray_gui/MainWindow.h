// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "GuiTypes.h"
#include "QtQuirks.h"

#include <QLabel>
#include <QMainWindow>
#include <QTimer>

class QEvent;
class QCloseEvent;

namespace moonray_gui {

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent, CameraType initialType, const char *crtOverride, const std::string& snapPath);

    ~MainWindow();

    bool event(QEvent* event);
    void closeEvent(QCloseEvent* event);

    RenderViewport* getRenderViewport() const { return mRenderViewport; }
    const QLabel* getSettings() const { return mSettings; }

private:
    void setupUi(CameraType initialType, const char *crtOverride, const std::string& snapPath);

    void setFastModeText();

    RenderViewport* mRenderViewport;
    
    QLabel* mFastMode;
    QLabel* mGuide;
    QLabel* mSettings;
    
    QTimer* mTimer;

    
public slots:
    void hideTextOverlay();
};

class Handler : public QObject
{
    Q_OBJECT
public:
    Handler(QObject* parent = 0) : QObject(parent), mIsActive(false) {};
    bool mIsActive;

public Q_SLOTS:
    void quitApp();

};

} // namespace moonray_gui

