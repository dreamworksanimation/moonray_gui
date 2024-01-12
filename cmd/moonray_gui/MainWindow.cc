// Copyright 2023-2024 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "MainWindow.h"

#include "FrameUpdateEvent.h"
#include "RenderViewport.h"

#include <QtGui>

#define NO_KEY -1

namespace moonray_gui {

void
Handler::quitApp()
{
    mIsActive = false;
}

MainWindow::MainWindow(QWidget* parent, CameraType initialType, const char *crtOverride, const std::string& snapPath):
    QMainWindow(parent),
    mRenderViewport(nullptr),
    mFastMode(nullptr),
    mGuide(nullptr),
    mSettings(nullptr),
    mTimer(nullptr)
{
    setupUi(initialType, crtOverride, snapPath);

    // Setup fast progressive mode text overlay
    mFastMode = new QLabel(this);
    mFastMode->setStyleSheet(QString("QLabel { margin: 10; padding: 5; background-color : rgba(0.0, 0.0, 0.0, 0.5);") +
                             QString("color: rgba(255.0, 255.0, 255.0, 0.5); }"));
    constexpr int widthResize = 175;
    constexpr int heightResize = 50;
    mFastMode->resize(widthResize, heightResize);
    mFastMode->hide(); // hide text overlay at the start of application

    // Setup exposure/gamma values text overlay
    mSettings = new QLabel(this);
    mSettings->setStyleSheet(QString::fromStdString("QLabel { margin : 10; padding : 5; background-color : ") + 
                             QString::fromStdString("rgba(0.0, 0.0, 0.0, 0.5); color : ") +
                             QString::fromStdString("rgba(255.0, 255.0, 255.0, 1.0); }"));
    mSettings->setText(mRenderViewport->getSettings());
    mSettings->resize(width() / 4, height() / 10);
    mSettings->hide(); // hide text overlay at the start of application

    // Setup hotkey guide text overlay
    mGuide = new QLabel(this);
    mGuide->setText(QString(RenderViewport::mHelp));
    mGuide->setStyleSheet(QString::fromStdString("QLabel { margin : 10; padding : 5; font : 9.5pt;") + 
                          QString::fromStdString("background-color : rgba(0.0, 0.0, 0.0, 0.5); ") + 
                          QString::fromStdString("color : rgba(255.0, 255.0, 255.0, 1.0); }"));
    
    mGuide->resize(width() / 2 , height());
    mGuide->hide();

    // Setup the timer for text overlay
    mTimer = new QTimer(this);
    connect(mTimer, SIGNAL(timeout()), this, SLOT(hideTextOverlay()));

    // Print welcome message to console
    std::cout << "Welcome to Moonray GUI. Press H while running the application to open the hotkey guide." << std::endl;
}

MainWindow::~MainWindow()
{
    if (mRenderViewport) delete mRenderViewport;
    if (mFastMode) delete mFastMode;
    if (mGuide) delete mGuide;
    if (mSettings) delete mSettings;
    if (mTimer) delete mTimer;
}


void
MainWindow::setupUi(CameraType initialType, const char *crtOverride, const std::string& snapPath)
{
    // We don't support window maximization.
    setWindowFlags(windowFlags() ^ Qt::WindowMaximizeButtonHint);
    //setAttribute(Qt::WA_DeleteOnClose);

    // The RenderViewport is our only widget for now.
    mRenderViewport = new RenderViewport(this, initialType, crtOverride, snapPath);
    setCentralWidget(mRenderViewport);

    // Restore previous window position if we have it.
    QSettings settings("DWA", "moonray_gui");
    settings.beginGroup("MainWindow");
    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());
    settings.endGroup();
}

void
MainWindow::setFastModeText()
{
    if (mRenderViewport == nullptr) return;
    moonray::rndr::FastRenderMode currentMode = mRenderViewport->getFastMode();
    switch(currentMode) {
        case moonray::rndr::FastRenderMode::NORMALS:
            mFastMode->setText("Geometric normals");
            std::cout << "Fast render mode: Geometric normals" << std::endl;
            break;
        case moonray::rndr::FastRenderMode::NORMALS_SHADING:
            mFastMode->setText("Shading normals");
            std::cout << "Fast render mode: Shading normals" << std::endl;
            break;
        case moonray::rndr::FastRenderMode::FACING_RATIO:
            mFastMode->setText("Facing ratio");
            std::cout << "Fast render mode: Facing ratio" << std::endl;
            break;
        case moonray::rndr::FastRenderMode::FACING_RATIO_INVERSE:
            mFastMode->setText("Inverse facing ratio");
            std::cout << "Fast render mode: Inverse facing ratio" << std::endl;
            break;
        case moonray::rndr::FastRenderMode::UVS:
            mFastMode->setText("UVs");
            std::cout << "Fast render mode: UVs" << std::endl;
            break;
        default:
            break;
    }
}

bool
MainWindow::event(QEvent* event)
{
    // Handle frame updates by handling them off to the RenderViewport and
    // resizing the window to account for viewport changes.
    if (event->type() == FrameUpdateEvent::type()) {
        mRenderViewport->updateFrame(static_cast<FrameUpdateEvent*>(event));
        mSettings->setText(mRenderViewport->getSettings());
        resize(minimumSizeHint());
        return true;
    }

    else if (event->type() == QEvent::KeyPress) {
        QKeyEvent *key = static_cast<QKeyEvent *>(event);
        // ESC key closes interactive viewport.
        if (key->key() == Qt::Key_Escape) {
            close();
            return true;
        } else if (key->key() == Qt::Key_H) {
            mGuide->show();
        } else if (key->key() == Qt::Key_X || key->key() == Qt::Key_Y) {
            if ((mRenderViewport->getUpdateGamma() || mRenderViewport->getUpdateExposure())) {
                mSettings->show();
            }
        }
    }

    else if (event->type() == QEvent::KeyRelease) {
        QKeyEvent *key = static_cast<QKeyEvent *>(event);
        if (!key->isAutoRepeat()) {
            // set text overlay timeouts in milliseconds
            constexpr int hideExposureGamma = 2000;
            constexpr int hideHelp = 3500;
            constexpr int hideFastMode = 3500;
            if (key->modifiers() == Qt::NoModifier) {
                if ((key->key() == Qt::Key_X || key->key() == Qt::Key_Y) && 
                    QGuiApplication::mouseButtons() == Qt::NoButton) {
                    mTimer->start(hideExposureGamma);
                } else if (key->key() == Qt::Key_H) {
                    mTimer->start(hideHelp);
                } else if (key->key() == Qt::Key_L) {
                    // Check if fast mode is enabled
                    if (mRenderViewport->isFastProgressive()) {
                        setFastModeText();
                        mFastMode->show();
                        mTimer->start(hideFastMode);
                    }
                }
            } else if (key->modifiers() == Qt::ShiftModifier) {
                if (key->key() == Qt::Key_X || key->key() == Qt::Key_Y || 
                    key->key() == Qt::Key_Up || key->key() == Qt::Key_Down) {
                    mSettings->show();
                    mTimer->start(hideExposureGamma);
                }
            } else if (key->modifiers() == Qt::AltModifier) {
                if (mRenderViewport->isFastProgressive()) {
                    if (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down) {
                        setFastModeText();
                        mFastMode->show();
                        mTimer->start(hideFastMode);
                    }
                }
            }
        }
    }

    else if (event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent *button = static_cast<QMouseEvent *>(event);
        // set text overlay timeout in millsssssiseconds
        constexpr int hideExposureGamma = 2000;
        if (button->button() == Qt::LeftButton && mRenderViewport->getKey() == NO_KEY) {
            mTimer->start(hideExposureGamma);
        }
    }
        
    return QMainWindow::event(event);
}

void
MainWindow::closeEvent(QCloseEvent* event)
{
    // Save the window position when we exit.
    QSettings settings("DWA", "moonray_gui");
    settings.beginGroup("MainWindow");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
    settings.endGroup();

    QMainWindow::closeEvent(event);
}

void
MainWindow::hideTextOverlay()
{
    // hide the text overlay when timer goes off
    mSettings->hide();
    mGuide->hide();
    mFastMode->hide();
}

} // namespace moonray_gui

