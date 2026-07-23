#include "battery_logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <limits.h>
#include <math.h>

BatteryLogger::BatteryLogger() {
    memset(&m_log, 0, sizeof(BatteryLog));
    m_log.system_events.last_cleanup = time(NULL);
    initLogPath();
}

BatteryLogger::~BatteryLogger() {}

void BatteryLogger::initLogPath() {
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
        else home = ".";
    }
    snprintf(m_logPath, sizeof(m_logPath), "%s/.config/yabatman/battery_history.bin", home);
    
    // Ensure parent directories exist
    char tempPath[512];
    strncpy(tempPath, m_logPath, sizeof(tempPath));
    tempPath[sizeof(tempPath) - 1] = '\0';
    
    // Find directory portion
    char *lastSlash = strrchr(tempPath, '/');
    if (lastSlash) {
        *lastSlash = '\0';
        struct stat st = {0};
        if (stat(tempPath, &st) == -1) {
            // Recurse directory creation for ~/.config/yabatman
            char subPath[512];
            char *p = tempPath;
            while (*p) {
                if (*p == '/' && p != tempPath) {
                    *p = '\0';
                    mkdir(tempPath, 0700);
                    *p = '/';
                }
                p++;
            }
            mkdir(tempPath, 0700);
        }
    }
}

void BatteryLogger::load() {
    struct stat st = {0};
    if (stat(m_logPath, &st) == 0) {
        if (st.st_size != (off_t)sizeof(BatteryLog)) {
            unlink(m_logPath);
            clear();
            return;
        }
    } else {
        const char *home = getenv("HOME");
        if (!home) {
            struct passwd *pw = getpwuid(getuid());
            if (pw) home = pw->pw_dir;
            else home = ".";
        }
        char oldPath[512];
        snprintf(oldPath, sizeof(oldPath), "%s/.yabatman/battery_history.bin", home);
        if (stat(oldPath, &st) == 0) {
            if (st.st_size != (off_t)sizeof(BatteryLog)) {
                unlink(oldPath);
            }
        }
    }

    FILE *f = fopen(m_logPath, "rb");
    if (!f) {
        // Fallback to old path ~/.yabatman/battery_history.bin if it exists
        const char *home = getenv("HOME");
        if (!home) {
            struct passwd *pw = getpwuid(getuid());
            if (pw) home = pw->pw_dir;
            else home = ".";
        }
        char oldPath[512];
        snprintf(oldPath, sizeof(oldPath), "%s/.yabatman/battery_history.bin", home);
        f = fopen(oldPath, "rb");
        if (!f) {
            clear();
            return;
        }
    }
    size_t readCount = fread(&m_log, sizeof(BatteryLog), 1, f);
    fclose(f);

    if (readCount != 1) {
        clear();
    }
}

void BatteryLogger::save() {
    FILE *f = fopen(m_logPath, "wb");
    if (!f) {
        return;
    }
    fwrite(&m_log, sizeof(BatteryLog), 1, f);
    fclose(f);
}

void BatteryLogger::clear() {
    memset(&m_log, 0, sizeof(BatteryLog));
    m_log.system_events.last_cleanup = time(NULL);
}

void BatteryLogger::addSample(int capacity, int charging) {
    time_t now = time(NULL);
    int nextIdx = m_log.battery_history.next_index;
    
    m_log.battery_history.samples[nextIdx].timestamp = now;
    m_log.battery_history.samples[nextIdx].capacity = capacity;
    m_log.battery_history.samples[nextIdx].charging = charging;
    
    m_log.battery_history.next_index = (nextIdx + 1) % HISTORY_SIZE;
    if (m_log.battery_history.count < HISTORY_SIZE) {
        m_log.battery_history.count++;
    }

    cleanupOldEvents();
}

void BatteryLogger::addEvent(SystemEventType event_type, int capacity, int charging) {
    time_t now = time(NULL);
    int nextIdx = m_log.system_events.next_index;

    m_log.system_events.events[nextIdx].timestamp = now;
    m_log.system_events.events[nextIdx].event_type = event_type;
    m_log.system_events.events[nextIdx].capacity = capacity;
    m_log.system_events.events[nextIdx].charging = charging;

    m_log.system_events.next_index = (nextIdx + 1) % EVENTS_SIZE;
    if (m_log.system_events.count < EVENTS_SIZE) {
        m_log.system_events.count++;
    }
}

void BatteryLogger::cleanupOldEvents() {
    time_t now = time(NULL);
    // cutoff: 3 days = 72 hours
    time_t cutoff = now - (3 * 24 * 3600);
    
    if (now - m_log.system_events.last_cleanup < 24 * 3600) {
        return;
    }

    SystemEvent validEvents[EVENTS_SIZE];
    int validCount = 0;

    int totalEvents = m_log.system_events.count;
    int nextIdx = m_log.system_events.next_index;

    for (int i = 0; i < totalEvents; i++) {
        int readIdx = (nextIdx - totalEvents + i + EVENTS_SIZE) % EVENTS_SIZE;
        if (m_log.system_events.events[readIdx].timestamp >= cutoff) {
            validEvents[validCount++] = m_log.system_events.events[readIdx];
        }
    }

    memset(m_log.system_events.events, 0, sizeof(m_log.system_events.events));
    for (int i = 0; i < validCount; i++) {
        m_log.system_events.events[i] = validEvents[i];
    }

    m_log.system_events.count = validCount;
    m_log.system_events.next_index = validCount % EVENTS_SIZE;
    m_log.system_events.last_cleanup = now;
}

static inline bool isCutEvent(int t) {
    return  t == EVENT_SUSPEND               ||
            t == EVENT_HIBERNATE             ||
            t == EVENT_HYBRID_SUSPEND        ||
            t == EVENT_SUSPEND_THEN_HIBERNATE||
            t == EVENT_POWER_OFF;
}

void BatteryLogger::getAverageRates(double& avgChargeRate, double& avgDischargeRate) {
    avgChargeRate = 0.0;
    avgDischargeRate = 0.0;

    int historyCount = m_log.battery_history.count;
    if (historyCount < 2) {
        return;
    }

    double charge_d = 0.0, charge_t = 0.0;
    double discharge_d = 0.0, discharge_t = 0.0;

    int h_idx = (m_log.battery_history.next_index - historyCount + HISTORY_SIZE) % HISTORY_SIZE;
    int h_left = historyCount;

    int e_idx = 0, e_left = 0;
    int eventCount = m_log.system_events.count;
    if (eventCount > 0) {
        e_idx = (m_log.system_events.next_index - eventCount + EVENTS_SIZE) % EVENTS_SIZE;
        e_left = eventCount;
    }

    const BatterySample* first = &m_log.battery_history.samples[h_idx];
    time_t last_time = first->timestamp;
    int last_cap = first->capacity;
    int charging = first->charging;

    while (h_left > 1 || e_left > 0) {
        time_t next_sample_time = (h_left > 1)
            ? m_log.battery_history.samples[(h_idx + 1) % HISTORY_SIZE].timestamp
            : LONG_MAX;

        time_t next_event_time = (e_left > 0)
            ? m_log.system_events.events[e_idx].timestamp
            : LONG_MAX;

        if (next_sample_time <= next_event_time) {
            const BatterySample* s = &m_log.battery_history.samples[(h_idx + 1) % HISTORY_SIZE];
            double dt = difftime(s->timestamp, last_time) / 3600.0;

            if (dt > 0 && charging != -1) {
                if (charging == 1 && s->capacity > last_cap) {
                    charge_d += s->capacity - last_cap;
                    charge_t += dt;
                } else if (charging == 0 && s->capacity < last_cap) {
                    discharge_d += last_cap - s->capacity;
                    discharge_t += dt;
                }
            }

            last_time = s->timestamp;
            last_cap = s->capacity;
            charging = s->charging;

            h_idx = (h_idx + 1) % HISTORY_SIZE;
            --h_left;
        } else {
            const SystemEvent* ev = &m_log.system_events.events[e_idx];

            if (ev->timestamp > last_time) {
                last_time = ev->timestamp;
            }

            if (isCutEvent(ev->event_type)) {
                charging = -1; // suspend measurement
            } else if (ev->event_type == EVENT_CHARGER_CONNECTED) {
                charging = 1;
            } else if (ev->event_type == EVENT_CHARGER_DISCONNECTED) {
                charging = 0;
            }

            e_idx = (e_idx + 1) % EVENTS_SIZE;
            --e_left;
        }
    }

    avgChargeRate = (charge_t > 0.0) ? charge_d / charge_t : 0.0;
    avgDischargeRate = (discharge_t > 0.0) ? discharge_d / discharge_t : 0.0;
}

bool BatteryLogger::isAlways100PercentOver72h() const {
    if (m_log.battery_history.count < HISTORY_SIZE) {
        return false;
    }
    for (int i = 0; i < HISTORY_SIZE; i++) {
        if (m_log.battery_history.samples[i].capacity != 100) {
            return false;
        }
    }
    return true;
}

TQValueList<ActiveInterval> BatteryLogger::calculateActivePeriods(time_t startTime, time_t endTime) const {
    TQValueList<ActiveInterval> result;
    int eventCount = m_log.system_events.count;
    int nextIdx = m_log.system_events.next_index;

    // Scan backwards from oldest to determine system_active state right before startTime
    int system_active = 1;
    
    // Find the first power-related event in the buffer to initialize system_active
    for (int i = 0; i < eventCount; i++) {
        int idx = (nextIdx - eventCount + i + EVENTS_SIZE) % EVENTS_SIZE;
        const SystemEvent* e = &m_log.system_events.events[idx];
        if (e->event_type == EVENT_WAKE_UP) {
            system_active = 0;
            break;
        }
        if (e->event_type == EVENT_SUSPEND || e->event_type == EVENT_HIBERNATE ||
            e->event_type == EVENT_SUSPEND_THEN_HIBERNATE || e->event_type == EVENT_POWER_OFF) {
            system_active = 1;
            break;
        }
    }

    for (int i = 0; i < eventCount; i++) {
        int idx = (nextIdx - eventCount + i + EVENTS_SIZE) % EVENTS_SIZE;
        const SystemEvent* e = &m_log.system_events.events[idx];
        if (e->timestamp >= startTime) break;

        bool is_boot = (e->event_type == EVENT_WAKE_UP && i > 0 &&
                        m_log.system_events.events[(idx - 1 + EVENTS_SIZE) % EVENTS_SIZE].event_type == EVENT_POWER_OFF);

        if (e->event_type == EVENT_WAKE_UP || is_boot) {
            system_active = 1;
        } else if (e->event_type == EVENT_SUSPEND || e->event_type == EVENT_HIBERNATE ||
                   e->event_type == EVENT_SUSPEND_THEN_HIBERNATE || e->event_type == EVENT_POWER_OFF) {
            system_active = 0;
        }
    }

    time_t period_start = startTime;

    for (int i = 0; i < eventCount; i++) {
        int idx = (nextIdx - eventCount + i + EVENTS_SIZE) % EVENTS_SIZE;
        const SystemEvent* e = &m_log.system_events.events[idx];
        if (e->timestamp < startTime) continue;
        if (e->timestamp > endTime) break;

        bool is_boot = (e->event_type == EVENT_WAKE_UP && i > 0 &&
                        m_log.system_events.events[(idx - 1 + EVENTS_SIZE) % EVENTS_SIZE].event_type == EVENT_POWER_OFF);

        if ((e->event_type == EVENT_WAKE_UP || is_boot) && !system_active) {
            system_active = 1;
            period_start = e->timestamp;
        } else if ((e->event_type == EVENT_SUSPEND || e->event_type == EVENT_HIBERNATE ||
                    e->event_type == EVENT_SUSPEND_THEN_HIBERNATE || e->event_type == EVENT_POWER_OFF) && system_active) {
            if (period_start >= startTime && e->timestamp <= endTime) {
                ActiveInterval interval;
                interval.start = period_start;
                interval.end = e->timestamp;
                result.append(interval);
            }
            system_active = 0;
        }
    }

    if (system_active && period_start <= endTime) {
        ActiveInterval interval;
        interval.start = period_start;
        interval.end = endTime;
        result.append(interval);
    }

    return result;
}

static inline bool isOnEvent(int event_type) {
    return event_type == EVENT_SCREEN_ON || event_type == EVENT_WAKE_UP;
}

static inline bool isOffEvent(int event_type) {
    return event_type == EVENT_SCREEN_OFF ||
           event_type == EVENT_SUSPEND ||
           event_type == EVENT_SUSPEND_THEN_HIBERNATE ||
           event_type == EVENT_HIBERNATE ||
           event_type == EVENT_HYBRID_SUSPEND ||
           event_type == EVENT_POWER_OFF;
}

ScreenTimes BatteryLogger::calculateScreenTimes(const TQValueList<ActiveInterval>& activePeriods, time_t oldestSampleTime) const {
    (void)oldestSampleTime; // Unused now, scanning historical events directly
    ScreenTimes result = {0, 0};
    int eventCount = m_log.system_events.count;
    int nextIdx = m_log.system_events.next_index;

    for (TQValueList<ActiveInterval>::ConstIterator it = activePeriods.begin(); it != activePeriods.end(); ++it) {
        time_t period_start = (*it).start;
        time_t period_end = (*it).end;
        time_t now = time(NULL);
        if (period_end > now) period_end = now;

        long long period_duration = period_end - period_start;
        if (period_duration <= 0) continue;

        int screen_on = 1;
        
        // Find the first screen-related event in the buffer to initialize screen_on
        for (int i = 0; i < eventCount; i++) {
            int idx = (nextIdx - eventCount + i + EVENTS_SIZE) % EVENTS_SIZE;
            const SystemEvent* e = &m_log.system_events.events[idx];
            if (e->event_type == EVENT_SCREEN_ON) {
                screen_on = 0;
                break;
            }
            if (e->event_type == EVENT_SCREEN_OFF) {
                screen_on = 1;
                break;
            }
        }

        // Establish screen_on state before period_start by scanning historical buffer
        for (int i = 0; i < eventCount; i++) {
            int idx = (nextIdx - eventCount + i + EVENTS_SIZE) % EVENTS_SIZE;
            const SystemEvent* e = &m_log.system_events.events[idx];
            if (e->timestamp >= period_start) break;
            if (e->event_type == EVENT_SCREEN_ON) screen_on = 1;
            else if (e->event_type == EVENT_SCREEN_OFF) screen_on = 0;
        }

        long long screen_off_time = 0;
        time_t last_off = 0;
        int in_off = screen_on ? 0 : 1;
        if (in_off) last_off = period_start;

        for (int i = 0; i < eventCount; i++) {
            int idx = (nextIdx - eventCount + i + EVENTS_SIZE) % EVENTS_SIZE;
            const SystemEvent* e = &m_log.system_events.events[idx];
            if (e->timestamp < period_start) continue;
            if (e->timestamp > period_end) break;

            if (e->event_type == EVENT_SCREEN_OFF && !in_off) {
                in_off = 1;
                last_off = e->timestamp;
            } else if (e->event_type == EVENT_SCREEN_ON && in_off) {
                in_off = 0;
                screen_off_time += e->timestamp - last_off;
            }
        }

        if (in_off && last_off >= period_start) {
            screen_off_time += period_end - last_off;
        }

        result.screen_off += screen_off_time;
        result.screen_on += period_duration - screen_off_time;
    }

    return result;
}

#include <tqfile.h>
#include <tqtextstream.h>
#include <tqdir.h>
#include <tqregexp.h>

static int parseSystemdDuration(const TQString& valStr) {
    TQString cleanStr = valStr.stripWhiteSpace();
    if (cleanStr.isEmpty()) return 1200; // default 20 minutes

    TQRegExp rx("^(\\d+)(s|min|h|d|minutess?|hours?|days?|seconds?)?$");
    if (rx.search(cleanStr) != -1) {
        int val = rx.cap(1).toInt();
        TQString unit = rx.cap(2).lower();
        if (unit == "min" || unit.startsWith("minute")) {
            return val * 60;
        } else if (unit == "h" || unit.startsWith("hour")) {
            return val * 3600;
        } else if (unit == "d" || unit.startsWith("day")) {
            return val * 86400;
        } else {
            return val; // seconds by default
        }
    }
    return cleanStr.toInt();
}

static int readSystemdHibernateDelaySec() {
    int delay = 1200; // default 20 minutes
    TQStringList files;
    files.append("/etc/systemd/sleep.conf");

    // Scan standard systemd drop-in directories
    TQStringList dropinDirs;
    dropinDirs.append("/usr/lib/systemd/sleep.conf.d");
    dropinDirs.append("/run/systemd/sleep.conf.d");
    dropinDirs.append("/etc/systemd/sleep.conf.d");

    for (TQStringList::Iterator it = dropinDirs.begin(); it != dropinDirs.end(); ++it) {
        TQDir dir(*it);
        if (dir.exists()) {
            TQStringList list = dir.entryList("*.conf", TQDir::Files, TQDir::Name);
            for (TQStringList::Iterator fit = list.begin(); fit != list.end(); ++fit) {
                files.append(*it + "/" + *fit);
            }
        }
    }

    for (TQStringList::Iterator it = files.begin(); it != files.end(); ++it) {
        TQFile file(*it);
        if (file.open(IO_ReadOnly)) {
            TQTextStream stream(&file);
            TQString line;
            while (!stream.atEnd()) {
                line = stream.readLine().stripWhiteSpace();
                if (line.startsWith("#") || line.startsWith(";")) continue;
                if (line.contains("HibernateDelaySec")) {
                    int eq = line.find('=');
                    if (eq != -1) {
                        TQString valStr = line.mid(eq + 1).stripWhiteSpace();
                        delay = parseSystemdDuration(valStr);
                    }
                }
            }
            file.close();
        }
    }
    return delay;
}

long long BatteryLogger::calculateSleepTime(time_t startTime, time_t endTime, int hibernateDelaySec) const {
    if (endTime <= startTime) return 0;
    long long total_sleep = 0;

    int systemdDelay = readSystemdHibernateDelaySec();
    if (systemdDelay <= 0) systemdDelay = hibernateDelaySec;

    int eventCount = m_log.system_events.count;
    int nextIdx = m_log.system_events.next_index;

    time_t last_suspend_time = 0;
    int last_suspend_type = 0; // 1: SUSPEND, 2: SUSPEND_THEN_HIBERNATE, 3: HYBRID_SUSPEND, 4: HIBERNATE

    for (int i = 0; i < eventCount; i++) {
        int idx = (nextIdx - eventCount + i + EVENTS_SIZE) % EVENTS_SIZE;
        const SystemEvent* e = &m_log.system_events.events[idx];
        if (e->timestamp > endTime) break;

        if (e->event_type == EVENT_SUSPEND) {
            last_suspend_time = e->timestamp;
            last_suspend_type = 1;
        } else if (e->event_type == EVENT_SUSPEND_THEN_HIBERNATE) {
            last_suspend_time = e->timestamp;
            last_suspend_type = 2;
        } else if (e->event_type == EVENT_HYBRID_SUSPEND) {
            last_suspend_time = e->timestamp;
            last_suspend_type = 3;
        } else if (e->event_type == EVENT_HIBERNATE) {
            last_suspend_time = e->timestamp;
            last_suspend_type = 4;
        } else if (e->event_type == EVENT_WAKE_UP) {
            if (last_suspend_time > 0) {
                time_t sleep_start = last_suspend_time;
                time_t sleep_end = e->timestamp;

                // Check overlap with the target timeline interval
                if (sleep_end > startTime && sleep_start < endTime) {
                    if (sleep_start < startTime) sleep_start = startTime;
                    if (sleep_end > endTime) sleep_end = endTime;

                    if (last_suspend_type == 1 || last_suspend_type == 3) {
                        total_sleep += (sleep_end - sleep_start);
                    } else if (last_suspend_type == 2) {
                        long long original_delay = sleep_start - last_suspend_time;
                        long long remaining_delay = systemdDelay - original_delay;
                        if (remaining_delay < 0) remaining_delay = 0;

                        long long duration = sleep_end - sleep_start;
                        total_sleep += duration <= remaining_delay ? duration : remaining_delay;
                    } else if (last_suspend_type == 4) {
                        total_sleep += 0;
                    }
                }
                last_suspend_time = 0;
                last_suspend_type = 0;
            }
        }
    }

    // Handle ongoing suspend at the end of the timeline
    if (last_suspend_time > 0 && last_suspend_time < endTime) {
        time_t sleep_start = last_suspend_time;
        time_t sleep_end = endTime;

        if (sleep_start < startTime) sleep_start = startTime;

        if (last_suspend_type == 1 || last_suspend_type == 3) {
            total_sleep += (sleep_end - sleep_start);
        } else if (last_suspend_type == 2) {
            long long original_delay = sleep_start - last_suspend_time;
            long long remaining_delay = systemdDelay - original_delay;
            if (remaining_delay < 0) remaining_delay = 0;

            long long duration = sleep_end - sleep_start;
            total_sleep += duration <= remaining_delay ? duration : remaining_delay;
        }
    }

    return total_sleep;
}
