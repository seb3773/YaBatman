#include "screensavers.h"
#include "tqtaapainter.h"
#include <tqpainter.h>
#include <tqapplication.h>
#include <tqdatetime.h>
#include <tqdir.h>
#include <tqimage.h>
#include <tqcursor.h>
#include <math.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ==========================================
// ScreensaverWidget Implementation
// ==========================================
ScreensaverWidget::ScreensaverWidget(const TQString& type, const TQString& slideshowDir, bool randomOrder, bool zoomEffect, TQWidget *parent)
    : TQWidget(parent, "ScreensaverWidget", WStyle_Customize | WStyle_NoBorder | WStyle_StaysOnTop)
{
    m_matrixYArray = NULL;

    m_type = type;
    if (m_type == "random") {
        const char *options[] = { "clock", "analog_clock", "matrix", "pipes", "plasma", "slideshow", "starfield" };
        srand(time(NULL));
        m_type = options[rand() % 7];
    }
    m_slideshowDir = slideshowDir;
    m_slideshowRandomOrder = randomOrder;
    m_slideshowZoomEffect = zoomEffect;
    m_frameCount = 0;
    m_lastMouseX = -1;
    m_lastMouseY = -1;
    m_firstMouseMove = true;
    m_blackout = false;
    m_activityEmitted = false;

    // Cover current active screen space (multi-monitor safe)
    int activeScreen = tqApp->desktop()->screenNumber(TQCursor::pos());
    TQRect screenGeom = tqApp->desktop()->screenGeometry(activeScreen);
    setGeometry(screenGeom);
    setCursor(TQCursor(Qt::BlankCursor)); // hide cursor

    // Set black background initially
    setBackgroundColor(TQColor(0, 0, 0));
    setBackgroundMode(TQt::NoBackground);
    setFocusPolicy(TQWidget::StrongFocus);
    setFocus();
    setMouseTracking(true);

    tqApp->installEventFilter(this);
    grabKeyboard();
    grabMouse();

    // Initialize state depending on type
    if (m_type == "clock") initClock();
    else if (m_type == "analog_clock") initAnalogClock();
    else if (m_type == "matrix") initMatrix();
    else if (m_type == "pipes") initPipes();
    else if (m_type == "plasma") initPlasma();
    else if (m_type == "slideshow") initSlideshow();
    else if (m_type == "starfield") initStarfield();
    else initStarfield(); // Default fallback

    m_timer = new TQTimer(this);
    connect(m_timer, TQT_SIGNAL(timeout()), this, TQT_SLOT(updateAnimation()));
    
    // Set appropriate frame rates:
    // Clock: 60ms timer
    // Analog Clock: 16ms timer (60 FPS for sweep second hand)
    // Matrix: 30ms timer
    // Pipes: 30ms timer
    // Plasma: 30ms timer
    // Slideshow: 30ms timer
    // Starfield: 16ms timer (60 FPS)
    int interval = 33;
    if (m_type == "clock") interval = 60;
    else if (m_type == "analog_clock") interval = 16;
    else if (m_type == "matrix") interval = 30;
    else if (m_type == "pipes") interval = 30;
    else if (m_type == "plasma") interval = 30;
    else if (m_type == "slideshow") interval = 30;
    else if (m_type == "starfield") interval = 16;
    m_interval = interval;
    m_timer->start(interval);
}

ScreensaverWidget::~ScreensaverWidget() {
    tqApp->removeEventFilter(this);
    releaseKeyboard();
    releaseMouse();
    delete m_timer;
    if (m_matrixYArray) {
        free(m_matrixYArray);
    }
    freeSlideImages();
}

void ScreensaverWidget::setBlackout(bool enable) {
    m_blackout = enable;
    if (m_blackout) {
        m_timer->stop();
        update(); // Paint solid black immediately
    } else {
        m_timer->start(m_interval);
        update();
    }
}

void ScreensaverWidget::updateAnimation() {
    m_frameCount++;
    if (width() <= 0 || height() <= 0) return; // Guard: no valid geometry yet

    if (m_type == "clock") {
        TQTime t = TQTime::currentTime();
        m_clockStr = t.toString("hh:mm:ss");

        int fontHeight = (height() / 5) * 2 / 3;
        if (fontHeight <= 0) fontHeight = 20;
        TQFont font("Sans", fontHeight, TQFont::Bold);
        TQFontMetrics fm(font);

        TQRect capExt = fm.boundingRect("00:00:00");
        int boxH = fm.ascent() + fm.descent();
        int boxW = fm.width("00:00:00");
        if (boxH <= 0) boxH = 100;
        if (boxW <= 0) boxW = 400;

        int vMargin = (boxH - capExt.height()) / 2;
        int hMargin = (boxW - capExt.width()) / 2;

        m_clockX += m_clockVelX;
        m_clockY += m_clockVelY;

        double left_ink = m_clockX + hMargin;
        double right_ink = m_clockX + boxW - hMargin;
        double top_ink = m_clockY + vMargin;
        double bottom_ink = m_clockY + boxH - vMargin;

        if (left_ink < 0) {
            m_clockX = -hMargin;
            m_clockVelX = fabs(m_clockVelX);
        }
        if (right_ink > width()) {
            m_clockX = width() - boxW + hMargin;
            m_clockVelX = -fabs(m_clockVelX);
        }
        if (top_ink < 0) {
            m_clockY = -vMargin;
            m_clockVelY = fabs(m_clockVelY);
        }
        if (bottom_ink > height()) {
            m_clockY = height() - boxH + vMargin;
            m_clockVelY = -fabs(m_clockVelY);
        }
    }
    else if (m_type == "analog_clock") {
        // Time and burn-in orbit offset are computed dynamically in paintAnalogClock
    }
    else if (m_type == "matrix") {
        if (m_matrixYArray && m_matrixCols > 0 && m_matrixRows > 0) {
            // Check if widget was resized - reallocate array if needed
            int newCols = width() / 22;
            int newRows = height() / 22;
            if (newCols <= 0) newCols = 80;
            if (newRows <= 0) newRows = 30;
            if (newCols != m_matrixCols || newRows != m_matrixRows) {
                free(m_matrixYArray);
                m_matrixCols = newCols;
                m_matrixRows = newRows;
                m_matrixYArray = (int*)malloc(m_matrixCols * sizeof(int));
                if (m_matrixYArray) {
                    for (int i = 0; i < m_matrixCols; i++)
                        m_matrixYArray[i] = rand() % m_matrixRows;
                }
            }
            if (m_matrixYArray) {
                for (int x = 0; x < m_matrixCols; ++x) {
                    if (rand() % 5 == 0) {
                        m_matrixYArray[x] = (m_matrixYArray[x] + 1) % m_matrixRows;
                    }
                }
            }
        }
    }
    else if (m_type == "pipes") {
        bool all_finished = true;
        for (int i = 0; i < 8; ++i) {
            if (m_pipesArray[i].progress < 599) { // Cap at 599 to stay within points3d[601] bounds
                m_pipesArray[i].progress++;
                all_finished = false;
            }
        }
        if (all_finished) {
            initPipes();
        }
    }
    else if (m_type == "plasma") {
        m_plasmaTime += m_plasmaSpeed;
        // Wrap to prevent float precision loss after many hours
        if (m_plasmaTime > 1000.0) m_plasmaTime -= 1000.0;
    }
    else if (m_type == "slideshow") {
        if (m_slideImages.count() == 0) { /* no images - nothing to animate */ }
        else if (m_slideInFade) {
            m_slideFadeStep++;
            m_slideFadeAlpha = (double)m_slideFadeStep / 30.0;
            if (m_slideFadeAlpha > 1.0) m_slideFadeAlpha = 1.0;
            if (m_slideFadeStep >= 30) {
                m_slideCurrentOriginal = m_slideNextOriginal;
                m_slideNextOriginal = TQImage();
                m_slideInFade = false;
                m_slideFadeAlpha = 1.0;
                m_slideZoomFactor = 1.0;
                m_slideZoomElapsedTime = 0;
                if (m_slideImages.count() > 0)
                    m_slideCurrentIdx = m_slideNextIdx;
            }
        } else {
            m_slideZoomElapsedTime += 30; // 30ms interval
            double progress = (double)m_slideZoomElapsedTime / 12000.0; // 12s display
            if (progress > 1.0) {
                progress = 1.0;
                loadNextSlideImage();
            }
            m_slideZoomFactor = m_slideshowZoomEffect ? (1.0 + 0.09 * progress) : 1.0;
        }
    }
    else if (m_type == "starfield") {
        updateStarfieldStars();
    }

    update();
}

void ScreensaverWidget::triggerActivity() {
    if (!m_activityEmitted) {
        m_activityEmitted = true;
        tqApp->removeEventFilter(this);
        releaseKeyboard();
        releaseMouse();
        emit userActivityDetected();
    }
}

bool ScreensaverWidget::eventFilter(TQObject *obj, TQEvent *e) {
    if (e && !m_activityEmitted) {
        TQEvent::Type t = e->type();
        if (t == TQEvent::KeyPress || t == TQEvent::MouseButtonPress || t == TQEvent::Wheel) {
            triggerActivity();
            return true;
        } else if (t == TQEvent::MouseMove) {
            TQMouseEvent *me = static_cast<TQMouseEvent*>(e);
            if (m_firstMouseMove) {
                m_lastMouseX = me->globalX();
                m_lastMouseY = me->globalY();
                m_firstMouseMove = false;
            } else {
                if (abs(me->globalX() - m_lastMouseX) > 2 || abs(me->globalY() - m_lastMouseY) > 2) {
                    triggerActivity();
                    return true;
                }
            }
        }
    }
    return TQWidget::eventFilter(obj, e);
}

void ScreensaverWidget::keyPressEvent(TQKeyEvent *e) {
    triggerActivity();
}

void ScreensaverWidget::mousePressEvent(TQMouseEvent *e) {
    triggerActivity();
}

void ScreensaverWidget::mouseMoveEvent(TQMouseEvent *e) {
    if (m_firstMouseMove) {
        m_lastMouseX = e->globalX();
        m_lastMouseY = e->globalY();
        m_firstMouseMove = false;
        return;
    }
    if (abs(e->globalX() - m_lastMouseX) > 2 || abs(e->globalY() - m_lastMouseY) > 2) {
        triggerActivity();
    }
}

void ScreensaverWidget::paintEvent(TQPaintEvent *e) {
    if (width() <= 0 || height() <= 0) return;

    if (m_blackout) {
        TQPainter screenPainter(this);
        screenPainter.fillRect(rect(), TQColor(0, 0, 0));
        return;
    }

    TQPixmap buffer(width(), height());
    TQPainter p(&buffer);
    p.fillRect(buffer.rect(), TQColor(0, 0, 0)); // fill black

    if (m_type == "clock") paintClock(p);
    else if (m_type == "analog_clock") paintAnalogClock(p);
    else if (m_type == "matrix") paintMatrix(p);
    else if (m_type == "pipes") paintPipes(p);
    else if (m_type == "plasma") paintPlasma(p);
    else if (m_type == "slideshow") paintSlideshow(p);
    else if (m_type == "starfield") paintStarfield(p);

    p.end();

    TQPainter screenPainter(this);
    screenPainter.drawPixmap(0, 0, buffer);
}

// 1. Clock (Digital)
void ScreensaverWidget::initClock() {
    m_clockX = 200.0;
    m_clockY = 400.0;
    m_clockVelX = 2.0;
    m_clockVelY = 1.0;
    m_clockStr = "";
}

void ScreensaverWidget::paintClock(TQPainter& p) {
    int fontHeight = (height() / 5) * 2 / 3;
    if (fontHeight <= 0) fontHeight = 20;
    TQFont font("Sans", fontHeight, TQFont::Bold);

    TQFontMetrics fm(font);
    int boxH = fm.ascent() + fm.descent();
    int boxW = fm.width("00:00:00");
    if (boxH <= 0) boxH = 100;
    if (boxW <= 0) boxW = 400;

    // Create the bitmap box pixmap
    TQPixmap clockBox(boxW, boxH);
    clockBox.fill(TQColor(0, 0, 0)); // fill black background

    TQPainter boxPainter(&clockBox);
    boxPainter.setFont(font);
    boxPainter.setPen(TQColor(255, 255, 255));

    // Draw the clock string perfectly centered inside the bitmap box
    boxPainter.drawText(clockBox.rect(), TQt::AlignCenter, m_clockStr);
    boxPainter.end();

    // Blit the bitmap box to the screen at its animated coordinates
    p.drawPixmap((int)m_clockX, (int)m_clockY, clockBox);
}

// 1b. Analog Clock
void ScreensaverWidget::initAnalogClock() {
    m_clockX = 0;
    m_clockY = 0;
    m_clockVelX = 0;
    m_clockVelY = 0;
    m_clockStr = "";
}

void ScreensaverWidget::paintAnalogClock(TQPainter& p) {
    if (width() <= 0 || height() <= 0) return;

    int w = width();
    int h = height();

    TQImage img(w, h, 32);
    img.fill(0xff000000); // Black background

    // Center and radius
    // Subtle burn-in protection orbit (radius 15px, period ~10 minutes at 60 FPS)
    double orbitAngle = (m_frameCount % 36000) * (2.0 * M_PI / 36000.0);
    double offsetX = 15.0 * cos(orbitAngle);
    double offsetY = 15.0 * sin(orbitAngle);
    double cx = w / 2.0 + offsetX;
    double cy = h / 2.0 + offsetY;

    double r = (w < h ? w : h) * 0.40;
    if (r < 50) r = 50;

    // 1. Draw bezel and dial rims
    // Outer grey ring
    TQtAAPainter::drawCircleAA(&img, (int)cx, (int)cy, (int)r, TQColor(60, 60, 60), 0);
    // Inner silver ring
    TQtAAPainter::drawCircleAA(&img, (int)cx, (int)cy, (int)(r - 8), TQColor(160, 160, 160), 0);

    // 2. Draw tick marks
    // 12 hour ticks
    for (int i = 0; i < 12; i++) {
        double angle = i * (2.0 * M_PI / 12.0) - M_PI_2;
        double cosA = cos(angle);
        double sinA = sin(angle);
        int xStart = (int)(cx + (r - 22) * cosA);
        int yStart = (int)(cy + (r - 22) * sinA);
        int xEnd = (int)(cx + (r - 10) * cosA);
        int yEnd = (int)(cy + (r - 10) * sinA);
        TQtAAPainter::drawLineAA(&img, xStart, yStart, xEnd, yEnd, TQColor(230, 230, 230), 4);
    }

    // 60 minute ticks
    for (int j = 0; j < 60; j++) {
        if (j % 5 == 0) continue; // Skip hour ticks
        double angle = j * (2.0 * M_PI / 60.0) - M_PI_2;
        double cosA = cos(angle);
        double sinA = sin(angle);
        int xStart = (int)(cx + (r - 16) * cosA);
        int yStart = (int)(cy + (r - 16) * sinA);
        int xEnd = (int)(cx + (r - 10) * cosA);
        int yEnd = (int)(cy + (r - 10) * sinA);
        TQtAAPainter::drawLineAA(&img, xStart, yStart, xEnd, yEnd, TQColor(100, 100, 100), 2);
    }

    // 3. Get current time (high precision for sweep second hand)
    TQTime time = TQTime::currentTime();
    double ms = time.msec() / 1000.0;
    double sec = time.second() + ms;
    double min = time.minute() + sec / 60.0;
    double hour = (time.hour() % 12) + min / 60.0;

    double hourAngle = hour * (2.0 * M_PI / 12.0) - M_PI_2;
    double minAngle = min * (2.0 * M_PI / 60.0) - M_PI_2;
    double secAngle = sec * (2.0 * M_PI / 60.0) - M_PI_2;

    // 4. Draw hands
    // Hour hand (thick, white)
    {
        int xStart = (int)(cx - (r * 0.08) * cos(hourAngle));
        int yStart = (int)(cy - (r * 0.08) * sin(hourAngle));
        int xEnd = (int)(cx + (r * 0.50) * cos(hourAngle));
        int yEnd = (int)(cy + (r * 0.50) * sin(hourAngle));
        TQtAAPainter::drawLineAA(&img, xStart, yStart, xEnd, yEnd, TQColor(240, 240, 240), 8);
    }

    // Minute hand (medium, light silver)
    {
        int xStart = (int)(cx - (r * 0.10) * cos(minAngle));
        int yStart = (int)(cy - (r * 0.10) * sin(minAngle));
        int xEnd = (int)(cx + (r * 0.75) * cos(minAngle));
        int yEnd = (int)(cy + (r * 0.75) * sin(minAngle));
        TQtAAPainter::drawLineAA(&img, xStart, yStart, xEnd, yEnd, TQColor(200, 200, 200), 5);
    }

    // Second hand (thin, red sweep)
    {
        int xStart = (int)(cx - (r * 0.18) * cos(secAngle));
        int yStart = (int)(cy - (r * 0.18) * sin(secAngle));
        int xEnd = (int)(cx + (r * 0.85) * cos(secAngle));
        int yEnd = (int)(cy + (r * 0.85) * sin(secAngle));
        TQtAAPainter::drawLineAA(&img, xStart, yStart, xEnd, yEnd, TQColor(255, 60, 60), 2);
    }

    // 5. Center Pin (silver base + red cap)
    TQtAAPainter::fillCircleAA(&img, (int)cx, (int)cy, 10, TQColor(160, 160, 160));
    TQtAAPainter::fillCircleAA(&img, (int)cx, (int)cy, 4, TQColor(255, 60, 60));

    // Blit background and hands to widget
    p.drawImage(0, 0, img);

    // 6. Draw hour numbers (12, 3, 6, 9) on top using standard antialiased font
    int fontSize = (int)(r * 0.15);
    if (fontSize < 12) fontSize = 12;
    TQFont numFont("Sans", fontSize, TQFont::Bold);
    p.setFont(numFont);
    p.setPen(TQColor(220, 220, 220));

    TQFontMetrics fm(numFont);
    double dist = r - 48;
    if (dist < 20) dist = 20;

    // "12"
    {
        TQString str = "12";
        int tw = fm.width(str);
        int th = fm.ascent();
        p.drawText((int)(cx - tw / 2), (int)(cy - dist + th / 2), str);
    }
    // "3"
    {
        TQString str = "3";
        int tw = fm.width(str);
        int th = fm.ascent();
        p.drawText((int)(cx + dist - tw / 2), (int)(cy + th / 2 - fm.descent() / 2), str);
    }
    // "6"
    {
        TQString str = "6";
        int tw = fm.width(str);
        int th = fm.ascent();
        p.drawText((int)(cx - tw / 2), (int)(cy + dist + th / 2 - fm.descent() / 2), str);
    }
    // "9"
    {
        TQString str = "9";
        int tw = fm.width(str);
        int th = fm.ascent();
        p.drawText((int)(cx - dist - tw / 2), (int)(cy + th / 2 - fm.descent() / 2), str);
    }
}

// 2. Matrix
void ScreensaverWidget::initMatrix() {
    m_matrixCols = width() / 22;
    m_matrixRows = height() / 22;
    if (m_matrixCols <= 0) m_matrixCols = 80;
    if (m_matrixRows <= 0) m_matrixRows = 30;

    m_matrixYArray = (int*)malloc(m_matrixCols * sizeof(int));
    srand(time(NULL));
    for (int i = 0; i < m_matrixCols; i++) {
        m_matrixYArray[i] = rand() % m_matrixRows;
    }
    m_matrixCharsetLen = strlen("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@#$%&*");
}

void ScreensaverWidget::paintMatrix(TQPainter& p) {
    if (!m_matrixYArray || m_matrixCols <= 0 || m_matrixRows <= 0) return;
    p.setFont(TQFont("monospace", 22, TQFont::Bold));
    const char *charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@#$%&*";

    for (int x = 0; x < m_matrixCols; x++) {
        int y = m_matrixYArray[x];
        int head_y = y * 22;

        char c = charset[rand() % m_matrixCharsetLen];
        p.setPen(TQColor(178, 255, 178)); // Light green head (0.7, 1.0, 0.7)
        p.drawText(x * 22, head_y + 18, TQString(TQChar(c)));

        for (int k = 1; k < 12; k++) {
            int tail_row = (y - k + m_matrixRows) % m_matrixRows;
            int tail_y = tail_row * 22;
            double fade = 1.0 - k * 0.08;
            if (fade < 0.0) fade = 0.0;
            char t = charset[rand() % m_matrixCharsetLen];
            
            int g = (int)(255 * fade);
            int rb = (int)(51 * fade);
            p.setPen(TQColor(rb, g, rb)); // Fading trail green (0.2, 1.0, 0.2)
            p.drawText(x * 22, tail_y + 18, TQString(TQChar(t)));
        }
    }
}

// 3. Pipes
void ScreensaverWidget::initPipes() {
    int w = width();
    int h = height();
    if (w <= 0) w = 800;
    if (h <= 0) h = 600;
    srand(time(NULL));
    for (int i = 0; i < 8; i++) {
        PipesPipe &pipe = m_pipesArray[i];
        pipe.r = (double)rand() / RAND_MAX;
        pipe.g = (double)rand() / RAND_MAX;
        pipe.b = (double)rand() / RAND_MAX;
        pipe.points3d[0].x = (rand() % w) - w/2;
        pipe.points3d[0].y = (rand() % h) - h/2;
        pipe.points3d[0].z = (rand() % 250) - 125;

        int dir = rand() % 6;
        int dx = 0, dy = 0, dz = 0;
        if (dir == 0) dx = 1;
        else if (dir == 1) dx = -1;
        else if (dir == 2) dy = 1;
        else if (dir == 3) dy = -1;
        else if (dir == 4) dz = 1;
        else dz = -1;

        for (int j = 1; j <= 600; j++) {
            if (rand() % 10 == 0) {
                int newdir = rand() % 6;
                if (newdir == 0)      { dx = 1; dy = dz = 0; }
                else if (newdir == 1) { dx = -1; dy = dz = 0; }
                else if (newdir == 2) { dy = 1; dx = dz = 0; }
                else if (newdir == 3) { dy = -1; dx = dz = 0; }
                else if (newdir == 4) { dz = 1; dx = dy = 0; }
                else                  { dz = -1; dx = dy = 0; }
            }
            double nx = pipe.points3d[j-1].x + 16 * dx;
            double ny = pipe.points3d[j-1].y + 16 * dy;
            double nz = pipe.points3d[j-1].z + 16 * dz;
            if (nx < -w/2 || nx > w/2) dx = -dx;
            if (ny < -h/2 || ny > h/2) dy = -dy;
            if (nz < -200 || nz > 200) dz = -dz;
            nx = pipe.points3d[j-1].x + 16 * dx;
            ny = pipe.points3d[j-1].y + 16 * dy;
            nz = pipe.points3d[j-1].z + 16 * dz;
            pipe.points3d[j].x = nx;
            pipe.points3d[j].y = ny;
            pipe.points3d[j].z = nz;
        }
        pipe.progress = 1;
    }
}

void ScreensaverWidget::paintPipes(TQPainter& p) {
    if (width() <= 0 || height() <= 0) return;

    TQImage img(width(), height(), 32);
    img.fill(0xff000000); // fill black

    int screen_cx = width() / 2;
    int screen_cy = height() / 2;
    double camera_z = 400.0;

    for (int i = 0; i < 8; i++) {
        PipesPipe &pipe = m_pipesArray[i];
        
        double lr = pipe.r + (1.0 - pipe.r) * 0.6;
        double lg = pipe.g + (1.0 - pipe.g) * 0.6;
        double lb = pipe.b + (1.0 - pipe.b) * 0.6;

        PipesPoint2D proj[601];
        for (int j = 0; j <= pipe.progress; j++) {
            double factor = camera_z / (camera_z + pipe.points3d[j].z);
            proj[j].x = screen_cx + pipe.points3d[j].x * factor;
            proj[j].y = screen_cy + pipe.points3d[j].y * factor;
        }

        // Draw shadow/outline AA lines (width 18)
        TQColor outlineColor((int)(lr*255), (int)(lg*255), (int)(lb*255));
        for (int j = 1; j <= pipe.progress; j++) {
            TQtAAPainter::drawLineAA(&img,
                                     (int)(proj[j-1].x - 4), (int)(proj[j-1].y - 4),
                                     (int)(proj[j].x - 4), (int)(proj[j].y - 4),
                                     outlineColor, 18);
        }

        // Draw foreground AA lines (width 8)
        TQColor fgColor((int)(pipe.r*255), (int)(pipe.g*255), (int)(pipe.b*255));
        for (int j = 1; j <= pipe.progress; j++) {
            TQtAAPainter::drawLineAA(&img,
                                     (int)proj[j-1].x, (int)proj[j-1].y,
                                     (int)proj[j].x, (int)proj[j].y,
                                     fgColor, 8);
        }
    }

    p.drawImage(0, 0, img);
}

// 4. Plasma
void ScreensaverWidget::initSinLut() {
    for (int i = 0; i < 1024; ++i) {
        m_sinLut[i] = sin((2.0 * M_PI * i) / 1024.0);
    }
}

double ScreensaverWidget::fastSin(double rad) {
    while (rad < 0) rad += 2.0 * M_PI;
    while (rad >= 2.0 * M_PI) rad -= 2.0 * M_PI;
    int index = (int)((rad / (2.0 * M_PI)) * 1024.0);
    return m_sinLut[index % 1024];
}

void ScreensaverWidget::getPlasmaRgb(int x, int y, double t, double &r, double &g, double &b) {
    double v = fastSin(x * m_plasmaScale + t)
             + fastSin((y * m_plasmaScale + t) / 2.0)
             + fastSin((x * m_plasmaScale + y * m_plasmaScale + t) / 2.0)
             + fastSin(sqrt((double)(x * x + y * y)) * m_plasmaScale / 2.0 + t);
    v = (v + 4.0) / 8.0;
    double hue = fmod(v + t * 0.07 + m_plasmaHueOffset, 1.0);
    double s = m_plasmaSaturation;
    double l = m_plasmaLuminosity;
    double c = (1.0 - fabs(2.0 * l - 1.0)) * s;
    double h = hue * 6.0;
    double xcol = c * (1.0 - fabs(fmod(h, 2.0) - 1.0));
    double m = l - c / 2.0;
    double rr, gg, bb;
    if (h < 1.0)      { rr = c; gg = xcol; bb = 0; }
    else if (h < 2.0) { rr = xcol; gg = c; bb = 0; }
    else if (h < 3.0) { rr = 0; gg = c; bb = xcol; }
    else if (h < 4.0) { rr = 0; gg = xcol; bb = c; }
    else if (h < 5.0) { rr = xcol; gg = 0; bb = c; }
    else              { rr = c; gg = 0; bb = xcol; }
    r = rr + m;
    g = gg + m;
    b = bb + m;
}

void ScreensaverWidget::initPlasma() {
    initSinLut();
    srand(time(NULL));
    m_plasmaScale = 0.01 + ((double)rand() / RAND_MAX) * (0.03 - 0.01);
    m_plasmaSpeed = 0.005 + ((double)rand() / RAND_MAX) * (0.02 - 0.005);
    m_plasmaHueOffset = (double)rand() / RAND_MAX;
    m_plasmaSaturation = 0.7 + ((double)rand() / RAND_MAX) * (1.0 - 0.7);
    m_plasmaLuminosity = 0.4 + ((double)rand() / RAND_MAX) * (0.6 - 0.4);
    m_plasmaTime = 0.0;
}

void ScreensaverWidget::paintPlasma(TQPainter& p) {
    int plasmaW = width() / 4;
    int plasmaH = height() / 4;
    if (plasmaW <= 0) plasmaW = 320;
    if (plasmaH <= 0) plasmaH = 240;

    TQImage img(plasmaW, plasmaH, 32);
    unsigned int *pixels = (unsigned int*)img.bits();
    for (int y = 0; y < plasmaH; y++) {
        for (int x = 0; x < plasmaW; x++) {
            double r, g, b;
            getPlasmaRgb(x * 4, y * 4, m_plasmaTime, r, g, b);
            int ir = (int)(r * 255.0);
            int ig = (int)(g * 255.0);
            int ib = (int)(b * 255.0);
            pixels[y * plasmaW + x] = (0xFF << 24) | (ir << 16) | (ig << 8) | ib;
        }
    }

    p.drawImage(rect(), img);
}

// 5. Slideshow
void ScreensaverWidget::loadSlideImages() {
    TQString searchDir = m_slideshowDir;
    if (searchDir.isEmpty()) {
        const char *home = getenv("HOME");
        searchDir = TQString(home ? home : ".") + "/Pictures";
    }

    TQDir dir(searchDir);
    if (dir.exists()) {
        TQStringList list = dir.entryList(TQDir::Files);
        for (TQStringList::Iterator it = list.begin(); it != list.end(); ++it) {
            TQString lower = it->lower();
            if (lower.endsWith(".png") || lower.endsWith(".jpg") || lower.endsWith(".jpeg")) {
                m_slideImages.append(searchDir + "/" + *it);
            }
        }
    }
    m_slideCurrentIdx = 0;
    if (m_slideshowRandomOrder && m_slideImages.count() > 1) {
        srand(time(NULL));
        for (int i = m_slideImages.count() - 1; i > 0; i--) {
            int j = rand() % (i + 1);
            TQString temp = m_slideImages[i];
            m_slideImages[i] = m_slideImages[j];
            m_slideImages[j] = temp;
        }
    }
}

static TQImage subpixelScaleAndCrop(const TQImage& orig, double zoom, int w, int h) {
    if (orig.isNull() || w <= 0 || h <= 0 || orig.width() <= 0 || orig.height() <= 0) {
        TQImage blank(w > 0 ? w : 1, h > 0 ? h : 1, 32);
        blank.fill(0xFF000000);
        return blank;
    }
    if (zoom <= 0.0) zoom = 1.0;
    TQImage dest(w, h, 32);
    
    double srcW = (double)w / zoom;
    double srcH = (double)h / zoom;
    double srcX = (orig.width() - srcW) / 2.0;
    double srcY = (orig.height() - srcH) / 2.0;
    
    if (srcX < 0.0) srcX = 0.0;
    if (srcY < 0.0) srcY = 0.0;
    
    int origW = orig.width();
    int origH = orig.height();
    
    // Fixed point variables (16.16)
    int dx_fp = (int)((srcW / w) * 65536.0);
    int dy_fp = (int)((srcH / h) * 65536.0);
    int startX_fp = (int)(srcX * 65536.0);
    int startY_fp = (int)(srcY * 65536.0);
    
    const unsigned int* srcBits = (const unsigned int*)orig.bits();
    unsigned int* destBits = (unsigned int*)dest.bits();
    
    for (int y = 0; y < h; ++y) {
        int ys_fp = startY_fp + y * dy_fp;
        int ys_int = ys_fp >> 16;
        int ys_frac = (ys_fp >> 8) & 0xFF;
        int ys_frac_inv = 256 - ys_frac;
        
        // Handle borders
        int y0 = ys_int;
        int y1 = ys_int + 1;
        if (y0 < 0) y0 = 0;
        if (y0 >= origH) y0 = origH - 1;
        if (y1 < 0) y1 = 0;
        if (y1 >= origH) y1 = origH - 1;
        
        const unsigned int* row0 = srcBits + y0 * origW;
        const unsigned int* row1 = srcBits + y1 * origW;
        unsigned int* destRow = destBits + y * w;
        
        int xs_fp = startX_fp;
        for (int x = 0; x < w; ++x) {
            int xs_int = xs_fp >> 16;
            int xs_frac = (xs_fp >> 8) & 0xFF;
            int xs_frac_inv = 256 - xs_frac;
            
            int x0 = xs_int;
            int x1 = xs_int + 1;
            if (x0 < 0) x0 = 0;
            if (x0 >= origW) x0 = origW - 1;
            if (x1 < 0) x1 = 0;
            if (x1 >= origW) x1 = origW - 1;
            
            unsigned int p00 = row0[x0];
            unsigned int p10 = row0[x1];
            unsigned int p01 = row1[x0];
            unsigned int p11 = row1[x1];
            
            // Interpolate R, G, B channels separately using 8-bit fixed point weights
            int w00 = xs_frac_inv * ys_frac_inv;
            int w10 = xs_frac * ys_frac_inv;
            int w01 = xs_frac_inv * ys_frac;
            int w11 = xs_frac * ys_frac;
            
            unsigned int r00 = (p00 >> 16) & 0xFF;
            unsigned int r10 = (p10 >> 16) & 0xFF;
            unsigned int r01 = (p01 >> 16) & 0xFF;
            unsigned int r11 = (p11 >> 16) & 0xFF;
            unsigned int r = (r00 * w00 + r10 * w10 + r01 * w01 + r11 * w11) >> 16;
            
            unsigned int g00 = (p00 >> 8) & 0xFF;
            unsigned int g10 = (p10 >> 8) & 0xFF;
            unsigned int g01 = (p01 >> 8) & 0xFF;
            unsigned int g11 = (p11 >> 8) & 0xFF;
            unsigned int g = (g00 * w00 + g10 * w10 + g01 * w01 + g11 * w11) >> 16;
            
            unsigned int b00 = p00 & 0xFF;
            unsigned int b10 = p10 & 0xFF;
            unsigned int b01 = p01 & 0xFF;
            unsigned int b11 = p11 & 0xFF;
            unsigned int b = (b00 * w00 + b10 * w10 + b01 * w01 + b11 * w11) >> 16;
            
            destRow[x] = 0xFF000000 | (r << 16) | (g << 8) | b;
            
            xs_fp += dx_fp;
        }
    }
    
    return dest;
}

void ScreensaverWidget::freeSlideImages() {
    m_slideImages.clear();
    m_slideCurrentOriginal = TQImage();
    m_slideNextOriginal = TQImage();
}

void ScreensaverWidget::loadNextSlideImage() {
    if (m_slideImages.count() <= 1) return;
    if (width() <= 0 || height() <= 0) return;
    
    int attempt = 0;
    int nextIdx = m_slideCurrentIdx;
    TQImage img;
    bool loaded = false;
    
    while (attempt < m_slideImages.count()) {
        nextIdx = (nextIdx + 1) % m_slideImages.count();
        if (img.load(m_slideImages[nextIdx]) && img.width() > 0 && img.height() > 0) {
            loaded = true;
            break;
        }
        attempt++;
    }
    
    if (!loaded) {
        // Reset timer to avoid spinning CPU on fully corrupt directory
        m_slideZoomElapsedTime = 0;
        return;
    }
    
    double scale = ((double)width() / img.width() > (double)height() / img.height() ? (double)width() / img.width() : (double)height() / img.height());
    int targetW = (int)(img.width() * scale);
    int targetH = (int)(img.height() * scale);
    if (targetW <= 0) targetW = 1;
    if (targetH <= 0) targetH = 1;
    
    m_slideNextOriginal = img.smoothScale(targetW, targetH).convertDepth(32);
    m_slideNextIdx = nextIdx;
    
    m_slideInFade = true;
    m_slideFadeStep = 0;
    m_slideFadeAlpha = 0.0;
}

void ScreensaverWidget::initSlideshow() {
    loadSlideImages();
    if (m_slideImages.count() > 0 && width() > 0 && height() > 0) {
        TQImage img;
        if (img.load(m_slideImages[0]) && img.width() > 0 && img.height() > 0) {
            double scale = ((double)width() / img.width() > (double)height() / img.height() ? (double)width() / img.width() : (double)height() / img.height());
            int targetW = (int)(img.width() * scale);
            int targetH = (int)(img.height() * scale);
            if (targetW <= 0) targetW = 1;
            if (targetH <= 0) targetH = 1;
            m_slideCurrentOriginal = img.smoothScale(targetW, targetH).convertDepth(32);
        }
        m_slideCurrentIdx = 1 % m_slideImages.count();
    }
    m_slideFadeAlpha = 1.0;
    m_slideInFade = false;
    m_slideFadeStep = 0;
    m_slideZoomFactor = 1.0;
    m_slideZoomElapsedTime = 0;
}

void ScreensaverWidget::paintSlideshow(TQPainter& p) {
    if (m_slideImages.count() == 0) {
        p.setPen(TQColor(255, 255, 255));
        p.setFont(TQFont("Sans", 14));
        p.drawText(rect(), AlignCenter, "No pictures found in Pictures folder");
        return;
    }

    if (m_slideCurrentOriginal.isNull()) return;

    int w = width();
    int h = height();

    if (m_slideInFade && !m_slideNextOriginal.isNull()) {
        // Render both frames at subpixel accuracy
        TQImage curZoomed = subpixelScaleAndCrop(m_slideCurrentOriginal, m_slideZoomFactor, w, h);
        TQImage nextZoomed = subpixelScaleAndCrop(m_slideNextOriginal, 1.0, w, h);

        // Fast integer alpha blend (both are 32-bit, same w x h)
        TQImage blendImg(w, h, 32);
        unsigned int *dst = (unsigned int*)blendImg.bits();
        const unsigned int *src1 = (const unsigned int*)curZoomed.bits();
        const unsigned int *src2 = (const unsigned int*)nextZoomed.bits();
        int total = w * h;
        int alpha = (int)(m_slideFadeAlpha * 256);
        if (alpha > 256) alpha = 256;
        int inv_alpha = 256 - alpha;

        for (int i = 0; i < total; ++i) {
            unsigned int p1 = src1[i];
            unsigned int p2 = src2[i];
            unsigned int r = (((p1 >> 16) & 0xFF) * inv_alpha + ((p2 >> 16) & 0xFF) * alpha) >> 8;
            unsigned int g = (((p1 >> 8) & 0xFF) * inv_alpha + ((p2 >> 8) & 0xFF) * alpha) >> 8;
            unsigned int b = ((p1 & 0xFF) * inv_alpha + (p2 & 0xFF) * alpha) >> 8;
            dst[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
        }

        p.drawImage(0, 0, blendImg);
    } else {
        // Render current frame at subpixel accuracy
        TQImage zoomed = subpixelScaleAndCrop(m_slideCurrentOriginal, m_slideZoomFactor, w, h);
        p.drawImage(0, 0, zoomed);
    }
}

// 6. Starfield
void ScreensaverWidget::initStarfieldStar(int i) {
    m_starfieldStars[i].a = ((double)rand() / RAND_MAX) * (2.0 * M_PI);
    m_starfieldStars[i].r = 3.0 + ((double)rand() / RAND_MAX) * (50.0 - 3.0);
    m_starfieldStars[i].s = 2.0 + (8.0 - 2.0) * ((double)rand() / RAND_MAX);
    m_starfieldStars[i].rgb_r = 0.5 + ((double)rand() / RAND_MAX) * 0.5;
    m_starfieldStars[i].rgb_g = 0.5 + ((double)rand() / RAND_MAX) * 0.5;
    m_starfieldStars[i].rgb_b = 0.5 + ((double)rand() / RAND_MAX) * 0.5;
}

void ScreensaverWidget::initStarfield() {
    srand(time(NULL));
    for (int i = 0; i < 600; i++) {
        initStarfieldStar(i);
    }
}

void ScreensaverWidget::updateStarfieldStars() {
    int cx = width() / 2;
    int cy = height() / 2;
    for (int i = 0; i < 600; i++) {
        m_starfieldStars[i].r += m_starfieldStars[i].s;
        double x = cx + cos(m_starfieldStars[i].a) * m_starfieldStars[i].r;
        double y = cy + sin(m_starfieldStars[i].a) * m_starfieldStars[i].r;
        if (x < 0 || x >= width() || y < 0 || y >= height()) {
            initStarfieldStar(i);
        }
    }
}

void ScreensaverWidget::paintStarfield(TQPainter& p) {
    int cx = width() / 2;
    int cy = height() / 2;
    int max_radius = (cx < cy) ? cx : cy;
    if (max_radius <= 0) max_radius = 100;

    for (int i = 0; i < 600; i++) {
        double pr = m_starfieldStars[i].r - m_starfieldStars[i].s;
        double x0 = cx + cos(m_starfieldStars[i].a) * pr;
        double y0 = cy + sin(m_starfieldStars[i].a) * pr;
        double x1 = cx + cos(m_starfieldStars[i].a) * m_starfieldStars[i].r;
        double y1 = cy + sin(m_starfieldStars[i].a) * m_starfieldStars[i].r;

        p.setPen(TQPen(TQColor((int)(m_starfieldStars[i].rgb_r * 255), (int)(m_starfieldStars[i].rgb_g * 255), (int)(m_starfieldStars[i].rgb_b * 255)),
                       (int)(1.0 + 2.0 * (m_starfieldStars[i].r / max_radius))));
        p.drawLine((int)x0, (int)y0, (int)x1, (int)y1);
    }
}

// ==========================================
// TransitionOverlay Implementation
// ==========================================
TransitionOverlay::TransitionOverlay(int mode, TQWidget *parent)
    : TQWidget(parent, "TransitionOverlay", WStyle_Customize | WStyle_NoBorder | WStyle_StaysOnTop | WNoAutoErase)
{
    m_mode = mode;
    m_step = 0;
    m_lastBlendedStep = -1;

    int activeScreen = tqApp->desktop()->screenNumber(TQCursor::pos());
    TQRect screenGeom = tqApp->desktop()->screenGeometry(activeScreen);
    setGeometry(screenGeom);
    setCursor(TQCursor(Qt::BlankCursor)); // Hide cursor during transition

    // Capture active screen
    m_screenshot = TQPixmap::grabWindow(tqApp->desktop()->winId(),
                                        screenGeom.x(), screenGeom.y(),
                                        screenGeom.width(), screenGeom.height());

    if (m_mode == 0) {
        int sw = screenGeom.width();
        int sh = screenGeom.height();
        m_originalImage = m_screenshot.convertToImage().convertDepth(32);
        m_currentImage = TQImage(sw, sh, 32);
    }

    m_timer = new TQTimer(this);
    connect(m_timer, TQT_SIGNAL(timeout()), this, TQT_SLOT(animateStep()));
    m_timer->start(16); // 60 FPS (16ms) matching GTK3
}

TransitionOverlay::~TransitionOverlay() {
    delete m_timer;
}

void TransitionOverlay::keyPressEvent(TQKeyEvent *e) {
    // Consume keys during transition
}

void TransitionOverlay::animateStep() {
    m_step++;
    int maxSteps = 25;
    if (m_mode == 1) maxSteps = 160;
    else if (m_mode == 2) maxSteps = 50;

    if (m_step > maxSteps) {
        m_timer->stop();
        emit transitionComplete();
    } else {
        update();
    }
}

void TransitionOverlay::paintEvent(TQPaintEvent *e) {
    TQPainter p(this);
    int w = width();
    int h = height();

    if (m_mode == 0) { // Fade out
        if (!m_originalImage.isNull() && !m_currentImage.isNull()) {
            if (m_step != m_lastBlendedStep) {
                const unsigned int *src = (const unsigned int*)m_originalImage.bits();
                unsigned int *dest = (unsigned int*)m_currentImage.bits();
                int total = m_originalImage.width() * m_originalImage.height();
                int opacity = 256 - (m_step * 256 / 25);
                if (opacity < 0) opacity = 0;
                if (opacity > 256) opacity = 256;

                for (int i = 0; i < total; ++i) {
                    unsigned int pix = src[i];
                    unsigned int r = (((pix >> 16) & 0xFF) * opacity) >> 8;
                    unsigned int g = (((pix >> 8) & 0xFF) * opacity) >> 8;
                    unsigned int b = ((pix & 0xFF) * opacity) >> 8;
                    dest[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
                }
                m_lastBlendedStep = m_step;
            }
            p.drawImage(0, 0, m_currentImage);
        }
    } 
    else if (m_mode == 1) { // Old TV turn-off
        p.fillRect(rect(), TQColor(0, 0, 0)); // fill black background first
        
        if (m_step < 50) {
            // Phase 0: Vertical Collapse
            double t = (double)m_step / 50.0;
            double ep = t * t;
            int max_bh = h / 2 - 1;
            int bh = (int)(ep * max_bh);
            
            int curH = h - 2 * bh;
            if (curH > 0) {
                p.drawPixmap(0, bh, m_screenshot, 0, bh, w, curH);
            }
        }
        else if (m_step < 60) {
            // Phase 1: White horizontal line
            int cy = h / 2;
            p.setPen(TQPen(TQColor(255, 255, 255), 3));
            p.drawLine(0, cy, w, cy);
        }
        else if (m_step < 100) {
            // Phase 2: Horizontal Collapse
            int s = m_step - 60;
            int cx = w / 2;
            int cy = h / 2;
            
            if (s < 25) {
                int lw = w - (s * w) / 25;
                int sx = (w - lw) / 2;
                p.setPen(TQPen(TQColor(255, 255, 255), 3));
                p.drawLine(sx, cy, sx + lw, cy);
            }
            else if (s < 30) {
                p.setPen(TQPen(TQColor(255, 255, 255), 3));
                p.drawLine(cx - 1, cy, cx + 1, cy);
            }
            else if (s < 40) {
                int fade = 255 - ((s - 30) * 255) / 10;
                if (fade > 0) {
                    p.setPen(TQPen(TQColor(fade, fade, fade), 3));
                    p.drawLine(cx - 1, cy, cx + 1, cy);
                }
            }
        }
        // Phase 3 (100 to 160): Pure black, already handled by fillRect at start
    } 
    else if (m_mode == 2) { // Circular Wipe
        p.fillRect(rect(), TQColor(0, 0, 0)); // fill black background first
        
        double t = (double)m_step / 50.0;
        int maxR = (int)(sqrt(w*w + h*h) / 2);
        int r = maxR - (int)(t * maxR);
        
        if (r > 0) {
            int cx = w / 2;
            int cy = h / 2;
            
            TQRegion clipCircle(cx - r, cy - r, r * 2, r * 2, TQRegion::Ellipse);
            p.setClipRegion(clipCircle);
            p.drawPixmap(0, 0, m_screenshot);
        }
    }
}
