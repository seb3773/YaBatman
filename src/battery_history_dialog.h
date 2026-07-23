#ifndef BATTERY_HISTORY_DIALOG_H
#define BATTERY_HISTORY_DIALOG_H

#include <tqdialog.h>
#include <tqwidget.h>
#include <tqcombobox.h>
#include <tqcheckbox.h>
#include <tqpushbutton.h>
#include "battery_logger.h"

class BatteryHistoryGraph : public TQWidget {
    TQ_OBJECT
public:
    BatteryHistoryGraph(BatteryLogger *logger, TQWidget *parent = 0);
    ~BatteryHistoryGraph();

    void setParams(int displayCount, bool showScreenEvents, bool showSystemEvents, bool useCurve);
    void updateGraph();

protected:
    virtual void paintEvent(TQPaintEvent *e);
    virtual void resizeEvent(TQResizeEvent *e);
    virtual void mouseMoveEvent(TQMouseEvent *e);
    virtual void leaveEvent(TQEvent *e);

private:
    void renderAA(TQImage& img);
    void drawGrid(TQPainter& p, int w, int h, double marginLeft, double marginRight, double marginTop, double graphHeight);
    void drawEvents(TQPainter& p, int w, int h, double marginLeft, double graphWidth, double graphHeight, double barWidth, double offsetPx, double barsWidth, time_t startTime, time_t endTime);
    void drawTooltip(TQPainter& p, int w, int h);

    BatteryLogger *m_logger;
    int m_displayCount;
    bool m_showScreenEvents;
    bool m_showSystemEvents;
    bool m_useCurve;

    // Hover variables for tooltips
    bool m_hovered;
    int m_hoverX;
    int m_hoverY;

    // Layout margins cached during paint
    double m_lastMarginLeft;
    double m_lastGraphWidth;
    double m_lastBarWidth;
    double m_lastOffsetPx;
    double m_lastBarsWidth;
    time_t m_lastStartTime;
    time_t m_lastEndTime;
};

class BatteryHistoryDialog : public TQDialog {
    TQ_OBJECT
public:
    BatteryHistoryDialog(BatteryLogger *logger, TQWidget *parent = 0);
    ~BatteryHistoryDialog();

protected:
    virtual void keyPressEvent(TQKeyEvent *e);

private slots:
    void onPeriodChanged(int index);
    void onToggleSystemEvents(bool checked);
    void onToggleScreenEvents(bool checked);
    void onToggleGraphType();

private:
    void setupUI();

    BatteryLogger *m_logger;
    BatteryHistoryGraph *m_graph;

    TQComboBox *m_periodCombo;
    TQCheckBox *m_systemEventsCheck;
    TQCheckBox *m_screenEventsCheck;
    TQPushButton *m_graphTypeBtn;

    int m_displayCount;
    bool m_showSystemEvents;
    bool m_showScreenEvents;
    bool m_useCurve;
};

#endif // BATTERY_HISTORY_DIALOG_H
