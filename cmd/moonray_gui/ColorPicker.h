// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <QColorDialog>
#include <QWidget>
#include <QObject>

class QPushButton;

namespace moonray_gui {

/// The ColorPicker is a widget that displays a square swatch that
/// the user can click on to select a new color.
class ColorPicker : public QWidget
{
    Q_OBJECT

public:
    explicit ColorPicker(QWidget* parent, QString label, QColor initialColor);
    ~ColorPicker() {}

    int red()   { return mColor.red(); }
    int green() { return mColor.green(); }
    int blue()  { return mColor.blue(); }

private:
    QColor mColor;
    QColorDialog* mColorDialog;
    QPushButton* mSelector;
    
Q_SIGNALS:
    void sig_colorChanged(const QColor& color);

public Q_SLOTS:
    void slot_changeColor(const QColor& color);
    void slot_openColorDialog();
};

} // namespace moonray_gui
