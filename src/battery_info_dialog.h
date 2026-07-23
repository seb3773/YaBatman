#ifndef BATTERY_INFO_DIALOG_H
#define BATTERY_INFO_DIALOG_H

#include <tqdialog.h>
#include <tqlabel.h>

class InactivityManager;
class BatteryLogger;
class CalibrationManager;

class BatteryInfoDialog : public TQDialog {
    TQ_OBJECT
public:
    BatteryInfoDialog(InactivityManager *inactivity, CalibrationManager *calibration, TQWidget *parent = 0);
    ~BatteryInfoDialog();

protected:
    virtual void keyPressEvent(TQKeyEvent *e);

private slots:
    void onBatteryStatusChanged(int pct, int chg);
    void onCalibrateBattery();

private:
    void getBatterySysfsInfo();
    void setupUI();
    void updateUIValues();

    InactivityManager *m_inactivity;
    CalibrationManager *m_calibration;
    BatteryLogger *m_logger;
    TQString m_batteryPath;
    TQString m_batteryName;

    // Battery values
    TQString m_manufacturer;
    TQString m_model;
    TQString m_technology;
    int m_designCapacity;
    int m_fullCapacity;
    int m_currentCapacity;
    int m_capacityPercent;
    int m_voltageNow;
    int m_voltageMin;
    int m_powerNow;
    TQString m_status;
    int m_cycleCount;
    TQString m_serialNumber;
    double m_healthPercent;
    TQString m_healthDesc;

    // Rates
    double m_avgChargeRate;
    double m_avgDischargeRate;
    double m_currentRate;

    // Labels for dynamic updates
    TQLabel *m_currentCapVal;
    TQLabel *m_statusVal;
    TQLabel *m_cycleCountVal;
    TQLabel *m_remainingTimeVal;
    TQLabel *m_voltageVal;
    TQLabel *m_powerNowVal;
    TQLabel *m_avgChargeRateVal;
    TQLabel *m_avgDischargeRateVal;
    TQLabel *m_currentRateLbl;
    TQLabel *m_currentRateVal;
    TQLabel *m_healthPercentVal;
    TQLabel *m_healthDescVal;
    TQLabel *m_remainingEnergyVal;
    TQLabel *m_lastCalibVal;
    TQLabel *m_sec1Icon;
};

#endif // BATTERY_INFO_DIALOG_H
