#ifndef BATTERY_LOGGER_H
#define BATTERY_LOGGER_H

#include <time.h>
#include <stddef.h>
#include <tqvaluelist.h>

#define HISTORY_SIZE 1000
#define EVENTS_SIZE 250

enum SystemEventType {
    EVENT_SUSPEND = 1,
    EVENT_SUSPEND_THEN_HIBERNATE = 2,
    EVENT_HIBERNATE = 3,
    EVENT_HYBRID_SUSPEND = 4,
    EVENT_POWER_OFF = 5,
    EVENT_WAKE_UP = 6,
    EVENT_SCREEN_OFF = 7,
    EVENT_SCREEN_ON = 8,
    EVENT_CHARGER_CONNECTED = 9,
    EVENT_CHARGER_DISCONNECTED = 10
};

struct BatterySample {
    time_t timestamp;
    int capacity;
    int charging; // 0 = discharging, 1 = charging, 2 = full
};

struct BatteryHistory {
    BatterySample samples[HISTORY_SIZE];
    int next_index;
    int count;
};

struct SystemEvent {
    time_t timestamp;
    int event_type; // Maps to SystemEventType
    int capacity;
    int charging;
};

struct SystemEvents {
    SystemEvent events[EVENTS_SIZE];
    int next_index;
    int count;
    time_t last_cleanup;
};

struct BatteryLog {
    BatteryHistory battery_history;
    SystemEvents system_events;
};

struct ActiveInterval {
    time_t start;
    time_t end;
};

struct ScreenTimes {
    long long screen_off;
    long long screen_on;
};

class BatteryLogger {
public:
    BatteryLogger();
    ~BatteryLogger();

    void load();
    void save();
    void clear();

    void addSample(int capacity, int charging);
    void addEvent(SystemEventType event_type, int capacity, int charging);

    // Rate calculations
    void getAverageRates(double& avgChargeRate, double& avgDischargeRate);
    bool isAlways100PercentOver72h() const;

    // History getters for graph plotting
    const BatteryLog& getBatteryLog() const { return m_log; }
    
    // Active periods and screen times helpers
    TQValueList<ActiveInterval> calculateActivePeriods(time_t startTime, time_t endTime) const;
    ScreenTimes calculateScreenTimes(const TQValueList<ActiveInterval>& activePeriods, time_t oldestSampleTime) const;
    long long calculateSleepTime(time_t startTime, time_t endTime, int hibernateDelaySec) const;

private:
    void initLogPath();
    void cleanupOldEvents();

    BatteryLog m_log;
    char m_logPath[512];
};

#endif // BATTERY_LOGGER_H
