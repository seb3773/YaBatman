#ifndef CALIBRATION_MANAGER_H
#define CALIBRATION_MANAGER_H

#include <tqobject.h>
#include <tqwidget.h>
#include <tqthread.h>
#include <tqvaluelist.h>
#include <tqlabel.h>
#include <tqpushbutton.h>
#include "inactivity_manager.h"

enum CalibrationState {
    CAL_INACTIVE = 0,
    CAL_PHASE1_CHARGING,
    CAL_WAITING_UNPLUG,
    CAL_PHASE2_DISCHARGING,
    CAL_WAITING_PLUG,
    CAL_PHASE3_CHARGING
};

class CpuBurnerThread : public TQThread {
public:
    CpuBurnerThread();
    void stop();
protected:
    virtual void run();
private:
    volatile bool m_stop;
};

class CalibrationOverlay : public TQWidget {
    TQ_OBJECT
public:
    CalibrationOverlay(TQWidget *parent = 0);
    ~CalibrationOverlay();

    void setStatus(const TQString& phase, const TQString& message, const TQColor& bg);
    void setChargerStatus(bool connected);
    void setBatteryPercentage(int pct);

signals:
    void cancelClicked();

protected:
    virtual void paintEvent(TQPaintEvent *e);
    virtual void keyPressEvent(TQKeyEvent *e);

private:
    TQPushButton *m_cancelBtn;
    TQColor m_bgColor;
    TQString m_phaseText;
    TQString m_messageText;
    bool m_chargerConnected;
    int m_batteryPct;
};

class CalibrationManager : public TQObject {
    TQ_OBJECT
public:
    CalibrationManager(InactivityManager *inactivityManager, YabatmanConfig *config, TQObject *parent = 0);
    ~CalibrationManager();

    void startCalibration();
    void cancelCalibration();
    void abortCalibration(const TQString& errorMessage);
    bool isActive() const { return m_state != CAL_INACTIVE; }

private slots:
    void onBatteryStatusChanged(int percentage, int chargingState);
    void onOverlayCancelled();
    void slotTransitionToWaitingUnplug();

private:
    void transitionTo(CalibrationState state);
    void startBurners();
    void stopBurners();
    void setBrightness(int val);

    InactivityManager *m_inactivity;
    YabatmanConfig *m_config;
    CalibrationState m_state;
    CalibrationOverlay *m_overlay;
    bool m_startedCharging;
    bool m_waitingForUnplugTransition;

    // CPU Burners
    TQValueList<CpuBurnerThread*> m_burners;
    int m_originalBrightness;
};

#endif // CALIBRATION_MANAGER_H
