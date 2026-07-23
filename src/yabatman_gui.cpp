#include "yabatman_gui.h"
#include "battery_icons.h"
#include "battery_info_dialog.h"
#include "battery_history_dialog.h"
#include "config_dialog.h"
#include <tqpainter.h>
#include <tqlayout.h>
#include <tqframe.h>
#include <tqfont.h>
#include <tqtooltip.h>
#include <tqmessagebox.h>
#include <tqdialog.h>
#include <tqimage.h>
#include <tqpixmap.h>
#include <tqbitmap.h>
#include <tqdir.h>
#include <tqfile.h>
#include <tqtextstream.h>
#include <tqcursor.h>
#include <tqapplication.h>
#ifndef PURE_TQT3
#include <tdeglobalsettings.h>
#endif
#include <tqobjectlist.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#undef Status
#undef Bool
#undef Cursor
#undef Unsorted
#undef Always
#undef None
#undef KeyPress
#undef KeyRelease
#undef FocusIn
#undef FocusOut
#undef FontChange



#define SOCK_PATH "/run/yabatmand/daemon.sock"



// ==========================================
// Shared PNG decode (same pipeline as systray)
// ==========================================
static TQImage loadEmbeddedPng(const unsigned char *data, size_t size) {
    TQImage img;
    if (!img.loadFromData(data, (int)size, "PNG")) {
        return TQImage();
    }
    if (img.depth() != 32) {
        img = img.convertDepth(32);
    }
    img.setAlphaBuffer(true);
    return img;
}

// ==========================================
// Popup helpers (original Gtk3 look)
// ==========================================
static const TQColor kPopupHoverColor(0x3D, 0xAE, 0xE9);

static int choosePopupIconSize(int srcW, int srcH, int preferredPx) {
    int src = srcW < srcH ? srcW : srcH;
    if (src <= preferredPx) {
        return src;
    }
    // Large assets (history 64px, charge 64px): avoid crushing to 20px in TQt3.
    if (src >= 48) {
        return 32;
    }
    return preferredPx;
}

static TQPixmap popupIconPixmap(const unsigned char *data, size_t size, int preferredPx) {
    TQImage src = loadEmbeddedPng(data, size);
    if (src.isNull()) {
        return TQPixmap();
    }

    int px = choosePopupIconSize(src.width(), src.height(), preferredPx);
    if (src.width() == px && src.height() == px) {
        return TQPixmap(src);
    }

    TQImage scaled = src.smoothScale(px, px);
    if (scaled.isNull()) {
        return TQPixmap();
    }
    if (scaled.depth() != 32) {
        scaled = scaled.convertDepth(32);
    }
    scaled.setAlphaBuffer(true);
    return TQPixmap(scaled);
}

static void setIconLabelPixmap(TQLabel *lbl, const TQPixmap &pm) {
    if (!lbl) return;
    if (pm.isNull()) {
        lbl->setFixedSize(20, 20);
        return;
    }
    lbl->setPixmap(pm);
    lbl->setFixedSize(pm.width(), pm.height());
}

static TQFrame *addPopupSeparator(TQVBoxLayout *layout, TQWidget *parent) {
    TQFrame *sep = new TQFrame(parent);
    sep->setFrameShape(TQFrame::HLine);
    sep->setFrameShadow(TQFrame::Sunken);
    sep->setLineWidth(1);
    layout->addSpacing(2);
    layout->addWidget(sep);
    layout->addSpacing(2);
    return sep;
}

static void setWidgetTransparent(TQWidget *w) {
    if (!w) return;
    w->setBackgroundMode(TQt::NoBackground);
}

static const int kPopupWidth = 268;
static const int kPopupInnerWidth = kPopupWidth - 10;
static const int kPopupRowMargin = 4;
static const int kProfSliderBottomSpacing = 22;
static const int kPopupRowSpacing = 6;
static const int kPopupIconSlotW = 24;
static const int kPopupRowHeight = 28;
static const int kPopupMarkSize = 14;

static TQPixmap greyedPopupIcon(const TQPixmap &icon) {
    if (icon.isNull()) {
        return icon;
    }
    TQImage img = icon.convertToImage();
    if (img.isNull()) {
        return icon;
    }
    if (img.depth() != 32) {
        img = img.convertDepth(32);
    }
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            TQRgb px = img.pixel(x, y);
            const int alpha = tqAlpha(px);
            if (alpha == 0) {
                continue;
            }
            const int grey = (tqRed(px) + tqGreen(px) + tqBlue(px)) / 3;
            const int blend = (grey + 160) / 2;
            const int newAlpha = alpha * 55 / 100;
            img.setPixel(x, y, tqRgba(blend, blend, blend, newAlpha));
        }
    }
    return TQPixmap(img);
}

static void paintCheckMark(TQPainter &p, const TQRect &rect, bool enabled) {
    static TQPixmap checkPm;
    if (checkPm.isNull()) {
        checkPm = popupIconPixmap(check_data, check_size, kPopupMarkSize);
    }
    const int x = rect.width() - kPopupRowMargin - kPopupMarkSize;
    const int y = (rect.height() - kPopupMarkSize) / 2;
    if (enabled) {
        p.drawPixmap(x, y, checkPm);
    } else {
        p.drawPixmap(x, y, greyedPopupIcon(checkPm));
    }
}

static void paintPopupRowContent(TQPainter &p, const TQRect &rect, const TQPixmap &icon,
                                  const TQString &text, const TQColor &normalBg,
                                  bool hovered, bool enabled, int rightReserve, bool bold = false) {
    const TQColor fill = (hovered && enabled) ? kPopupHoverColor : normalBg;
    p.fillRect(rect, fill);

    int x = kPopupRowMargin;
    if (!icon.isNull()) {
        const TQPixmap &drawIcon = enabled ? icon : greyedPopupIcon(icon);
        const int iy = (rect.height() - drawIcon.height()) / 2;
        p.drawPixmap(x, iy, drawIcon);
        x += drawIcon.width() + kPopupRowSpacing;
    } else {
        x += kPopupIconSlotW + kPopupRowSpacing;
    }

    const TQColor textColor = enabled ? TQColor(0, 0, 0) : TQColor(120, 120, 120);
    p.setPen(textColor);

#ifdef PURE_TQT3
    TQFont font = TQApplication::font();
#else
    TQFont font = TDEGlobalSettings::menuFont();
#endif
    if (bold && enabled) {
        font.setBold(true);
    }
    p.setFont(font);

    const TQFontMetrics fm = p.fontMetrics();
    const int textY = (rect.height() + fm.ascent() - fm.descent()) / 2;
    const int textW = rect.width() - x - rightReserve;
    if (textW > 0) {
        p.drawText(x, textY, text);
    }
}

class PopupRow : public TQWidget {
public:
    PopupRow(TQWidget *parent) : TQWidget(parent) {}
    virtual void clearHoverState() = 0;
};

class PopupMenuRow : public PopupRow {
public:
    typedef void (YabatmanPopup::*ActionSlot)();
    PopupMenuRow(const TQPixmap &icon, const TQString &text, const TQColor &bg,
                 YabatmanPopup *popup, ActionSlot slot, TQWidget *parent)
        : PopupRow(parent), m_icon(icon), m_text(text), m_bg(bg),
          m_hovered(false), m_popup(popup), m_slot(slot)
    {
        setFixedHeight(kPopupRowHeight);
        setBackgroundMode(TQt::NoBackground);
    }

protected:
    void enterEvent(TQEvent *) {
        m_hovered = true;
        update();
    }
    void leaveEvent(TQEvent *) {
        TQPoint localPos = mapFromGlobal(TQCursor::pos());
        if (rect().contains(localPos)) {
            return;
        }
        m_hovered = false;
        update();
    }
    void paintEvent(TQPaintEvent *) {
        TQPainter p(this);
        paintPopupRowContent(p, rect(), m_icon, m_text, m_bg,
                             m_hovered, isEnabled(), kPopupRowMargin);
    }
    void mousePressEvent(TQMouseEvent *) {
        if (m_popup && m_slot) {
            (m_popup->*m_slot)();
        }
    }
public:
    void clearHoverState() {
        if (m_hovered) {
            m_hovered = false;
            update();
        }
    }

private:
    TQPixmap m_icon;
    TQString m_text;
    TQColor m_bg;
    bool m_hovered;
    YabatmanPopup *m_popup;
    ActionSlot m_slot;
};

class PopupCheckRow : public PopupRow {
public:
    typedef void (YabatmanPopup::*ToggleSlot)(bool);

    PopupCheckRow(const TQPixmap &icon, const TQString &text, const TQColor &bg, bool checked,
                  YabatmanPopup *popup, ToggleSlot slot, TQWidget *parent)
        : PopupRow(parent), m_icon(icon), m_text(text), m_bg(bg), m_checked(checked),
          m_hovered(false), m_popup(popup), m_slot(slot)
    {
        setFixedHeight(kPopupRowHeight);
        setBackgroundMode(TQt::NoBackground);
    }

    void setChecked(bool checked) {
        if (m_checked != checked) {
            m_checked = checked;
            update();
        }
    }

    bool isChecked() const {
        return m_checked;
    }

    void setEnabled(bool enabled) {
        TQWidget::setEnabled(enabled);
        update();
    }

protected:
    void enterEvent(TQEvent *) {
        if (!isEnabled()) {
            return;
        }
        m_hovered = true;
        update();
    }
    void leaveEvent(TQEvent *) {
        TQPoint localPos = mapFromGlobal(TQCursor::pos());
        if (rect().contains(localPos)) {
            return;
        }
        m_hovered = false;
        update();
    }
    void paintEvent(TQPaintEvent *) {
        TQPainter p(this);
        const int rightReserve = kPopupRowMargin + kPopupMarkSize + kPopupRowSpacing;
        paintPopupRowContent(p, rect(), m_icon, m_text, m_bg,
                             m_hovered, isEnabled(), rightReserve, m_checked);
        if (m_checked) {
            paintCheckMark(p, rect(), isEnabled());
        }
    }
    void mousePressEvent(TQMouseEvent *) {
        if (!isEnabled()) {
            return;
        }
        m_checked = !m_checked;
        update();
        if (m_popup && m_slot) {
            (m_popup->*m_slot)(m_checked);
        }
    }
public:
    void clearHoverState() {
        if (m_hovered) {
            m_hovered = false;
            update();
        }
    }

private:
    TQPixmap m_icon;
    TQString m_text;
    TQColor m_bg;
    bool m_checked;
    bool m_hovered;
    YabatmanPopup *m_popup;
    ToggleSlot m_slot;
};

class PopupStatusRow : public TQWidget {
public:
    PopupStatusRow(const TQColor &bg, TQWidget *parent)
        : TQWidget(parent), m_bg(bg)
    {
        setFixedHeight(kPopupRowHeight);
        setBackgroundMode(TQt::NoBackground);
    }

    void setContent(const TQPixmap &icon, const TQString &text) {
        m_icon = icon;
        m_text = text;
        update();
    }

    void setBackgroundColor(const TQColor &bg) {
        m_bg = bg;
        update();
    }

protected:
    void paintEvent(TQPaintEvent *) {
        TQPainter p(this);
        p.fillRect(rect(), m_bg);

        int x = kPopupRowMargin;
        if (!m_icon.isNull()) {
            const int iy = (rect().height() - m_icon.height()) / 2;
            p.drawPixmap(x, iy, m_icon);
            x += m_icon.width() + kPopupRowSpacing;
        }

        p.setPen(TQColor(0, 0, 0));
        p.setFont(TQFont("Sans", 10, TQFont::Bold));
        const TQFontMetrics fm = p.fontMetrics();
        const int textY = (height() + fm.ascent() - fm.descent()) / 2;
        const int textW = width() - x - kPopupRowMargin;
        if (textW > 0) {
            p.drawText(x, textY, m_text);
        }
    }

private:
    TQColor m_bg;
    TQPixmap m_icon;
    TQString m_text;
};



static void readLiveBatteryState(const TQString &batPath, int &pct, int &chg) {
    if (batPath.isEmpty()) {
        return;
    }
    pct = readSysfsInt(batPath + "/capacity");
    const TQString status = readSysfsString(batPath + "/status");
    if (status == "Charging") {
        chg = 1;
    } else if (status == "Full" || status == "Not charging") {
        chg = 2;
    } else {
        chg = 0;
    }
}

static TQString buildBatteryStatusText(int pct, int chg, const TQString &batPath) {
    TQString status;
    if (pct == 100 && (chg == 1 || chg == 2)) {
        status = "100%, Full";
    } else {
        status.sprintf("%d%%, ", pct);
        if (chg == 2) {
            status += "Full";
        } else if (chg == 1) {
            status += "Charging";
        } else {
            status += "Discharging";
        }
    }

    if (!batPath.isEmpty() && chg != 2) {
        int full = readSysfsInt(batPath + "/energy_full");
        if (full == 0) full = readSysfsInt(batPath + "/charge_full");
        int now = readSysfsInt(batPath + "/energy_now");
        if (now == 0) now = readSysfsInt(batPath + "/charge_now");
        int pwr = readSysfsInt(batPath + "/power_now");
        if (pwr == 0) pwr = readSysfsInt(batPath + "/current_now");

        if (pwr > 0) {
            int remain = (chg == 1) ? (full - now) : now;
            if (remain > 0) {
                int hours = remain / pwr;
                int totalMinutes = (remain * 60) / pwr;
                int mins = totalMinutes % 60;
                char timeBuf[16];
                snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", hours, mins);
                if (chg == 1) {
                    status += TQString(" - ") + timeBuf;
                } else {
                    status += TQString(" - ") + timeBuf + " remaining.";
                }
            }
        }
    }
    return status;
}

static TQImage superimposePngFromMem(const unsigned char *overlayData, size_t overlaySize,
                                      const unsigned char *baseData, size_t baseSize) {
    TQImage base = loadEmbeddedPng(baseData, baseSize);
    TQImage overlay = loadEmbeddedPng(overlayData, overlaySize);
    if (base.isNull()) {
        return TQImage();
    }
    if (overlay.isNull()) {
        return base;
    }

    TQImage out = base.copy();
    out.setAlphaBuffer(true);

    const int w = out.width() < overlay.width() ? out.width() : overlay.width();
    const int h = out.height() < overlay.height() ? out.height() : overlay.height();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const TQRgb overPx = overlay.pixel(x, y);
            if (tqAlpha(overPx) != 0) {
                out.setPixel(x, y, overPx);
            }
        }
    }
    return out;
}

static TQPixmap scalePopupStatusIcon(const TQImage &img) {
    if (img.isNull()) {
        return TQPixmap();
    }

    TQImage scaled = img;
    if (img.width() != 24 || img.height() != 24) {
        scaled = img.smoothScale(24, 24);
    }
    if (scaled.isNull()) {
        return TQPixmap();
    }
    if (scaled.depth() != 32) {
        scaled = scaled.convertDepth(32);
    }
    scaled.setAlphaBuffer(true);
    return TQPixmap(scaled);
}

static TQPixmap batteryPopupIcon(int pct, int chg, bool warnSimple, bool warnCrit) {
    (void)pct;
    static TQPixmap cachedCrit;
    static TQPixmap cachedWarnSimple;
    static TQPixmap cachedAc;
    static TQPixmap cachedNormal;

    if (chg == 0 && warnCrit) {
        if (cachedCrit.isNull()) {
            cachedCrit = scalePopupStatusIcon(loadEmbeddedPng(yabatman_crit_data, yabatman_crit_size));
        }
        return cachedCrit;
    } else if (chg == 0 && warnSimple) {
        if (cachedWarnSimple.isNull()) {
            cachedWarnSimple = scalePopupStatusIcon(superimposePngFromMem(warn_data, warn_size,
                                                                         yabatman_bat_data, yabatman_bat_size));
        }
        return cachedWarnSimple;
    } else if (chg == 1) {
        if (cachedAc.isNull()) {
            cachedAc = scalePopupStatusIcon(superimposePngFromMem(yabatman_ac_data, yabatman_ac_size,
                                                                 yabatman_bat_data, yabatman_bat_size));
        }
        return cachedAc;
    } else {
        if (cachedNormal.isNull()) {
            cachedNormal = scalePopupStatusIcon(loadEmbeddedPng(yabatman_bat_data, yabatman_bat_size));
        }
        return cachedNormal;
    }
}

// ==========================================
// YabatmanPopup Implementation
// ==========================================
YabatmanPopup::YabatmanPopup(InactivityManager *inactivity, CalibrationManager *calibration, YabatmanConfig *config, TQWidget *parent)
    : TQWidget(parent, "YabatmanPopup",
               WStyle_Customize | WStyle_NoBorder | WStyle_StaysOnTop | WType_Popup | WX11BypassWM)
{
    m_inactivity = inactivity;
    m_calibration = calibration;
    m_config = config;

    m_closeTimer = new TQTimer(this);
    m_outsideTicks = 0;
    connect(m_closeTimer, TQT_SIGNAL(timeout()), this, TQT_SLOT(onCloseTimerTimeout()));

    setFocusPolicy(StrongFocus);

    m_opacity = m_config->popup_opacity;
    m_bgColor = TQColor(m_config->tint_popup_r, m_config->tint_popup_g, m_config->tint_popup_b);
    setPaletteBackgroundColor(m_bgColor);
    setBackgroundMode(TQt::PaletteBackground);

    TQVBoxLayout *layout = new TQVBoxLayout(this, 5, 0);
    layout->setMargin(5);

    // 1. Battery status row (opaque painted row, like menu entries)
    m_statusRow = new PopupStatusRow(m_bgColor, this);
    m_statusRow->setMinimumWidth(kPopupInnerWidth);
    layout->addWidget(m_statusRow);
    connect(m_inactivity, TQT_SIGNAL(batteryStatusChanged(int, int)),
            this, TQT_SLOT(refreshPopupBatteryStatus(int, int)));
    connect(m_inactivity, TQT_SIGNAL(brightnessChanged(int)),
            this, TQT_SLOT(onBrightnessChanged(int)));
    connect(m_inactivity, TQT_SIGNAL(powerProfileChanged(int)),
            this, TQT_SLOT(onPowerProfileChanged(int)));

    addPopupSeparator(layout, this);
    layout->addSpacing(16); // Increased visual margin below separator, above performance slider

    // 2. Power profile slider with eco/perf icons
    TQHBoxLayout *profRow = new TQHBoxLayout(layout, 5);
    TQLabel *ecoLbl = new TQLabel(this);
    setIconLabelPixmap(ecoLbl, popupIconPixmap(eco_data, eco_size, 24));
    profRow->addWidget(ecoLbl);
    m_profSlider = new TQSlider(0, 2, 1, 1, TQt::Horizontal, this);
    m_profSlider->setMinimumWidth(kPopupInnerWidth - 60);
    connect(m_profSlider, TQT_SIGNAL(valueChanged(int)), this, TQT_SLOT(onProfileChanged(int)));
    profRow->addWidget(m_profSlider, 1);
    TQLabel *perfLbl = new TQLabel(this);
    setIconLabelPixmap(perfLbl, popupIconPixmap(perf_data, perf_size, 24));
    profRow->addWidget(perfLbl);

    layout->addSpacing(kProfSliderBottomSpacing);

    // 3. Backlight slider block (shown when enabled in settings and hardware allows)
    m_backlightBlock = new TQWidget(this);
    m_backlightBlock->setMinimumWidth(kPopupInnerWidth);
    TQVBoxLayout *blBlockLayout = new TQVBoxLayout(m_backlightBlock, 0, 0);
    blBlockLayout->setMargin(0);
    TQHBoxLayout *blRow = new TQHBoxLayout(blBlockLayout, 5);
    TQLabel *sunLbl = new TQLabel(m_backlightBlock);
    setIconLabelPixmap(sunLbl, popupIconPixmap(backlight_data, backlight_size, 24));
    blRow->addWidget(sunLbl);
    m_blSlider = new ClickJumpSlider(1, 100, 1, 100, TQt::Horizontal, m_backlightBlock);
    m_blSlider->setMinimumWidth(kPopupInnerWidth - 36);
    m_blSlider->setValue(inactivity->getBrightness());
    connect(m_blSlider, TQT_SIGNAL(valueChanged(int)), this, TQT_SLOT(onBacklightChanged(int)));
    blRow->addWidget(m_blSlider, 1);
    blBlockLayout->addSpacing(12);
    layout->addWidget(m_backlightBlock);
    updateBacklightBlockVisibility();

    // 4. Presentation mode row
    m_presRow = new PopupCheckRow(popupIconPixmap(presmode_data, presmode_size, 24),
                                  "Presentation mode", m_bgColor,
                                  inactivity->isPresentationMode(),
                                  this, &YabatmanPopup::onPresentationToggled, this);
    m_presRow->setMinimumWidth(kPopupInnerWidth);
    layout->addWidget(m_presRow);
    connect(inactivity, TQT_SIGNAL(presentationModeChanged(bool)), this, TQT_SLOT(onPresentationModeChanged(bool)));
    connect(inactivity, TQT_SIGNAL(mediaPlayingChanged(bool)), this, TQT_SLOT(onMediaPlayingChanged(bool)));
    m_presRow->setEnabled(!inactivity->isMediaPlaying());

    // 5. Powernap row
    m_powernapRow = new PopupCheckRow(popupIconPixmap(powernap_data, powernap_size, 24),
                                      "Powernap", m_bgColor, inactivity->isPowernapSelected(),
                                      this, &YabatmanPopup::onPowernapToggled, this);
    m_powernapRow->setMinimumWidth(kPopupInnerWidth);
    layout->addWidget(m_powernapRow);
    m_powernapRow->setEnabled(inactivity->getChargingState() != 0);
    TQToolTip::add(m_powernapRow, "Powernap is available when the charger is connected.");

    // 6. Menu rows (original + extra items)
    PopupMenuRow *infoRow = new PopupMenuRow(popupIconPixmap(info_data, info_size, 24), "Battery infos",
                                       m_bgColor, this, &YabatmanPopup::openInfo, this);
    infoRow->setMinimumWidth(kPopupInnerWidth);
    layout->addWidget(infoRow);
    PopupMenuRow *histRow = new PopupMenuRow(popupIconPixmap(history_data, history_size, 24), "History",
                                       m_bgColor, this, &YabatmanPopup::openHistory, this);
    histRow->setMinimumWidth(kPopupInnerWidth);
    layout->addWidget(histRow);
    PopupMenuRow *cfgRow = new PopupMenuRow(popupIconPixmap(settings_data, settings_size, 24), "Settings...",
                                       m_bgColor, this, &YabatmanPopup::openConfig, this);
    cfgRow->setMinimumWidth(kPopupInnerWidth);
    layout->addWidget(cfgRow);
    m_menuRows.append(m_presRow);
    m_menuRows.append(m_powernapRow);
    m_menuRows.append(infoRow);
    m_menuRows.append(histRow);
    m_menuRows.append(cfgRow);

    applyPopupGeometry();
    refreshPopupBatteryStatus(m_inactivity->getBatteryPercentage(),
                              m_inactivity->getChargingState());
}

YabatmanPopup::~YabatmanPopup() {}

void YabatmanPopup::applyPopupGeometry() {
    // Do not use adjustSize() here: in TQt3 it resizes to sizeHint() and ignores kPopupWidth.
    TQSize hint = sizeHint();
    int h = hint.height();
    if (h <= 0) {
        h = minimumSizeHint().height();
    }
    if (h <= 0) {
        h = 400;
    }
    setMinimumWidth(kPopupWidth);
    setMaximumWidth(kPopupWidth);
    resize(kPopupWidth, h);
}

void YabatmanPopup::showNear(int x, int y) {
    applyPopupGeometry();
    // Position popup cleanly relative to cursor click
    int px = x - width() / 2;
    int py = y - height() - 10;
    if (py < 0) py = y + 10; // below cursor if no space at top
    if (px < 10) px = 10;
    if (px + width() > tqApp->desktop()->width() - 10) {
        px = tqApp->desktop()->width() - width() - 10;
    }

    move(px, py);
    show();
    raise();
    setFocus();
}

void YabatmanPopup::showEvent(TQShowEvent *e) {
    TQWidget::showEvent(e);
    m_outsideTicks = 0;

    // Reset hover states of all row widgets to avoid persistent highlights
    for (TQValueList<PopupRow*>::Iterator it = m_menuRows.begin(); it != m_menuRows.end(); ++it) {
        (*it)->clearHoverState();
    }

    m_closeTimer->start(500);

#if defined(Q_WS_X11)
    Display *dpy = tqt_xdisplay();
    Window wid = winId();
    unsigned long opacity = (unsigned long)(m_opacity * 0xffffffffUL);
    Atom opacityAtom = XInternAtom(dpy, "_NET_WM_WINDOW_OPACITY", False);
    XChangeProperty(dpy, wid, opacityAtom, XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&opacity, 1);

    Atom netWmState = XInternAtom(dpy, "_NET_WM_STATE", False);
    Atom skipTaskbar = XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", False);
    Atom skipPager = XInternAtom(dpy, "_NET_WM_STATE_SKIP_PAGER", False);
    Atom states[2] = { skipTaskbar, skipPager };
    XChangeProperty(dpy, wid, netWmState, XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)states, 2);
#endif

    refreshPopupBatteryStatus(m_inactivity->getBatteryPercentage(),
                              m_inactivity->getChargingState());

    if (m_blSlider) {
        m_blSlider->blockSignals(true);
        m_blSlider->setValue(m_inactivity->getBrightness());
        m_blSlider->blockSignals(false);
    }
    if (m_profSlider) {
        m_profSlider->blockSignals(true);
        int prof = m_inactivity->getPowerProfile();
        if (prof == 3) prof = 0;
        m_profSlider->setValue(prof);
        m_profSlider->blockSignals(false);
    }
}

void YabatmanPopup::refreshPopupBatteryStatus(int pct, int chg) {
    if (!m_statusRow) {
        return;
    }

    const TQString batPath = m_inactivity->getBatteryPath();
    readLiveBatteryState(batPath, pct, chg);

    const bool warnSimple = (chg == 0 && pct <= m_config->warn_level);
    const bool warnCrit = (chg == 0 && pct <= m_config->critical_level);

    m_statusRow->setContent(batteryPopupIcon(pct, chg, warnSimple, warnCrit),
                            buildBatteryStatusText(pct, chg, batPath));

    if (m_powernapRow) {
        const bool onAc = (chg != 0);
        m_powernapRow->setEnabled(onAc);
        if (!onAc) {
            m_powernapRow->setChecked(false);
            m_inactivity->setPowernapSelected(false);
        } else {
            m_powernapRow->setChecked(m_inactivity->isPowernapSelected());
        }
    }
}

void YabatmanPopup::hideEvent(TQHideEvent *e) {
    m_closeTimer->stop();
    TQWidget::hideEvent(e);
    emit popupHidden();
}

void YabatmanPopup::focusOutEvent(TQFocusEvent *e) {
    // Keep popup open while interacting with sliders inside the popup.
    TQWidget *fw = tqApp->focusWidget();
    while (fw) {
        if (fw == this) {
            TQWidget::focusOutEvent(e);
            return;
        }
        // Tray click toggles the popup; don't close here before the handler runs.
        if (fw->inherits("KSystemTray") || fw->inherits("YabatmanTrayIcon")) {
            TQWidget::focusOutEvent(e);
            return;
        }
        fw = fw->parentWidget();
    }
    m_closeTimer->stop();
    hide();
}

void YabatmanPopup::onCloseTimerTimeout() {
    TQPoint localPos = mapFromGlobal(TQCursor::pos());
    bool inside = rect().contains(localPos);

    if (!inside) {
        m_outsideTicks++;
        if (m_outsideTicks >= 6) {
            m_closeTimer->stop();
            hide();
        }
    } else {
        m_outsideTicks = 0;
    }
}

void YabatmanPopup::paintEvent(TQPaintEvent *e) {
    TQPainter p(this);
    p.fillRect(rect(), m_bgColor);
    (void)e;
}



void YabatmanPopup::onBacklightChanged(int val) {
    m_inactivity->setBrightness(val);
}

void YabatmanPopup::onProfileChanged(int val) {
    m_inactivity->setProfile(val);
}

void YabatmanPopup::onBrightnessChanged(int percent) {
    if (m_blSlider) {
        m_blSlider->blockSignals(true);
        m_blSlider->setValue(percent);
        m_blSlider->blockSignals(false);
    }
}

void YabatmanPopup::onPowerProfileChanged(int profile) {
    if (m_profSlider) {
        m_profSlider->blockSignals(true);
        int val = profile;
        if (val == 3) val = 0;
        m_profSlider->setValue(val);
        m_profSlider->blockSignals(false);
    }
}

void YabatmanPopup::onPresentationToggled(bool checked) {
    if (m_inactivity->isPresentationMode() == checked) {
        return;
    }
    m_inactivity->setPresentationMode(checked);
}

void YabatmanPopup::onPresentationModeChanged(bool active) {
    if (!m_presRow || m_inactivity->isMediaPlaying()) {
        return;
    }
    m_presRow->setChecked(active);
}

void YabatmanPopup::onMediaPlayingChanged(bool playing) {
    if (!m_presRow) {
        return;
    }
    m_presRow->setChecked(m_inactivity->isPresentationMode());
    m_presRow->setEnabled(!playing);
}

void YabatmanPopup::quitYaBatman() {
    close();
    tqApp->quit();
}

void YabatmanPopup::onPowernapToggled(bool checked) {
    m_inactivity->setPowernapSelected(checked);
}

void YabatmanPopup::openInfo() {
    close();
    BatteryInfoDialog *dlg = new BatteryInfoDialog(m_inactivity, m_calibration, NULL);
    dlg->exec();
    delete dlg;
}

void YabatmanPopup::openHistory() {
    close();
    BatteryHistoryDialog *dlg = new BatteryHistoryDialog(m_inactivity->getBatteryLogger(), NULL);
    dlg->exec();
    delete dlg;
}

void YabatmanPopup::updateBacklightBlockVisibility() {
    if (!m_backlightBlock) {
        return;
    }

    const bool show = m_config->backlight_slider == 1 && m_inactivity->hasControllableBacklight();
    if (show) {
        m_backlightBlock->show();
        if (m_blSlider) {
            m_blSlider->setValue(m_inactivity->getBrightness());
        }
    } else {
        m_backlightBlock->hide();
    }
    applyPopupGeometry();
}

void YabatmanPopup::applyConfigSettings() {
    m_opacity = m_config->popup_opacity;
    m_bgColor = TQColor(m_config->tint_popup_r, m_config->tint_popup_g, m_config->tint_popup_b);
    setPaletteBackgroundColor(m_bgColor);

    if (m_statusRow) {
        m_statusRow->setBackgroundColor(m_bgColor);
    }

    updateBacklightBlockVisibility();
    m_inactivity->updateTimeouts();
    applyPopupGeometry();
    emit configApplied();
}

void YabatmanPopup::openConfig() {
    close();
    ConfigDialog *dlg = new ConfigDialog(m_inactivity->getConfigManager(), m_config, m_inactivity->hasControllableBacklight(), NULL);
    const int result = dlg->exec();
    delete dlg;
    if (result == TQDialog::Accepted) {
        applyConfigSettings();
    }
}

void YabatmanPopup::openAbout() {
    close();
    TQMessageBox::about(NULL, "About YaBatman",
                       "<b>YaBatman Power & Battery Manager</b><br>"
                       "A premium Trinity Desktop Environment native component.<br><br>"
                       "Developed in C++/TQt3 for high visual quality, reliability, and performance.");
}

// ==========================================
// YabatmanTrayIcon Implementation
// ==========================================
YabatmanTrayIcon::YabatmanTrayIcon(InactivityManager *inactivity, CalibrationManager *calibration, YabatmanConfig *config, TQWidget *parent)
    : KSystemTray(parent, "YabatmanTrayIcon")
{
    m_inactivity = inactivity;
    m_calibration = calibration;
    m_config = config;
    m_popup = NULL;
    m_popupOpen = false;

    m_blinkTimer = new TQTimer(this);
    connect(m_blinkTimer, TQT_SIGNAL(timeout()), this, TQT_SLOT(onBlinkTimeout()));
    m_blinkState = false;

    m_chargeAnimTimer = new TQTimer(this);
    connect(m_chargeAnimTimer, TQT_SIGNAL(timeout()), this, TQT_SLOT(onChargeAnimTimeout()));
    m_chargeAnimLevel = -1;
    m_chargeAnimDirection = 1;

    setScaledContents(true);

    connect(inactivity, TQT_SIGNAL(batteryStatusChanged(int, int)), this, TQT_SLOT(updateIcon()));
    connect(inactivity, TQT_SIGNAL(presentationModeChanged(bool)), this, TQT_SLOT(updateIcon()));
    connect(inactivity, TQT_SIGNAL(mediaPlayingChanged(bool)), this, TQT_SLOT(updateIcon()));
    updateIcon();
}

YabatmanTrayIcon::~YabatmanTrayIcon() {
    if (m_popup) delete m_popup;
}

void YabatmanTrayIcon::mousePressEvent(TQMouseEvent *e) {
    if (e->button() == TQt::LeftButton || e->button() == TQt::RightButton) {
        onTrayClicked();
    }
}

void YabatmanTrayIcon::mouseReleaseEvent(TQMouseEvent *e) {
    (void)e;
}

void YabatmanTrayIcon::onTrayClicked() {
    if (m_popupOpen && m_popup) {
        m_popup->hide();
        return;
    }

    if (!m_popup) {
        m_popup = new YabatmanPopup(m_inactivity, m_calibration, m_config);
        connect(m_popup, TQT_SIGNAL(popupHidden()), this, TQT_SLOT(onPopupHidden()));
        connect(m_popup, TQT_SIGNAL(configApplied()), this, TQT_SLOT(updateIcon()));
    }

    TQPoint pt = TQCursor::pos();
    m_popup->showNear(pt.x(), pt.y());
    m_popupOpen = true;
}

void YabatmanTrayIcon::onPopupHidden() {
    m_popupOpen = false;
}

void YabatmanTrayIcon::onBlinkTimeout() {
    m_blinkState = !m_blinkState;
    if (m_blinkState) {
        setPixmap(TQPixmap());
    } else {
        updateIcon();
    }
}

void YabatmanTrayIcon::onChargeAnimTimeout() {
    int percentage = m_inactivity->getBatteryPercentage();
    int charging = m_inactivity->getChargingState();

    if (charging != 1 || !m_config->animate_charge_icon) {
        m_chargeAnimTimer->stop();
        updateIcon();
        return;
    }

    int currentLevel = percentage / 10;
    if (currentLevel < 0) currentLevel = 0;
    if (currentLevel > 10) currentLevel = 10;

    m_chargeAnimLevel += m_chargeAnimDirection;

    if (m_chargeAnimLevel >= 10) {
        m_chargeAnimLevel = 10;
        m_chargeAnimDirection = -1;
    } else if (m_chargeAnimLevel <= currentLevel) {
        m_chargeAnimLevel = currentLevel;
        m_chargeAnimDirection = 1;
    }

    updateIcon();
}

static void getBatteryIconData(int percentage, int charging, const unsigned char* &data, size_t &size) {
    int level = percentage / 10;
    if (level < 0) level = 0;
    if (level > 10) level = 10;

    if (charging == 2 || (charging == 1 && level == 10)) {
        data = battery_level_100_charged_symbolic_data;
        size = battery_level_100_charged_symbolic_size;
        return;
    }

    if (charging == 1) {
        switch (level) {
            case 0: data = battery_level_0_charging_symbolic_data; size = battery_level_0_charging_symbolic_size; break;
            case 1: data = battery_level_10_charging_symbolic_data; size = battery_level_10_charging_symbolic_size; break;
            case 2: data = battery_level_20_charging_symbolic_data; size = battery_level_20_charging_symbolic_size; break;
            case 3: data = battery_level_30_charging_symbolic_data; size = battery_level_30_charging_symbolic_size; break;
            case 4: data = battery_level_40_charging_symbolic_data; size = battery_level_40_charging_symbolic_size; break;
            case 5: data = battery_level_50_charging_symbolic_data; size = battery_level_50_charging_symbolic_size; break;
            case 6: data = battery_level_60_charging_symbolic_data; size = battery_level_60_charging_symbolic_size; break;
            case 7: data = battery_level_70_charging_symbolic_data; size = battery_level_70_charging_symbolic_size; break;
            case 8: data = battery_level_80_charging_symbolic_data; size = battery_level_80_charging_symbolic_size; break;
            case 9: data = battery_level_90_charging_symbolic_data; size = battery_level_90_charging_symbolic_size; break;
            default: data = battery_level_100_charged_symbolic_data; size = battery_level_100_charged_symbolic_size; break;
        }
    } else {
        switch (level) {
            case 0: data = battery_level_0_symbolic_data; size = battery_level_0_symbolic_size; break;
            case 1: data = battery_level_10_symbolic_data; size = battery_level_10_symbolic_size; break;
            case 2: data = battery_level_20_symbolic_data; size = battery_level_20_symbolic_size; break;
            case 3: data = battery_level_30_symbolic_data; size = battery_level_30_symbolic_size; break;
            case 4: data = battery_level_40_symbolic_data; size = battery_level_40_symbolic_size; break;
            case 5: data = battery_level_50_symbolic_data; size = battery_level_50_symbolic_size; break;
            case 6: data = battery_level_60_symbolic_data; size = battery_level_60_symbolic_size; break;
            case 7: data = battery_level_70_symbolic_data; size = battery_level_70_symbolic_size; break;
            case 8: data = battery_level_80_symbolic_data; size = battery_level_80_symbolic_size; break;
            case 9: data = battery_level_90_symbolic_data; size = battery_level_90_symbolic_size; break;
            default: data = battery_level_100_symbolic_data; size = battery_level_100_symbolic_size; break;
        }
    }
}

static void tintImage(TQImage &img, const TQColor &color) {
    int w = img.width();
    int h = img.height();
    unsigned int rgb = color.rgb();
    unsigned int r = tqRed(rgb);
    unsigned int g = tqGreen(rgb);
    unsigned int b = tqBlue(rgb);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            TQRgb pixel = img.pixel(x, y);
            int a = tqAlpha(pixel);
            if (a > 0) {
                if (tqRed(pixel) == 0 && tqGreen(pixel) == 0 && tqBlue(pixel) == 0) {
                    img.setPixel(x, y, tqRgba(r, g, b, a));
                }
            }
        }
    }
}

static TQImage compositeImages(const TQImage &bg, const TQImage &overlay) {
    TQImage out = bg.copy();
    out.setAlphaBuffer(true);

    int w = out.width();
    int h = out.height();
    int ow = overlay.width();
    int oh = overlay.height();

    for (int y = 0; y < h && y < oh; ++y) {
        for (int x = 0; x < w && x < ow; ++x) {
            TQRgb overPx = overlay.pixel(x, y);
            int aOver = tqAlpha(overPx);
            if (aOver == 0) continue;

            if (aOver == 255) {
                out.setPixel(x, y, overPx);
                continue;
            }

            TQRgb underPx = out.pixel(x, y);
            int aUnder = tqAlpha(underPx);
            int aOut = aOver + (aUnder * (255 - aOver)) / 255;
            if (aOut == 0) continue;

            int r = (tqRed(overPx) * aOver + tqRed(underPx) * aUnder * (255 - aOver) / 255) / aOut;
            int g = (tqGreen(overPx) * aOver + tqGreen(underPx) * aUnder * (255 - aOver) / 255) / aOut;
            int b = (tqBlue(overPx) * aOver + tqBlue(underPx) * aUnder * (255 - aOver) / 255) / aOut;
            out.setPixel(x, y, tqRgba(r, g, b, aOut));
        }
    }
    return out;
}

void YabatmanTrayIcon::updateIcon() {
    int percentage = m_inactivity->getBatteryPercentage();
    int charging = m_inactivity->getChargingState();

    // Handle charging animation state
    bool isAnimating = (charging == 1 && m_config->animate_charge_icon);
    if (isAnimating) {
        if (!m_chargeAnimTimer->isActive()) {
            int currentLevel = percentage / 10;
            if (currentLevel < 0) currentLevel = 0;
            if (currentLevel > 10) currentLevel = 10;
            m_chargeAnimLevel = currentLevel;
            m_chargeAnimDirection = 1;
            m_chargeAnimTimer->start(500);
        }
    } else {
        m_chargeAnimTimer->stop();
    }

    int activePercentage = percentage;
    if (isAnimating && m_chargeAnimLevel >= 0) {
        activePercentage = m_chargeAnimLevel * 10;
    }

    // Blink on critical alert level if configured
    if (percentage <= m_config->critical_level && m_config->icon_blink_on_critical && charging == 0) {
        if (!m_blinkTimer->isActive()) m_blinkTimer->start(500); // blink every 500ms
    } else {
        m_blinkTimer->stop();
    }

    // Determine color if custom tinting is enabled
    TQColor col;
    if (m_config->coloured_icon) {
        bool isFull = (charging == 2 || percentage >= 99);
        if (percentage <= m_config->critical_level) {
            if (m_config->custom_color_critical) {
                col = TQColor(m_config->tint_icon_critical_r, m_config->tint_icon_critical_g, m_config->tint_icon_critical_b);
            } else {
                col = TQColor(245, 45, 45); // default red
            }
        } else if (percentage <= m_config->warn_level) {
            if (m_config->custom_color_low) {
                col = TQColor(m_config->tint_icon_warning_r, m_config->tint_icon_warning_g, m_config->tint_icon_warning_b);
            } else {
                col = TQColor(205, 100, 0); // default orange/brown
            }
        } else if (isFull) {
            if (m_config->custom_color_full) {
                col = TQColor(m_config->tint_icon_full_r, m_config->tint_icon_full_g, m_config->tint_icon_full_b);
            } else {
                col = TQColor(40, 200, 40); // default green
            }
        } else {
            if (m_config->custom_color_normal) {
                col = TQColor(m_config->tint_icon_normal_r, m_config->tint_icon_normal_g, m_config->tint_icon_normal_b);
            } else {
                col = TQColor(15, 15, 245); // default blue
            }
        }
    }

    bool mediaPlaying = m_inactivity->isMediaPlaying();
    bool presentationMode = m_inactivity->isPresentationMode();
    bool colouredIcon = m_config->coloured_icon;
    bool mediaModeIcon = m_config->media_mode_icon;
    bool presentationModeIcon = m_config->presentation_mode_icon;

    static int lastActivePercentage = -1;
    static int lastCharging = -1;
    static bool lastIsAnimating = false;
    static bool lastMediaPlaying = false;
    static bool lastPresentationMode = false;
    static bool lastColouredIcon = false;
    static bool lastMediaModeIcon = false;
    static bool lastPresentationModeIcon = false;
    static TQColor lastColor;
    static TQPixmap cachedPixmap;

    if (activePercentage == lastActivePercentage &&
        charging == lastCharging &&
        isAnimating == lastIsAnimating &&
        mediaPlaying == lastMediaPlaying &&
        presentationMode == lastPresentationMode &&
        colouredIcon == lastColouredIcon &&
        mediaModeIcon == lastMediaModeIcon &&
        presentationModeIcon == lastPresentationModeIcon &&
        col == lastColor &&
        !cachedPixmap.isNull())
    {
        setPixmap(cachedPixmap);
        repaint(false);

        // Live tooltip text
        TQString tip;
        if (charging == 1) {
            tip.sprintf("Charging (%d%%)", percentage);
        } else if (charging == 2) {
            tip.sprintf("Full (%d%%)", percentage);
        } else {
            tip.sprintf("Discharging (%d%%)", percentage);
        }
        TQToolTip::add(this, tip);
        return;
    }

    // Save inputs
    lastActivePercentage = activePercentage;
    lastCharging = charging;
    lastIsAnimating = isAnimating;
    lastMediaPlaying = mediaPlaying;
    lastPresentationMode = presentationMode;
    lastColouredIcon = colouredIcon;
    lastMediaModeIcon = mediaModeIcon;
    lastPresentationModeIcon = presentationModeIcon;
    lastColor = col;

    // Load base icon
    const unsigned char *data = NULL;
    size_t size = 0;
    getBatteryIconData(activePercentage, charging, data, size);

    TQImage img;
    if (data && size > 0) {
        img = loadEmbeddedPng(data, size);
        if (!img.isNull() && colouredIcon) {
            tintImage(img, col);
        }
    }

    if (!img.isNull()) {
        // Media icon takes priority over presentation (original get_battery_icon logic).
        if (mediaPlaying && mediaModeIcon) {
            static TQImage mediaOverlay;
            if (mediaOverlay.isNull()) {
                mediaOverlay = loadEmbeddedPng(media_data, media_size);
            }
            if (!mediaOverlay.isNull()) {
                img = compositeImages(img, mediaOverlay);
            }
        } else if (presentationMode && presentationModeIcon) {
            static TQImage presOverlay;
            if (presOverlay.isNull()) {
                presOverlay = loadEmbeddedPng(pres_data, pres_size);
            }
            if (!presOverlay.isNull()) {
                img = compositeImages(img, presOverlay);
            }
        }
    }

    cachedPixmap = TQPixmap(img);
    setPixmap(cachedPixmap);
    repaint(false);

    // Live tooltip text
    TQString tip;
    if (charging == 1) {
        tip.sprintf("Charging (%d%%)", percentage);
    } else if (charging == 2) {
        tip.sprintf("Full (%d%%)", percentage);
    } else {
        tip.sprintf("Discharging (%d%%)", percentage);
    }
    TQToolTip::add(this, tip);
}
