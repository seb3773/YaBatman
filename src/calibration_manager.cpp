#include "calibration_manager.h"
#include "battery_icons.h"
#include <tqapplication.h>
#include <tqpainter.h>
#include <tqimage.h>
#include <tqlayout.h>
#include <tqfont.h>
#include <tqmessagebox.h>
#include <tqdir.h>
#include <tqstringlist.h>
#include <tqdatetime.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

#define SOCK_PATH "/run/yabatmand/daemon.sock"

static TQPixmap getInvertedIcon(const unsigned char *data, size_t size) {
    TQImage img;
    if (!img.loadFromData(data, size, "PNG")) {
        return TQPixmap();
    }
    if (img.depth() != 32) {
        img = img.convertDepth(32);
    }
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            TQRgb px = img.pixel(x, y);
            int r = tqRed(px);
            int g = tqGreen(px);
            int b = tqBlue(px);
            int a = tqAlpha(px);
            if (a < 50 || (r > 240 && g > 240 && b > 240)) {
                img.setPixel(x, y, tqRgba(0, 0, 0, 0));
            } else {
                int grey = (r + g + b) / 3;
                int inv = 255 - grey;
                if (inv < 180) inv = 180;
                img.setPixel(x, y, tqRgba(inv, inv, inv, a));
            }
        }
    }
    TQPixmap pm;
    pm.convertFromImage(img);
    return pm;
}

// ==========================================
// CpuBurnerThread Implementation
// ==========================================
CpuBurnerThread::CpuBurnerThread() {
    m_stop = false;
}

void CpuBurnerThread::stop() {
    m_stop = true;
}

void CpuBurnerThread::run() {
    m_stop = false;
    volatile double x = 12.34;
    while (!m_stop) {
        x = x * 1.0000001;
    }
}

// ==========================================
// CalibrationOverlay Implementation
// ==========================================
CalibrationOverlay::CalibrationOverlay(TQWidget *parent)
    : TQWidget(parent, "CalibrationOverlay", WStyle_Customize | WStyle_NoBorder | WStyle_StaysOnTop)
{
    // Make fullscreen covering all monitors
    setGeometry(tqApp->desktop()->geometry());

    m_bgColor = TQColor(20, 20, 50); // dark blue
    m_chargerConnected = false;
    m_batteryPct = 0;

    setBackgroundMode(NoBackground);

    TQVBoxLayout *layout = new TQVBoxLayout(this, 30);
    layout->addStretch(1);

    TQHBoxLayout *btnLayout = new TQHBoxLayout(layout);
    btnLayout->addStretch();
    m_cancelBtn = new TQPushButton("Cancel Calibration", this);
    m_cancelBtn->setPaletteBackgroundColor(TQColor(100, 20, 20));
    m_cancelBtn->setPaletteForegroundColor(TQColor(255, 255, 255));
    connect(m_cancelBtn, TQT_SIGNAL(clicked()), this, TQT_SIGNAL(cancelClicked()));
    btnLayout->addWidget(m_cancelBtn);
    btnLayout->addStretch();

    layout->addSpacing(50);
}

CalibrationOverlay::~CalibrationOverlay() {}

void CalibrationOverlay::setStatus(const TQString& phase, const TQString& message, const TQColor& bg) {
    if (m_phaseText != phase || m_messageText != message || m_bgColor != bg) {
        m_phaseText = phase;
        m_messageText = message;
        m_bgColor = bg;
        update();
    }
}

void CalibrationOverlay::setChargerStatus(bool connected) {
    if (m_chargerConnected != connected) {
        m_chargerConnected = connected;
        update();
    }
}

void CalibrationOverlay::setBatteryPercentage(int pct) {
    if (m_batteryPct != pct) {
        m_batteryPct = pct;
        update();
    }
}

void CalibrationOverlay::paintEvent(TQPaintEvent *e) {
    int w = width();
    int h = height();
    if (w <= 0 || h <= 0) return;

    TQPixmap doubleBuffer(w, h);
    TQPainter p(&doubleBuffer);

    // Draw background
    p.fillRect(0, 0, w, h, m_bgColor);

    // Draw phase text centered in top third
    p.setPen(TQColor(255, 255, 255));
    p.setFont(TQFont("Sans", 22, TQFont::Bold));
    p.drawText(20, h / 3 - 30, w - 40, 60, AlignCenter, m_phaseText);

    // Draw status message centered in middle
    p.setPen(TQColor(240, 240, 240));
    p.setFont(TQFont("Sans", 14));
    p.drawText(20, h / 2 - 30, w - 40, 120, AlignCenter, m_messageText);

    // Draw inverted status icon in bottom left
    TQPixmap pm;
    if (m_chargerConnected) {
        static TQPixmap chargePm;
        if (chargePm.isNull()) {
            chargePm = getInvertedIcon(charge_data, charge_size);
        }
        pm = chargePm;
    } else {
        static TQPixmap unchargePm;
        if (unchargePm.isNull()) {
            unchargePm = getInvertedIcon(uncharge_data, uncharge_size);
        }
        pm = unchargePm;
    }

    if (!pm.isNull()) {
        int iconSize = 32;
        TQImage img = pm.convertToImage().smoothScale(iconSize, iconSize);
        TQPixmap scaledPm;
        scaledPm.convertFromImage(img);

        int x = 40;
        int y = h - iconSize - 40;
        p.drawPixmap(x, y, scaledPm);

        p.setPen(TQColor(240, 240, 240));
        p.setFont(TQFont("Sans", 14, TQFont::Bold));
        TQString text = m_chargerConnected ? "Charger connected" : "Charger disconnected";
        p.drawText(x + iconSize + 12, y + (iconSize + p.fontMetrics().ascent() - p.fontMetrics().descent()) / 2, text);
    }

    // Draw battery percentage in bottom right
    p.setPen(TQColor(240, 240, 240));
    p.setFont(TQFont("Sans", 14, TQFont::Bold));
    TQString pctText = TQString("Battery: %1%").arg(m_batteryPct);
    int textWidth = p.fontMetrics().width(pctText);
    int rx = w - 40 - textWidth;
    int ry = h - 32 - 40 + (32 + p.fontMetrics().ascent() - p.fontMetrics().descent()) / 2;
    p.drawText(rx, ry, pctText);

    p.end();

    // Paint double buffer on widget
    TQPainter widgetPainter(this);
    widgetPainter.drawPixmap(0, 0, doubleBuffer);
}

void CalibrationOverlay::keyPressEvent(TQKeyEvent *e) {
    // Consume key presses so the overlay is non-interactive
    if (e->key() == Key_Escape) {
        emit cancelClicked();
    }
}

// ==========================================
// CalibrationManager Implementation
// ==========================================
CalibrationManager::CalibrationManager(InactivityManager *inactivityManager, YabatmanConfig *config, TQObject *parent)
    : TQObject(parent)
{
    m_inactivity = inactivityManager;
    m_config = config;
    m_state = CAL_INACTIVE;
    m_overlay = NULL;
    m_originalBrightness = 100;
    m_startedCharging = false;
    m_waitingForUnplugTransition = false;

    connect(m_inactivity, TQT_SIGNAL(batteryStatusChanged(int, int)), this, TQT_SLOT(onBatteryStatusChanged(int, int)));
}

CalibrationManager::~CalibrationManager() {
    stopBurners();
    if (m_overlay) {
        delete m_overlay;
    }
}

void CalibrationManager::startCalibration() {
    if (m_state != CAL_INACTIVE) return;

    m_startedCharging = false;
    m_waitingForUnplugTransition = false;

    // Suspend idle monitoring and warning checks, keeping battery and udev active
    m_inactivity->suspendIdle();

    // Store original brightness
    m_originalBrightness = m_inactivity->getBrightness();

    // Create and show overlay
    m_overlay = new CalibrationOverlay();
    connect(m_overlay, TQT_SIGNAL(cancelClicked()), this, TQT_SLOT(onOverlayCancelled()));
    m_overlay->show();

    // Set initial status in overlay
    m_overlay->setChargerStatus(m_inactivity->getChargingState() != 0);
    m_overlay->setBatteryPercentage(m_inactivity->getBatteryPercentage());

    // Begin Phase 1: Charging
    transitionTo(CAL_PHASE1_CHARGING);
}

void CalibrationManager::cancelCalibration() {
    if (m_state == CAL_INACTIVE) return;

    stopBurners();

    if (m_overlay) {
        m_overlay->close();
        delete m_overlay;
        m_overlay = NULL;
    }

    m_state = CAL_INACTIVE;

    // Restore profile & brightness
    callDaemonSocket("set_low_profile:0");
    setBrightness(m_originalBrightness);

    // Resume idle checking
    m_inactivity->resumeIdle();

    TQMessageBox::information(NULL, "Calibration", "Calibration was cancelled by the user.");
}

void CalibrationManager::abortCalibration(const TQString& errorMessage) {
    if (m_state == CAL_INACTIVE) return;

    stopBurners();

    if (m_overlay) {
        m_overlay->close();
        delete m_overlay;
        m_overlay = NULL;
    }

    m_state = CAL_INACTIVE;

    // Restore profile & brightness
    callDaemonSocket("set_low_profile:0");
    setBrightness(m_originalBrightness);

    // Resume idle checking
    m_inactivity->resumeIdle();

    TQMessageBox::critical(NULL, "Calibration Error", errorMessage);
}

void CalibrationManager::onOverlayCancelled() {
    cancelCalibration();
}



void CalibrationManager::setBrightness(int val) {
    m_inactivity->setBrightness(val);
}

void CalibrationManager::startBurners() {
    stopBurners();
    // Start a burner thread for each core to consume battery
    int numCores = sysconf(_SC_NPROCESSORS_ONLN);
    if (numCores <= 0) numCores = 4;
    for (int i = 0; i < numCores; ++i) {
        CpuBurnerThread *burner = new CpuBurnerThread();
        m_burners.append(burner);
        burner->start();
    }
}

void CalibrationManager::stopBurners() {
    for (TQValueList<CpuBurnerThread*>::Iterator it = m_burners.begin(); it != m_burners.end(); ++it) {
        (*it)->stop();
        (*it)->wait();
        delete (*it);
    }
    m_burners.clear();
}

void CalibrationManager::transitionTo(CalibrationState state) {
    m_state = state;
    if (!m_overlay) return;

    m_overlay->setChargerStatus(m_inactivity->getChargingState() != 0);

    switch (m_state) {
        case CAL_PHASE1_CHARGING:
            m_startedCharging = false;
            setBrightness(100); // Keep screen bright for initial connect prompt
            m_overlay->setStatus("Calibration: Charging phase (1/2)",
                                 "Please connect charger to begin calibration.",
                                 TQColor(20, 20, 60)); // deep blue
            break;
        case CAL_WAITING_UNPLUG:
            setBrightness(100); // Keep screen bright for unplug prompt
            m_overlay->setStatus("Calibration: waiting...",
                                 "Battery is fully charged (100%).\nPlease UNPLUG charger to start Discharging phase.",
                                 TQColor(20, 60, 20)); // deep green
            break;
        case CAL_PHASE2_DISCHARGING:
            m_overlay->setStatus("Calibration: Discharging phase",
                                 "Charger unplugged. Discharging battery to 3%.\nDO NOT close this overlay.",
                                 TQColor(80, 40, 20)); // deep orange
            break;
        case CAL_WAITING_PLUG:
            setBrightness(100); // Keep screen bright for plug prompt
            m_overlay->setStatus("Calibration: waiting...",
                                 "Battery level is critical (3%).\nPlease PLUG charger now to complete calibration.",
                                 TQColor(20, 60, 20)); // deep green
            break;
        case CAL_PHASE3_CHARGING:
            setBrightness(100); // Keep screen bright during final charging
            m_overlay->setStatus("Calibration: Charging phase (2/2)",
                                 "Charger connected. Final charge to 100% in progress.",
                                 TQColor(20, 20, 60)); // deep blue
            break;
        case CAL_INACTIVE:
            break;
    }
}

void CalibrationManager::onBatteryStatusChanged(int percentage, int chargingState) {
    if (m_state == CAL_INACTIVE) return;

    if (m_overlay) {
        m_overlay->setChargerStatus(chargingState != 0);
        m_overlay->setBatteryPercentage(percentage);
    }

    switch (m_state) {
        case CAL_PHASE1_CHARGING:
            if (chargingState == 1 || chargingState == 2) {
                m_startedCharging = true;
                // Ensure ECO profile & keep backlight bright
                callDaemonSocket("set_low_profile:1");
                setBrightness(100); // Keep screen bright even when charging

                if (percentage == 100) {
                    if (!m_waitingForUnplugTransition) {
                        m_waitingForUnplugTransition = true;
                        m_overlay->setStatus("Calibration: Charging phase (1/2)",
                                             "Battery is fully charged (100%).\nStarting in 3 seconds...",
                                             TQColor(20, 20, 60));
                        TQTimer::singleShot(3000, this, TQT_SLOT(slotTransitionToWaitingUnplug()));
                    }
                } else {
                    m_overlay->setStatus("Calibration: Charging phase (1/2)",
                                         TQString("Charging in progress... Current: %1%").arg(percentage),
                                         TQColor(20, 20, 60));
                }
            } else {
                if (m_startedCharging) {
                    abortCalibration("Calibration aborted: Charger was unplugged during the first charging phase.");
                } else {
                    setBrightness(100); // Keep screen bright for connect prompt
                    m_overlay->setStatus("Calibration: Charging phase (1/2)",
                                         "Waiting charger connection. Please plug charger.",
                                         TQColor(20, 20, 60));
                }
            }
            break;

        case CAL_WAITING_UNPLUG:
            if (chargingState == 0) {
                transitionTo(CAL_PHASE2_DISCHARGING);
                startBurners();
                callDaemonSocket("set_perf_profile");
                setBrightness(m_inactivity->getBatteryPercentage()); // Full brightness
            } else {
                setBrightness(100); // Ensure bright
            }
            break;

        case CAL_PHASE2_DISCHARGING:
            if (chargingState == 1 || chargingState == 2) {
                abortCalibration("Calibration aborted: Charger was connected during the discharging phase.");
            } else {
                m_overlay->setStatus("Calibration: Discharging phase",
                                     TQString("Discharging in progress... Current: %1%").arg(percentage),
                                     TQColor(80, 40, 20));

                if (percentage <= 3) {
                    stopBurners();
                    callDaemonSocket("set_low_profile:1");
                    setBrightness(100); // Make bright to grab user's attention!
                    tqApp->beep();
                    transitionTo(CAL_WAITING_PLUG);
                }
            }
            break;

        case CAL_WAITING_PLUG:
            if (chargingState == 1 || chargingState == 2) {
                transitionTo(CAL_PHASE3_CHARGING);
            } else {
                setBrightness(100); // Keep screen bright for plug prompt
                m_overlay->setStatus("Calibration: waiting...",
                                     TQString("Battery level critical: %1%.\nPlease PLUG charger now!").arg(percentage),
                                     TQColor(20, 60, 20));

                // If drops to 2%, suspend to protect battery
                if (percentage <= 2) {
                    system("systemctl suspend");
                }
            }
            break;

        case CAL_PHASE3_CHARGING:
            if (chargingState == 0) {
                abortCalibration("Calibration aborted: Charger was unplugged during the final charging phase.");
            } else {
                m_overlay->setStatus("Calibration: Charging phase (2/2)",
                                     TQString("Final charge in progress... Current: %1%").arg(percentage),
                                     TQColor(20, 20, 60));

                if (percentage == 100) {
                    // Complete!
                    m_state = CAL_INACTIVE;
                    if (m_overlay) {
                        m_overlay->close();
                        delete m_overlay;
                        m_overlay = NULL;
                    }

                    callDaemonSocket("set_low_profile:0");
                    setBrightness(m_originalBrightness);
                    m_inactivity->resumeIdle();

                    // Save calibration date and update config file
                    m_config->last_calibration = TQDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm");
                    m_inactivity->getConfigManager()->save(*m_config);

                    tqApp->beep();
                    TQMessageBox::information(NULL, "Calibration Complete", "Battery calibration has been completed successfully!");
                }
            }
            break;

        case CAL_INACTIVE:
            break;
    }
}

void CalibrationManager::slotTransitionToWaitingUnplug() {
    if (m_state == CAL_PHASE1_CHARGING && m_waitingForUnplugTransition) {
        m_waitingForUnplugTransition = false;
        transitionTo(CAL_WAITING_UNPLUG);
        tqApp->beep();
    }
}
