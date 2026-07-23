#include "config_manager.h"
#ifdef PURE_TQT3
#include <tqfile.h>
#include <tqtextstream.h>
#include <tqdir.h>
#include <tqmap.h>
#include <stdlib.h>

class SimpleConfig {
public:
    SimpleConfig(const TQString &filename) {
        const char *home = getenv("HOME");
        TQString configDir = TQString(home ? home : ".") + "/.config/yabatman";
        TQDir dir;
        dir.mkdir(configDir, true);
        m_filePath = configDir + "/" + filename;
        load();
    }

    void setGroup(const TQString &group) {
        m_currentGroup = group;
    }

    int readNumEntry(const TQString &key, int defaultVal) {
        if (m_data.contains(m_currentGroup) && m_data[m_currentGroup].contains(key)) {
            bool ok = false;
            int val = m_data[m_currentGroup][key].toInt(&ok);
            if (ok) return val;
        }
        return defaultVal;
    }

    bool readBoolEntry(const TQString &key, bool defaultVal) {
        if (m_data.contains(m_currentGroup) && m_data[m_currentGroup].contains(key)) {
            TQString val = m_data[m_currentGroup][key].lower();
            if (val == "true" || val == "1") return true;
            if (val == "false" || val == "0") return false;
        }
        return defaultVal;
    }

    double readDoubleNumEntry(const TQString &key, double defaultVal) {
        if (m_data.contains(m_currentGroup) && m_data[m_currentGroup].contains(key)) {
            bool ok = false;
            double val = m_data[m_currentGroup][key].toDouble(&ok);
            if (ok) return val;
        }
        return defaultVal;
    }

    TQString readEntry(const TQString &key, const TQString &defaultVal) {
        if (m_data.contains(m_currentGroup) && m_data[m_currentGroup].contains(key)) {
            return m_data[m_currentGroup][key];
        }
        return defaultVal;
    }

    TQString readPathEntry(const TQString &key, const TQString &defaultVal) {
        return readEntry(key, defaultVal);
    }

    TQStringList readListEntry(const TQString &key, const TQStringList &defaultVal) {
        if (m_data.contains(m_currentGroup) && m_data[m_currentGroup].contains(key)) {
            TQString str = m_data[m_currentGroup][key];
            if (str.isEmpty()) return TQStringList();
            return TQStringList::split(",", str);
        }
        return defaultVal;
    }

    void writeEntry(const TQString &key, int val) {
        m_data[m_currentGroup][key] = TQString::number(val);
    }

    void writeEntry(const TQString &key, bool val) {
        m_data[m_currentGroup][key] = val ? "true" : "false";
    }

    void writeEntry(const TQString &key, double val) {
        m_data[m_currentGroup][key] = TQString::number(val);
    }

    void writeEntry(const TQString &key, const TQString &val) {
        m_data[m_currentGroup][key] = val;
    }

    void writeEntry(const TQString &key, const TQStringList &val) {
        m_data[m_currentGroup][key] = val.join(",");
    }

    void writePathEntry(const TQString &key, const TQString &val) {
        m_data[m_currentGroup][key] = val;
    }

    void sync() {
        TQFile file(m_filePath);
        if (file.open(IO_WriteOnly | IO_Truncate)) {
            TQTextStream stream(&file);
            for (TQMap<TQString, TQMap<TQString, TQString> >::Iterator git = m_data.begin(); git != m_data.end(); ++git) {
                stream << "[" << git.key() << "]\n";
                for (TQMap<TQString, TQString>::Iterator kit = git.data().begin(); kit != git.data().end(); ++kit) {
                    stream << kit.key() << "=" << kit.data() << "\n";
                }
                stream << "\n";
            }
            file.close();
        }
    }

private:
    void load() {
        m_data.clear();
        TQFile file(m_filePath);
        if (file.open(IO_ReadOnly)) {
            TQTextStream stream(&file);
            TQString currentGroup = "General";
            while (!stream.atEnd()) {
                TQString line = stream.readLine().stripWhiteSpace();
                if (line.isEmpty() || line.startsWith("#") || line.startsWith(";")) continue;
                if (line.startsWith("[") && line.endsWith("]")) {
                    currentGroup = line.mid(1, line.length() - 2);
                } else {
                    int idx = line.find('=');
                    if (idx > 0) {
                        TQString key = line.left(idx).stripWhiteSpace();
                        TQString val = line.mid(idx + 1).stripWhiteSpace();
                        m_data[currentGroup][key] = val;
                    }
                }
            }
            file.close();
        }
    }

    TQString m_filePath;
    TQString m_currentGroup;
    TQMap<TQString, TQMap<TQString, TQString> > m_data;
};

#define TDEConfig SimpleConfig
#else
#include <tdeconfig.h>
#endif

ConfigManager::ConfigManager() {}
ConfigManager::~ConfigManager() {}

void ConfigManager::loadDefaults(YabatmanConfig& config) {
    // General
    config.power_button = 2; // 2 = hibernate
    config.sleep_button = 1; // 1 = sleep_then_hibernate
    config.backlight_slider = 1;
    config.status_notifs = 1;
    config.notif_charger = true;
    config.notif_full = true;
    config.notif_low = true;
    config.notif_critical = true;
    config.last_calibration = "";

    // Battery Profile
    config.bat_backlight_reduce_timeout = 1;
    config.bat_display_sleep_timeout = 2;
    config.bat_idle_timeout = 20;
    config.bat_energy_saving = 1;
    config.bat_lid_action = 1;
    config.bat_power_profile = 1;

    // AC Profile
    config.ac_backlight_reduce_timeout = 1;
    config.ac_display_sleep_timeout = 2;
    config.ac_idle_timeout = 50;
    config.ac_energy_saving = 1;
    config.ac_lid_action = 1;
    config.ac_power_profile = 1;
    config.ac_full_power_profile = 2;

    // Levels
    config.warn_level = 20;
    config.critical_level = 10;
    config.critical_action = 0; // 0 = hibernate

    // Adaptive features
    config.reduce_brightness_more_during_idle = true;
    config.reduce_brightness_when_charge_decrease = true;
    config.adjust_brightness_when_status_change = true;
    config.timeouts_auto_adapt = true;
    config.minimal_state_before_suspend = true;

    // Freezing
    config.lowbat_freeze_services = true;
    config.whitelist.clear();
    config.whitelist.append("colord");
    config.blacklist.clear();
    config.blacklist.append("ollama");

    config.critical_freeze_processes = true;
    config.p_whitelist.clear();
    config.p_whitelist.append("tdesudo");
    config.p_whitelist.append("sudo");
    config.p_whitelist.append("tdenetworkmanager");
    config.p_blacklist.clear();
    config.p_blacklist.append("kraptor");

    // Connectivity
    config.disable_eth = 2; // on battery
    config.lowbat_bt_off_on_display_off = true;
    config.ac_lid_enable_powernap = true;
    config.ac_lid_powernap_mode_disable_bt = true;
    config.ac_lid_powernap_mode_disable_wifi = false;

    // Locking
    config.lock_on_display_off = 0;
    config.lock_on_sleep = 1;
    config.authorized_ssids.clear();
    config.authorized_ssids.append("TECH-INFO");
    config.authorized_ssids.append("Minabox");
    config.icon_blink_on_critical = true;

    // Appearance
    config.tv_effect_on_suspend_and_shutdown = 4; // random
    config.ac_screensaver = "random";
    config.slideshow_image_dir = ""; // will fall back to Pictures dir
    config.slideshow_random_order = false;
    config.slideshow_zoom_effect = true;
    config.close_popup_animation = true;
    config.dark_mode = 0;
    config.popup_opacity = 1.0;
    config.presentation_mode_icon = true;
    config.media_mode_icon = true;
    config.coloured_icon = false;
    config.animate_charge_icon = false;

    // Tints (Popup)
    config.tint_popup_r = 245;
    config.tint_popup_g = 245;
    config.tint_popup_b = 245;

    // Tints (Icon normal)
    config.custom_color_full = false;
    config.custom_color_normal = false;
    config.custom_color_low = false;
    config.custom_color_critical = false;

    config.tint_icon_full_r = 40;
    config.tint_icon_full_g = 200;
    config.tint_icon_full_b = 40;

    config.tint_icon_normal_r = 15;
    config.tint_icon_normal_g = 15;
    config.tint_icon_normal_b = 245;

    // Tints (Icon warning)
    config.tint_icon_warning_r = 205;
    config.tint_icon_warning_g = 100;
    config.tint_icon_warning_b = 0;

    // Tints (Icon critical)
    config.tint_icon_critical_r = 245;
    config.tint_icon_critical_g = 45;
    config.tint_icon_critical_b = 45;

    // Advanced
    config.charge_limit_enabled = false;
    config.charge_limit_value = 100;
    config.eco_freq_cap = 40;
    config.balanced_usb_autosuspend = true;
}

void ConfigManager::load(YabatmanConfig& config) {
    loadDefaults(config);

    TDEConfig tdeConfig("yabatmanrc");

    // General
    tdeConfig.setGroup("General");
    config.power_button = tdeConfig.readNumEntry("PowerButton", config.power_button);
    config.sleep_button = tdeConfig.readNumEntry("SleepButton", config.sleep_button);
    config.backlight_slider = tdeConfig.readNumEntry("BacklightSlider", config.backlight_slider);
    config.status_notifs = tdeConfig.readNumEntry("StatusNotifs", config.status_notifs);
    config.notif_charger = tdeConfig.readBoolEntry("NotifCharger", config.notif_charger);
    config.notif_full = tdeConfig.readBoolEntry("NotifFull", config.notif_full);
    config.notif_low = tdeConfig.readBoolEntry("NotifLow", config.notif_low);
    config.notif_critical = tdeConfig.readBoolEntry("NotifCritical", config.notif_critical);

    // Battery Profile
    tdeConfig.setGroup("BatteryProfile");
    config.bat_backlight_reduce_timeout = tdeConfig.readNumEntry("ReduceTimeout", config.bat_backlight_reduce_timeout);
    config.bat_display_sleep_timeout = tdeConfig.readNumEntry("DisplaySleepTimeout", config.bat_display_sleep_timeout);
    config.bat_idle_timeout = tdeConfig.readNumEntry("IdleTimeout", config.bat_idle_timeout);
    config.bat_energy_saving = tdeConfig.readNumEntry("EnergySaving", config.bat_energy_saving);
    config.bat_lid_action = tdeConfig.readNumEntry("LidAction", config.bat_lid_action);
    config.bat_power_profile = tdeConfig.readNumEntry("PowerProfile", config.bat_power_profile);

    // AC Profile
    tdeConfig.setGroup("ACProfile");
    config.ac_backlight_reduce_timeout = tdeConfig.readNumEntry("ReduceTimeout", config.ac_backlight_reduce_timeout);
    config.ac_display_sleep_timeout = tdeConfig.readNumEntry("DisplaySleepTimeout", config.ac_display_sleep_timeout);
    config.ac_idle_timeout = tdeConfig.readNumEntry("IdleTimeout", config.ac_idle_timeout);
    config.ac_energy_saving = tdeConfig.readNumEntry("EnergySaving", config.ac_energy_saving);
    config.ac_lid_action = tdeConfig.readNumEntry("LidAction", config.ac_lid_action);
    config.ac_power_profile = tdeConfig.readNumEntry("PowerProfile", config.ac_power_profile);
    config.ac_full_power_profile = tdeConfig.readNumEntry("FullPowerProfile", config.ac_full_power_profile);

    // Levels
    tdeConfig.setGroup("Levels");
    config.warn_level = tdeConfig.readNumEntry("WarnLevel", config.warn_level);
    config.critical_level = tdeConfig.readNumEntry("CriticalLevel", config.critical_level);
    config.critical_action = tdeConfig.readNumEntry("CriticalAction", config.critical_action);

    // Adaptive features
    tdeConfig.setGroup("AdaptiveFeatures");
    config.reduce_brightness_more_during_idle = tdeConfig.readBoolEntry("ReduceBrightnessMoreDuringIdle", config.reduce_brightness_more_during_idle);
    config.reduce_brightness_when_charge_decrease = tdeConfig.readBoolEntry("ReduceBrightnessWhenChargeDecrease", config.reduce_brightness_when_charge_decrease);
    config.adjust_brightness_when_status_change = tdeConfig.readBoolEntry("AdjustBrightnessWhenStatusChange", config.adjust_brightness_when_status_change);
    config.timeouts_auto_adapt = tdeConfig.readBoolEntry("TimeoutsAutoAdapt", config.timeouts_auto_adapt);
    config.minimal_state_before_suspend = tdeConfig.readBoolEntry("MinimalStateBeforeSuspend", config.minimal_state_before_suspend);

    // Freezing
    tdeConfig.setGroup("Freezing");
    config.lowbat_freeze_services = tdeConfig.readBoolEntry("LowBatFreezeServices", config.lowbat_freeze_services);
    config.whitelist = tdeConfig.readListEntry("ServicesWhitelist", config.whitelist);
    config.blacklist = tdeConfig.readListEntry("ServicesBlacklist", config.blacklist);
    config.critical_freeze_processes = tdeConfig.readBoolEntry("CriticalFreezeProcesses", config.critical_freeze_processes);
    config.p_whitelist = tdeConfig.readListEntry("ProcessesWhitelist", config.p_whitelist);
    config.p_blacklist = tdeConfig.readListEntry("ProcessesBlacklist", config.p_blacklist);

    // Connectivity
    tdeConfig.setGroup("Connectivity");
    config.disable_eth = tdeConfig.readNumEntry("DisableEth", config.disable_eth);
    config.lowbat_bt_off_on_display_off = tdeConfig.readBoolEntry("LowBatBtOffOnDisplayOff", config.lowbat_bt_off_on_display_off);
    config.ac_lid_enable_powernap = tdeConfig.readBoolEntry("AcLidEnablePowernap", config.ac_lid_enable_powernap);
    config.ac_lid_powernap_mode_disable_bt = tdeConfig.readBoolEntry("AcLidPowernapModeDisableBt", config.ac_lid_powernap_mode_disable_bt);
    config.ac_lid_powernap_mode_disable_wifi = tdeConfig.readBoolEntry("AcLidPowernapModeDisableWifi", config.ac_lid_powernap_mode_disable_wifi);

    // Locking
    tdeConfig.setGroup("Locking");
    config.lock_on_display_off = tdeConfig.readNumEntry("LockOnDisplayOff", config.lock_on_display_off);
    config.lock_on_sleep = tdeConfig.readNumEntry("LockOnSleep", config.lock_on_sleep);
    config.authorized_ssids = tdeConfig.readListEntry("AuthorizedSSIDs", config.authorized_ssids);
    config.icon_blink_on_critical = tdeConfig.readBoolEntry("IconBlinkOnCritical", config.icon_blink_on_critical);

    // Appearance
    tdeConfig.setGroup("Appearance");
    config.tv_effect_on_suspend_and_shutdown = tdeConfig.readNumEntry("TvEffectOnSuspendShutdown", config.tv_effect_on_suspend_and_shutdown);
    config.ac_screensaver = tdeConfig.readEntry("AcScreensaver", config.ac_screensaver);
    config.slideshow_image_dir = tdeConfig.readPathEntry("SlideshowImageDir", config.slideshow_image_dir);
    config.slideshow_random_order = tdeConfig.readBoolEntry("SlideshowRandomOrder", config.slideshow_random_order);
    config.slideshow_zoom_effect = tdeConfig.readBoolEntry("SlideshowZoomEffect", config.slideshow_zoom_effect);
    config.close_popup_animation = tdeConfig.readBoolEntry("ClosePopupAnimation", config.close_popup_animation);
    config.dark_mode = tdeConfig.readNumEntry("DarkMode", config.dark_mode);
    config.popup_opacity = tdeConfig.readDoubleNumEntry("PopupOpacity", config.popup_opacity);
    config.presentation_mode_icon = tdeConfig.readBoolEntry("PresentationModeIcon", config.presentation_mode_icon);
    config.media_mode_icon = tdeConfig.readBoolEntry("MediaModeIcon", config.media_mode_icon);
    config.coloured_icon = tdeConfig.readBoolEntry("ColouredIcon", config.coloured_icon);
    config.animate_charge_icon = tdeConfig.readBoolEntry("AnimateChargeIcon", config.animate_charge_icon);

    config.tint_popup_r = tdeConfig.readNumEntry("TintPopupR", config.tint_popup_r);
    config.tint_popup_g = tdeConfig.readNumEntry("TintPopupG", config.tint_popup_g);
    config.tint_popup_b = tdeConfig.readNumEntry("TintPopupB", config.tint_popup_b);

    config.custom_color_full = tdeConfig.readBoolEntry("CustomColorFull", config.custom_color_full);
    config.custom_color_normal = tdeConfig.readBoolEntry("CustomColorNormal", config.custom_color_normal);
    config.custom_color_low = tdeConfig.readBoolEntry("CustomColorLow", config.custom_color_low);
    config.custom_color_critical = tdeConfig.readBoolEntry("CustomColorCritical", config.custom_color_critical);

    config.tint_icon_full_r = tdeConfig.readNumEntry("TintIconFullR", config.tint_icon_full_r);
    config.tint_icon_full_g = tdeConfig.readNumEntry("TintIconFullG", config.tint_icon_full_g);
    config.tint_icon_full_b = tdeConfig.readNumEntry("TintIconFullB", config.tint_icon_full_b);

    config.tint_icon_normal_r = tdeConfig.readNumEntry("TintIconNormalR", config.tint_icon_normal_r);
    config.tint_icon_normal_g = tdeConfig.readNumEntry("TintIconNormalG", config.tint_icon_normal_g);
    config.tint_icon_normal_b = tdeConfig.readNumEntry("TintIconNormalB", config.tint_icon_normal_b);

    config.tint_icon_warning_r = tdeConfig.readNumEntry("TintIconWarningR", config.tint_icon_warning_r);
    config.tint_icon_warning_g = tdeConfig.readNumEntry("TintIconWarningG", config.tint_icon_warning_g);
    config.tint_icon_warning_b = tdeConfig.readNumEntry("TintIconWarningB", config.tint_icon_warning_b);

    config.tint_icon_critical_r = tdeConfig.readNumEntry("TintIconCriticalR", config.tint_icon_critical_r);
    config.tint_icon_critical_g = tdeConfig.readNumEntry("TintIconCriticalG", config.tint_icon_critical_g);
    config.tint_icon_critical_b = tdeConfig.readNumEntry("TintIconCriticalB", config.tint_icon_critical_b);

    // Advanced
    tdeConfig.setGroup("Advanced");
    config.charge_limit_enabled = tdeConfig.readBoolEntry("ChargeLimitEnabled", config.charge_limit_enabled);
    config.charge_limit_value = tdeConfig.readNumEntry("ChargeLimitValue", config.charge_limit_value);
    config.eco_freq_cap = tdeConfig.readNumEntry("EcoFreqCap", config.eco_freq_cap);
    config.balanced_usb_autosuspend = tdeConfig.readBoolEntry("BalancedUsbAutosuspend", config.balanced_usb_autosuspend);
    config.last_calibration = tdeConfig.readEntry("LastCalibration", config.last_calibration);
}

void ConfigManager::save(const YabatmanConfig& config) {
    TDEConfig tdeConfig("yabatmanrc");

    // General
    tdeConfig.setGroup("General");
    tdeConfig.writeEntry("PowerButton", config.power_button);
    tdeConfig.writeEntry("SleepButton", config.sleep_button);
    tdeConfig.writeEntry("BacklightSlider", config.backlight_slider);
    tdeConfig.writeEntry("StatusNotifs", config.status_notifs);
    tdeConfig.writeEntry("NotifCharger", config.notif_charger);
    tdeConfig.writeEntry("NotifFull", config.notif_full);
    tdeConfig.writeEntry("NotifLow", config.notif_low);
    tdeConfig.writeEntry("NotifCritical", config.notif_critical);

    // Battery Profile
    tdeConfig.setGroup("BatteryProfile");
    tdeConfig.writeEntry("ReduceTimeout", config.bat_backlight_reduce_timeout);
    tdeConfig.writeEntry("DisplaySleepTimeout", config.bat_display_sleep_timeout);
    tdeConfig.writeEntry("IdleTimeout", config.bat_idle_timeout);
    tdeConfig.writeEntry("EnergySaving", config.bat_energy_saving);
    tdeConfig.writeEntry("LidAction", config.bat_lid_action);
    tdeConfig.writeEntry("PowerProfile", config.bat_power_profile);

    // AC Profile
    tdeConfig.setGroup("ACProfile");
    tdeConfig.writeEntry("ReduceTimeout", config.ac_backlight_reduce_timeout);
    tdeConfig.writeEntry("DisplaySleepTimeout", config.ac_display_sleep_timeout);
    tdeConfig.writeEntry("IdleTimeout", config.ac_idle_timeout);
    tdeConfig.writeEntry("EnergySaving", config.ac_energy_saving);
    tdeConfig.writeEntry("LidAction", config.ac_lid_action);
    tdeConfig.writeEntry("PowerProfile", config.ac_power_profile);
    tdeConfig.writeEntry("FullPowerProfile", config.ac_full_power_profile);

    // Levels
    tdeConfig.setGroup("Levels");
    tdeConfig.writeEntry("WarnLevel", config.warn_level);
    tdeConfig.writeEntry("CriticalLevel", config.critical_level);
    tdeConfig.writeEntry("CriticalAction", config.critical_action);

    // Adaptive features
    tdeConfig.setGroup("AdaptiveFeatures");
    tdeConfig.writeEntry("ReduceBrightnessMoreDuringIdle", config.reduce_brightness_more_during_idle);
    tdeConfig.writeEntry("ReduceBrightnessWhenChargeDecrease", config.reduce_brightness_when_charge_decrease);
    tdeConfig.writeEntry("AdjustBrightnessWhenStatusChange", config.adjust_brightness_when_status_change);
    tdeConfig.writeEntry("TimeoutsAutoAdapt", config.timeouts_auto_adapt);
    tdeConfig.writeEntry("MinimalStateBeforeSuspend", config.minimal_state_before_suspend);

    // Freezing
    tdeConfig.setGroup("Freezing");
    tdeConfig.writeEntry("LowBatFreezeServices", config.lowbat_freeze_services);
    tdeConfig.writeEntry("ServicesWhitelist", config.whitelist);
    tdeConfig.writeEntry("ServicesBlacklist", config.blacklist);
    tdeConfig.writeEntry("CriticalFreezeProcesses", config.critical_freeze_processes);
    tdeConfig.writeEntry("ProcessesWhitelist", config.p_whitelist);
    tdeConfig.writeEntry("ProcessesBlacklist", config.p_blacklist);

    // Connectivity
    tdeConfig.setGroup("Connectivity");
    tdeConfig.writeEntry("DisableEth", config.disable_eth);
    tdeConfig.writeEntry("LowBatBtOffOnDisplayOff", config.lowbat_bt_off_on_display_off);
    tdeConfig.writeEntry("AcLidEnablePowernap", config.ac_lid_enable_powernap);
    tdeConfig.writeEntry("AcLidPowernapModeDisableBt", config.ac_lid_powernap_mode_disable_bt);
    tdeConfig.writeEntry("AcLidPowernapModeDisableWifi", config.ac_lid_powernap_mode_disable_wifi);

    // Locking
    tdeConfig.setGroup("Locking");
    tdeConfig.writeEntry("LockOnDisplayOff", config.lock_on_display_off);
    tdeConfig.writeEntry("LockOnSleep", config.lock_on_sleep);
    tdeConfig.writeEntry("AuthorizedSSIDs", config.authorized_ssids);
    tdeConfig.writeEntry("IconBlinkOnCritical", config.icon_blink_on_critical);

    // Appearance
    tdeConfig.setGroup("Appearance");
    tdeConfig.writeEntry("TvEffectOnSuspendShutdown", config.tv_effect_on_suspend_and_shutdown);
    tdeConfig.writeEntry("AcScreensaver", config.ac_screensaver);
    tdeConfig.writePathEntry("SlideshowImageDir", config.slideshow_image_dir);
    tdeConfig.writeEntry("SlideshowRandomOrder", config.slideshow_random_order);
    tdeConfig.writeEntry("SlideshowZoomEffect", config.slideshow_zoom_effect);
    tdeConfig.writeEntry("ClosePopupAnimation", config.close_popup_animation);
    tdeConfig.writeEntry("DarkMode", config.dark_mode);
    tdeConfig.writeEntry("PopupOpacity", config.popup_opacity);
    tdeConfig.writeEntry("PresentationModeIcon", config.presentation_mode_icon);
    tdeConfig.writeEntry("MediaModeIcon", config.media_mode_icon);
    tdeConfig.writeEntry("ColouredIcon", config.coloured_icon);
    tdeConfig.writeEntry("AnimateChargeIcon", config.animate_charge_icon);

    tdeConfig.writeEntry("TintPopupR", config.tint_popup_r);
    tdeConfig.writeEntry("TintPopupG", config.tint_popup_g);
    tdeConfig.writeEntry("TintPopupB", config.tint_popup_b);

    tdeConfig.writeEntry("CustomColorFull", config.custom_color_full);
    tdeConfig.writeEntry("CustomColorNormal", config.custom_color_normal);
    tdeConfig.writeEntry("CustomColorLow", config.custom_color_low);
    tdeConfig.writeEntry("CustomColorCritical", config.custom_color_critical);

    tdeConfig.writeEntry("TintIconFullR", config.tint_icon_full_r);
    tdeConfig.writeEntry("TintIconFullG", config.tint_icon_full_g);
    tdeConfig.writeEntry("TintIconFullB", config.tint_icon_full_b);

    tdeConfig.writeEntry("TintIconNormalR", config.tint_icon_normal_r);
    tdeConfig.writeEntry("TintIconNormalG", config.tint_icon_normal_g);
    tdeConfig.writeEntry("TintIconNormalB", config.tint_icon_normal_b);

    tdeConfig.writeEntry("TintIconWarningR", config.tint_icon_warning_r);
    tdeConfig.writeEntry("TintIconWarningG", config.tint_icon_warning_g);
    tdeConfig.writeEntry("TintIconWarningB", config.tint_icon_warning_b);

    tdeConfig.writeEntry("TintIconCriticalR", config.tint_icon_critical_r);
    tdeConfig.writeEntry("TintIconCriticalG", config.tint_icon_critical_g);
    tdeConfig.writeEntry("TintIconCriticalB", config.tint_icon_critical_b);

    // Advanced
    tdeConfig.setGroup("Advanced");
    tdeConfig.writeEntry("ChargeLimitEnabled", config.charge_limit_enabled);
    tdeConfig.writeEntry("ChargeLimitValue", config.charge_limit_value);
    tdeConfig.writeEntry("EcoFreqCap", config.eco_freq_cap);
    tdeConfig.writeEntry("BalancedUsbAutosuspend", config.balanced_usb_autosuspend);
    tdeConfig.writeEntry("LastCalibration", config.last_calibration);

    tdeConfig.sync();
}
