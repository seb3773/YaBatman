#ifndef YABATMAN_GUI_H
#define YABATMAN_GUI_H

#include <tqwidget.h>
#include <tqslider.h>
#include <tqcheckbox.h>
#include <tqlabel.h>
#include <tqtimer.h>
#include <tqpushbutton.h>
#include <tqtooltip.h>
#include "inactivity_manager.h"
#include "calibration_manager.h"

#ifdef PURE_TQT3
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
#include <stdio.h>
#include <string.h>

class TQSimpleSystemTray : public TQLabel {
public:
    TQSimpleSystemTray(TQWidget *parent = 0, const char *name = 0)
        : TQLabel(parent, name, WStyle_Customize | WStyle_NoBorder | WStyle_StaysOnTop) {
        setScaledContents(true);
        resize(24, 24);
        dockInSystemTray();
    }

    void setCaption(const TQString &caption) {
        TQToolTip::add(this, caption);
    }

private:
    void dockInSystemTray() {
        Display *dpy = tqt_xdisplay();
        if (!dpy) return;

        int scrNum = DefaultScreen(dpy);
        char selectionName[32];
        snprintf(selectionName, sizeof(selectionName), "_NET_SYSTEM_TRAY_S%d", scrNum);
        Atom traySelection = XInternAtom(dpy, selectionName, False);
        Window trayWin = XGetSelectionOwner(dpy, traySelection);

        if (trayWin != 0) {
            Window myWin = winId();
            Atom opcodeAtom = XInternAtom(dpy, "_NET_SYSTEM_TRAY_OPCODE", False);
            XEvent ev;
            memset(&ev, 0, sizeof(ev));
            ev.xclient.type = ClientMessage;
            ev.xclient.window = trayWin;
            ev.xclient.message_type = opcodeAtom;
            ev.xclient.format = 32;
            ev.xclient.data.l[0] = CurrentTime;
            ev.xclient.data.l[1] = 0; // SYSTEM_TRAY_REQUEST_DOCK
            ev.xclient.data.l[2] = myWin;
            ev.xclient.data.l[3] = 0;
            ev.xclient.data.l[4] = 0;
            XSendEvent(dpy, trayWin, False, NoEventMask, &ev);
            XSync(dpy, False);
        }
    }
};

#define KSystemTray TQSimpleSystemTray
#else
#include <ksystemtray.h>
#endif

class PopupRow;
class PopupMenuRow;
class PopupCheckRow;
class PopupStatusRow;

class YabatmanPopup : public TQWidget {
    TQ_OBJECT
    friend class PopupRow;
    friend class PopupMenuRow;
    friend class PopupCheckRow;
public:
    YabatmanPopup(InactivityManager *inactivity, CalibrationManager *calibration, YabatmanConfig *config, TQWidget *parent = 0);
    ~YabatmanPopup();

    void showNear(int x, int y);

signals:
    void popupHidden();
    void configApplied();

protected:
    virtual void paintEvent(TQPaintEvent *e);
    virtual void focusOutEvent(TQFocusEvent *e);
    virtual void showEvent(TQShowEvent *e);
    virtual void hideEvent(TQHideEvent *e);

private slots:
    void onBacklightChanged(int val);
    void onProfileChanged(int val);
    void onPresentationToggled(bool checked);
    void onPowernapToggled(bool checked);
    void onPresentationModeChanged(bool active);
    void onMediaPlayingChanged(bool playing);
    void refreshPopupBatteryStatus(int percentage, int chargingState);
    void onBrightnessChanged(int percent);
    void onPowerProfileChanged(int profile);

    // Navigation buttons
    void openInfo();
    void openHistory();
    void openConfig();
    void openAbout();
    void quitYaBatman();
    void onCloseTimerTimeout();

private:
    void applyConfigSettings();
    void updateBacklightBlockVisibility();
    void applyPopupGeometry();

    InactivityManager *m_inactivity;
    CalibrationManager *m_calibration;
    YabatmanConfig *m_config;

    // UI elements
    PopupStatusRow *m_statusRow;
    TQWidget *m_backlightBlock;
    TQSlider *m_blSlider;
    TQSlider *m_profSlider;
    PopupCheckRow *m_presRow;
    PopupCheckRow *m_powernapRow;

    // Translucency state
    double m_opacity;
    TQColor m_bgColor;

    TQTimer *m_closeTimer;
    int m_outsideTicks;
    TQValueList<PopupRow*> m_menuRows;
};

class YabatmanTrayIcon : public KSystemTray {
    TQ_OBJECT
public:
    YabatmanTrayIcon(InactivityManager *inactivity, CalibrationManager *calibration, YabatmanConfig *config, TQWidget *parent = 0);
    ~YabatmanTrayIcon();

public slots:
    void updateIcon();
    void onTrayClicked();
    void onBlinkTimeout();
    void onChargeAnimTimeout();

protected:
    virtual void mousePressEvent(TQMouseEvent *e);
    virtual void mouseReleaseEvent(TQMouseEvent *e);

private slots:
    void onPopupHidden();

private:
    InactivityManager *m_inactivity;
    CalibrationManager *m_calibration;
    YabatmanConfig *m_config;
    YabatmanPopup *m_popup;
    bool m_popupOpen;

    TQTimer *m_blinkTimer;
    bool m_blinkState;

    TQTimer *m_chargeAnimTimer;
    int m_chargeAnimLevel;     // level shown in animation
    int m_chargeAnimDirection; // 1 = upwards, -1 = downwards
};

#endif // YABATMAN_GUI_H
