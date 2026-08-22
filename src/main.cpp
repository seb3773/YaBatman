#ifdef PURE_TQT3
#include <tqapplication.h>
#else
#include <tdeapplication.h>
#include <tdecmdlineargs.h>
#include <tdeaboutdata.h>
#endif
#include <tqobject.h>
#include <ntqmessagebox.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <tqdialog.h>
#include <tqvaluelist.h>
#include <tqwidgetlist.h>
#include "config_manager.h"
#include "battery_logger.h"
#include "inactivity_manager.h"
#include "calibration_manager.h"
#include "yabatman_gui.h"
#include "screensavers.h"

static bool checkDaemonRunning() {
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return false;

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/run/yabatmand/daemon.sock", sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return false;
    }

    write(sock, "ping\n", 5);
    char buf[16] = {0};
    ssize_t n = read(sock, buf, sizeof(buf) - 1);
    close(sock);

    if (n > 0 && ::atoi(buf) == 1) {
        return true;
    }
    return false;
}

// Main Application Controller Class
class YabatmanApp : public TQObject {
    TQ_OBJECT
public:
    YabatmanApp(TQApplication *app) {
        m_app = app;
        m_screensaverWidget = NULL;
        m_transitionOverlay = NULL;
        
        if (!checkDaemonRunning()) {
            TQMessageBox::warning(NULL, "YaBatman Warning",
                "Can't connect to yabatmand daemon.\n"
                "Power functions won't be available and backlight control may be unavailable too.",
                TQMessageBox::Ok, 0, 0);
        }
        m_configManager = new ConfigManager();
        m_config = new YabatmanConfig();
        m_configManager->load(*m_config);

        m_logger = new BatteryLogger();
        m_logger->load();

        m_inactivity = new InactivityManager(m_configManager, m_config, m_logger, this);
        m_calibration = new CalibrationManager(m_inactivity, m_config, this);
        m_tray = new YabatmanTrayIcon(m_inactivity, m_calibration, m_config, NULL);
        m_tray->show();

        connect(m_inactivity, TQT_SIGNAL(triggerTvOffEffect()), this, TQT_SLOT(runTvOff()));
        connect(m_inactivity, TQT_SIGNAL(triggerCircleWipeEffect()), this, TQT_SLOT(runCircleWipe()));
        connect(m_inactivity, TQT_SIGNAL(triggerFadeOutEffect()), this, TQT_SLOT(runFadeOut()));
        connect(m_inactivity, TQT_SIGNAL(triggerScreensaver()), this, TQT_SLOT(launchScreensaver()));
        connect(m_inactivity, TQT_SIGNAL(wokeUp()), this, TQT_SLOT(handleWakeUp()));
        connect(m_inactivity, TQT_SIGNAL(blackoutScreensaver(bool)), this, TQT_SLOT(handleBlackoutScreensaver(bool)));
        connect(m_inactivity, TQT_SIGNAL(triggerSleepTransition(int)), this, TQT_SLOT(runSleepTransition(int)));

        m_inactivity->start();
    }

    ~YabatmanApp() {
        if (m_screensaverWidget) delete m_screensaverWidget;
        if (m_transitionOverlay) delete m_transitionOverlay;
        delete m_tray;
        delete m_calibration;
        delete m_inactivity;
        delete m_logger;
        delete m_config;
        delete m_configManager;
    }

private slots:
    void runTvOff() {
        if (m_transitionOverlay) delete m_transitionOverlay;
        m_transitionOverlay = new TransitionOverlay(1); // TV turn-off
        connect(m_transitionOverlay, TQT_SIGNAL(destroyed()), this, TQT_SLOT(onTransitionDestroyed()));
        connect(m_transitionOverlay, TQT_SIGNAL(transitionComplete()), this, TQT_SLOT(onTransitionComplete()));
        m_transitionOverlay->show();
    }

    void runCircleWipe() {
        if (m_transitionOverlay) delete m_transitionOverlay;
        m_transitionOverlay = new TransitionOverlay(2); // Circle wipe
        connect(m_transitionOverlay, TQT_SIGNAL(destroyed()), this, TQT_SLOT(onTransitionDestroyed()));
        connect(m_transitionOverlay, TQT_SIGNAL(transitionComplete()), this, TQT_SLOT(onTransitionComplete()));
        m_transitionOverlay->show();
    }

    void runFadeOut() {
        if (m_transitionOverlay) delete m_transitionOverlay;
        m_transitionOverlay = new TransitionOverlay(0); // Fade out
        connect(m_transitionOverlay, TQT_SIGNAL(destroyed()), this, TQT_SLOT(onTransitionDestroyed()));
        connect(m_transitionOverlay, TQT_SIGNAL(transitionComplete()), this, TQT_SLOT(onTransitionComplete()));
        m_transitionOverlay->show();
    }

    void runSleepTransition(int effect) {
        int mode;
        if (effect == 1) mode = 1;       // TV turn-off
        else if (effect == 2) mode = 2;  // Circle wipe
        else mode = 0;                   // Fade out

        dismissAllDialogs();

        // Clean up any existing overlay/screensaver first
        if (m_screensaverWidget) {
            m_screensaverWidget->hide();
            delete m_screensaverWidget;
            m_screensaverWidget = NULL;
        }
        if (m_transitionOverlay) {
            delete m_transitionOverlay;
            m_transitionOverlay = NULL;
        }

        // Create transition overlay
        m_transitionOverlay = new TransitionOverlay(mode);
        connect(m_transitionOverlay, TQT_SIGNAL(transitionComplete()), this, TQT_SLOT(onSleepTransitionComplete()));
        m_transitionOverlay->show();
    }

    void onSleepTransitionComplete() {
        m_inactivity->setSleepTransitionDone(true);
    }

    // Unified transition complete handler for screensaver transitions
    void onTransitionComplete() {
        // This transition was for screensaver activation
        if (m_transitionOverlay) {
            m_transitionOverlay->hide();
            delete m_transitionOverlay;
            m_transitionOverlay = NULL;
        }
        launchScreensaver();
    }

    void launchScreensaver() {
        if (m_config->ac_screensaver != "none") {
            dismissAllDialogs();

            if (m_screensaverWidget) delete m_screensaverWidget;
            // Instantiate screensaver widget
            m_screensaverWidget = new ScreensaverWidget(m_config->ac_screensaver, m_config->slideshow_image_dir, m_config->slideshow_random_order, m_config->slideshow_zoom_effect);
            connect(m_screensaverWidget, TQT_SIGNAL(destroyed()), this, TQT_SLOT(onScreensaverDestroyed()));
            connect(m_screensaverWidget, TQT_SIGNAL(userActivityDetected()), m_screensaverWidget, TQT_SLOT(close()));
            connect(m_screensaverWidget, TQT_SIGNAL(userActivityDetected()), m_inactivity, TQT_SLOT(onResume()));
            m_screensaverWidget->showFullScreen();
        }
    }

    void handleWakeUp() {
        // Silently destroy overlays - screen is still DPMS off at this point
        if (m_transitionOverlay) {
            m_transitionOverlay->hide();
            delete m_transitionOverlay;
            m_transitionOverlay = NULL;
        }
        if (m_screensaverWidget) {
            m_screensaverWidget->hide();
            delete m_screensaverWidget;
            m_screensaverWidget = NULL;
        }
    }

    void handleBlackoutScreensaver(bool enable) {
        if (m_screensaverWidget) {
            m_screensaverWidget->setBlackout(enable);
        }
    }

    void onScreensaverDestroyed() {
        m_screensaverWidget = NULL;
    }

    void onTransitionDestroyed() {
        m_transitionOverlay = NULL;
    }

    void dismissAllDialogs() {
        TQWidgetList *list = TQApplication::topLevelWidgets();
        if (list) {
            TQValueList<TQWidget*> dialogs;
            TQPtrListIterator<TQWidget> it(*list);
            TQWidget *w;
            while ((w = it.current()) != 0) {
                ++it;
                if (w->inherits("TQDialog")) {
                    dialogs.append(w);
                }
            }
            for (TQValueList<TQWidget*>::Iterator dit = dialogs.begin(); dit != dialogs.end(); ++dit) {
                TQWidgetList *currentList = TQApplication::topLevelWidgets();
                if (currentList) {
                    if (currentList->findRef(*dit) != -1) {
                        (*dit)->close();
                    }
                    delete currentList;
                }
            }
            delete list;
        }
    }

private:
    TQApplication *m_app;
    ConfigManager *m_configManager;
    YabatmanConfig *m_config;
    BatteryLogger *m_logger;
    InactivityManager *m_inactivity;
    CalibrationManager *m_calibration;
    YabatmanTrayIcon *m_tray;
    ScreensaverWidget *m_screensaverWidget;
    TransitionOverlay *m_transitionOverlay;
};

// Global termination handling
static void sigHandler(int sig) {
    tqApp->quit();
}


int main(int argc, char **argv) {
    // Setup signal handlers for graceful exit
    signal(SIGINT, sigHandler);
    signal(SIGTERM, sigHandler);

    bool nofork = false;
    int clean_argc = 0;
    char **clean_argv = new char*[argc];
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--nofork") == 0) {
            nofork = true;
        } else {
            clean_argv[clean_argc++] = argv[i];
        }
    }

    if (!nofork) {
        // Fork to background BEFORE initializing X11 connection
        if (fork() != 0) {
            delete[] clean_argv;
            return 0;
        }
    }

#ifdef PURE_TQT3
    TQApplication app(clean_argc, clean_argv);
#else
    TDEAboutData about("yabatman", "YaBatman", "1.1",
                      "YaBatman Battery and Energy Manager for TDE",
                      TDEAboutData::License_GPL,
                      "(c) 2026 seb3773");

    TDECmdLineArgs::init(clean_argc, clean_argv, &about);

    TDEApplication app;
#endif

    delete[] clean_argv;

    YabatmanApp controller(&app);

    return app.exec();
}

#include "main.moc"
