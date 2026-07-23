#ifndef SCREENSAVERS_H
#define SCREENSAVERS_H

#include <tqwidget.h>
#include <tqtimer.h>
#include <tqpixmap.h>
#include <tqimage.h>
#include <tqstringlist.h>
#include <tqvaluelist.h>

class ScreensaverWidget : public TQWidget {
    TQ_OBJECT
public:
    ScreensaverWidget(const TQString& type, const TQString& slideshowDir, bool randomOrder = false, bool zoomEffect = true, TQWidget *parent = 0);
    ~ScreensaverWidget();
    void setBlackout(bool enable);

signals:
    void userActivityDetected();

protected:
    virtual void paintEvent(TQPaintEvent *e);
    virtual void keyPressEvent(TQKeyEvent *e);
    virtual void mousePressEvent(TQMouseEvent *e);
    virtual void mouseMoveEvent(TQMouseEvent *e);
    virtual bool eventFilter(TQObject *obj, TQEvent *e);

private slots:
    void updateAnimation();

private:
    // Initialize screensavers
    void initClock();
    void initAnalogClock();
    void initMatrix();
    void initPipes();
    void initPlasma();
    void initSlideshow();
    void initStarfield();

    // Paint screensavers
    void paintClock(TQPainter& p);
    void paintAnalogClock(TQPainter& p);
    void paintMatrix(TQPainter& p);
    void paintPipes(TQPainter& p);
    void paintPlasma(TQPainter& p);
    void paintSlideshow(TQPainter& p);
    void paintStarfield(TQPainter& p);

    TQString m_type;
    TQString m_slideshowDir;
    TQTimer *m_timer;
    int m_frameCount;

    // Screensavers State Variables
    // 1. Clock
    double m_clockX;
    double m_clockY;
    double m_clockVelX;
    double m_clockVelY;
    TQString m_clockStr;

    // 2. Matrix
    int m_matrixCols;
    int m_matrixRows;
    int *m_matrixYArray;
    int m_matrixCharsetLen;

    // 3. Pipes
    struct PipesPoint3D { double x, y, z; };
    struct PipesPoint2D { double x, y; };
    struct PipesPipe {
        PipesPoint3D points3d[601];
        double r, g, b;
        int progress;
    };
    PipesPipe m_pipesArray[8];

    // 4. Plasma
    double m_plasmaTime;
    double m_plasmaScale;
    double m_plasmaSpeed;
    double m_plasmaHueOffset;
    double m_plasmaSaturation;
    double m_plasmaLuminosity;
    double m_sinLut[1024];
    void initSinLut();
    double fastSin(double rad);
    void getPlasmaRgb(int x, int y, double t, double &r, double &g, double &b);

    // 5. Slideshow
    TQStringList m_slideImages;
    int m_slideCurrentIdx;
    int m_slideNextIdx;
    TQImage m_slideCurrentOriginal;
    TQImage m_slideNextOriginal;
    double m_slideFadeAlpha;
    bool m_slideInFade;
    int m_slideFadeStep;
    double m_slideZoomFactor;
    int m_slideZoomElapsedTime;
    void loadSlideImages();
    void freeSlideImages();
    void loadNextSlideImage();

    // 6. Starfield
    struct StarfieldStar {
        double a, r, s, rgb_r, rgb_g, rgb_b;
    };
    StarfieldStar m_starfieldStars[600];
    void initStarfieldStar(int i);
    void updateStarfieldStars();

    void triggerActivity();

    // Activity tracking
    int m_lastMouseX;
    int m_lastMouseY;
    bool m_firstMouseMove;
    bool m_blackout;
    bool m_slideshowRandomOrder;
    bool m_slideshowZoomEffect;
    bool m_activityEmitted;
    int m_interval;
};

class TransitionOverlay : public TQWidget {
    TQ_OBJECT
public:
    TransitionOverlay(int mode, TQWidget *parent = 0); // 0=fade, 1=tv, 2=circle
    ~TransitionOverlay();

signals:
    void transitionComplete();

protected:
    virtual void paintEvent(TQPaintEvent *e);
    virtual void keyPressEvent(TQKeyEvent *e);

private slots:
    void animateStep();

private:
    int m_mode;
    TQPixmap m_screenshot;
    TQImage m_originalImage;
    TQImage m_currentImage;
    TQTimer *m_timer;
    int m_step;
    int m_lastBlendedStep;
};

#endif // SCREENSAVERS_H
