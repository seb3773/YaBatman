#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <tqstring.h>
#include <tqstringlist.h>

struct YabatmanConfig {
    // General config
    int power_button;
    int sleep_button;
    int backlight_slider;
    int status_notifs;
    bool notif_charger;
    bool notif_full;
    bool notif_low;
    bool notif_critical;
    TQString last_calibration;

    // Battery profile config
    int bat_backlight_reduce_timeout;
    int bat_display_sleep_timeout;
    int bat_idle_timeout;
    int bat_energy_saving;
    int bat_lid_action;
    int bat_power_profile;

    // AC profile config
    int ac_backlight_reduce_timeout;
    int ac_display_sleep_timeout;
    int ac_idle_timeout;
    int ac_energy_saving;
    int ac_lid_action;
    int ac_power_profile;
    int ac_full_power_profile;

    // Warning and critical levels
    int warn_level;
    int critical_level;
    int critical_action;

    // Brightness adjustment options
    bool reduce_brightness_more_during_idle;
    bool reduce_brightness_when_charge_decrease;
    bool adjust_brightness_when_status_change;
    bool timeouts_auto_adapt;
    bool minimal_state_before_suspend;
    bool intercept_external_sleep_requests;

    // Services freezing
    bool lowbat_freeze_services;
    TQStringList whitelist;
    TQStringList blacklist;

    // Processes freezing
    bool critical_freeze_processes;
    TQStringList p_whitelist;
    TQStringList p_blacklist;

    // Connectivity & Powernap
    int disable_eth;
    bool lowbat_bt_off_on_display_off;
    bool ac_lid_enable_powernap;
    bool ac_lid_powernap_mode_disable_bt;
    bool ac_lid_powernap_mode_disable_wifi;

    // Lock screen options
    int lock_on_display_off;
    int lock_on_sleep;
    TQStringList authorized_ssids;
    bool icon_blink_on_critical;

    // Visual styles
    int tv_effect_on_suspend_and_shutdown;
    TQString ac_screensaver;
    TQString slideshow_image_dir;
    bool slideshow_random_order;
    bool slideshow_zoom_effect;
    bool close_popup_animation;
    int dark_mode;
    double popup_opacity;
    bool presentation_mode_icon;
    bool media_mode_icon;
    bool coloured_icon;
    bool animate_charge_icon;

    // Color tints (Popup)
    int tint_popup_r;
    int tint_popup_g;
    int tint_popup_b;

    // Color tints (Systray Icons)
    bool custom_color_full;
    bool custom_color_normal;
    bool custom_color_low;
    bool custom_color_critical;
    int tint_icon_full_r;
    int tint_icon_full_g;
    int tint_icon_full_b;
    int tint_icon_normal_r;
    int tint_icon_normal_g;
    int tint_icon_normal_b;
    int tint_icon_warning_r;
    int tint_icon_warning_g;
    int tint_icon_warning_b;
    int tint_icon_critical_r;
    int tint_icon_critical_g;
    int tint_icon_critical_b;

    // Advanced profile tuning
    bool charge_limit_enabled;
    int charge_limit_value;       // 60-100 %
    int eco_freq_cap;             // 20-80 %
    bool balanced_usb_autosuspend;
};

class ConfigManager {
public:
    ConfigManager();
    ~ConfigManager();

    void load(YabatmanConfig& config);
    void save(const YabatmanConfig& config);
    void loadDefaults(YabatmanConfig& config);
};

#endif // CONFIG_MANAGER_H
