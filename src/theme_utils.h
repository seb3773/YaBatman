#ifndef THEME_UTILS_H
#define THEME_UTILS_H

#include "config_manager.h"
#include <tqcolor.h>
#include <tqpalette.h>
#include <tqpixmap.h>
#include <tqimage.h>
#include <tqdialog.h>
#include <tqframe.h>
#include <tqlabel.h>

struct YabatmanTheme {
    int mode;               // YabatmanConfig::ThemeMode: 0=Follow TDE, 1=Light, 2=Dark
    bool isDark;            // whether N&B monochrome icons should be inverted

    // Colors for window, widgets, and dialogs
    TQColor windowBg;
    TQColor baseBg;
    TQColor textColor;
    TQColor disabledText;
    TQColor buttonBg;
    TQColor buttonText;
    TQColor highlightBg;
    TQColor highlightText;
    TQColor hoverBg;
    TQColor headerBg;
    TQColor headerText;
    TQColor separatorColor;
    TQColor gridColor;
};

// Calculate perceived luminance according to ITU-R BT.601
int colorLuminance(const TQColor &c);

// Invert RGB channels while preserving alpha
void invertImage(TQImage &img);

// Invert pixmap using invertImage
TQPixmap invertedPixmap(const TQPixmap &pm);

// Resolve theme properties according to current config and TDE desktop state
YabatmanTheme resolveTheme(const YabatmanConfig *config);

// Get cached or newly generated themed icon (normal or inverted)
TQPixmap getThemedPixmap(const unsigned char *data, size_t size, int width, int height, bool isDark);

// Apply palette and header styling to a TQDialog or TQWidget
void applyDialogTheme(TQWidget *dlg, const YabatmanTheme &theme, TQFrame *headerFrame = 0, TQLabel *headerTitle = 0);

#endif // THEME_UTILS_H
