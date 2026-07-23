#include "battery_history_dialog.h"
#include "battery_icons.h"
#include "tqtaapainter.h"
#include <tqlayout.h>
#include <tqlabel.h>
#include <tqfont.h>
#include <tqpainter.h>
#include <tqevent.h>
#include <tqpointarray.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// GraphData helper structures for rendering
struct RenderEventLabel {
    double x_pos;
    TQString label;
    bool is_screen_event;
    bool is_charger_event;
    bool is_system_event;
    int height_idx;
};

struct RenderBar {
    double x;
    double bar_width;
    double bar_height;
    int capacity;
    int charging;
    TQString time_str;
    TQString percent_str;
    time_t timestamp;
};

// ==========================================
// BatteryHistoryGraph Implementation
// ==========================================

BatteryHistoryGraph::BatteryHistoryGraph(BatteryLogger *logger, TQWidget *parent)
    : TQWidget(parent)
{
    m_logger = logger;
    m_displayCount = 48;
    m_showScreenEvents = false;
    m_showSystemEvents = false;
    m_useCurve = false;

    m_hovered = false;
    m_hoverX = 0;
    m_hoverY = 0;

    m_lastMarginLeft = 0;
    m_lastGraphWidth = 0;
    m_lastBarWidth = 0;
    m_lastOffsetPx = 0;
    m_lastBarsWidth = 0;
    m_lastStartTime = 0;
    m_lastEndTime = 0;

    setMouseTracking(true);
    setBackgroundMode(NoBackground);
}

BatteryHistoryGraph::~BatteryHistoryGraph() {}

void BatteryHistoryGraph::setParams(int displayCount, bool showScreenEvents, bool showSystemEvents, bool useCurve) {
    m_displayCount = displayCount;
    m_showScreenEvents = showScreenEvents;
    m_showSystemEvents = showSystemEvents;
    m_useCurve = useCurve;
}

void BatteryHistoryGraph::updateGraph() {
    update();
}

void BatteryHistoryGraph::resizeEvent(TQResizeEvent *e) {
    TQWidget::resizeEvent(e);
}

void BatteryHistoryGraph::mouseMoveEvent(TQMouseEvent *e) {
    m_hovered = true;
    m_hoverX = e->x();
    m_hoverY = e->y();
    update();
}

void BatteryHistoryGraph::leaveEvent(TQEvent *e) {
    m_hovered = false;
    update();
}

void BatteryHistoryGraph::paintEvent(TQPaintEvent *e) {
    int w = width();
    int h = height();
    if (w <= 0 || h <= 0) return;

    TQPixmap doubleBuffer(w, h);
    TQPainter p(&doubleBuffer);

    // Fill background (sleek dark/light theme depending on TQColor)
    p.fillRect(0, 0, w, h, palette().color(TQPalette::Active, TQColorGroup::Background));

    const BatteryLog& log = m_logger->getBatteryLog();
    if (log.battery_history.count == 0 || m_displayCount == 0) {
        p.setPen(palette().color(TQPalette::Active, TQColorGroup::Text));
        p.setFont(TQFont("Sans", 12, TQFont::Bold));
        p.drawText(0, 0, w, h, AlignCenter, "No battery history");
        p.end();
        TQPainter widgetPainter(this);
        widgetPainter.drawPixmap(0, 0, doubleBuffer);
        return;
    }

    // Set Margins
    double margin_left = w / 20.0;
    double margin_right = w / 30.0;
    double margin_top = h / 8.0;
    double margin_bottom = h / 6.0;
    double legend_height = h / 12.0;

    double graph_width = w - margin_left - margin_right;
    double graph_height = h - margin_top - margin_bottom - legend_height;

    // Draw Grid first
    drawGrid(p, w, h, margin_left, margin_right, margin_top, graph_height);

    // Prepare Graph Data
    time_t endTime = time(NULL);
    time_t startTime = endTime - m_displayCount * 1800;

    double bar_width = graph_width / m_displayCount;
    double offset_px = 0.0;
    double bars_width = graph_width;

    m_lastMarginLeft = margin_left;
    m_lastGraphWidth = graph_width;
    m_lastBarWidth = bar_width;
    m_lastOffsetPx = offset_px;
    m_lastBarsWidth = bars_width;
    m_lastStartTime = startTime;
    m_lastEndTime = endTime;

    // Local lists for drawing
    TQValueList<RenderBar> bars;

    // Locate matching samples for each time slot (spaced by 30 mins = 1800s)
    for (int i = 0; i < m_displayCount; i++) {
        int slot_idx = m_displayCount - 1 - i;
        time_t slot_time = endTime - slot_idx * 1800;

        // Find closest sample before slot_time and first sample after slot_time
        const BatterySample* prev_sample = NULL;
        const BatterySample* next_sample = NULL;

        for (int k = 0; k < log.battery_history.count; k++) {
            int idx = (log.battery_history.next_index - log.battery_history.count + k + HISTORY_SIZE) % HISTORY_SIZE;
            const BatterySample* sample = &log.battery_history.samples[idx];

            if (sample->timestamp <= slot_time) {
                if (!prev_sample || sample->timestamp > prev_sample->timestamp) {
                    prev_sample = sample;
                }
            }
            if (sample->timestamp >= slot_time) {
                if (!next_sample || sample->timestamp < next_sample->timestamp) {
                    next_sample = sample;
                }
            }
        }

        RenderBar bar;
        bar.x = i * bar_width;
        bar.bar_width = bar_width - 2;
        if (bar.bar_width < 1.0) bar.bar_width = 1.0;

        int capacity = -1;
        int charging = 0;
        time_t timestamp = 0;

        if (prev_sample && next_sample) {
            if (prev_sample == next_sample || prev_sample->timestamp == next_sample->timestamp) {
                capacity = prev_sample->capacity;
                charging = prev_sample->charging;
                timestamp = prev_sample->timestamp;
            } else {
                double ratio = (double)(slot_time - prev_sample->timestamp) / (next_sample->timestamp - prev_sample->timestamp);
                capacity = prev_sample->capacity + (int)(ratio * (next_sample->capacity - prev_sample->capacity));
                charging = (ratio < 0.5) ? prev_sample->charging : next_sample->charging;
                timestamp = slot_time;
            }
        } else if (prev_sample) {
            capacity = prev_sample->capacity;
            charging = prev_sample->charging;
            timestamp = slot_time;
        }

        if (capacity >= 0) {
            bar.bar_height = capacity / 100.0;
            bar.capacity = capacity;
            bar.charging = charging;
            bar.timestamp = timestamp;

            struct tm *tm_s = localtime(&slot_time);
            char tstr[32];
            strftime(tstr, sizeof(tstr), "%H:%M", tm_s);
            bar.time_str = tstr;
            bar.percent_str.sprintf("%d%%", capacity);
        } else {
            bar.bar_height = 0.0;
            bar.capacity = -1;
            bar.charging = 0;
            bar.timestamp = 0;
            bar.time_str = "--:--";
            bar.percent_str = "--";
        }
        bars.append(bar);
    }

    // Render Bars or Curve
    if (!m_useCurve) {
        // Draw bars
        int idx = 0;
        for (TQValueList<RenderBar>::Iterator it = bars.begin(); it != bars.end(); ++it, ++idx) {
            RenderBar& bar = *it;
            if (bar.bar_height <= 0.0) continue;

            double x_pos = margin_left + bar.x;
            double y_pos = margin_top + graph_height - bar.bar_height * graph_height;

            TQColor col = (bar.charging == 1 || bar.charging == 2) ? TQColor(52, 128, 204) : TQColor(255, 128, 0); // Blue vs Amber
            p.fillRect((int)x_pos, (int)y_pos, (int)bar.bar_width, (int)(bar.bar_height * graph_height), col);

            // Draw time labels & values if spaced out enough
            bool showTime = false;
            bool showPercent = false;
            if (m_displayCount == 48 && idx % 2 == 0) showTime = showPercent = true;
            else if (m_displayCount == 96) { if (idx % 4 == 0) showTime = true; if (idx % 2 == 0) showPercent = true; }
            else if (m_displayCount == 144) { if (idx % 8 == 0) showTime = true; if (idx % 4 == 0) showPercent = true; }

            p.setPen(palette().color(TQPalette::Active, TQColorGroup::Text));
            p.setFont(TQFont("Sans", 8));
            if (showTime) {
                p.drawText((int)x_pos - 10, (int)(margin_top + graph_height + 5), 30, 15, AlignCenter, bar.time_str);
            }
            if (showPercent && y_pos > margin_top + 15) {
                p.drawText((int)x_pos - 10, (int)(y_pos - 15), 30, 15, AlignCenter, bar.percent_str);
            }
        }
    } else {
        // Curve Interpolation Mode using evaluated Bezier segments and AA Polyline
        TQValueList<RenderBar> validBars;
        for (TQValueList<RenderBar>::Iterator it = bars.begin(); it != bars.end(); ++it) {
            if ((*it).capacity >= 0) {
                validBars.append(*it);
            }
        }

        int n = validBars.count();
        if (n >= 2) {
            double *px = new double[n];
            double *py = new double[n];
            int idx = 0;
            for (TQValueList<RenderBar>::Iterator it = validBars.begin(); it != validBars.end(); ++it, ++idx) {
                px[idx] = margin_left + (*it).x + (*it).bar_width / 2.0;
                py[idx] = margin_top + graph_height - (*it).bar_height * graph_height;
            }

            // Calculate segment slopes
            double *d = new double[n - 1];
            for (int i = 0; i < n - 1; i++) {
                double dx = px[i + 1] - px[i];
                if (dx == 0.0) dx = 1.0;
                d[i] = (py[i + 1] - py[i]) / dx;
            }

            // Calculate tangents at points
            double *m = new double[n];
            for (int i = 0; i < n; i++) {
                if (i == 0) {
                    m[0] = d[0];
                } else if (i == n - 1) {
                    m[n - 1] = d[n - 2];
                } else {
                    m[i] = (d[i - 1] + d[i]) / 2.0;
                    // Monotonicity constraint
                    if (d[i - 1] * d[i] <= 0.0) {
                        m[i] = 0.0;
                    } else {
                        double limit = 2.0 * (fabs(d[i - 1]) < fabs(d[i]) ? fabs(d[i - 1]) : fabs(d[i]));
                        if (m[i] > limit) m[i] = limit;
                        else if (m[i] < -limit) m[i] = -limit;
                    }
                }
            }

            TQColor bgColor = palette().color(TQPalette::Active, TQColorGroup::Background);
            for (int i = 0; i < n - 1; i++) {
                double x1 = px[i];
                double y1 = py[i];
                double x2 = px[i + 1];
                double y2 = py[i + 1];

                double ctrl1_x = x1 + (x2 - x1) / 3.0;
                double ctrl1_y = y1 + m[i] * (x2 - x1) / 3.0;
                double ctrl2_x = x2 - (x2 - x1) / 3.0;
                double ctrl2_y = y2 - m[i + 1] * (x2 - x1) / 3.0;

                double y_min = margin_top;
                double y_max = margin_top + graph_height;
                if (ctrl1_y < y_min) ctrl1_y = y_min;
                if (ctrl1_y > y_max) ctrl1_y = y_max;
                if (ctrl2_y < y_min) ctrl2_y = y_min;
                if (ctrl2_y > y_max) ctrl2_y = y_max;

                // Evaluate Bezier points
                int steps = 16;
                TQPointArray splinePoints(steps + 1);
                for (int step = 0; step <= steps; ++step) {
                    double t = (double)step / steps;
                    double omt = 1.0 - t;
                    double x = omt*omt*omt*x1 + 3*omt*omt*t*ctrl1_x + 3*omt*t*t*ctrl2_x + t*t*t*x2;
                    double y = omt*omt*omt*y1 + 3*omt*omt*t*ctrl1_y + 3*omt*t*t*ctrl2_y + t*t*t*y2;
                    splinePoints.setPoint(step, (int)x, (int)y);
                }

                TQColor color = (validBars[i].charging == 1 || validBars[i+1].charging == 1) ? TQColor(52, 128, 204) : TQColor(255, 128, 0);
                TQtAAPainter::drawPolylineAA(&p, splinePoints.data(), splinePoints.size(), color, bgColor, 3);
            }

            // Draw dot points on each sample
            idx = 0;
            for (TQValueList<RenderBar>::Iterator it = validBars.begin(); it != validBars.end(); ++it, ++idx) {
                TQColor color = ((*it).charging == 1 || (*it).charging == 2) ? TQColor(52, 128, 204) : TQColor(255, 128, 0);
                p.setBrush(TQBrush(color));
                p.setPen(TQPen(color, 1));
                p.drawEllipse((int)px[idx] - 3, (int)py[idx] - 3, 6, 6);
            }

            delete[] px;
            delete[] py;
            delete[] d;
            delete[] m;
        }

        // Draw time labels & values for Curve Mode
        int idx = 0;
        for (TQValueList<RenderBar>::Iterator it = validBars.begin(); it != validBars.end(); ++it, ++idx) {
            double x_pos = margin_left + (*it).x;
            double y_pos = margin_top + graph_height - (*it).bar_height * graph_height;

            bool showTime = false;
            bool showPercent = false;
            if (m_displayCount == 48 && idx % 2 == 0) showTime = showPercent = true;
            else if (m_displayCount == 96) { if (idx % 4 == 0) showTime = true; if (idx % 2 == 0) showPercent = true; }
            else if (m_displayCount == 144) { if (idx % 8 == 0) showTime = true; if (idx % 4 == 0) showPercent = true; }

            p.setPen(palette().color(TQPalette::Active, TQColorGroup::Text));
            p.setFont(TQFont("Sans", 8));
            if (showTime) {
                p.drawText((int)x_pos - 10, (int)(margin_top + graph_height + 5), 30, 15, AlignCenter, (*it).time_str);
            }
            if (showPercent && y_pos > margin_top + 15) {
                p.drawText((int)x_pos - 10, (int)(y_pos - 15), 30, 15, AlignCenter, (*it).percent_str);
            }
        }
    }

    // Draw active periods & screen times info
    time_t log_start_time = startTime;
    if (log.battery_history.count > 0) {
        int first_idx = (log.battery_history.next_index - log.battery_history.count + HISTORY_SIZE) % HISTORY_SIZE;
        time_t first_sample_time = log.battery_history.samples[first_idx].timestamp;
        if (first_sample_time > startTime) {
            log_start_time = first_sample_time;
        }
    }

    TQValueList<ActiveInterval> activePeriods = m_logger->calculateActivePeriods(log_start_time, endTime);
    ScreenTimes screenTimes = m_logger->calculateScreenTimes(activePeriods, log_start_time);
    long long totalSleep = m_logger->calculateSleepTime(log_start_time, endTime, 1200); // 1200s default hibernate delay

    TQString screenOffStr, screenOnStr, sleepStr;
    screenOffStr.sprintf("Screen Off: %lld H %02lld min", screenTimes.screen_off / 3600, (screenTimes.screen_off % 3600) / 60);
    screenOnStr.sprintf("Screen On: %lld H %02lld min", screenTimes.screen_on / 3600, (screenTimes.screen_on % 3600) / 60);
    sleepStr.sprintf("Sleep: %lld H %02lld min", totalSleep / 3600, (totalSleep % 3600) / 60);

    p.setFont(TQFont("Sans", 9));
    p.setPen(palette().color(TQPalette::Active, TQColorGroup::Text));
    double labelY = margin_top + graph_height + 25;
    p.drawText((int)margin_left, (int)labelY, w, 20, AlignLeft, screenOffStr);
    p.drawText((int)margin_left, (int)(labelY + 18), w, 20, AlignLeft, screenOnStr);
    p.drawText((int)margin_left, (int)(labelY + 36), w, 20, AlignLeft, sleepStr);

    // Draw Legend (Connected / Disconnected)
    double legendX = w - margin_right - 180;
    double legendY = labelY;

    p.fillRect((int)legendX, (int)legendY + 2, 12, 12, TQColor(52, 128, 204));
    p.drawText((int)legendX + 18, (int)legendY + 12, "Charger connected");

    p.fillRect((int)legendX, (int)legendY + 20, 12, 12, TQColor(255, 128, 0));
    p.drawText((int)legendX + 18, (int)legendY + 30, "Charger disconnected");

    // Draw Event Labels at the top of the chart
    drawEvents(p, w, h, margin_left, graph_width, graph_height, bar_width, offset_px, bars_width, startTime, endTime);

    // Draw Hover Tooltip if active
    if (m_hovered) {
        drawTooltip(p, w, h);
    }

    p.end();
    TQPainter widgetPainter(this);
    widgetPainter.drawPixmap(0, 0, doubleBuffer);
}

void BatteryHistoryGraph::drawGrid(TQPainter& p, int w, int h, double marginLeft, double marginRight, double marginTop, double graphHeight) {
    p.setPen(TQPen(TQColor(180, 180, 180), 1, TQPen::DotLine));
    for (int i = 0; i <= 100; i += 20) {
        double y = marginTop + graphHeight - (i / 100.0) * graphHeight;
        p.drawLine((int)marginLeft, (int)y, (int)(w - marginRight), (int)y);

        // Labels
        TQString lbl;
        lbl.sprintf("%d%%", i);
        p.setPen(palette().color(TQPalette::Active, TQColorGroup::Text));
        p.setFont(TQFont("Sans", 9, TQFont::Bold));
        p.drawText((int)(marginLeft - 40), (int)(y - 7), 35, 15, AlignRight, lbl);
        p.setPen(TQPen(TQColor(180, 180, 180), 1, TQPen::DotLine));
    }
}

void BatteryHistoryGraph::drawEvents(TQPainter& p, int w, int h, double marginLeft, double graphWidth, double graphHeight, double barWidth, double offsetPx, double barsWidth, time_t startTime, time_t endTime) {
    const BatteryLog& log = m_logger->getBatteryLog();
    int eventCount = log.system_events.count;
    int nextIdx = log.system_events.next_index;

    double marginTop = h / 8.0;
    int visibleEventCount = 0;

    for (int i = 0; i < eventCount; i++) {
        int idx = (nextIdx - eventCount + i + EVENTS_SIZE) % EVENTS_SIZE;
        const SystemEvent* e = &log.system_events.events[idx];

        if (e->timestamp < startTime || e->timestamp > endTime) continue;

        TQString label = "";
        bool isScreenEvent = false;
        bool isChargerEvent = false;
        bool isSystemEvent = false;

        switch (e->event_type) {
            case EVENT_WAKE_UP: {
                int prevIdx = (idx - 1 + EVENTS_SIZE) % EVENTS_SIZE;
                bool isBoot = (i > 0 && log.system_events.events[prevIdx].event_type == EVENT_POWER_OFF);
                label = isBoot ? "BOOT" : "WAKE_UP";
                isSystemEvent = true;
                break;
            }
            case EVENT_SCREEN_ON:
                label = "SCREEN_ON";
                isScreenEvent = true;
                break;
            case EVENT_SCREEN_OFF:
                label = "SCREEN_OFF";
                isScreenEvent = true;
                break;
            case EVENT_CHARGER_CONNECTED:
                label = "CHARGER_CONNECTED";
                isChargerEvent = true;
                break;
            case EVENT_CHARGER_DISCONNECTED:
                label = "CHARGER_DISCONNECTED";
                isChargerEvent = true;
                break;
            case EVENT_SUSPEND:
                label = "SUSPEND";
                isSystemEvent = true;
                break;
            case EVENT_HIBERNATE:
                label = "HIBERNATE";
                isSystemEvent = true;
                break;
            case EVENT_SUSPEND_THEN_HIBERNATE:
                label = "SUSPEND_THEN_HIBERNATE";
                isSystemEvent = true;
                break;
            case EVENT_HYBRID_SUSPEND:
                label = "HYBRID_SUSPEND";
                isSystemEvent = true;
                break;
            case EVENT_POWER_OFF:
                label = "POWER_OFF";
                isSystemEvent = true;
                break;
        }

        if (label.isEmpty()) continue;
        if (isScreenEvent && !m_showScreenEvents) continue;
        if (isSystemEvent && !m_showSystemEvents) continue;

        // Snap system event within 5 minutes (300s) to graph bounds if it is past the last sample
        time_t timestamp = e->timestamp;
        if (e->event_type != EVENT_SCREEN_ON && e->event_type != EVENT_SCREEN_OFF) {
            if (timestamp > endTime && timestamp - endTime < 300) {
                timestamp = endTime;
            }
        }

        double x_pos = marginLeft + (double)(timestamp - startTime) / (endTime - startTime) * barsWidth + offsetPx;
        
        int height_idx = visibleEventCount % 6;
        visibleEventCount++;

        double line_start_y = marginTop - 15 - height_idx * 12;
        double label_y = line_start_y - 10;

        if (isChargerEvent) {
            p.setPen(TQPen(e->event_type == EVENT_CHARGER_CONNECTED ? TQColor(0, 180, 0) : TQColor(230, 20, 20), 1, TQPen::SolidLine));
        } else if (isScreenEvent) {
            p.setPen(TQPen(e->event_type == EVENT_SCREEN_ON ? TQColor(0, 200, 0) : TQColor(200, 0, 0), 1, TQPen::SolidLine));
        } else {
            p.setPen(TQPen(TQColor(0, 0, 200), 1, TQPen::SolidLine));
        }

        p.drawLine((int)x_pos, (int)line_start_y, (int)x_pos, (int)(marginTop + graphHeight));

        // Draw text label or icon
        if (isChargerEvent) {
            TQImage img;
            const unsigned char* iconData = (e->event_type == EVENT_CHARGER_CONNECTED) ? charge_data : uncharge_data;
            size_t iconSize = (e->event_type == EVENT_CHARGER_CONNECTED) ? charge_size : uncharge_size;
            if (img.loadFromData(iconData, iconSize, "PNG")) {
                TQImage scaled = img.smoothScale(16, 16);
                TQPixmap pm;
                pm.convertFromImage(scaled);
                p.drawPixmap((int)(x_pos - 8), (int)(line_start_y - 18), pm);
            }
        } else {
            p.setFont(TQFont("Sans", 7));
            int textWidth = 150;
            int drawX = (int)(x_pos - 12);
            if (drawX + textWidth > w) {
                drawX = w - textWidth;
            }
            p.drawText(drawX, (int)label_y, textWidth, 12, AlignLeft, label);
        }
    }
}

void BatteryHistoryGraph::drawTooltip(TQPainter& p, int w, int h) {
    if (m_hoverX < m_lastMarginLeft || m_hoverX > m_lastMarginLeft + m_lastBarsWidth + m_lastOffsetPx) {
        return;
    }

    double relativeX = m_hoverX - m_lastMarginLeft - m_lastOffsetPx;
    if (relativeX < 0) return;

    int slot = (int)(relativeX / m_lastBarWidth);
    if (slot < 0 || slot >= m_displayCount) return;

    // Find the sample for this slot
    time_t slot_time = m_lastEndTime - (m_displayCount - 1 - slot) * 1800;

    const BatteryLog& log = m_logger->getBatteryLog();
    const BatterySample* prev_sample = NULL;
    const BatterySample* next_sample = NULL;

    for (int k = 0; k < log.battery_history.count; k++) {
        int idx = (log.battery_history.next_index - log.battery_history.count + k + HISTORY_SIZE) % HISTORY_SIZE;
        const BatterySample* sample = &log.battery_history.samples[idx];

        if (sample->timestamp <= slot_time) {
            if (!prev_sample || sample->timestamp > prev_sample->timestamp) {
                prev_sample = sample;
            }
        }
        if (sample->timestamp >= slot_time) {
            if (!next_sample || sample->timestamp < next_sample->timestamp) {
                next_sample = sample;
            }
        }
    }

    int capacity = -1;
    int charging = 0;

    if (prev_sample && next_sample) {
        if (prev_sample == next_sample || prev_sample->timestamp == next_sample->timestamp) {
            capacity = prev_sample->capacity;
            charging = prev_sample->charging;
        } else {
            double ratio = (double)(slot_time - prev_sample->timestamp) / (next_sample->timestamp - prev_sample->timestamp);
            capacity = prev_sample->capacity + (int)(ratio * (next_sample->capacity - prev_sample->capacity));
            charging = (ratio < 0.5) ? prev_sample->charging : next_sample->charging;
        }
    } else if (prev_sample) {
        capacity = prev_sample->capacity;
        charging = prev_sample->charging;
    }

    if (capacity < 0) return;

    TQString timeStr, capacityStr, statusStr;
    struct tm *tm_s = localtime(&slot_time);
    char tstr[32];
    strftime(tstr, sizeof(tstr), "%d/%m/%Y %H:%M", tm_s);
    timeStr = tstr;
    capacityStr.sprintf("Charge: %d%%", capacity);
    statusStr = (charging == 1) ? "Status: Charging" : (charging == 2) ? "Status: Full" : "Status: Discharging";

    // Setup Tooltip window
    int boxW = 160;
    int boxH = 55;
    int boxX = m_hoverX + 15;
    int boxY = m_hoverY + 15;

    if (boxX + boxW > w) boxX = m_hoverX - boxW - 15;
    if (boxY + boxH > h) boxY = m_hoverY - boxH - 15;

    // Draw solid dark background and border directly
    p.fillRect(boxX, boxY, boxW, boxH, TQColor(30, 30, 30));
    p.setPen(TQColor(80, 80, 80));
    p.drawRect(boxX, boxY, boxW, boxH);

    // Draw details
    p.setFont(TQFont("Sans", 8));
    p.setPen(TQColor(240, 240, 240));
    p.drawText(boxX + 10, boxY + 15, timeStr);
    p.drawText(boxX + 10, boxY + 30, capacityStr);
    p.drawText(boxX + 10, boxY + 45, statusStr);
}

// ==========================================
// BatteryHistoryDialog Implementation
// ==========================================

BatteryHistoryDialog::BatteryHistoryDialog(BatteryLogger *logger, TQWidget *parent)
    : TQDialog(parent, "BatteryHistoryDialog", true)
{
    m_logger = logger;
    m_displayCount = 48;
    m_showSystemEvents = false;
    m_showScreenEvents = false;
    m_useCurve = false;

    setCaption("Battery History");
    setWFlags(WStyle_Customize | WStyle_DialogBorder | WStyle_Title);

    // Set Window Icon from embedded yabatman icon data
    TQImage iconImg;
    if (iconImg.loadFromData(yabatman_data, yabatman_size, "PNG")) {
        TQPixmap pm;
        pm.convertFromImage(iconImg);
        setIcon(pm);
    }

    setupUI();
}

BatteryHistoryDialog::~BatteryHistoryDialog() {}

void BatteryHistoryDialog::keyPressEvent(TQKeyEvent *e) {
    if (e->key() == Key_Escape) {
        accept();
    } else {
        TQDialog::keyPressEvent(e);
    }
}

void BatteryHistoryDialog::setupUI() {
    TQVBoxLayout *mainLayout = new TQVBoxLayout(this, 0, 0);

    // Title Block
    TQFrame *headerFrame = new TQFrame(this);
    headerFrame->setPaletteBackgroundColor(TQColor(215, 215, 215)); // Darker gray background
    
    TQHBoxLayout *titleLayout = new TQHBoxLayout(headerFrame, 10, 10);
    
    TQLabel *iconLabel = new TQLabel(headerFrame);
    TQImage img;
    if (img.loadFromData(history_data, history_size, "PNG")) {
        TQPixmap pm;
        pm.convertFromImage(img);
        iconLabel->setPixmap(pm);
    }
    titleLayout->addWidget(iconLabel, 0, AlignVCenter);

    TQLabel *titleText = new TQLabel("Battery usage", headerFrame);
    TQFont f = titleText->font();
    f.setPointSize(f.pointSize() + 3);
    f.setBold(true);
    titleText->setFont(f);
    titleLayout->addWidget(titleText, 0, AlignVCenter);
    titleLayout->addStretch();
    
    mainLayout->addWidget(headerFrame);

    TQVBoxLayout *contentLayout = new TQVBoxLayout(mainLayout, 10);
    contentLayout->setMargin(15);
    contentLayout->addSpacing(8);

    // Toolbar HBox
    TQHBoxLayout *toolbar = new TQHBoxLayout(contentLayout, 10);

    // Combobox period
    toolbar->addWidget(new TQLabel("Period:", this));
    m_periodCombo = new TQComboBox(false, this);
    m_periodCombo->insertItem("Last 24h");
    m_periodCombo->insertItem("Last 48h");
    m_periodCombo->insertItem("Last 72h");
    connect(m_periodCombo, TQT_SIGNAL(activated(int)), this, TQT_SLOT(onPeriodChanged(int)));
    toolbar->addWidget(m_periodCombo);

    // Checkboxes
    m_systemEventsCheck = new TQCheckBox("Show system events", this);
    connect(m_systemEventsCheck, TQT_SIGNAL(toggled(bool)), this, TQT_SLOT(onToggleSystemEvents(bool)));
    toolbar->addWidget(m_systemEventsCheck);

    m_screenEventsCheck = new TQCheckBox("Show screen events", this);
    connect(m_screenEventsCheck, TQT_SIGNAL(toggled(bool)), this, TQT_SLOT(onToggleScreenEvents(bool)));
    toolbar->addWidget(m_screenEventsCheck);

    // Graph style toggle button
    m_graphTypeBtn = new TQPushButton("Curves Style", this);
    connect(m_graphTypeBtn, TQT_SIGNAL(clicked()), this, TQT_SLOT(onToggleGraphType()));
    toolbar->addWidget(m_graphTypeBtn);

    // Graph Area
    m_graph = new BatteryHistoryGraph(m_logger, this);
    m_graph->setParams(m_displayCount, m_showScreenEvents, m_showSystemEvents, m_useCurve);
    contentLayout->addWidget(m_graph, 1); // Expandable widget

    resize(1150, 750);
}

void BatteryHistoryDialog::onPeriodChanged(int index) {
    m_displayCount = (index == 0) ? 48 : (index == 1) ? 96 : 144;
    m_graph->setParams(m_displayCount, m_showScreenEvents, m_showSystemEvents, m_useCurve);
    m_graph->updateGraph();
}

void BatteryHistoryDialog::onToggleSystemEvents(bool checked) {
    m_showSystemEvents = checked;
    m_graph->setParams(m_displayCount, m_showScreenEvents, m_showSystemEvents, m_useCurve);
    m_graph->updateGraph();
}

void BatteryHistoryDialog::onToggleScreenEvents(bool checked) {
    m_showScreenEvents = checked;
    m_graph->setParams(m_displayCount, m_showScreenEvents, m_showSystemEvents, m_useCurve);
    m_graph->updateGraph();
}

void BatteryHistoryDialog::onToggleGraphType() {
    m_useCurve = !m_useCurve;
    m_graphTypeBtn->setText(m_useCurve ? "Bars Style" : "Curves Style");
    m_graph->setParams(m_displayCount, m_showScreenEvents, m_showSystemEvents, m_useCurve);
    m_graph->updateGraph();
}
