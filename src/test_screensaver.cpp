#include <tqapplication.h>
#include <tqstring.h>
#include <stdio.h>
#include <stdlib.h>
#include "screensavers.h"
#include "tde_screensaver_helper.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <clock|matrix|pipes|plasma|slideshow|starfield|random|tde:<binary>|--list> [slideshow_dir]\n", argv[0]);
        return 1;
    }

    if (TQString(argv[1]) == "--list") {
        TQApplication app(argc, argv, false);
        TQValueList<TDEScreensaverInfo> list = TDEScreensavers::getAvailableScreensavers();
        printf("Discovered %d TDE screensavers:\n", list.count());
        for (TQValueList<TDEScreensaverInfo>::ConstIterator it = list.begin(); it != list.end(); ++it) {
            printf("  - ID: %s | Name: %s | Setup: %s | Path: %s\n",
                   (*it).id.latin1(),
                   (*it).name.utf8().data(),
                   (*it).hasSetup ? "yes" : "no",
                   (*it).fullPath.latin1());
        }
        return 0;
    }

    TQString type = argv[1];
    TQString slideshowDir = "";
    int timeoutMs = 0;
    for (int i = 1; i < argc; ++i) {
        if (TQString(argv[i]) == "--timeout" && i + 1 < argc) {
            timeoutMs = atoi(argv[i + 1]);
        }
    }
    if (argc >= 3 && TQString(argv[2]) != "--timeout") {
        slideshowDir = argv[2];
    }

    TQApplication app(argc, argv);

    ScreensaverWidget *ss = new ScreensaverWidget(type, slideshowDir, NULL);
    TQObject::connect(ss, TQT_SIGNAL(userActivityDetected()), &app, TQT_SLOT(quit()));
    if (timeoutMs > 0) {
        TQTimer::singleShot(timeoutMs, &app, TQT_SLOT(quit()));
    }
    ss->showFullScreen();

    int res = app.exec();
    delete ss;
    return res;
}
