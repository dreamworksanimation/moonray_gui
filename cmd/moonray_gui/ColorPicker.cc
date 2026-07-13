// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "ColorPicker.h"

#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QColorDialog>

namespace moonray_gui {

ColorPicker::ColorPicker(QWidget* parent, QString labelString, QColor initialColor) :
    QWidget(parent), mColor(initialColor)
{
    // Set up the layout
    QHBoxLayout* layout = new QHBoxLayout(this);
    layout->setContentsMargins(5, 5, 5, 5);
    
    // Set up the label
    QLabel* label = new QLabel(labelString, this);
    layout->addWidget(label);

    // Set up the color swatch button
    mSelector = new QPushButton(this);
    mSelector->setFixedWidth(20);
    mSelector->setFixedHeight(20);
    mSelector->setCursor(Qt::PointingHandCursor);

    QString style = QString("QPushButton { background-color: rgb(%1, %2, %3); }").arg(initialColor.red())
                                                                                 .arg(initialColor.green())
                                                                                 .arg(initialColor.blue());
    mSelector->setStyleSheet(style);
    connect(mSelector, SIGNAL(clicked()), this, SLOT(slot_openColorDialog()));
    layout->addWidget(mSelector);

    mColorDialog = new QColorDialog(initialColor, this);
    mColorDialog->hide();
    connect(mColorDialog, SIGNAL(colorSelected(const QColor&)), this, SLOT(slot_changeColor(const QColor&)));

    // Set the layout for this widget
    setLayout(layout);
}

void ColorPicker::slot_openColorDialog()
{
    mColorDialog->show();
}

void ColorPicker::slot_changeColor(const QColor& color)
{
    mColor = color;
    QString style = QString("QPushButton { background-color: rgb(%1, %2, %3); }").arg(mColor.red())
                                                                                 .arg(mColor.green())
                                                                                 .arg(mColor.blue());
    mSelector->setStyleSheet(style);

    Q_EMIT sig_colorChanged(mColor);
}

}
