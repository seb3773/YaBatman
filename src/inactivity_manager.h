#ifndef INACTIVITY_MANAGER_H
#define INACTIVITY_MANAGER_H

#include <tqobject.h>
#include <tqtimer.h>
#include <tqstring.h>
#include <tqvaluelist.h>
#include "config_manager.h"
#include "battery_logger.h"
#include "yabatman_utils.h"

#ifdef PURE_TQT3
class DCOPObject {
public:
    DCOPObject() {}
    DCOPObject(const char *) {}
    virtual ~DCOPObject() {}
};
#else
#include <dcopobject.h>
class TDEPowersaveDcopCompat;
#endif

// X11 Forward Declarations
typedef struct _XDisplay Display;

struct udev;
struct udev_monitor;
class TQSocketNotifier;
class TQThread;

struct BatteryDevice {
    TQString name;         // e.g. "BAT0"
    TQString path;         // e.g. "/sys/class/power_supply/BAT0"
    TQString vendor;       // manufacturer
    TQString model;        // model_name
    TQString serial;       // serial_number
    TQString technology;   // technology
    TQString status;       // "Charging", "Discharging", "Full", "Not charging"
    int percentage;        // 0 - 100
    int energyNow;         // uWh or uAh
    int energyFull;        // uWh or uAh
    int energyDesign;      // uWh or uAh
    bool isEnergy;         // true if uWh, false if uAh
    int powerNow;          // uW or uA
    int voltageNow;        // uV
    int voltageMin;        // uV
    int cycleCount;
    int state;             // 0 = discharging, 1 = charging, 2 = full
};

class InactivityManager : public TQObject, public DCOPObject {
    TQ_OBJECT
public:
    InactivityManager(ConfigManager *configManager, YabatmanConfig *config, BatteryLogger *batteryLogger, TQObject *parent = 0);
    ~InactivityManager();

    void start();
    void stop();
    void suspendIdle();
    void resumeIdle();
    void updateTimeouts();
    void setBrightness(int val);
    int getBrightness();

    bool hasControllableBacklight() const { return m_backlightFd >= 0; }

    TQString getBatteryPath() const { return m_batteryPath; }

    // Multi-battery support
    bool hasBattery() const { return !m_batteries.isEmpty(); }
    int getBatteryCount() const { return m_batteries.count(); }
    const TQValueList<BatteryDevice>& getBatteries() const { return m_batteries; }
    const BatteryDevice* getBattery(int index) const {
        if (index >= 0 && index < (int)m_batteries.count()) {
            return &m_batteries[index];
        }
        return NULL;
    }

    // Sleep inhibitor process check
    bool hasRunningSleepInhibitors();

    // Check states
    bool isMediaPlaying() const { return m_mediaPlaying; }
    bool isPresentationMode() const { return m_presentationMode; }
    void setPresentationMode(bool enable);

    bool isPowernapSelected() const { return (m_config && m_config->ac_lid_enable_powernap) ? m_powernapEnabled : false; }
    void setPowernapSelected(bool selected) {
        if (selected && m_config && !m_config->ac_lid_enable_powernap) return;
        m_powernapEnabled = selected;
    }

    BatteryLogger* getBatteryLogger() const { return m_batteryLogger; }
    ConfigManager* getConfigManager() const { return m_configManager; }
    YabatmanConfig* getConfig() const { return m_config; }

    // Current battery states
    int getBatteryPercentage() const { return m_batteryPercentage; }
    int getChargingState() const { return m_chargingState; } // 0 = discharging, 1 = charging, 2 = full
    int getPowerProfile() const { return m_powerProfile; }
    double getCurrentRate() const { return m_currentRate; }

    void setProfile(int profile);
    void sendNotification(const TQString &title, const TQString &message, bool critical);
    void callDaemon(const TQString &cmd);
    void syncDaemonAdvancedSettings();
    void adjustProfile();
    int getRfkillState(const char *target);
    void setRfkillState(const char *target, int state);
    void runCriticalAction();

    // System Power Commands (public for UI & DCOP access)
    void suspendSystem();
    void suspendThenHibernate();
    void hibernateSystem();
    void hybridSuspendSystem();
    void lockScreen(bool useOverlay = false);
    void lockScreenNow();

#ifndef PURE_TQT3
    // DCOPObject interface
    virtual bool process(const TQCString &fun, const TQByteArray &data, TQCString &replyType, TQByteArray &replyData);
    virtual QCStringList functions();
#endif

signals:
    void batteryStatusChanged(int percentage, int chargingState);
    void presentationModeChanged(bool active);
    void mediaPlayingChanged(bool playing);
    void triggerTvOffEffect();
    void triggerCircleWipeEffect();
    void triggerFadeOutEffect();
    void brightnessChanged(int percent);
    void powerProfileChanged(int profile);
    void triggerScreensaver();
    void wokeUp();
    void triggerSleepTransition(int effect);
    void blackoutScreensaver(bool enable);
    void dismissPopups();

public slots:
    void forceCheck();
    void onLidClosed(bool closed);
    void onResume();
    void executePendingAction(int action);
    void setSleepTransitionDone(bool done);
    void afterWakeActions();

private slots:
    void checkIdle();
    void pollMpris();
    void updateBrightnessTransition();
    void refreshBatteryIcon();
    void onUdevSocket(int socket);
    void onUdevRefreshTimeout();
    void triggerDelayedNotification();
    void checkLidState();
    void restoreBluetooth();
    void iterateGlib();
    void checkWifiAndLockRetry();
    void hideWifiOverlaySlot();
    void reacquireDelayInhibitor();

private:
    void checkBatteryStatus(bool force = false);
    void setupUdevMonitor();
    void teardownUdevMonitor();

    void setScreenDpms(bool enable);
    void disableDpms();

    void setBrightnessImmediate(int rawBrightness, bool forceImmediate);
    void startBrightnessTransition(int targetRaw, bool fast);
    int getBrightnessRaw() const;
    void setBrightnessRaw(int raw);
    void readCurrentBrightness();

    // Config & Logger pointers
    ConfigManager *m_configManager;
    YabatmanConfig *m_config;
    BatteryLogger *m_batteryLogger;

    // Timers
    TQTimer *m_idleTimer;
    TQTimer *m_mprisTimer;
    TQTimer *m_transitionTimer;
    TQTimer *m_batteryTimer;
    TQTimer *m_udevRefreshTimer;

    // Inactivity State
    Display *m_x11Display;
    unsigned long m_prevIdleTime;
    bool m_backlightReduced;
    bool m_screenSleeping;
    bool m_screensaverActive;
    int m_originalBrightness;
    int m_originalBrightnessRaw;
    int m_backlightCurrentRaw;
    int m_backlightFd;
    int m_backlightMax;
    int m_brightnessStep;
    long long m_nextBrightnessIdleCheck;
    time_t m_lastBatteryCheck;

    // Brightness transition (matches yabatman.c)
    enum {
        TRANSITION_STEPS = 40,
        TRANSITION_INTERVAL_MS = 25
    };
    int m_transitionStartRaw;
    int m_transitionTargetRaw;
    int m_transitionStep;
    bool m_transitionInProgress;

    // Battery State
    int m_batteryPercentage;
    int m_chargingState;
    int m_lastBattPercentage;
    bool m_warnSimple;
    bool m_warnCrit;
    int m_criticalLevelReached;
    int m_xLevel;

    // MPRIS & Presentation
    bool m_mediaPlaying;
    int m_mediaPlayingType; // 0 = video, 1 = audio
    bool m_presentationMode;
    bool m_powernapEnabled;
    bool m_inPowernap;

    // RFKill & Power Nap state
    int m_bluetoothInitialState;
    int m_wifiInitialState;
    int m_powerProfile;
    int m_lastHwProfile;
    int m_lastOpmode;
    double m_currentRate;
    time_t m_phaseStartTime;
    int m_phaseStartCapacity;
    bool m_warned100Percent;
    bool m_justWokeUp;
    time_t m_wakeTime;

    // Lid & Inhibitors
    bool m_lastLidState;
    int m_inhibitFd;
    int m_inhibitSleepFd;
    int m_inhibitShutdownFd;
    int m_inhibitPowerKeyFd;
    int m_inhibitSuspendKeyFd;
    int m_inhibitHibernateFd;
    TQTimer *m_lidTimer;
    bool m_minimalMode;
    unsigned int m_dbusPrepareSleepSubId;
    unsigned int m_dbusPrepareShutdownSubId;
    unsigned int m_keycodes[2];

    TQString getRemainingDurationStr();
    int checkAuthorizedSsid();
    void startLockTimer(bool useOverlay);
    void checkWifiAndLock();
    void showWifiOverlay(bool show);
    void simulateUserActivity();

    // D-Bus and logind methods
    void setupDbusMonitoring();
    void prepareSuspendGeneral(bool isHibernateOrPoweroff);
    void enterMinimalMode();
    void exitMinimalMode();
    bool readLidState();
    int inhibitPowerSleep(const char *what, const char *who, const char *why, const char *mode);
    void setupInhibitors();
    void releaseInhibitors();

    // Power & Sleep Button Grabbing
    void setupPowerSleepKeys();
    void releasePowerSleepKeys();
    void handleSleepButton();
    void handlePowerButton();
    void runSleepTransitionSync();

public:
    virtual bool x11EventFilter(XEvent *event);

public:
    void onPrepareForSleep(bool aboutToSleep);
    void onPrepareForShutdown(bool aboutToShutdown);

private:
    // Internal flags
    bool m_actionInProgress;
    bool m_calibrationActive;

    // udev power_supply events (charger plug/unplug)
    udev *m_udev;
    udev_monitor *m_udevMon;
    TQSocketNotifier *m_udevNotifier;

    TQt::HANDLE m_mainThread;
    volatile bool m_sleepTransitionDone;
    TQTimer *m_glibTimer;

    TQWidget *m_wifiOverlay;
    bool m_useWifiOverlay;
    int m_wifiCheckCounter;

    // Shared GDBusConnection and XScreenSaverInfo pointers (cast internally to avoid glib/xss header pollution)
    void *m_systemBus;
    void *m_xssInfo;
    TQString m_batteryPath;
    TQValueList<BatteryDevice> m_batteries;
#ifndef PURE_TQT3
    TDEPowersaveDcopCompat *m_tdepowersaveCompat;
    TDEPowersaveDcopCompat *m_tdepowersaveIfaceCompat;
#endif
};

#endif // INACTIVITY_MANAGER_H
