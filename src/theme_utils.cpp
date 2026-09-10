#include "theme_utils.h"
#include <tqapplication.h>
#include <tqmap.h>
#include <tqobjectlist.h>

int colorLuminance(const TQColor &c) {
    return (c.red() * 299 + c.green() * 587 + c.blue() * 114) / 1000;
}

void invertImage(TQImage &img) {
    if (img.isNull()) return;
    if (img.depth() != 32) {
        img = img.convertDepth(32);
    }
    img.setAlphaBuffer(true);
    const int w = img.width();
    const int h = img.height();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            TQRgb px = img.pixel(x, y);
            const int alpha = tqAlpha(px);
            if (alpha > 0) {
                const int r = 255 - tqRed(px);
                const int g = 255 - tqGreen(px);
                const int b = 255 - tqBlue(px);
                img.setPixel(x, y, tqRgba(r, g, b, alpha));
            }
        }
    }
}

TQPixmap invertedPixmap(const TQPixmap &pm) {
    if (pm.isNull()) return pm;
    TQImage img = pm.convertToImage();
    invertImage(img);
    return TQPixmap(img);
}

YabatmanTheme resolveTheme(const YabatmanConfig *config) {
    YabatmanTheme t;
    int mode = config ? config->dark_mode : 0;
    if (mode < 0 || mode > 2) mode = 0;
    t.mode = mode;

    if (mode == YabatmanConfig::THEME_MODE_LIGHT) {
        t.isDark = false;
        if (config && (config->tint_popup_r != 242 || config->tint_popup_g != 242 || config->tint_popup_b != 242)) {
            t.windowBg = TQColor(config->tint_popup_r, config->tint_popup_g, config->tint_popup_b);
        } else {
            t.windowBg = TQColor(238, 238, 238);
        }
        t.baseBg = TQColor(255, 255, 255);
        t.textColor = TQColor(0, 0, 0);
        t.disabledText = TQColor(128, 128, 128);
        t.buttonBg = TQColor(226, 226, 226);
        t.buttonText = TQColor(0, 0, 0);
        t.highlightBg = TQColor(61, 174, 233);
        t.highlightText = TQColor(255, 255, 255);
        t.hoverBg = TQColor(220, 220, 220);
        t.headerBg = TQColor(215, 215, 215);
        t.headerText = TQColor(0, 0, 0);
        t.separatorColor = TQColor(200, 200, 200);
        t.gridColor = TQColor(180, 180, 180);
    } else if (mode == YabatmanConfig::THEME_MODE_DARK) {
        t.isDark = true;
        t.windowBg = TQColor(28, 28, 28);
        t.baseBg = TQColor(18, 18, 18);
        t.textColor = TQColor(240, 240, 240);
        t.disabledText = TQColor(112, 112, 112);
        t.buttonBg = TQColor(44, 44, 44);
        t.buttonText = TQColor(240, 240, 240);
        t.highlightBg = TQColor(41, 128, 185);
        t.highlightText = TQColor(255, 255, 255);
        t.hoverBg = TQColor(46, 46, 46);
        t.headerBg = TQColor(38, 38, 38);
        t.headerText = TQColor(245, 245, 245);
        t.separatorColor = TQColor(50, 50, 50);
        t.gridColor = TQColor(65, 65, 65);
    } else {
        // Follow TDE
        TQColor tdeBg = tqApp->palette().color(TQPalette::Active, TQColorGroup::Background);
        TQColor tdeFg = tqApp->palette().color(TQPalette::Active, TQColorGroup::Text);
        TQColor tdeBase = tqApp->palette().color(TQPalette::Active, TQColorGroup::Base);
        TQColor tdeBtn = tqApp->palette().color(TQPalette::Active, TQColorGroup::Button);
        TQColor tdeBtnText = tqApp->palette().color(TQPalette::Active, TQColorGroup::ButtonText);
        TQColor tdeHl = tqApp->palette().color(TQPalette::Active, TQColorGroup::Highlight);
        TQColor tdeHlText = tqApp->palette().color(TQPalette::Active, TQColorGroup::HighlightedText);

        int bgLum = colorLuminance(tdeBg);
        int fgLum = colorLuminance(tdeFg);
        t.isDark = (bgLum < 128) || (fgLum > bgLum);

        t.windowBg = tdeBg;
        t.baseBg = tdeBase;
        t.textColor = tdeFg;
        t.disabledText = tqApp->palette().color(TQPalette::Disabled, TQColorGroup::Text);
        t.buttonBg = tdeBtn;
        t.buttonText = tdeBtnText;
        t.highlightBg = tdeHl;
        t.highlightText = tdeHlText;

        if (t.isDark) {
            t.hoverBg = tdeBg.light(125);
            t.headerBg = tdeBg.light(115);
            t.headerText = tdeFg;
            t.separatorColor = tdeBg.light(135);
            t.gridColor = tdeBg.light(140);
        } else {
            t.hoverBg = tdeBg.dark(110);
            t.headerBg = tdeBg.dark(108);
            t.headerText = tdeFg;
            t.separatorColor = tdeBg.dark(120);
            t.gridColor = TQColor(180, 180, 180);
        }
    }
    return t;
}

struct IconCacheKey {
    const unsigned char *data;
    int width;
    int height;
    bool operator<(const IconCacheKey &other) const {
        if (data != other.data) return data < other.data;
        if (width != other.width) return width < other.width;
        return height < other.height;
    }
};

struct CachedIconPair {
    TQPixmap normal;
    TQPixmap inverted;
};

TQPixmap getThemedPixmap(const unsigned char *data, size_t size, int width, int height, bool isDark) {
    if (!data || size == 0) return TQPixmap();

    static TQMap<IconCacheKey, CachedIconPair> *s_globalIconCache = NULL;
    if (!s_globalIconCache) {
        s_globalIconCache = new TQMap<IconCacheKey, CachedIconPair>();
    }

    IconCacheKey key;
    key.data = data;
    key.width = width;
    key.height = height;

    CachedIconPair &pair = (*s_globalIconCache)[key];
    if (pair.normal.isNull()) {
        TQImage img;
        if (img.loadFromData(data, (int)size, "PNG")) {
            if (width > 0 && height > 0 && (img.width() != width || img.height() != height)) {
                img = img.smoothScale(width, height);
            }
            if (img.depth() != 32) {
                img = img.convertDepth(32);
            }
            img.setAlphaBuffer(true);
            pair.normal = TQPixmap(img);
            pair.inverted = invertedPixmap(pair.normal);
        }
    }

    return isDark ? pair.inverted : pair.normal;
}

static bool isDescendantOf(const TQWidget *child, const TQWidget *parent) {
    if (!child || !parent) return false;
    for (const TQWidget *p = child->parentWidget(); p != NULL; p = p->parentWidget()) {
        if (p == parent) return true;
    }
    return false;
}

void applyDialogTheme(TQWidget *dlg, const YabatmanTheme &theme, TQFrame *headerFrame, TQLabel *headerTitle) {
    if (!dlg) return;

    if (theme.mode == YabatmanConfig::THEME_MODE_FOLLOW_TDE) {
        // In Follow TDE, do not override palette, let TDE style everything.
        dlg->unsetPalette();
        dlg->setPalette(tqApp->palette());
        TQObjectList *children = dlg->queryList("TQWidget");
        if (children) {
            for (TQObjectListIt it(*children); it.current(); ++it) {
                TQWidget *w = (TQWidget*)it.current();
                if (w != headerFrame && !isDescendantOf(w, headerFrame)) {
                    w->unsetPalette();
                    w->setPalette(tqApp->palette());
                }
            }
            delete children;
        }
        if (headerFrame) {
            headerFrame->setPaletteBackgroundColor(theme.headerBg);
            headerFrame->setBackgroundMode(TQt::PaletteBackground);
            TQPalette hPal = tqApp->palette();
            hPal.setColor(TQPalette::Active, TQColorGroup::Background, theme.headerBg);
            hPal.setColor(TQPalette::Inactive, TQColorGroup::Background, theme.headerBg);
            hPal.setColor(TQPalette::Disabled, TQColorGroup::Background, theme.headerBg);
            hPal.setColor(TQPalette::Active, TQColorGroup::Foreground, theme.headerText);
            hPal.setColor(TQPalette::Inactive, TQColorGroup::Foreground, theme.headerText);
            hPal.setColor(TQPalette::Active, TQColorGroup::Text, theme.headerText);
            hPal.setColor(TQPalette::Inactive, TQColorGroup::Text, theme.headerText);
            headerFrame->setPalette(hPal);

            TQObjectList *hChildren = headerFrame->queryList("TQWidget");
            if (hChildren) {
                for (TQObjectListIt it(*hChildren); it.current(); ++it) {
                    TQWidget *hw = (TQWidget*)it.current();
                    hw->setBackgroundMode(TQt::PaletteBackground);
                    hw->setPalette(hPal);
                    hw->setPaletteBackgroundColor(theme.headerBg);
                }
                delete hChildren;
            }
        }
        if (headerTitle) {
            headerTitle->setBackgroundMode(TQt::PaletteBackground);
            headerTitle->setPaletteBackgroundColor(theme.headerBg);
            headerTitle->setPaletteForegroundColor(theme.headerText);
        }
        return;
    }

    dlg->setPaletteBackgroundColor(theme.windowBg);

    TQPalette pal = dlg->palette();
    // Active
    pal.setColor(TQPalette::Active, TQColorGroup::Background, theme.windowBg);
    pal.setColor(TQPalette::Active, TQColorGroup::Base, theme.baseBg);
    pal.setColor(TQPalette::Active, TQColorGroup::Foreground, theme.textColor);
    pal.setColor(TQPalette::Active, TQColorGroup::Text, theme.textColor);
    pal.setColor(TQPalette::Active, TQColorGroup::Button, theme.buttonBg);
    pal.setColor(TQPalette::Active, TQColorGroup::ButtonText, theme.buttonText);
    pal.setColor(TQPalette::Active, TQColorGroup::Highlight, theme.highlightBg);
    pal.setColor(TQPalette::Active, TQColorGroup::HighlightedText, theme.highlightText);
    pal.setColor(TQPalette::Active, TQColorGroup::Dark, theme.separatorColor);

    // Inactive
    pal.setColor(TQPalette::Inactive, TQColorGroup::Background, theme.windowBg);
    pal.setColor(TQPalette::Inactive, TQColorGroup::Base, theme.baseBg);
    pal.setColor(TQPalette::Inactive, TQColorGroup::Foreground, theme.textColor);
    pal.setColor(TQPalette::Inactive, TQColorGroup::Text, theme.textColor);
    pal.setColor(TQPalette::Inactive, TQColorGroup::Button, theme.buttonBg);
    pal.setColor(TQPalette::Inactive, TQColorGroup::ButtonText, theme.buttonText);
    pal.setColor(TQPalette::Inactive, TQColorGroup::Highlight, theme.highlightBg);
    pal.setColor(TQPalette::Inactive, TQColorGroup::HighlightedText, theme.highlightText);
    pal.setColor(TQPalette::Inactive, TQColorGroup::Dark, theme.separatorColor);

    // Disabled
    pal.setColor(TQPalette::Disabled, TQColorGroup::Background, theme.windowBg);
    pal.setColor(TQPalette::Disabled, TQColorGroup::Base, theme.baseBg);
    pal.setColor(TQPalette::Disabled, TQColorGroup::Foreground, theme.disabledText);
    pal.setColor(TQPalette::Disabled, TQColorGroup::Text, theme.disabledText);
    pal.setColor(TQPalette::Disabled, TQColorGroup::Button, theme.buttonBg);
    pal.setColor(TQPalette::Disabled, TQColorGroup::ButtonText, theme.disabledText);

    dlg->setPalette(pal);

    TQObjectList *children = dlg->queryList("TQWidget");
    if (children) {
        for (TQObjectListIt it(*children); it.current(); ++it) {
            TQWidget *w = (TQWidget*)it.current();
            if (w != headerFrame && !isDescendantOf(w, headerFrame)) {
                w->setPalette(pal);
            }
        }
        delete children;
    }

    if (headerFrame) {
        headerFrame->setPaletteBackgroundColor(theme.headerBg);
        headerFrame->setBackgroundMode(TQt::PaletteBackground);
        TQPalette hPal = pal;
        hPal.setColor(TQPalette::Active, TQColorGroup::Background, theme.headerBg);
        hPal.setColor(TQPalette::Inactive, TQColorGroup::Background, theme.headerBg);
        hPal.setColor(TQPalette::Disabled, TQColorGroup::Background, theme.headerBg);
        hPal.setColor(TQPalette::Active, TQColorGroup::Foreground, theme.headerText);
        hPal.setColor(TQPalette::Inactive, TQColorGroup::Foreground, theme.headerText);
        hPal.setColor(TQPalette::Active, TQColorGroup::Text, theme.headerText);
        hPal.setColor(TQPalette::Inactive, TQColorGroup::Text, theme.headerText);
        headerFrame->setPalette(hPal);

        TQObjectList *hChildren = headerFrame->queryList("TQWidget");
        if (hChildren) {
            for (TQObjectListIt it(*hChildren); it.current(); ++it) {
                TQWidget *hw = (TQWidget*)it.current();
                hw->setBackgroundMode(TQt::PaletteBackground);
                hw->setPalette(hPal);
                hw->setPaletteBackgroundColor(theme.headerBg);
            }
            delete hChildren;
        }
    }
    if (headerTitle) {
        headerTitle->setBackgroundMode(TQt::PaletteBackground);
        headerTitle->setPaletteBackgroundColor(theme.headerBg);
        headerTitle->setPaletteForegroundColor(theme.headerText);
    }
}
