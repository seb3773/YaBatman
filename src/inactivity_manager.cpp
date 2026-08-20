#include "inactivity_manager.h"
#include <tqapplication.h>
#include <tqthread.h>
#include <tqimage.h>
#include <tqpixmap.h>
#include <tqpainter.h>
#include <tqcursor.h>
#include <tqfile.h>
#include <tqtextstream.h>
#include <tqdir.h>
#include <tqdatetime.h>
#include <tqsocketnotifier.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/scrnsaver.h>
#include <X11/extensions/dpms.h>

#undef signals
#include <gio/gio.h>
#include <glib.h>
#include <libnotify/notify.h>
#define signals protected

typedef int (*QX11EventFilter)(XEvent*);
QX11EventFilter tqt_set_x11_event_filter(QX11EventFilter filter);

static QX11EventFilter prev_x11_filter = NULL;
static InactivityManager *g_inactivity_instance = NULL;

static int custom_x11_event_filter(XEvent *event) {
    if (g_inactivity_instance && g_inactivity_instance->x11EventFilter(event)) {
        return 1; // handled
    }
    if (prev_x11_filter) {
        return prev_x11_filter(event);
    }
    return 0; // not handled
}
#include "battery_icons.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

class WifiStatusOverlay : public TQWidget {
public:
    WifiStatusOverlay(TQWidget *parent = 0)
        : TQWidget(parent, "WifiStatusOverlay", WStyle_Customize | WStyle_NoBorder | WStyle_StaysOnTop)
    {
        setGeometry(tqApp->desktop()->geometry());
        setCursor(TQCursor(Qt::BlankCursor));
        setBackgroundColor(TQColor(0, 0, 0));
    }
protected:
    void showEvent(TQShowEvent *e) {
        TQWidget::showEvent(e);
        Display *dpy = tqt_xdisplay();
        Window wid = winId();
        unsigned long opacity = (unsigned long)(0.8 * 0xffffffffUL);
        Atom opacityAtom = XInternAtom(dpy, "_NET_WM_WINDOW_OPACITY", False);
        XChangeProperty(dpy, wid, opacityAtom, XA_CARDINAL, 32, PropModeReplace,
                        (unsigned char *)&opacity, 1);
    }

    void paintEvent(TQPaintEvent *) {
        TQPainter p(this);
        p.fillRect(rect(), TQColor(0, 0, 0)); // solid black background

        int x = 20;
        int y = 20;

        TQImage img;
        if (img.loadFromData(iswifi_data, iswifi_size, "PNG")) {
            TQPixmap pm;
            pm.convertFromImage(img.smoothScale(20, 20));
            p.drawPixmap(x, y, pm);
            x += pm.width() + 10;
        }

        p.setPen(TQColor(255, 255, 255));
        p.setFont(TQFont("Sans", 12, TQFont::Bold));
        p.drawText(x, y + 16, " Checking WiFi ssid...");
    }
};

#include <sys/socket.h>
#include <sys/un.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/wireless.h>

#include <libudev.h>

#include "mpris_detection.h"





static TQString joinList(const TQStringList &list) {
    TQString result = "";
    bool first = true;
    for (TQStringList::ConstIterator it = list.begin(); it != list.end(); ++it) {
        if (!first) result += ",";
        result += *it;
        first = false;
    }
    return result;
}

InactivityManager::InactivityManager(ConfigManager *configManager, YabatmanConfig *config, BatteryLogger *batteryLogger, TQObject *parent)
    : TQObject(parent)
{
    m_configManager = configManager;
    m_config = config;
    m_batteryLogger = batteryLogger;

    m_x11Display = tqt_xdisplay();
    m_prevIdleTime = 0;
    m_backlightReduced = false;
    m_screenSleeping = false;
    m_screensaverActive = false;
    m_originalBrightness = 100;
    m_originalBrightnessRaw = 0;
    m_backlightCurrentRaw = 0;
    m_backlightFd = -1;
    m_backlightMax = 100;
    m_brightnessStep = 1;
    m_nextBrightnessIdleCheck = 0;
    m_lastBatteryCheck = 0;
    m_transitionStartRaw = 0;
    m_transitionTargetRaw = 0;
    m_transitionStep = 0;
    m_transitionInProgress = false;

    m_batteryPercentage = -1;
    m_chargingState = -1;
    m_lastBattPercentage = -1;
    m_warnSimple = false;
    m_warnCrit = false;
    m_criticalLevelReached = 0;
    m_xLevel = 0;

    m_mediaPlaying = false;
    m_mediaPlayingType = 0; // 0 = video (matches original default)
    m_presentationMode = false;
    m_powernapEnabled = false;
    m_inPowernap = false;

    m_bluetoothInitialState = -1;
    m_wifiInitialState = -1;
    m_powerProfile = -1;
    m_currentRate = 0.0;
    m_phaseStartTime = time(NULL);
    m_phaseStartCapacity = 100;
    m_warned100Percent = false;
    m_justWokeUp = true;
    m_wakeTime = time(NULL);
    m_calibrationActive = false;

    m_lastLidState = true;
    m_inhibitFd = -1;
    m_inhibitSleepFd = -1;
    m_inhibitShutdownFd = -1;
    m_inhibitPowerKeyFd = -1;
    m_inhibitSuspendKeyFd = -1;
    m_inhibitHibernateFd = -1;
    m_minimalMode = false;
    m_wifiOverlay = NULL;
    m_useWifiOverlay = false;
    m_wifiCheckCounter = 0;
    m_dbusPrepareSleepSubId = 0;
    m_dbusPrepareShutdownSubId = 0;
    m_keycodes[0] = 0;
    m_keycodes[1] = 0;

    m_actionInProgress = false;
    m_udev = NULL;
    m_udevMon = NULL;
    m_udevNotifier = NULL;
    m_systemBus = NULL;
    m_xssInfo = XScreenSaverAllocInfo();
    m_batteryPath = "";

    // Find and cache battery sysfs path
    TQString batBase = "/sys/class/power_supply/";
    TQDir batDir(batBase);
    if (batDir.exists()) {
        TQStringList list = batDir.entryList(TQDir::Dirs);
        for (TQStringList::Iterator it = list.begin(); it != list.end(); ++it) {
            if (*it == "." || *it == "..") continue;
            TQString type = readSysfsString(batBase + *it + "/type");
            if (type == "Battery") {
                m_batteryPath = batBase + *it;
                break;
            }
        }
    }

    // Locate backlight sysfs
    TQString base = "/sys/class/backlight/";
    TQDir dir(base);
    if (dir.exists()) {
        TQStringList list = dir.entryList(TQDir::Dirs);
        if (list.count() > 0) {
            TQString path;
            for (TQStringList::Iterator it = list.begin(); it != list.end(); ++it) {
                if (*it == "." || *it == "..") continue;
                path = base + *it + "/brightness";
                m_backlightMax = readSysfsInt(base + *it + "/max_brightness");
                break;
            }
            if (!path.isEmpty()) {
                m_backlightFd = open(path.latin1(), O_RDWR);
                if (m_backlightMax <= 0) m_backlightMax = 100;
                m_brightnessStep = m_backlightMax / 100;
                if (m_brightnessStep <= 0) m_brightnessStep = 1;
            }
        }
    }

    // Setup timers
    m_idleTimer = new TQTimer(this);
    connect(m_idleTimer, TQT_SIGNAL(timeout()), this, TQT_SLOT(checkIdle()));

    m_mprisTimer = new TQTimer(this);
    connect(m_mprisTimer, TQT_SIGNAL(timeout()), this, TQT_SLOT(pollMpris()));

    m_transitionTimer = new TQTimer(this);
    connect(m_transitionTimer, TQT_SIGNAL(timeout()), this, TQT_SLOT(updateBrightnessTransition()));

    m_batteryTimer = new TQTimer(this);
    connect(m_batteryTimer, TQT_SIGNAL(timeout()), this, TQT_SLOT(refreshBatteryIcon()));

    m_udevRefreshTimer = new TQTimer(this);
    connect(m_udevRefreshTimer, TQT_SIGNAL(timeout()), this, TQT_SLOT(onUdevRefreshTimeout()));

    m_lidTimer = new TQTimer(this);
    connect(m_lidTimer, TQT_SIGNAL(timeout()), this, TQT_SLOT(checkLidState()));

    readCurrentBrightness();
    m_originalBrightnessRaw = m_backlightCurrentRaw;
    m_originalBrightness = getBrightness();

    m_lastLidState = readLidState();
    m_mainThread = TQThread::currentThread();
    m_sleepTransitionDone = false;

    m_glibTimer = new TQTimer(this);
    connect(m_glibTimer, TQT_SIGNAL(timeout()), this, TQT_SLOT(iterateGlib()));

    setupDbusMonitoring();
    setupInhibitors();
    notify_init("yabatman");
}

InactivityManager::~InactivityManager() {
    stop();
    releaseInhibitors();
    if (m_backlightFd >= 0) {
        close(m_backlightFd);
    }
    if (m_xssInfo) {
        XFree(m_xssInfo);
    }
    if (m_systemBus) {
        g_object_unref(m_systemBus);
    }
    notify_uninit();
}

void InactivityManager::start() {
    disableDpms();
    checkBatteryStatus(true);
    setupUdevMonitor();
    m_idleTimer->start(1000);
    m_mprisTimer->start(2000);
    m_batteryTimer->start(2000);
    m_lidTimer->start(2000);
    m_glibTimer->start(50);
    setupPowerSleepKeys();
    g_inactivity_instance = this;
    prev_x11_filter = tqt_set_x11_event_filter(custom_x11_event_filter);
    if (m_config->disable_eth == 1) {
        callDaemon("toggle_ethernet:0");
    }

    // Send advanced profile settings to daemon
    TQString freqCmd;
    freqCmd.sprintf("set_eco_freq_cap:%d", m_config->eco_freq_cap);
    callDaemon(freqCmd);
    TQString usbCmd;
    usbCmd.sprintf("set_balanced_usb_autosuspend:%d", m_config->balanced_usb_autosuspend ? 1 : 0);
    callDaemon(usbCmd);
    if (m_config->charge_limit_enabled) {
        TQString chargeCmd;
        chargeCmd.sprintf("set_charge_limit:%d", m_config->charge_limit_value);
        callDaemon(chargeCmd);
    }
}

void InactivityManager::suspendIdle() {
    m_calibrationActive = true;
    m_idleTimer->stop();
    m_mprisTimer->stop();
    m_transitionTimer->stop();
    m_lidTimer->stop();
}

void InactivityManager::resumeIdle() {
    m_calibrationActive = false;
    m_idleTimer->start(1000);
    m_mprisTimer->start(2000);
    m_batteryTimer->start(2000);
    m_lidTimer->start(2000);
    checkBatteryStatus(true);
}

void InactivityManager::stop() {
    tqt_set_x11_event_filter(prev_x11_filter);
    g_inactivity_instance = NULL;
    releasePowerSleepKeys();
    teardownUdevMonitor();
    if (m_udevRefreshTimer) {
        m_udevRefreshTimer->stop();
    }
    m_idleTimer->stop();
    m_mprisTimer->stop();
    m_batteryTimer->stop();
    m_transitionTimer->stop();
    m_lidTimer->stop();
    m_glibTimer->stop();

    releaseInhibitors();

    // Exit cleanups
    if (m_config->disable_eth == 1 || m_config->disable_eth == 2) {
        callDaemon("toggle_ethernet:1");
    }
    callDaemon("unfreeze_all_frozen_services");
    callDaemon("freeze_processes:0");
    callDaemon("set_webcam_power:1");
    if (m_bluetoothInitialState != -1) {
        setRfkillState("bluetooth", m_bluetoothInitialState);
    }
    if (m_wifiInitialState != -1) {
        setRfkillState("wifi", m_wifiInitialState);
    }
}

void InactivityManager::setupUdevMonitor() {
    teardownUdevMonitor();

    m_udev = udev_new();
    if (!m_udev) {
        return;
    }

    m_udevMon = udev_monitor_new_from_netlink(m_udev, "udev");
    if (!m_udevMon) {
        udev_unref(m_udev);
        m_udev = NULL;
        return;
    }

    udev_monitor_filter_add_match_subsystem_devtype(m_udevMon, "power_supply", NULL);
    if (udev_monitor_enable_receiving(m_udevMon) < 0) {
        udev_monitor_unref(m_udevMon);
        m_udevMon = NULL;
        udev_unref(m_udev);
        m_udev = NULL;
        return;
    }

    int fd = udev_monitor_get_fd(m_udevMon);
    if (fd < 0) {
        udev_monitor_unref(m_udevMon);
        m_udevMon = NULL;
        udev_unref(m_udev);
        m_udev = NULL;
        return;
    }

    m_udevNotifier = new TQSocketNotifier(fd, TQSocketNotifier::Read, this);
    connect(m_udevNotifier, TQT_SIGNAL(activated(int)), this, TQT_SLOT(onUdevSocket(int)));
}

void InactivityManager::teardownUdevMonitor() {
    if (m_udevNotifier) {
        delete m_udevNotifier;
        m_udevNotifier = NULL;
    }
    if (m_udevMon) {
        udev_monitor_unref(m_udevMon);
        m_udevMon = NULL;
    }
    if (m_udev) {
        udev_unref(m_udev);
        m_udev = NULL;
    }
}

void InactivityManager::onUdevSocket(int) {
    if (!m_udevMon) {
        return;
    }

    struct udev_device *dev = udev_monitor_receive_device(m_udevMon);
    if (!dev) {
        return;
    }

    const char *subsystem = udev_device_get_subsystem(dev);
    if (subsystem && strcmp(subsystem, "power_supply") == 0) {
        checkBatteryStatus(true);
        if (m_udevRefreshTimer) {
            m_udevRefreshTimer->stop();
            m_udevRefreshTimer->start(250, true);
        }
    }

    udev_device_unref(dev);
}

void InactivityManager::onUdevRefreshTimeout() {
    checkBatteryStatus(true);
}

void InactivityManager::updateTimeouts() {
    forceCheck();
}

void InactivityManager::setPresentationMode(bool enable) {
    if (m_presentationMode == enable) {
        return;
    }
    m_presentationMode = enable;
    emit presentationModeChanged(m_presentationMode);

    if (enable) {
        disableDpms();
        // Original: stop idle monitoring unless critical battery or audio-only media.
        if (!m_warnCrit && !m_mediaPlayingType) {
            m_idleTimer->stop();
            m_transitionTimer->stop();
        }
    } else {
        setScreenDpms(true);
        m_idleTimer->start(1000);
        forceCheck();
    }
}

void InactivityManager::forceCheck() {
    checkIdle();
}

void InactivityManager::onLidClosed(bool closed) {
    if (m_calibrationActive) return;

    if (closed) {
        emit blackoutScreensaver(true);
        tqApp->processEvents();

        // Stop screen dimming and idle timers to avoid background checks
        m_idleTimer->stop();
        m_mprisTimer->stop();
        m_batteryTimer->stop();

        // Check if we should enter Powernap mode
        if (m_chargingState > 0 && m_config->ac_lid_enable_powernap && m_powernapEnabled && !m_inPowernap) {
            m_inPowernap = true;
            if (!m_mediaPlaying) {
                system("canberra-gtk-play --id window-attention &");
            }
            if (m_config->ac_lid_powernap_mode_disable_bt) {
                if (m_bluetoothInitialState == -1) {
                    m_bluetoothInitialState = getRfkillState("bluetooth");
                }
                setRfkillState("bluetooth", 0);
            }
            if (m_config->ac_lid_powernap_mode_disable_wifi) {
                if (m_wifiInitialState == -1) {
                    m_wifiInitialState = getRfkillState("wifi");
                }
                setRfkillState("wifi", 0);
            }
            setProfile(0); // Low profile
            m_batteryTimer->start(2000);
            return;
        }

        // Check lid close actions
        int lid_action = (m_chargingState == 1 || m_chargingState == 2) ? m_config->ac_lid_action : m_config->bat_lid_action;
        if (lid_action == 4) { // Lock Screen
            lockScreen();
            m_idleTimer->start(1000);
            m_mprisTimer->start(2000);
            m_batteryTimer->start(2000);
        } else if (lid_action == 0) { // Sleep
            suspendSystem();
        } else if (lid_action == 1) { // Sleep then Hibernate
            suspendThenHibernate();
        } else if (lid_action == 2) { // Hibernate
            hibernateSystem();
        } else if (lid_action == 3) { // Hybrid Sleep
            hybridSuspendSystem();
        } else if (lid_action == 5) { // Do Nothing
            m_idleTimer->start(1000);
            m_mprisTimer->start(2000);
            m_batteryTimer->start(2000);
        }
    } else {
        // Lid opened
        onResume();
    }
}

void InactivityManager::onResume() {
    m_justWokeUp = true;
    m_wakeTime = time(NULL);

    if (m_inPowernap) {
        m_inPowernap = false;
        adjustProfile();
        // Restore radios
        if (m_wifiInitialState != -1) {
            setRfkillState("wifi", m_wifiInitialState);
        }
        if (m_bluetoothInitialState != -1) {
            setRfkillState("bluetooth", m_bluetoothInitialState);
        }
    }

    m_idleTimer->start(1000);
    m_mprisTimer->start(2000);
    m_batteryTimer->start(2000);
    m_prevIdleTime = 0;

    if (m_backlightReduced || m_screensaverActive || m_screenSleeping) {
        setBrightnessImmediate(m_originalBrightnessRaw, true);
    }

    m_backlightReduced = false;
    m_screenSleeping = false;
    m_screensaverActive = false;
    readCurrentBrightness();
    m_originalBrightnessRaw = m_backlightCurrentRaw;
    m_originalBrightness = getBrightness();
    setScreenDpms(true);
    forceCheck();
}

void InactivityManager::readCurrentBrightness() {
    m_backlightCurrentRaw = getBrightnessRaw();
}

int InactivityManager::getBrightnessRaw() const {
    if (m_backlightFd < 0) {
        return m_backlightMax;
    }
    char buf[16];
    lseek(m_backlightFd, 0, SEEK_SET);
    ssize_t n = ::read(m_backlightFd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        int raw = atoi(buf);
        if (raw < 1) raw = 1;
        if (raw > m_backlightMax) raw = m_backlightMax;
        return raw;
    }
    return m_backlightMax;
}

void InactivityManager::setBrightnessRaw(int raw) {
    if (m_backlightFd < 0) {
        return;
    }
    if (raw < 1) raw = 1;
    if (raw > m_backlightMax) raw = m_backlightMax;
    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%d\n", raw);
    ::pwrite(m_backlightFd, buf, len, 0);
    m_backlightCurrentRaw = raw;

    int pct = (raw * 100) / m_backlightMax;
    if (pct < 1) pct = 1;
    if (pct > 100) pct = 100;
    emit brightnessChanged(pct);
}

void InactivityManager::setBrightness(int val) {
    if (val < 1) val = 1;
    if (val > 100) val = 100;
    int raw = (val * m_backlightMax) / 100;
    setBrightnessRaw(raw);
}

int InactivityManager::getBrightness() {
    int raw = getBrightnessRaw();
    int pct = (raw * 100) / m_backlightMax;
    if (pct < 1) pct = 1;
    if (pct > 100) pct = 100;
    return pct;
}

void InactivityManager::startBrightnessTransition(int targetRaw, bool fast) {
    if (m_transitionInProgress || m_backlightFd < 0) {
        return;
    }
    readCurrentBrightness();
    m_transitionStartRaw = m_backlightCurrentRaw;
    m_transitionTargetRaw = targetRaw;
    m_transitionStep = 0;
    m_transitionInProgress = true;
    m_transitionTimer->start(fast ? (TRANSITION_INTERVAL_MS / 2) : TRANSITION_INTERVAL_MS);
}

void InactivityManager::setBrightnessImmediate(int rawBrightness, bool forceImmediate) {
    if (m_transitionInProgress) {
        m_transitionInProgress = false;
        m_transitionTimer->stop();
        usleep(50000);
    }
    if (m_backlightFd < 0) {
        return;
    }
    if (rawBrightness < 1) rawBrightness = 1;
    if (rawBrightness > m_backlightMax) rawBrightness = m_backlightMax;

    if (forceImmediate) {
        setBrightnessRaw(rawBrightness);
    } else {
        startBrightnessTransition(rawBrightness, true);
    }
}

void InactivityManager::updateBrightnessTransition() {
    if (!m_transitionInProgress || m_backlightFd < 0) {
        m_transitionInProgress = false;
        m_transitionTimer->stop();
        return;
    }

    m_transitionStep++;
    float t = (float)m_transitionStep / (float)TRANSITION_STEPS;
    int newRaw = m_transitionStartRaw +
                 (int)((m_transitionTargetRaw - m_transitionStartRaw) * t);
    if (newRaw < 1) newRaw = 1;
    if (newRaw > m_backlightMax) newRaw = m_backlightMax;
    setBrightnessRaw(newRaw);

    if (m_transitionStep >= TRANSITION_STEPS) {
        setBrightnessRaw(m_transitionTargetRaw);
        m_transitionInProgress = false;
        m_transitionTimer->stop();
    }
}

void InactivityManager::refreshBatteryIcon() {
    checkBatteryStatus(true);
}

void InactivityManager::checkIdle() {
    if (m_actionInProgress || m_calibrationActive) return;

    if (m_justWokeUp) {
        time_t now = time(NULL);
        if (now - m_wakeTime >= 5) {
            m_justWokeUp = false;
        } else {
            m_prevIdleTime = 0;
            m_idleTimer->changeInterval(1000);
            return;
        }
    }

    // Get X11 Idle time using standard XScreenSaver extension
    unsigned long idle_time_ms = 0;
    if (m_xssInfo) {
        XScreenSaverInfo *xss = static_cast<XScreenSaverInfo*>(m_xssInfo);
        if (XScreenSaverQueryInfo(m_x11Display, DefaultRootWindow(m_x11Display), xss)) {
            idle_time_ms = xss->idle;
        }
    }
    // Cap idle time after wake up to prevent old X11 idle values from triggering screensaver
    unsigned long time_since_wake_ms = (time(NULL) - m_wakeTime) * 1000UL;
    if (time_since_wake_ms < 300000UL) {
        if (idle_time_ms > time_since_wake_ms + 5000UL) {
            idle_time_ms = time_since_wake_ms;
        }
    }
    // Adapt timeouts
    unsigned long reduce_timeout_ms = (m_chargingState == 0 ? m_config->bat_backlight_reduce_timeout : m_config->ac_backlight_reduce_timeout) * 60000UL;
    unsigned long sleep_timeout_ms  = (m_chargingState == 0 ? m_config->bat_display_sleep_timeout : m_config->ac_display_sleep_timeout) * 60000UL;
    unsigned long system_timeout_ms = (m_chargingState == 0 ? m_config->bat_idle_timeout : m_config->ac_idle_timeout) * 60000UL;

    if (m_config->timeouts_auto_adapt) {
        if (m_warnSimple && !m_warnCrit) {
            reduce_timeout_ms /= 2;
            sleep_timeout_ms /= 2;
            if (reduce_timeout_ms < 30000) reduce_timeout_ms = 30000;
            if (sleep_timeout_ms < 45000) sleep_timeout_ms = 45000;
        } else if (m_warnCrit) {
            reduce_timeout_ms /= 3;
            sleep_timeout_ms /= 3;
            if (reduce_timeout_ms < 20000) reduce_timeout_ms = 20000;
            if (sleep_timeout_ms < 30000) sleep_timeout_ms = 30000;
        }
    }

    // Query battery when active or during dimming (original check_idle behaviour).
    if (idle_time_ms < 1000 || m_prevIdleTime >= reduce_timeout_ms) {
        checkBatteryStatus(false);
    }

    readCurrentBrightness();

    // Set check frequency interval (applied at end if timer stays active).
    int nextInterval = (m_screenSleeping || m_backlightReduced || m_screensaverActive) ? 200 : 6000;

    // 1. Display Sleep / Screensaver Trigger
    if (idle_time_ms >= sleep_timeout_ms && m_prevIdleTime >= sleep_timeout_ms && !m_screenSleeping && !m_screensaverActive) {
        if (!m_mediaPlaying || m_mediaPlayingType == 1 || !m_presentationMode) {
            // Save original brightness if not already dimmed
            if (!m_backlightReduced) {
                readCurrentBrightness();
                m_originalBrightnessRaw = m_backlightCurrentRaw;
                m_originalBrightness = getBrightness();
            }
            if (m_chargingState != 0) { // AC Mode
                if (m_config->ac_screensaver != "none") {
                    disableDpms();
                    m_screenSleeping = false;
                    m_backlightReduced = true;
                    m_screensaverActive = true;
                    
                    // Restore full brightness for screensaver immediately
                    setBrightnessImmediate(m_originalBrightnessRaw, false);

                    // Trigger Screensaver activation event
                    emit triggerScreensaver();
                } else {
                    setScreenDpms(false);
                    m_screenSleeping = true;
                    m_backlightReduced = true;
                    m_batteryLogger->addEvent(EVENT_SCREEN_OFF, m_batteryPercentage, m_chargingState);
                }
            } else { // Battery Mode
                setScreenDpms(false);
                m_screenSleeping = true;
                m_backlightReduced = true;
                m_batteryLogger->addEvent(EVENT_SCREEN_OFF, m_batteryPercentage, m_chargingState);

                if (m_warnSimple && m_config->lowbat_bt_off_on_display_off) {
                    setRfkillState("bluetooth", 0);
                }
            }
        }
    }
    // Wake up from Sleep / Screensaver
    else if (m_prevIdleTime >= sleep_timeout_ms && idle_time_ms < 1000 && (m_screenSleeping || m_backlightReduced || m_screensaverActive)) {
        if (m_screensaverActive) {
            m_screensaverActive = false;
            m_backlightReduced = false;
            setBrightnessImmediate(m_originalBrightnessRaw, true);
        } else {
            if (m_config->lock_on_display_off == 1) {
                lockScreen();
            }
            setScreenDpms(true);
            m_screenSleeping = false;
            m_backlightReduced = false;
            setBrightnessImmediate(m_originalBrightnessRaw, true);

            if (m_warnSimple && m_config->lowbat_bt_off_on_display_off) {
                setRfkillState("bluetooth", 1);
            }
            m_batteryLogger->addEvent(EVENT_SCREEN_ON, m_batteryPercentage, m_chargingState);
        }
    }
    // 2. Brightness Dimming
    else if (!(m_screenSleeping || m_screensaverActive)) {
        if (idle_time_ms >= reduce_timeout_ms && m_prevIdleTime >= reduce_timeout_ms && !m_backlightReduced) {
            readCurrentBrightness();
            m_originalBrightnessRaw = m_backlightCurrentRaw;
            m_originalBrightness = getBrightness();
            int reducedRaw = m_originalBrightnessRaw / 5;
            if (reducedRaw < 1) reducedRaw = 1;
            m_backlightReduced = true;
            startBrightnessTransition(reducedRaw, false);
            m_idleTimer->start(150);
            return;
        } else if (m_prevIdleTime >= reduce_timeout_ms && idle_time_ms < 1000 && m_backlightReduced) {
            setBrightnessImmediate(m_originalBrightnessRaw, false);
            m_backlightReduced = false;
            m_prevIdleTime = idle_time_ms;
            m_idleTimer->start(nextInterval);
            return;
        }

        // Auto-dim further during idle
        long long now = (long long)time(NULL) * 1000LL;
        if (m_chargingState == 0 && m_backlightReduced && m_config->reduce_brightness_more_during_idle &&
            m_backlightCurrentRaw > 5 * m_brightnessStep && !m_transitionInProgress &&
            now >= m_nextBrightnessIdleCheck) {
            setBrightnessImmediate(m_backlightCurrentRaw - m_brightnessStep, false);
            m_nextBrightnessIdleCheck = now + 20000;
        }

        // Adapt on percentage decrease
        if (m_chargingState == 0 && !m_backlightReduced && m_config->reduce_brightness_when_charge_decrease) {
            int diff = m_lastBattPercentage - m_batteryPercentage;
            if (diff > 0 && !m_transitionInProgress && m_backlightCurrentRaw > 25 * m_brightnessStep) {
                setBrightnessImmediate(m_backlightCurrentRaw - diff * m_brightnessStep, true);
            }
        }
        m_lastBattPercentage = m_batteryPercentage;
    }

    // 3. System Suspend Timeout
    if ((!m_presentationMode || m_warnCrit) && idle_time_ms >= system_timeout_ms && m_prevIdleTime >= system_timeout_ms && !m_mediaPlaying) {
        m_prevIdleTime = 0;
        int action = (m_chargingState == 0) ? m_config->bat_energy_saving : m_config->ac_energy_saving;
        switch (action) {
            case 0: suspendSystem(); break;
            case 1: suspendThenHibernate(); break;
            case 2: hibernateSystem(); break;
            case 3: hybridSuspendSystem(); break;
        }
    }

    m_prevIdleTime = idle_time_ms;

    // Original check_idle_and_adjust_brightness tail: keep timer only when
    // presentation mode is off, or critical battery, or audio-only media.
    if (!m_presentationMode || m_warnCrit || m_mediaPlayingType) {
        m_idleTimer->changeInterval(nextInterval);
        if (!m_idleTimer->isActive()) {
            m_idleTimer->start(nextInterval);
        }
    } else {
        m_idleTimer->stop();
        m_transitionTimer->stop();
    }
}

void InactivityManager::pollMpris() {
    MprisStatus status;
    if (mpris_poll(&status) != 0) {
        return;
    }

    m_mediaPlayingType = status.type;

    if ((status.playing != 0) != m_mediaPlaying) {
        m_mediaPlaying = (status.playing != 0);
        setPresentationMode(m_mediaPlaying);
        emit mediaPlayingChanged(m_mediaPlaying);
    }
}

void InactivityManager::checkBatteryStatus(bool force) {
    time_t now = time(NULL);
    if (!force && m_lastBatteryCheck != 0 && (now - m_lastBatteryCheck) < 4) {
        return;
    }
    m_lastBatteryCheck = now;

    TQString base = "/sys/class/power_supply/";
    TQString batPath = "";
    TQDir dir(base);
    if (dir.exists()) {
        TQStringList list = dir.entryList(TQDir::Dirs);
        for (TQStringList::Iterator it = list.begin(); it != list.end(); ++it) {
            if (*it == "." || *it == "..") continue;
            TQString type = readSysfsString(base + *it + "/type");
            if (type == "Battery") {
                batPath = base + *it;
                break;
            }
        }
    }

    if (batPath.isEmpty()) {
        m_batteryPercentage = 100;
        m_chargingState = 2; // full
        return;
    }

    int percentage = readSysfsInt(batPath + "/capacity");
    TQString status = readSysfsString(batPath + "/status");

    int state = 0;
    if (status == "Charging") state = 1;
    else if (status == "Full" || status == "Not charging") state = 2;

    const bool changed = (percentage != m_batteryPercentage || state != m_chargingState);
    const int oldState = m_chargingState;

    if (changed) {
        m_batteryPercentage = percentage;
        m_chargingState = state;

        m_batteryLogger->addSample(m_batteryPercentage, m_chargingState);
        m_batteryLogger->save();
    }

    // Handle Charging State transitions
    if (oldState != -1 && state != oldState) {
        m_phaseStartTime = now;
        m_phaseStartCapacity = m_batteryPercentage;
        m_currentRate = 0.0;

        if (oldState == 0) { // Charger Connected
            m_batteryLogger->addEvent(EVENT_CHARGER_CONNECTED, m_batteryPercentage, m_chargingState);
            m_batteryLogger->save();
            if (!m_justWokeUp) {
                TQTimer::singleShot(1000, this, TQT_SLOT(triggerDelayedNotification()));
            }
            adjustProfile();
            simulateUserActivity();
            m_prevIdleTime = 0;
            if (!m_presentationMode && !m_mediaPlaying && !m_calibrationActive) {
                m_idleTimer->start(1000);
            }

            // Adjust brightness when status change
            if (!m_justWokeUp && m_config->adjust_brightness_when_status_change && !m_calibrationActive) {
                readCurrentBrightness();
                int currentPct = getBrightness();
                if (currentPct <= 60) {
                    setBrightnessImmediate(m_backlightCurrentRaw + (30 * m_brightnessStep), false);
                } else if (currentPct > 60 && currentPct <= 80) {
                    setBrightnessImmediate(m_backlightCurrentRaw + (20 * m_brightnessStep), false);
                }
            }
        } else if (state == 0) { // Charger Disconnected
            m_batteryLogger->addEvent(EVENT_CHARGER_DISCONNECTED, m_batteryPercentage, m_chargingState);
            m_batteryLogger->save();
            if (!m_justWokeUp) {
                TQTimer::singleShot(1000, this, TQT_SLOT(triggerDelayedNotification()));
            }
            adjustProfile();
            simulateUserActivity();
            m_prevIdleTime = 0;
            if (!m_presentationMode && !m_mediaPlaying && !m_calibrationActive) {
                m_idleTimer->start(1000);
            }
 
            // Adjust brightness when status change
            if (!m_justWokeUp && m_config->adjust_brightness_when_status_change && !m_calibrationActive) {
                readCurrentBrightness();
                int currentPct = getBrightness();
                if (currentPct >= 50) {
                    setBrightnessImmediate(m_backlightCurrentRaw - (30 * m_brightnessStep), false);
                } else if (currentPct > 30 && currentPct < 50) {
                    setBrightnessImmediate(m_backlightCurrentRaw - (20 * m_brightnessStep), false);
                }
            }

            // If powernap was active, charger disconnected while lid still closed -> exit powernap & suspend!
            if (m_inPowernap) {
                m_inPowernap = false;
                if (m_wifiInitialState != -1) {
                    setRfkillState("wifi", m_wifiInitialState);
                }
                if (m_bluetoothInitialState != -1) {
                    setRfkillState("bluetooth", m_bluetoothInitialState);
                }
                // Trigger lid action (since lid is closed)
                onLidClosed(true);
            }
        }
        m_justWokeUp = false;
    } else {
        if (m_justWokeUp && (now - m_wakeTime >= 5)) {
            m_justWokeUp = false;
        }
    }

    // If state did not change, update rate after 1 minute of phase duration
    if (state == oldState && (now - m_phaseStartTime) >= 60) {
        m_currentRate = (m_batteryPercentage - m_phaseStartCapacity) / ((double)(now - m_phaseStartTime) / 3600.0);
    }

    // Warning and Critical Level States (only if discharging)
    if (m_chargingState == 0 && !m_calibrationActive) {
        if (m_batteryPercentage <= m_config->critical_level) {
            if (!m_warnCrit) {
                m_warnCrit = true;
                m_criticalLevelReached = m_batteryPercentage;
                if (m_criticalLevelReached >= 8) m_xLevel = 3;
                else if (m_criticalLevelReached >= 5) m_xLevel = 2;
                else if (m_criticalLevelReached >= 3) m_xLevel = 1;
                else m_xLevel = 0;

                if (m_config->notif_critical) {
                    sendNotification("Critical Battery", "Battery level is critically low. Please connect to a power source.", true);
                }

                callDaemon("set_webcam_power:0");
                setRfkillState("bluetooth", 0);

                readCurrentBrightness();
                if (getBrightness() > 40) {
                    setBrightness(40);
                }

                if (m_config->critical_freeze_processes) {
                    callDaemon(TQString("set_procs_whitelist:") + joinList(m_config->p_whitelist));
                    callDaemon(TQString("set_procs_blacklist:") + joinList(m_config->p_blacklist));
                    callDaemon("freeze_processes:1");
                }
                adjustProfile();
            } else {
                if (m_batteryPercentage <= (m_criticalLevelReached - m_xLevel) || m_batteryPercentage <= 2) {
                    m_warnCrit = false;
                    runCriticalAction();
                }
            }
        } else if (m_batteryPercentage <= m_config->warn_level) {
            if (!m_warnSimple) {
                m_warnSimple = true;
                if (m_config->notif_low) {
                    sendNotification("Low Battery", "Battery level is low. Please consider connecting to a power source.", false);
                }
                
                if (m_config->lowbat_freeze_services) {
                    callDaemon(TQString("set_whitelist:") + joinList(m_config->whitelist));
                    callDaemon(TQString("set_blacklist:") + joinList(m_config->blacklist));
                    callDaemon("freeze_services_dbus");
                }
                adjustProfile();
            }
        }
    }

    // Reset warnings
    if (m_batteryPercentage > m_config->critical_level || m_chargingState != 0) {
        if (m_warnCrit) {
            m_warnCrit = false;
            callDaemon("set_webcam_power:1");
            if (m_config->critical_freeze_processes) {
                callDaemon("freeze_processes:0");
            }
            if (m_bluetoothInitialState != -1) {
                setRfkillState("bluetooth", m_bluetoothInitialState);
            } else {
                setRfkillState("bluetooth", 1);
            }
        }
    }
    if (m_batteryPercentage > (m_config->warn_level + 1) || m_chargingState != 0) {
        if (m_warnSimple) {
            m_warnSimple = false;
            if (m_config->lowbat_freeze_services) {
                callDaemon("unfreeze_all_frozen_services");
            }
        }
    }

    // 72h check
    if (m_batteryLogger->isAlways100PercentOver72h() && !m_warned100Percent && !m_calibrationActive) {
        m_warned100Percent = true;
        sendNotification("Battery Alert", "Battery has been at 100% for the last 72 hours. Please calibrate your battery to extend its lifetime.", false);
    }

    if (m_powerProfile == -1) {
        adjustProfile();
    }

    if (changed || force) {
        emit batteryStatusChanged(m_batteryPercentage, m_chargingState);
    }

}

void InactivityManager::executePendingAction(int action) {
    switch (action) {
        case 0: suspendSystem(); break;
        case 1: suspendThenHibernate(); break;
        case 2: hibernateSystem(); break;
        case 3: hybridSuspendSystem(); break;
        case 4: callDaemon("shutdown"); break;
    }
}

void InactivityManager::setSleepTransitionDone(bool done) {
    m_sleepTransitionDone = done;
}

void InactivityManager::iterateGlib() {
    g_main_context_iteration(NULL, FALSE);
}

void InactivityManager::suspendSystem() {
    if (m_actionInProgress) return;
    m_actionInProgress = true;
    m_batteryLogger->addEvent(EVENT_SUSPEND, m_batteryPercentage, m_chargingState);
    m_batteryLogger->save();

    m_idleTimer->stop();

    prepareSuspendGeneral(false);
    usleep(200000);

    if (m_inhibitSleepFd >= 0) { ::close(m_inhibitSleepFd); m_inhibitSleepFd = -1; }
    if (m_inhibitHibernateFd >= 0) { ::close(m_inhibitHibernateFd); m_inhibitHibernateFd = -1; }

    if (!m_systemBus) {
        m_systemBus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
    }
    GDBusConnection *system_bus = static_cast<GDBusConnection*>(m_systemBus);
    if (system_bus) {
        GError *error = NULL;
        GVariant *result = g_dbus_connection_call_sync(
            system_bus,
            "org.freedesktop.login1",
            "/org/freedesktop/login1",
            "org.freedesktop.login1.Manager",
            "Suspend",
            g_variant_new("(b)", TRUE),
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            -1, NULL, &error);
        if (result) {
            g_variant_unref(result);
        } else {
            if (error) g_error_free(error);
            m_actionInProgress = false;
        }
    }
}

void InactivityManager::suspendThenHibernate() {
    if (m_actionInProgress) return;
    m_actionInProgress = true;
    m_batteryLogger->addEvent(EVENT_SUSPEND_THEN_HIBERNATE, m_batteryPercentage, m_chargingState);
    m_batteryLogger->save();

    m_idleTimer->stop();

    prepareSuspendGeneral(false);
    usleep(200000);

    if (m_inhibitSleepFd >= 0) { ::close(m_inhibitSleepFd); m_inhibitSleepFd = -1; }
    if (m_inhibitHibernateFd >= 0) { ::close(m_inhibitHibernateFd); m_inhibitHibernateFd = -1; }

    if (!m_systemBus) {
        m_systemBus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
    }
    GDBusConnection *system_bus = static_cast<GDBusConnection*>(m_systemBus);
    if (system_bus) {
        GError *error = NULL;
        GVariant *result = g_dbus_connection_call_sync(
            system_bus,
            "org.freedesktop.login1",
            "/org/freedesktop/login1",
            "org.freedesktop.login1.Manager",
            "SuspendThenHibernate",
            g_variant_new("(b)", TRUE),
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            -1, NULL, &error);
        if (result) {
            g_variant_unref(result);
        } else {
            if (error) g_error_free(error);
            m_actionInProgress = false;
        }
    }
}

void InactivityManager::hibernateSystem() {
    if (m_actionInProgress) return;
    m_actionInProgress = true;
    m_batteryLogger->addEvent(EVENT_HIBERNATE, m_batteryPercentage, m_chargingState);
    m_batteryLogger->save();

    m_idleTimer->stop();

    prepareSuspendGeneral(true);
    usleep(200000);

    if (m_inhibitSleepFd >= 0) { ::close(m_inhibitSleepFd); m_inhibitSleepFd = -1; }
    if (m_inhibitHibernateFd >= 0) { ::close(m_inhibitHibernateFd); m_inhibitHibernateFd = -1; }

    if (!m_systemBus) {
        m_systemBus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
    }
    GDBusConnection *system_bus = static_cast<GDBusConnection*>(m_systemBus);
    if (system_bus) {
        GError *error = NULL;
        GVariant *result = g_dbus_connection_call_sync(
            system_bus,
            "org.freedesktop.login1",
            "/org/freedesktop/login1",
            "org.freedesktop.login1.Manager",
            "Hibernate",
            g_variant_new("(b)", TRUE),
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            -1, NULL, &error);
        if (result) {
            g_variant_unref(result);
        } else {
            if (error) g_error_free(error);
            m_actionInProgress = false;
        }
    }
}

void InactivityManager::hybridSuspendSystem() {
    if (m_actionInProgress) return;
    m_actionInProgress = true;
    m_batteryLogger->addEvent(EVENT_HYBRID_SUSPEND, m_batteryPercentage, m_chargingState);
    m_batteryLogger->save();

    m_idleTimer->stop();

    prepareSuspendGeneral(false);
    usleep(200000);

    if (m_inhibitSleepFd >= 0) { ::close(m_inhibitSleepFd); m_inhibitSleepFd = -1; }
    if (m_inhibitHibernateFd >= 0) { ::close(m_inhibitHibernateFd); m_inhibitHibernateFd = -1; }

    if (!m_systemBus) {
        m_systemBus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
    }
    GDBusConnection *system_bus = static_cast<GDBusConnection*>(m_systemBus);
    if (system_bus) {
        GError *error = NULL;
        GVariant *result = g_dbus_connection_call_sync(
            system_bus,
            "org.freedesktop.login1",
            "/org/freedesktop/login1",
            "org.freedesktop.login1.Manager",
            "HybridSleep",
            g_variant_new("(b)", TRUE),
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            -1, NULL, &error);
        if (result) {
            g_variant_unref(result);
        } else {
            if (error) g_error_free(error);
            m_actionInProgress = false;
        }
    }
}

void InactivityManager::lockScreen(bool useOverlay) {
    startLockTimer(useOverlay);
}

void InactivityManager::startLockTimer(bool useOverlay) {
    if (m_actionInProgress) return;

    m_useWifiOverlay = useOverlay;
    m_wifiCheckCounter = 0;

    checkWifiAndLock();
}

void InactivityManager::checkWifiAndLock() {
    if (m_useWifiOverlay) {
        showWifiOverlay(true);
    }

    if (m_config->authorized_ssids.isEmpty()) {
        lockScreenNow();
        if (m_useWifiOverlay) {
            TQTimer::singleShot(1500, this, TQT_SLOT(hideWifiOverlaySlot()));
        } else {
            emit wokeUp();
        }
        return;
    }

    // Check if wifi is enabled (rfkill state == 1)
    if (getRfkillState("wifi") != 0) {
        int result = checkAuthorizedSsid();
        if (result == 1) {
            if (m_useWifiOverlay) {
                showWifiOverlay(false);
            }
            emit wokeUp();
            return; // Authorized SSID detected -> bypass lock
        }

        if (result == 2) {
            if (m_wifiCheckCounter < 2) {
                m_wifiCheckCounter++;
                TQTimer::singleShot(3000, this, TQT_SLOT(checkWifiAndLockRetry()));
                return;
            }
        }

        lockScreenNow();
        if (m_useWifiOverlay) {
            TQTimer::singleShot(1500, this, TQT_SLOT(hideWifiOverlaySlot()));
        } else {
            showWifiOverlay(false);
            emit wokeUp();
        }
    } else {
        // Wifi is disabled! Let NM enable it or retry up to 2 times (3s delay each)
        if (m_useWifiOverlay && m_wifiCheckCounter < 2) {
            m_wifiCheckCounter++;
            TQTimer::singleShot(3000, this, TQT_SLOT(checkWifiAndLockRetry()));
        } else {
            lockScreenNow();
            if (m_useWifiOverlay) {
                TQTimer::singleShot(1500, this, TQT_SLOT(hideWifiOverlaySlot()));
            } else {
                showWifiOverlay(false);
                emit wokeUp();
            }
        }
    }
}

void InactivityManager::checkWifiAndLockRetry() {
    checkWifiAndLock();
}


void InactivityManager::hideWifiOverlaySlot() {
    showWifiOverlay(false);
    emit wokeUp();
}

void InactivityManager::showWifiOverlay(bool show) {
    if (show) {
        if (!m_wifiOverlay) {
            m_wifiOverlay = new WifiStatusOverlay();
        }
        m_wifiOverlay->show();
        tqApp->processEvents();
    } else {
        if (m_wifiOverlay) {
            m_wifiOverlay->hide();
            delete m_wifiOverlay;
            m_wifiOverlay = NULL;
        }
    }
}

void InactivityManager::lockScreenNow() {
    system("dcop kdesktop KScreensaverIface lock &");
}

void InactivityManager::setScreenDpms(bool enable) {
    Display *dpy = m_x11Display;
    if (!dpy) return;
    int dummy;
    if (DPMSQueryExtension(dpy, &dummy, &dummy)) {
        if (DPMSEnable(dpy)) {
            DPMSSetTimeouts(dpy, 0, 0, 0);
            XSetScreenSaver(dpy, 0, 0, DontPreferBlanking, DontAllowExposures);
            DPMSForceLevel(dpy, enable ? DPMSModeOn : DPMSModeOff);
            XFlush(dpy);
        }
    }
}

void InactivityManager::disableDpms() {
    // Mirrors original set_presentation_mode_settings() / disable_dpms().
    Display *dpy = m_x11Display;
    if (dpy) {
        int dummy;
        if (DPMSQueryExtension(dpy, &dummy, &dummy)) {
            DPMSDisable(dpy);
            DPMSSetTimeouts(dpy, 0, 0, 0);
        }
        XSetScreenSaver(dpy, 0, 0, DontPreferBlanking, DontAllowExposures);
        XSync(dpy, False);
    }
    system("xset s off 2>/dev/null");
    system("xset -dpms 2>/dev/null");
    system("xset dpms force on 2>/dev/null");
    system("xscreensaver-command -deactivate 2>/dev/null");
    system("xfce4-screensaver-command --deactivate 2>/dev/null");
}

void InactivityManager::callDaemon(const TQString &cmd) {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return;

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/run/yabatmand/daemon.sock", sizeof(addr.sun_path) - 1);

    if (::connect(sock, (struct sockaddr*)&addr, sizeof(addr)) >= 0) {
        write(sock, cmd.latin1(), cmd.length());
        write(sock, "\n", 1);
    }
    ::close(sock);
}

static void superimposeImages(TQImage &base, const TQImage &overlay) {
    if (base.isNull() || overlay.isNull()) return;
    int w = base.width();
    int h = base.height();
    int ow = overlay.width();
    int oh = overlay.height();

    if (base.depth() != 32) base = base.convertDepth(32);
    TQImage srcOverlay = overlay;
    if (srcOverlay.depth() != 32) srcOverlay = srcOverlay.convertDepth(32);

    for (int y = 0; y < h && y < oh; ++y) {
        unsigned int *baseRow = (unsigned int *)base.scanLine(y);
        const unsigned int *overlayRow = (const unsigned int *)srcOverlay.scanLine(y);
        for (int x = 0; x < w && x < ow; ++x) {
            unsigned int op = overlayRow[x];
            unsigned int alpha = (op >> 24) & 0xFF;
            if (alpha > 0) {
                baseRow[x] = op;
            }
        }
    }
}

void InactivityManager::sendNotification(const TQString &title, const TQString &message, bool critical) {
    if (m_config->status_notifs == 0) return;

    TQString iconPath;
    if (critical) {
        iconPath = "/tmp/yabatman_crit_warn.png";
        if (::access(iconPath.latin1(), F_OK) != 0) {
            TQImage baseImg;
            if (baseImg.loadFromData(yabatman_crit_data, yabatman_crit_size, "PNG")) {
                TQImage overlayImg;
                if (overlayImg.loadFromData(warn_data, warn_size, "PNG")) {
                    superimposeImages(baseImg, overlayImg);
                    baseImg.save(iconPath, "PNG");
                } else {
                    baseImg.save(iconPath, "PNG");
                }
            }
        }
    } else {
        bool charging = (getChargingState() != 0);
        if (charging) {
            iconPath = "/tmp/yabatman_ac_bat.png";
            if (::access(iconPath.latin1(), F_OK) != 0) {
                TQImage baseImg;
                if (baseImg.loadFromData(yabatman_bat_data, yabatman_bat_size, "PNG")) {
                    TQImage overlayImg;
                    if (overlayImg.loadFromData(yabatman_ac_data, yabatman_ac_size, "PNG")) {
                        superimposeImages(baseImg, overlayImg);
                        baseImg.save(iconPath, "PNG");
                    } else {
                        baseImg.save(iconPath, "PNG");
                    }
                }
            }
        } else {
            iconPath = "/tmp/yabatman_bat.png";
            if (::access(iconPath.latin1(), F_OK) != 0) {
                TQImage baseImg;
                if (baseImg.loadFromData(yabatman_bat_data, yabatman_bat_size, "PNG")) {
                    baseImg.save(iconPath, "PNG");
                }
            }
        }
    }

    NotifyNotification *n = notify_notification_new(
        title.utf8().data(),
        message.utf8().data(),
        !iconPath.isEmpty() && ::access(iconPath.latin1(), F_OK) == 0 ? iconPath.latin1() : NULL
    );
    if (n) {
        notify_notification_set_urgency(n, critical ? NOTIFY_URGENCY_CRITICAL : NOTIFY_URGENCY_NORMAL);
        notify_notification_set_timeout(n, critical ? 10000 : 5000);
        notify_notification_show(n, NULL);
        g_object_unref(G_OBJECT(n));
    }
}

int InactivityManager::getRfkillState(const char *target) {
    DIR *dir = opendir("/sys/class/rfkill");
    if (!dir) return -1;
    struct dirent *entry;
    int state = -1;
    while ((entry = readdir(dir))) {
        if (strncmp(entry->d_name, "rfkill", 6) != 0) continue;
        char path[256];
        snprintf(path, sizeof(path), "/sys/class/rfkill/%s/type", entry->d_name);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        char kind[32];
        if (fgets(kind, sizeof(kind), f)) {
            fclose(f);
            kind[strcspn(kind, "\n")] = '\0';
            if (strcmp(kind, target) == 0) {
                snprintf(path, sizeof(path), "/sys/class/rfkill/%s/soft", entry->d_name);
                FILE *fs = fopen(path, "r");
                if (fs) {
                    int v;
                    if (fscanf(fs, "%d", &v) == 1) {
                        state = (v == 0) ? 1 : 0;
                    }
                    fclose(fs);
                    break;
                }
            }
        } else {
            fclose(f);
        }
    }
    closedir(dir);
    return state;
}

void InactivityManager::setRfkillState(const char *target, int state) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "set_rfkill_state:%s:%d", target, state);
    callDaemon(cmd);
}

void InactivityManager::setProfile(int profile) {
    static int last_hw_profile = -1;
    if (profile == last_hw_profile) {
        return;
    }
    last_hw_profile = profile;
    m_powerProfile = (profile == 3) ? 0 : profile;
    switch (profile) {
        case 0:
            callDaemon("set_low_profile:0");
            break;
        case 1:
            callDaemon("set_normal_profile");
            break;
        case 2:
            callDaemon("set_perf_profile");
            break;
        case 3:
            callDaemon("set_low_profile:1");
            break;
    }
    emit powerProfileChanged(m_powerProfile);
}

void InactivityManager::adjustProfile() {
    // cpu driver opmode
    if (m_chargingState == 1 || m_chargingState == 2) {
        callDaemon("set_cpu_driver_opmode:1");
        if (m_config->disable_eth == 2) {
            callDaemon("toggle_ethernet:1");
        }
    } else {
        callDaemon("set_cpu_driver_opmode:0");
        if (m_config->disable_eth == 2) {
            callDaemon("toggle_ethernet:0");
        }
    }

    if (!m_warnCrit && !m_warnSimple) {
        switch (m_chargingState) {
            case 0:
                setProfile(m_config->bat_power_profile);
                break;
            case 1:
                setProfile(m_config->ac_power_profile);
                break;
            case 2:
                setProfile(m_config->ac_full_power_profile);
                break;
        }
    } else {
        if (m_warnCrit) {
            setProfile(3); // ultra low power
        } else if (m_warnSimple) {
            if (m_powerProfile == 2) {
                setProfile(1); // downgrade performance to normal
            }
        }
    }
}

void InactivityManager::runCriticalAction() {
    switch (m_config->critical_action) {
        case 0: // hibernate
            hibernateSystem();
            break;
        case 1: // hybrid
            hybridSuspendSystem();
            break;
        case 2: // shutdown
            callDaemon("shutdown");
            break;
    }
}

TQString InactivityManager::getRemainingDurationStr() {
    TQString batPath = "";
    TQString base = "/sys/class/power_supply/";
    TQDir dir(base);
    if (dir.exists()) {
        TQStringList list = dir.entryList(TQDir::Dirs);
        for (TQStringList::Iterator it = list.begin(); it != list.end(); ++it) {
            if (*it == "." || *it == "..") continue;
            TQString type = readSysfsString(base + *it + "/type");
            if (type == "Battery") {
                batPath = base + *it;
                break;
            }
        }
    }
    if (batPath.isEmpty()) return "N/A";

    int full = readSysfsInt(batPath + "/energy_full");
    if (full == 0) full = readSysfsInt(batPath + "/charge_full");
    int now = readSysfsInt(batPath + "/energy_now");
    if (now == 0) now = readSysfsInt(batPath + "/charge_now");
    int pwr = readSysfsInt(batPath + "/power_now");
    if (pwr == 0) pwr = readSysfsInt(batPath + "/current_now");

    if (pwr > 0) {
        int remain = (m_chargingState == 1) ? (full - now) : now;
        if (remain > 0) {
            int hours = remain / pwr;
            int mins = (int)((double)remain * 60.0 / pwr) % 60;
            TQString timeStr;
            timeStr.sprintf("%d:%02d", hours, mins);
            return timeStr;
        }
    }
    return "unknown duration";
}

void InactivityManager::triggerDelayedNotification() {
    if (m_config->status_notifs == 0) return;

    if (m_batteryPercentage == 100 && (m_chargingState == 1 || m_chargingState == 2)) {
        if (m_config->notif_full) {
            sendNotification("Battery Fully Charged", "Running on AC power.", false);
        }
    } else if (m_chargingState == 1) {
        if (m_config->notif_charger) {
            TQString duration = getRemainingDurationStr();
            sendNotification("Battery Charging", TQString("Estimated time to full charge: ") + duration, false);
        }
    } else {
        if (m_config->notif_charger) {
            TQString duration = getRemainingDurationStr();
            sendNotification("Running on Battery", TQString("Estimated time left: ") + duration + ".", false);
        }
    }
}



int InactivityManager::checkAuthorizedSsid() {
    if (m_config->authorized_ssids.isEmpty()) {
        return -1;
    }
    if (getRfkillState("wifi") == 0) {
        return -1; // Wifi disabled
    }
    TQString currentSsid = getActiveSsid();
    if (currentSsid.isEmpty()) {
        return 2; // Wifi scanning / connecting
    }
    for (TQStringList::ConstIterator it = m_config->authorized_ssids.begin(); it != m_config->authorized_ssids.end(); ++it) {
        if (currentSsid == *it) {
            return 1; // Authorized SSID detected
        }
    }
    return -1; // Unauthorized SSID
}

void InactivityManager::simulateUserActivity() {
    if (m_justWokeUp || m_calibrationActive) return;
    XResetScreenSaver(m_x11Display);
    XFlush(m_x11Display);

    bool oldScreenSleeping = m_screenSleeping;

    if (m_screensaverActive) {
        m_screensaverActive = false;
        m_backlightReduced = false;
        setBrightnessImmediate(m_originalBrightnessRaw, true);
        emit wokeUp(); // This closes screensaver widget in main.cpp
    }

    if (oldScreenSleeping) {
        setScreenDpms(true);
        m_screenSleeping = false;
        m_backlightReduced = false;
        setBrightnessImmediate(m_originalBrightnessRaw, true);
        if (m_warnSimple && m_config->lowbat_bt_off_on_display_off) {
            setRfkillState("bluetooth", 1);
        }
        m_batteryLogger->addEvent(EVENT_SCREEN_ON, m_batteryPercentage, m_chargingState);
    } else if (m_backlightReduced) {
        m_backlightReduced = false;
        setBrightnessImmediate(m_originalBrightnessRaw, false);
    }
}

extern "C" {
static void onPrepareForSleepCallback(GDBusConnection *connection,
                                     const gchar *sender_name,
                                     const gchar *object_path,
                                     const gchar *interface_name,
                                     const gchar *signal_name,
                                     GVariant *parameters,
                                     gpointer user_data) {
    InactivityManager *self = static_cast<InactivityManager*>(user_data);
    gboolean about_to_sleep;
    g_variant_get(parameters, "(b)", &about_to_sleep);
    self->onPrepareForSleep(about_to_sleep);
}

static void onPrepareForShutdownCallback(GDBusConnection *connection,
                                        const gchar *sender_name,
                                        const gchar *object_path,
                                        const gchar *interface_name,
                                        const gchar *signal_name,
                                        GVariant *parameters,
                                        gpointer user_data) {
    InactivityManager *self = static_cast<InactivityManager*>(user_data);
    gboolean about_to_shutdown;
    g_variant_get(parameters, "(b)", &about_to_shutdown);
    self->onPrepareForShutdown(about_to_shutdown);
}
}

void InactivityManager::setupDbusMonitoring() {
    if (!m_systemBus) {
        m_systemBus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
    }
    GDBusConnection *system_bus = static_cast<GDBusConnection*>(m_systemBus);
    if (system_bus) {
        m_dbusPrepareSleepSubId = g_dbus_connection_signal_subscribe(system_bus,
            "org.freedesktop.login1",
            "org.freedesktop.login1.Manager",
            "PrepareForSleep",
            "/org/freedesktop/login1",
            NULL,
            G_DBUS_SIGNAL_FLAGS_NONE,
            onPrepareForSleepCallback,
            this,
            NULL);

        m_dbusPrepareShutdownSubId = g_dbus_connection_signal_subscribe(system_bus,
            "org.freedesktop.login1",
            "org.freedesktop.login1.Manager",
            "PrepareForShutdown",
            "/org/freedesktop/login1",
            NULL,
            G_DBUS_SIGNAL_FLAGS_NONE,
            onPrepareForShutdownCallback,
            this,
            NULL);
    }
}

void InactivityManager::onPrepareForSleep(bool aboutToSleep) {
    if (aboutToSleep) {
        if (!m_actionInProgress) {
            prepareSuspendGeneral(false);
            usleep(100000);
        }
        if (m_inhibitSleepFd >= 0) { ::close(m_inhibitSleepFd); m_inhibitSleepFd = -1; }
        if (m_inhibitHibernateFd >= 0) { ::close(m_inhibitHibernateFd); m_inhibitHibernateFd = -1; }
    } else {
        afterWakeActions();
    }
}

void InactivityManager::onPrepareForShutdown(bool aboutToShutdown) {
    if (aboutToShutdown) {
        if (!m_actionInProgress) {
            m_idleTimer->stop();
            m_batteryTimer->stop();
            m_batteryLogger->save();
            usleep(100000);
        }
        if (m_inhibitShutdownFd >= 0) { ::close(m_inhibitShutdownFd); m_inhibitShutdownFd = -1; }
    }
}

void InactivityManager::prepareSuspendGeneral(bool isHibernateOrPoweroff) {
    m_idleTimer->stop();
    m_batteryTimer->stop();
    sync();

    // Black out screensaver immediately before sleep so it shows solid black on wake-up!
    emit blackoutScreensaver(true);
    tqApp->processEvents();

    if (m_wifiInitialState == -1) m_wifiInitialState = getRfkillState("wifi");
    if (m_bluetoothInitialState == -1) m_bluetoothInitialState = getRfkillState("bluetooth");

    if (isHibernateOrPoweroff) {
        setProfile(2); // Performance for fast hibernation/shutdown
    } else {
        if (m_config->minimal_state_before_suspend) {
            enterMinimalMode();
        }
    }
}

void InactivityManager::enterMinimalMode() {
    setRfkillState("wifi", 0);
    setRfkillState("bluetooth", 0);
    callDaemon("set_webcam_power:0");
    setProfile(3); // Ultra Low Power
    sync();
    callDaemon("disable_cpu_cores");
    m_minimalMode = true;
    usleep(150000);
}

void InactivityManager::exitMinimalMode() {
    if (!m_minimalMode) return;
    callDaemon("reenable_cpu_cores");
    callDaemon("set_webcam_power:1");
    m_minimalMode = false;
}

void InactivityManager::afterWakeActions() {
    m_justWokeUp = true;
    m_wakeTime = time(NULL);
    m_actionInProgress = false;

    // Log wake-up event
    m_batteryLogger->addEvent(EVENT_WAKE_UP, m_batteryPercentage, m_chargingState);

    // FIRST: clean up transition overlay / screensaver
    emit wokeUp();

    if (m_config->lock_on_sleep == 1) {
        lockScreen(true);
    }

    // Restore screen settings
    XResetScreenSaver(m_x11Display);
    XSetScreenSaver(m_x11Display, 0, 0, DontPreferBlanking, DontAllowExposures);
    XSync(m_x11Display, False);

    // Re-acquire inhibitors after sleep!
    setupInhibitors();

    if (m_config->minimal_state_before_suspend) {
        exitMinimalMode();
    }

    adjustProfile();

    if (m_wifiInitialState != -1) {
        setRfkillState("wifi", m_wifiInitialState);
    }
    if (m_bluetoothInitialState != -1) {
        TQTimer::singleShot(2000, this, TQT_SLOT(restoreBluetooth()));
    }

    // Finally, restore brightness
    if (m_backlightReduced || m_screensaverActive || m_screenSleeping || getBrightness() < m_originalBrightness) {
        setBrightnessImmediate(m_originalBrightnessRaw, true);
    }

    m_screenSleeping = false;
    m_screensaverActive = false;
    m_backlightReduced = false;
    m_presentationMode = false;
    m_prevIdleTime = 0;

    // Restart timers with safe delay
    m_batteryTimer->start(5000);
    m_idleTimer->start(6000);
    m_mprisTimer->start(5000);
}

void InactivityManager::restoreBluetooth() {
    if (m_bluetoothInitialState != -1) {
        setRfkillState("bluetooth", m_bluetoothInitialState);
        if (m_bluetoothInitialState == 1) {
            system("bluetoothctl power on > /dev/null 2>&1");
        }
        m_bluetoothInitialState = -1;
    }
}

bool InactivityManager::readLidState() {
    static const char *files[] = {
        "/proc/acpi/button/lid/LID0/state",
        "/proc/acpi/button/lid/LID/state",
        NULL
    };
    char buf[32];
    for (int i = 0; files[i]; ++i) {
        int fd = open(files[i], O_RDONLY);
        if (fd < 0) continue;
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0) continue;
        buf[n] = '\0';
        for (int j = 0; j < n; ++j) {
            if ((buf[j] == 'c' && j + 5 < n &&
                 buf[j+1]=='l' && buf[j+2]=='o' && buf[j+3]=='s' && buf[j+4]=='e' && buf[j+5]=='d')
                || buf[j] == '0') {
                return false; // closed
            }
            if ((buf[j] == 'o' && j + 3 < n &&
                 buf[j+1]=='p' && buf[j+2]=='e' && buf[j+3]=='n')
                || buf[j] == '1') {
                return true; // open
            }
        }
    }
    return true; // open by default
}

void InactivityManager::checkLidState() {
    bool currentLidState = readLidState();
    if (currentLidState != m_lastLidState) {
        m_lastLidState = currentLidState;
        onLidClosed(!currentLidState);
    }
}

int InactivityManager::inhibitPowerSleep(const char *what, const char *who, const char *why, const char *mode) {
    GError *error = NULL;
    GUnixFDList *out_fd_list = NULL;
    gint32 handle = -1;
    if (!m_systemBus) {
        m_systemBus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
    }
    GDBusConnection *system_bus = static_cast<GDBusConnection*>(m_systemBus);
    if (!system_bus) return -1;

    GVariant *result = g_dbus_connection_call_with_unix_fd_list_sync(
        system_bus,
        "org.freedesktop.login1",
        "/org/freedesktop/login1",
        "org.freedesktop.login1.Manager",
        "Inhibit",
        g_variant_new("(ssss)", what, who, why, mode),
        G_VARIANT_TYPE("(h)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &out_fd_list,
        NULL,
        &error);
    if (!result) {
        if (error) g_error_free(error);
        return -1;
    }
    g_variant_get(result, "(h)", &handle);
    g_variant_unref(result);
    if (out_fd_list) {
        GError *fd_error = NULL;
        int fd = g_unix_fd_list_get(out_fd_list, handle, &fd_error);
        if (fd_error) {
            g_error_free(fd_error);
            g_object_unref(out_fd_list);
            return -1;
        }
        g_object_unref(out_fd_list);
        return fd;
    }
    return -1;
}

void InactivityManager::setupInhibitors() {
    releaseInhibitors(); // Prevent descriptor leaks
    m_inhibitFd = inhibitPowerSleep("handle-lid-switch", "yabatman", "Custom lid switch handling", "block");
    m_inhibitSleepFd = inhibitPowerSleep("sleep", "yabatman", "Custom sleep handling", "delay");
    m_inhibitShutdownFd = inhibitPowerSleep("shutdown", "yabatman", "Custom shutdown handling", "delay");
    m_inhibitPowerKeyFd = inhibitPowerSleep("handle-power-key", "yabatman", "Custom power key handling", "block");
    m_inhibitSuspendKeyFd = inhibitPowerSleep("handle-suspend-key", "yabatman", "Custom suspend key handling", "block");
    m_inhibitHibernateFd = inhibitPowerSleep("handle-hibernate-key", "yabatman", "Custom hibernate key handling", "block");
}

void InactivityManager::releaseInhibitors() {
    if (m_inhibitFd >= 0) { ::close(m_inhibitFd); m_inhibitFd = -1; }
    if (m_inhibitSleepFd >= 0) { ::close(m_inhibitSleepFd); m_inhibitSleepFd = -1; }
    if (m_inhibitShutdownFd >= 0) { ::close(m_inhibitShutdownFd); m_inhibitShutdownFd = -1; }
    if (m_inhibitPowerKeyFd >= 0) { ::close(m_inhibitPowerKeyFd); m_inhibitPowerKeyFd = -1; }
    if (m_inhibitSuspendKeyFd >= 0) { ::close(m_inhibitSuspendKeyFd); m_inhibitSuspendKeyFd = -1; }
    if (m_inhibitHibernateFd >= 0) { ::close(m_inhibitHibernateFd); m_inhibitHibernateFd = -1; }
}

void InactivityManager::setupPowerSleepKeys() {
    Display *dpy = m_x11Display;
    if (!dpy) return;

    const char* keysyms[] = {
        "XF86Sleep",
        "XF86PowerOff",
        "XF86Suspend",
        "XF86PowerButton",
        NULL
    };

    m_keycodes[0] = 0;
    m_keycodes[1] = 0;
    int index = 0;
    for (int i = 0; keysyms[i] && index < 2; ++i) {
        KeySym sym = XStringToKeysym(keysyms[i]);
        if (sym == NoSymbol) continue;
        KeyCode code = XKeysymToKeycode(dpy, sym);
        if (code == 0) continue;
        m_keycodes[index++] = code;
    }

    Window root = DefaultRootWindow(dpy);
    for (int i = 0; i < 2; ++i) {
        if (m_keycodes[i] != 0) {
            for (int mod = 0; mod < 256; ++mod) {
                XGrabKey(dpy, m_keycodes[i], mod, root, True, GrabModeAsync, GrabModeAsync);
            }
        }
    }
    XSelectInput(dpy, root, KeyPressMask | KeyReleaseMask);
    XSync(dpy, False);
}

void InactivityManager::releasePowerSleepKeys() {
    Display *dpy = m_x11Display;
    if (!dpy) return;
    Window root = DefaultRootWindow(dpy);
    for (int i = 0; i < 2; ++i) {
        if (m_keycodes[i] != 0) {
            XUngrabKey(dpy, m_keycodes[i], AnyModifier, root);
        }
    }
}

void InactivityManager::handleSleepButton() {
    if (m_actionInProgress || m_transitionInProgress) return;
    
    int action = m_config->sleep_button;
    if (action >= 0 && action <= 3) {
        m_transitionInProgress = true;
        runSleepTransitionSync();
        m_transitionInProgress = false;
    }
    switch (action) {
        case 0: suspendSystem(); break;
        case 1: suspendThenHibernate(); break;
        case 2: hibernateSystem(); break;
        case 3: hybridSuspendSystem(); break;
        case 4: break; // Do Nothing
    }
}

void InactivityManager::handlePowerButton() {
    if (m_actionInProgress || m_transitionInProgress) return;
    
    int action = m_config->power_button;
    if (action >= 0 && action <= 4) {
        m_transitionInProgress = true;
        runSleepTransitionSync();
        m_transitionInProgress = false;
    }
    switch (action) {
        case 0: suspendSystem(); break;
        case 1: suspendThenHibernate(); break;
        case 2: hibernateSystem(); break;
        case 3: hybridSuspendSystem(); break;
        case 4: callDaemon("shutdown"); break;
        case 5:
            system("dcop ksmserver default logout 1 2 3");
            break;
        case 6: break; // Do Nothing
    }
}

void InactivityManager::runSleepTransitionSync() {
    int effect = m_config->tv_effect_on_suspend_and_shutdown;
    if (effect != 0) {
        if (effect == 4) {
            srand(time(NULL));
            effect = rand() % 3 + 1;
        }
        m_sleepTransitionDone = false;
        emit triggerSleepTransition(effect);

        m_glibTimer->stop();
        while (!m_sleepTransitionDone) {
            tqApp->processEvents();
            usleep(10000);
        }
        m_glibTimer->start(50);
        setScreenDpms(false);
    }
}

bool InactivityManager::x11EventFilter(XEvent *event) {
    if (event->type == KeyPress) {
        unsigned int code = event->xkey.keycode;
        if (m_keycodes[0] != 0 && code == m_keycodes[0]) {
            handleSleepButton();
            return true;
        } else if (m_keycodes[1] != 0 && code == m_keycodes[1]) {
            handlePowerButton();
            return true;
        }
    }
    return false;
}
