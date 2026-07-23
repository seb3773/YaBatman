#include <tqapplication.h>
#include <tqstring.h>
#include <stdio.h>
#include <stdlib.h>
#include "screensavers.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s <clock|matrix|pipes|plasma|slideshow|starfield|random> [slideshow_dir]\n", argv[0]);
        return 1;
    }

    TQString type = argv[1];
    TQString slideshowDir = "";
    if (argc >= 3) {
        slideshowDir = argv[2];
    }

    TQApplication app(argc, argv);

    ScreensaverWidget *ss = new ScreensaverWidget(type, slideshowDir, NULL);
    TQObject::connect(ss, TQT_SIGNAL(userActivityDetected()), &app, TQT_SLOT(quit()));
    ss->showFullScreen();

    return app.exec();
}
