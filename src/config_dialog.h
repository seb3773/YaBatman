#ifndef CONFIG_DIALOG_H
#define CONFIG_DIALOG_H

#include <tqdialog.h>
#include <tqwidgetstack.h>
#include <tqcombobox.h>
#include <tqcheckbox.h>
#include <tqspinbox.h>
#include <tqlistbox.h>
#include <tqlineedit.h>
#include <tqpushbutton.h>
#include <tqslider.h>
#include <tqlabel.h>
#include "config_manager.h"

class TransitionOverlay;
class ScreensaverWidget;

class ConfigDialog : public TQDialog {
    TQ_OBJECT
public:
    ConfigDialog(ConfigManager *configManager, YabatmanConfig *config, bool hasControllableBacklight, TQWidget *parent = 0);
    ~ConfigDialog();

private slots:
    void onAccept();
    void onResetDefaults();
    void onScreensaverChanged(int index);
    void slotSelectionChanged();
    void onAbout();
    void onEcoFreqChanged(int value);
    void onOpacityChanged(int value);
    void onTestTransition();
    void onTestScreensaver();
    void onTestTransitionComplete();
    void onTestTransitionTimeout();
    void onTestScreensaverTimeout();
    void onTestScreensaverFinished();
    void onTvEffectComboChanged(int index);
    void onNotifCriticalToggled(bool checked);
    void onColouredIconToggled(bool checked);
    void onColorModeChanged(int index);
    void onChooseColorFull();
    void onChooseColorNormal();
    void onChooseColorLow();
    void onChooseColorCritical();

    // List buttons
    void onAddServiceWhitelist();
    void onRemoveServiceWhitelist();
    void onAddServiceBlacklist();
    void onRemoveServiceBlacklist();
    void onAddProcWhitelist();
    void onRemoveProcWhitelist();
    void onAddProcBlacklist();
    void onRemoveProcBlacklist();
    void onAddSSID();
    void onRemoveSSID();
    void onBrowseSlideshowDir();
    void onBatReduceChanged(int val);
    void onBatSleepChanged(int val);
    void onBatIdleChanged(int val);
    void onAcReduceChanged(int val);
    void onAcSleepChanged(int val);
    void onAcIdleChanged(int val);

private:
    void setupUI();
    void loadConfigValues();
    void saveConfigValues();
    void updateColorButtonBackground(TQPushButton *btn, const TQColor &color);

    ConfigManager *m_configManager;
    YabatmanConfig *m_config;
    bool m_hasControllableBacklight;
    bool m_updatingTimeouts;

    TQListBox *m_sidebar;
    TQWidgetStack *m_widgetStack;

    // Tab 1: General
    TQComboBox *m_powerBtnCombo;
    TQComboBox *m_sleepBtnCombo;
    TQCheckBox *m_backlightSliderCheck;
    TQCheckBox *m_statusNotifsCheck;
    TQCheckBox *m_notifChargerCheck;
    TQCheckBox *m_notifFullCheck;
    TQCheckBox *m_notifLowCheck;
    TQCheckBox *m_notifCriticalCheck;

    // Tab 2: Battery Profile
    TQSpinBox *m_batReduceSpin;
    TQSpinBox *m_batSleepSpin;
    TQSpinBox *m_batIdleSpin;
    TQComboBox *m_batActionCombo;
    TQComboBox *m_batLidCombo;
    TQComboBox *m_batProfileCombo;
    TQSpinBox *m_warnLevelSpin;
    TQSpinBox *m_critLevelSpin;
    TQComboBox *m_critActionCombo;

    // Tab 3: AC Profile
    TQSpinBox *m_acReduceSpin;
    TQSpinBox *m_acSleepSpin;
    TQSpinBox *m_acIdleSpin;
    TQComboBox *m_acActionCombo;
    TQComboBox *m_acLidCombo;
    TQComboBox *m_acProfileCombo;
    TQComboBox *m_acFullProfileCombo;

    // Tab 4: Adaptive & Connectivity
    TQCheckBox *m_idleBrightnessCheck;
    TQCheckBox *m_chargeBrightnessCheck;
    TQCheckBox *m_statusBrightnessCheck;
    TQCheckBox *m_autoAdaptCheck;
    TQCheckBox *m_minSuspendCheck;
    TQComboBox *m_disableEthCombo;
    TQCheckBox *m_lowbatBtCheck;
    TQCheckBox *m_powernapCheck;
    TQCheckBox *m_powernapBtCheck;
    TQCheckBox *m_powernapWifiCheck;

    // Tab 5: Security & Locking
    TQComboBox *m_lockDisplayCombo;
    TQCheckBox *m_lockSleepCheck;
    TQListBox *m_ssidsList;
    TQPushButton *m_ssidAddBtn;
    TQPushButton *m_ssidDelBtn;
    TQCheckBox *m_blinkCritCheck;

    // Tab 6: Services Freezing
    TQCheckBox *m_freezeServicesCheck;
    TQListBox *m_servicesWhitelist;
    TQPushButton *m_svcWlAddBtn;
    TQPushButton *m_svcWlDelBtn;
    TQListBox *m_servicesBlacklist;
    TQPushButton *m_svcBlAddBtn;
    TQPushButton *m_svcBlDelBtn;

    // Tab 7: Processes Freezing
    TQCheckBox *m_freezeProcsCheck;
    TQListBox *m_procsWhitelist;
    TQPushButton *m_procWlAddBtn;
    TQPushButton *m_procWlDelBtn;
    TQListBox *m_procsBlacklist;
    TQPushButton *m_procBlAddBtn;
    TQPushButton *m_procBlDelBtn;

    TQComboBox *m_tvEffectCombo;
    TQComboBox *m_screensaverCombo;
    TQLineEdit *m_slideshowDirEdit;
    TQPushButton *m_slideshowBrowseBtn;
    TQCheckBox *m_slideshowRandomCheck;
    TQCheckBox *m_slideshowZoomCheck;
    TQCheckBox *m_closeAnimCheck;
    TQComboBox *m_darkModeCombo;
    TQSlider *m_opacitySlider;
    TQLabel *m_opacityLbl;
    TQCheckBox *m_presModeIconCheck;
    TQCheckBox *m_mediaModeIconCheck;
    TQCheckBox *m_colouredIconCheck;
    TQCheckBox *m_animateChargeCheck;

    // Coloured Icon Sub-options
    TQWidget *m_colorOptsContainer;
    TQComboBox *m_colorModeFull;
    TQPushButton *m_colorBtnFull;
    TQComboBox *m_colorModeNormal;
    TQPushButton *m_colorBtnNormal;
    TQComboBox *m_colorModeLow;
    TQPushButton *m_colorBtnLow;
    TQComboBox *m_colorModeCritical;
    TQPushButton *m_colorBtnCritical;
    TQColor m_colorFull;
    TQColor m_colorNormal;
    TQColor m_colorLow;
    TQColor m_colorCritical;

    // Battery Health (in Battery Profile tab)
    TQCheckBox *m_chargeLimitCheck;
    TQSpinBox *m_chargeLimitSpin;
    TQLabel *m_chargeLimitUnavailLabel;
    bool m_chargeLimitHwAvailable;

    TQSlider *m_ecoFreqSlider;
    TQLabel *m_ecoFreqLabel;
    TQCheckBox *m_balancedUsbCheck;

    // Test buttons and widgets
    TQPushButton *m_testTransitionBtn;
    TQPushButton *m_testScreensaverBtn;
    TransitionOverlay *m_testTransitionOverlay;
    ScreensaverWidget *m_testScreensaverWidget;
    TQTimer *m_testTransitionTimer;
    TQTimer *m_testScreensaverTimer;
};

class AddSsidDialog : public TQDialog {
    TQ_OBJECT
    TQLineEdit *m_lineEdit;
    TQPushButton *m_currentSsidBtn;
public:
    AddSsidDialog(TQWidget *parent = 0);
    TQString ssid() const;

private slots:
    void slotUseCurrentSsid();
};
#endif // CONFIG_DIALOG_H
