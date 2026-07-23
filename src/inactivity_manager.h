#ifndef INACTIVITY_MANAGER_H
#define INACTIVITY_MANAGER_H

#include <tqobject.h>
#include <tqtimer.h>
#include <tqstring.h>
#include "config_manager.h"
#include "battery_logger.h"
#include "yabatman_utils.h"

// X11 Forward Declarations
typedef struct _XDisplay Display;

struct udev;
struct udev_monitor;
class TQSocketNotifier;
class TQThread;

class InactivityManager : public TQObject {
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

    // Check states
    bool isMediaPlaying() const { return m_mediaPlaying; }
    bool isPresentationMode() const { return m_presentationMode; }
    void setPresentationMode(bool enable);

    bool isPowernapSelected() const { return m_powernapEnabled; }
    void setPowernapSelected(bool selected) { m_powernapEnabled = selected; }

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
    void adjustProfile();
    int getRfkillState(const char *target);
    void setRfkillState(const char *target, int state);
    void runCriticalAction();

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
    void lockScreenNow();
    void checkWifiAndLockRetry();
    void hideWifiOverlaySlot();

private:
    void checkBatteryStatus(bool force = false);
    void setupUdevMonitor();
    void teardownUdevMonitor();

    // System Power Commands
    void suspendSystem();
    void suspendThenHibernate();
    void hibernateSystem();
    void hybridSuspendSystem();
    void lockScreen(bool useOverlay = false);
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
};

#endif // INACTIVITY_MANAGER_H
