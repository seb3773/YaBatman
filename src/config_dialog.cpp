#include "config_dialog.h"
#include "yabatman_utils.h"
#include "battery_icons.h"
#include "screensavers.h"
#include <tqlayout.h>
#include <tqgroupbox.h>
#include <tqlabel.h>
#include <tqinputdialog.h>
#include <tqmessagebox.h>
#include <tqslider.h>
#include <tqimage.h>
#include <tqpixmap.h>
#include <tqfiledialog.h>
#include <tqdir.h>
#include <tqfile.h>
#include <tqpainter.h>
#include <tqcolordialog.h>

class ConfigSidebarItem : public TQListBoxItem {
    TQString m_text;
public:
    ConfigSidebarItem(TQListBox *lb, const TQString &text)
        : TQListBoxItem(lb), m_text(text) {}

    int height(const TQListBox *lb) const {
        return lb->fontMetrics().lineSpacing() + 24;
    }

    int width(const TQListBox *lb) const {
        return lb->fontMetrics().width(m_text) + 20;
    }

    void paint(TQPainter *p) {
        int h = height(listBox());
        if (selected()) {
            p->setPen(listBox()->colorGroup().highlightedText());
        } else {
            p->setPen(listBox()->colorGroup().text());
        }
        p->drawText(12, 0, listBox()->width() - 12, h,
                    TQt::AlignVCenter | TQt::AlignLeft, m_text);
    }

    TQString text() const { return m_text; }
};

ConfigDialog::ConfigDialog(ConfigManager *configManager, YabatmanConfig *config, bool hasControllableBacklight, TQWidget *parent)
    : TQDialog(parent, "ConfigDialog", true)
{
    m_configManager = configManager;
    m_config = config;
    m_hasControllableBacklight = hasControllableBacklight;

    setCaption("YaBatman Energy Settings");
    setWFlags(WStyle_Customize | WStyle_DialogBorder | WStyle_Title);

    // Set window icon from embedded yabatman icon data
    TQImage iconImg;
    if (iconImg.loadFromData(yabatman_data, yabatman_size, "PNG")) {
        TQPixmap pm;
        pm.convertFromImage(iconImg);
        setIcon(pm);
    }

    m_updatingTimeouts = false;
    m_testTransitionOverlay = NULL;
    m_testScreensaverWidget = NULL;
    m_testTransitionTimer = new TQTimer(this);
    m_testScreensaverTimer = new TQTimer(this);
    connect(m_testTransitionTimer, TQT_SIGNAL(timeout()), this, TQT_SLOT(onTestTransitionTimeout()));
    connect(m_testScreensaverTimer, TQT_SIGNAL(timeout()), this, TQT_SLOT(onTestScreensaverTimeout()));

    setupUI();
    loadConfigValues();
}

ConfigDialog::~ConfigDialog() {
    if (m_testTransitionOverlay) delete m_testTransitionOverlay;
    if (m_testScreensaverWidget) delete m_testScreensaverWidget;
}

void ConfigDialog::setupUI() {
    TQVBoxLayout *mainLayout = new TQVBoxLayout(this, 0, 0);

    // Title Block
    TQFrame *headerFrame = new TQFrame(this);
    headerFrame->setPaletteBackgroundColor(TQColor(220, 220, 220)); // Light gray background
    
    TQHBoxLayout *titleLayout = new TQHBoxLayout(headerFrame, 10, 10);
    
    TQLabel *iconLabel = new TQLabel(headerFrame);
    TQImage img;
    if (img.loadFromData(settings_data, settings_size, "PNG")) {
        TQPixmap pm;
        pm.convertFromImage(img);
        iconLabel->setPixmap(pm);
    }
    titleLayout->addWidget(iconLabel, 0, AlignVCenter);

    TQLabel *titleText = new TQLabel("Settings", headerFrame);
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

    TQHBoxLayout *columnsLayout = new TQHBoxLayout(contentLayout, 10);

    m_sidebar = new TQListBox(this);
    m_sidebar->setFixedWidth(230);
    m_sidebar->setFrameShape(TQFrame::NoFrame);
    m_sidebar->setPaletteBackgroundColor(colorGroup().background());
    TQFont sidebarFont = m_sidebar->font();
    sidebarFont.setBold(true);
    m_sidebar->setFont(sidebarFont);
    columnsLayout->addWidget(m_sidebar);

    TQFrame *separator = new TQFrame(this);
    separator->setFrameShape(TQFrame::VLine);
    separator->setFrameShadow(TQFrame::Sunken);
    columnsLayout->addWidget(separator);

    m_widgetStack = new TQWidgetStack(this);
    columnsLayout->addWidget(m_widgetStack, 1);

    connect(m_sidebar, TQT_SIGNAL(selectionChanged()), this, TQT_SLOT(slotSelectionChanged()));

    // ==========================================
    // Tab 1: General Settings
    // ==========================================
    TQWidget *generalTab = new TQWidget(m_widgetStack);
    TQVBoxLayout *genLayout = new TQVBoxLayout(generalTab, 15, 10);

    TQGroupBox *sysBtnsGroup = new TQGroupBox(2, TQt::Horizontal, "System Buttons Actions", generalTab);
    new TQLabel("Power Button Action:", sysBtnsGroup);
    m_powerBtnCombo = new TQComboBox(false, sysBtnsGroup);
    m_powerBtnCombo->insertItem("Sleep");
    m_powerBtnCombo->insertItem("Sleep then Hibernate");
    m_powerBtnCombo->insertItem("Hibernate");
    m_powerBtnCombo->insertItem("Hybrid Sleep");
    m_powerBtnCombo->insertItem("Shutdown");
    m_powerBtnCombo->insertItem("Ask User");
    m_powerBtnCombo->insertItem("Do Nothing");

    new TQLabel("Sleep Button Action:", sysBtnsGroup);
    m_sleepBtnCombo = new TQComboBox(false, sysBtnsGroup);
    m_sleepBtnCombo->insertItem("Sleep");
    m_sleepBtnCombo->insertItem("Sleep then Hibernate");
    m_sleepBtnCombo->insertItem("Hibernate");
    m_sleepBtnCombo->insertItem("Hybrid Sleep");
    m_sleepBtnCombo->insertItem("Do Nothing");
    genLayout->addWidget(sysBtnsGroup);

    TQGroupBox *sysGfxGroup = new TQGroupBox(1, TQt::Horizontal, "Interface Options", generalTab);
    m_backlightSliderCheck = new TQCheckBox("Show Backlight Slider in Systray Popup", sysGfxGroup);
    m_statusNotifsCheck = new TQCheckBox("Enable notifications", sysGfxGroup);

    TQWidget *notifsSub = new TQWidget(sysGfxGroup);
    TQVBoxLayout *notifsSubLayout = new TQVBoxLayout(notifsSub, 4);
    notifsSubLayout->setMargin(0);

    TQHBoxLayout *notifChargerLayout = new TQHBoxLayout();
    notifChargerLayout->addSpacing(25);
    m_notifChargerCheck = new TQCheckBox("Charger plug/unplug", notifsSub);
    notifChargerLayout->addWidget(m_notifChargerCheck);
    notifsSubLayout->addLayout(notifChargerLayout);

    TQHBoxLayout *notifFullLayout = new TQHBoxLayout();
    notifFullLayout->addSpacing(25);
    m_notifFullCheck = new TQCheckBox("Full charge", notifsSub);
    notifFullLayout->addWidget(m_notifFullCheck);
    notifsSubLayout->addLayout(notifFullLayout);

    TQHBoxLayout *notifLowLayout = new TQHBoxLayout();
    notifLowLayout->addSpacing(25);
    m_notifLowCheck = new TQCheckBox("Low charge", notifsSub);
    notifLowLayout->addWidget(m_notifLowCheck);
    notifsSubLayout->addLayout(notifLowLayout);

    TQHBoxLayout *notifCriticalLayout = new TQHBoxLayout();
    notifCriticalLayout->addSpacing(25);
    m_notifCriticalCheck = new TQCheckBox("Critical level", notifsSub);
    notifCriticalLayout->addWidget(m_notifCriticalCheck);
    notifsSubLayout->addLayout(notifCriticalLayout);

    connect(m_statusNotifsCheck, TQT_SIGNAL(toggled(bool)), m_notifChargerCheck, TQT_SLOT(setEnabled(bool)));
    connect(m_statusNotifsCheck, TQT_SIGNAL(toggled(bool)), m_notifFullCheck, TQT_SLOT(setEnabled(bool)));
    connect(m_statusNotifsCheck, TQT_SIGNAL(toggled(bool)), m_notifLowCheck, TQT_SLOT(setEnabled(bool)));
    connect(m_statusNotifsCheck, TQT_SIGNAL(toggled(bool)), m_notifCriticalCheck, TQT_SLOT(setEnabled(bool)));

    connect(m_notifCriticalCheck, TQT_SIGNAL(toggled(bool)), this, TQT_SLOT(onNotifCriticalToggled(bool)));

    genLayout->addWidget(sysGfxGroup);
    genLayout->addStretch();
    m_widgetStack->addWidget(generalTab, 0);
    new ConfigSidebarItem(m_sidebar, "General");

    // ==========================================
    // Tab 2: Battery Profile
    // ==========================================
    TQWidget *batTab = new TQWidget(m_widgetStack);
    TQVBoxLayout *batLayout = new TQVBoxLayout(batTab, 15, 10);

    TQGroupBox *batTimeouts = new TQGroupBox(2, TQt::Horizontal, "Battery Inactivity Timeouts", batTab);
    new TQLabel("Dim Backlight After (mins):", batTimeouts);
    m_batReduceSpin = new TQSpinBox(1, 60, 1, batTimeouts);
    new TQLabel("Turn Off Display After (mins):", batTimeouts);
    m_batSleepSpin = new TQSpinBox(1, 180, 1, batTimeouts);
    new TQLabel("Suspend System After (mins):", batTimeouts);
    m_batIdleSpin = new TQSpinBox(1, 360, 1, batTimeouts);
    connect(m_batReduceSpin, TQT_SIGNAL(valueChanged(int)), this, TQT_SLOT(onBatReduceChanged(int)));
    connect(m_batSleepSpin, TQT_SIGNAL(valueChanged(int)), this, TQT_SLOT(onBatSleepChanged(int)));
    connect(m_batIdleSpin, TQT_SIGNAL(valueChanged(int)), this, TQT_SLOT(onBatIdleChanged(int)));
    batLayout->addWidget(batTimeouts);

    TQGroupBox *batActions = new TQGroupBox(2, TQt::Horizontal, "Actions / Profiles", batTab);
    new TQLabel("Energy Saving Action:", batActions);
    m_batActionCombo = new TQComboBox(false, batActions);
    m_batActionCombo->insertItem("Sleep");
    m_batActionCombo->insertItem("Sleep then Hibernate");
    m_batActionCombo->insertItem("Hibernate");
    m_batActionCombo->insertItem("Hybrid Sleep");

    new TQLabel("Lid Close Action:", batActions);
    m_batLidCombo = new TQComboBox(false, batActions);
    m_batLidCombo->insertItem("Sleep");
    m_batLidCombo->insertItem("Sleep then Hibernate");
    m_batLidCombo->insertItem("Hibernate");
    m_batLidCombo->insertItem("Hybrid Sleep");
    m_batLidCombo->insertItem("Lock Screen");
    m_batLidCombo->insertItem("Do Nothing");

    new TQLabel("Performance Profile:", batActions);
    m_batProfileCombo = new TQComboBox(false, batActions);
    m_batProfileCombo->insertItem("Power Save (Eco)");
    m_batProfileCombo->insertItem("Balanced");
    m_batProfileCombo->insertItem("Performance");
    batLayout->addWidget(batActions);

    TQGroupBox *batThresholds = new TQGroupBox(2, TQt::Horizontal, "Critical Battery Alerts", batTab);
    new TQLabel("Low Battery Alert Level (%):", batThresholds);
    m_warnLevelSpin = new TQSpinBox(10, 50, 1, batThresholds);
    new TQLabel("Critical Battery Level (%):", batThresholds);
    m_critLevelSpin = new TQSpinBox(3, 20, 1, batThresholds);
    new TQLabel("Critical Level Action:", batThresholds);
    m_critActionCombo = new TQComboBox(false, batThresholds);
    m_critActionCombo->insertItem("Hibernate");
    m_critActionCombo->insertItem("Hybrid Sleep");
    m_critActionCombo->insertItem("Shutdown");
    batLayout->addWidget(batThresholds);

    // Battery Health (charge limit)
    TQGroupBox *batHealthGroup = new TQGroupBox(1, TQt::Horizontal, "Battery Health", batTab);
    m_chargeLimitCheck = new TQCheckBox("Limit charge to protect battery health", batHealthGroup);

    TQWidget *chargeLimitContainer = new TQWidget(batHealthGroup);
    TQHBoxLayout *chargeLimitLayout = new TQHBoxLayout(chargeLimitContainer, 0, 10);
    chargeLimitLayout->addSpacing(25);
    TQLabel *chargeLimitLbl = new TQLabel("Charge limit (%):", chargeLimitContainer);
    chargeLimitLayout->addWidget(chargeLimitLbl);
    m_chargeLimitSpin = new TQSpinBox(60, 100, 5, chargeLimitContainer);
    chargeLimitLayout->addWidget(m_chargeLimitSpin);
    chargeLimitLayout->addStretch();

    // Detect hardware support
    m_chargeLimitHwAvailable = false;
    TQString batPath = "/sys/class/power_supply/";
    TQDir batDetectDir(batPath);
    if (batDetectDir.exists()) {
        TQStringList batList = batDetectDir.entryList(TQDir::Dirs);
        for (TQStringList::Iterator it = batList.begin(); it != batList.end(); ++it) {
            if (*it == "." || *it == "..") continue;
            TQString threshPath = batPath + *it + "/charge_control_end_threshold";
            if (TQFile::exists(threshPath)) {
                m_chargeLimitHwAvailable = true;
                break;
            }
        }
    }

    m_chargeLimitUnavailLabel = new TQLabel("<i>Not available on this hardware</i>", batHealthGroup);
    if (m_chargeLimitHwAvailable) {
        m_chargeLimitUnavailLabel->hide();
        connect(m_chargeLimitCheck, TQT_SIGNAL(toggled(bool)), m_chargeLimitSpin, TQT_SLOT(setEnabled(bool)));
        connect(m_chargeLimitCheck, TQT_SIGNAL(toggled(bool)), chargeLimitLbl, TQT_SLOT(setEnabled(bool)));
    } else {
        m_chargeLimitCheck->setEnabled(false);
        m_chargeLimitSpin->setEnabled(false);
        chargeLimitLbl->setEnabled(false);
    }

    batLayout->addWidget(batHealthGroup);
    batLayout->addStretch();
    m_widgetStack->addWidget(batTab, 1);
    new ConfigSidebarItem(m_sidebar, "Battery Profile");

    // ==========================================
    // Tab 3: AC Profile
    // ==========================================
    TQWidget *acTab = new TQWidget(m_widgetStack);
    TQVBoxLayout *acLayout = new TQVBoxLayout(acTab, 15, 10);

    TQGroupBox *acTimeouts = new TQGroupBox(2, TQt::Horizontal, "AC Inactivity Timeouts", acTab);
    new TQLabel("Dim Backlight After (mins):", acTimeouts);
    m_acReduceSpin = new TQSpinBox(1, 60, 1, acTimeouts);
    new TQLabel("Screensaver / Turn Off Display After (mins):", acTimeouts);
    m_acSleepSpin = new TQSpinBox(1, 180, 1, acTimeouts);
    new TQLabel("Suspend System After (mins):", acTimeouts);
    m_acIdleSpin = new TQSpinBox(1, 360, 1, acTimeouts);
    connect(m_acReduceSpin, TQT_SIGNAL(valueChanged(int)), this, TQT_SLOT(onAcReduceChanged(int)));
    connect(m_acSleepSpin, TQT_SIGNAL(valueChanged(int)), this, TQT_SLOT(onAcSleepChanged(int)));
    connect(m_acIdleSpin, TQT_SIGNAL(valueChanged(int)), this, TQT_SLOT(onAcIdleChanged(int)));
    acLayout->addWidget(acTimeouts);

    TQGroupBox *acActions = new TQGroupBox(2, TQt::Horizontal, "Actions / Profiles", acTab);
    new TQLabel("Energy Saving Action:", acActions);
    m_acActionCombo = new TQComboBox(false, acActions);
    m_acActionCombo->insertItem("Sleep");
    m_acActionCombo->insertItem("Sleep then Hibernate");
    m_acActionCombo->insertItem("Hibernate");
    m_acActionCombo->insertItem("Hybrid Sleep");

    new TQLabel("Lid Close Action:", acActions);
    m_acLidCombo = new TQComboBox(false, acActions);
    m_acLidCombo->insertItem("Sleep");
    m_acLidCombo->insertItem("Sleep then Hibernate");
    m_acLidCombo->insertItem("Hibernate");
    m_acLidCombo->insertItem("Hybrid Sleep");
    m_acLidCombo->insertItem("Lock Screen");
    m_acLidCombo->insertItem("Do Nothing");

    new TQLabel("Normal Charging Profile:", acActions);
    m_acProfileCombo = new TQComboBox(false, acActions);
    m_acProfileCombo->insertItem("Power Save (Eco)");
    m_acProfileCombo->insertItem("Balanced");
    m_acProfileCombo->insertItem("Performance");

    new TQLabel("Full Charge Profile:", acActions);
    m_acFullProfileCombo = new TQComboBox(false, acActions);
    m_acFullProfileCombo->insertItem("Power Save (Eco)");
    m_acFullProfileCombo->insertItem("Balanced");
    m_acFullProfileCombo->insertItem("Performance");
    acLayout->addWidget(acActions);
    acLayout->addStretch();
    m_widgetStack->addWidget(acTab, 2);
    new ConfigSidebarItem(m_sidebar, "AC Profile");

    // ==========================================
    // Tab 3: Advanced Profile Tuning
    // ==========================================
    TQWidget *advTab = new TQWidget(m_widgetStack);
    TQVBoxLayout *advLayout = new TQVBoxLayout(advTab, 15, 10);

    TQGroupBox *ecoGroup = new TQGroupBox(1, TQt::Horizontal, "Eco Profile Tuning", advTab);
    TQWidget *freqContainer = new TQWidget(ecoGroup);
    TQHBoxLayout *freqLayout = new TQHBoxLayout(freqContainer, 0, 10);
    freqLayout->addWidget(new TQLabel("CPU Max Frequency Cap:", freqContainer));
    m_ecoFreqSlider = new TQSlider(20, 80, 5, 40, TQt::Horizontal, freqContainer);
    m_ecoFreqSlider->setTickmarks(TQSlider::Below);
    m_ecoFreqSlider->setTickInterval(10);
    freqLayout->addWidget(m_ecoFreqSlider);
    m_ecoFreqLabel = new TQLabel("40%", freqContainer);
    m_ecoFreqLabel->setMinimumWidth(35);
    freqLayout->addWidget(m_ecoFreqLabel);
    connect(m_ecoFreqSlider, TQT_SIGNAL(valueChanged(int)), this, TQT_SLOT(onEcoFreqChanged(int)));
    advLayout->addWidget(ecoGroup);

    TQGroupBox *balancedGroup = new TQGroupBox(1, TQt::Horizontal, "Balanced Profile Tuning", advTab);
    m_balancedUsbCheck = new TQCheckBox("Enable USB autosuspend", balancedGroup);
    advLayout->addWidget(balancedGroup);

    TQGroupBox *suspendGroup = new TQGroupBox(1, TQt::Horizontal, "Suspend Tuning (s2idle Mitigation)", advTab);
    m_minSuspendCheck = new TQCheckBox("Put system in minimum energy state before suspend", suspendGroup);
    TQLabel *descLabel = new TQLabel("<font size=\"-1\" color=\"#555555\"><i>Force the hardware into a minimum power state before sleep to mitigate faulty or incomplete BIOS/firmware s2idle (modern standby) implementations.</i></font>", suspendGroup);
    descLabel->setAlignment(TQt::AlignLeft | TQt::WordBreak);
    advLayout->addWidget(suspendGroup);
    advLayout->addStretch();
    m_widgetStack->addWidget(advTab, 9);

    // ==========================================
    // Tab 4: Adaptive & Connectivity
    // ==========================================
    TQWidget *adaptTab = new TQWidget(m_widgetStack);
    TQVBoxLayout *adaptLayout = new TQVBoxLayout(adaptTab, 15, 10);

    TQGroupBox *adaptGroup = new TQGroupBox(1, TQt::Horizontal, "Adaptive Backlight / Sleep", adaptTab);
    m_idleBrightnessCheck = new TQCheckBox("Reduce brightness further during idle periods", adaptGroup);
    m_chargeBrightnessCheck = new TQCheckBox("Slightly reduce brightness when charge decreases (Batt mode)", adaptGroup);
    m_statusBrightnessCheck = new TQCheckBox("Adjust brightness instantly when charger plug state changes", adaptGroup);
    m_autoAdaptCheck = new TQCheckBox("Auto-shorten timeouts when battery is low or critical", adaptGroup);
    adaptLayout->addWidget(adaptGroup);

    TQGroupBox *connGroup = new TQGroupBox(1, TQt::Horizontal, "Connectivity / Powernap Settings", adaptTab);

    TQWidget *ethContainer = new TQWidget(connGroup);
    TQHBoxLayout *ethLayout = new TQHBoxLayout(ethContainer, 0, 10);
    TQLabel *ethLbl = new TQLabel("Disable Ethernet device:", ethContainer);
    ethLayout->addWidget(ethLbl);
    m_disableEthCombo = new TQComboBox(false, ethContainer);
    m_disableEthCombo->insertItem("Never");
    m_disableEthCombo->insertItem("Always");
    m_disableEthCombo->insertItem("Only on Battery");
    ethLayout->addWidget(m_disableEthCombo);
    ethLayout->addStretch();

    m_lowbatBtCheck = new TQCheckBox("Turn Bluetooth OFF on low battery when display turns off", connGroup);

    m_powernapCheck = new TQCheckBox("Enable Powernap on AC (Background tasks on Lid Close)", connGroup);

    TQWidget *powernapSub = new TQWidget(connGroup);
    TQVBoxLayout *subLayout = new TQVBoxLayout(powernapSub, 4);
    subLayout->setMargin(0);

    TQHBoxLayout *subBtLayout = new TQHBoxLayout(0);
    subBtLayout->addSpacing(25); // 25px indentation
    m_powernapBtCheck = new TQCheckBox("Disable Bluetooth in Powernap Mode", powernapSub);
    subBtLayout->addWidget(m_powernapBtCheck);
    subLayout->addLayout(subBtLayout);

    TQHBoxLayout *subWifiLayout = new TQHBoxLayout(0);
    subWifiLayout->addSpacing(25); // 25px indentation
    m_powernapWifiCheck = new TQCheckBox("Disable Wi-Fi in Powernap Mode", powernapSub);
    subWifiLayout->addWidget(m_powernapWifiCheck);
    subLayout->addLayout(subWifiLayout);

    connect(m_powernapCheck, TQT_SIGNAL(toggled(bool)), m_powernapBtCheck, TQT_SLOT(setEnabled(bool)));
    connect(m_powernapCheck, TQT_SIGNAL(toggled(bool)), m_powernapWifiCheck, TQT_SLOT(setEnabled(bool)));

    adaptLayout->addWidget(connGroup);
    adaptLayout->addStretch();
    m_widgetStack->addWidget(adaptTab, 3);
    new ConfigSidebarItem(m_sidebar, "Adaptive Features");

    // ==========================================
    // Tab 5: Locking & SSIDs
    // ==========================================
    TQWidget *lockTab = new TQWidget(m_widgetStack);
    TQHBoxLayout *lockLayout = new TQHBoxLayout(lockTab, 15, 10);

    TQVBoxLayout *lockLeft = new TQVBoxLayout(10);
    TQGroupBox *lockOpts = new TQGroupBox(2, TQt::Horizontal, "Locking Options", lockTab);
    new TQLabel("Lock on Display Off:", lockOpts);
    m_lockDisplayCombo = new TQComboBox(false, lockOpts);
    m_lockDisplayCombo->insertItem("Do Nothing");
    m_lockDisplayCombo->insertItem("Lock Session");

    new TQLabel("Lock on Sleep:", lockOpts);
    m_lockSleepCheck = new TQCheckBox("Lock on wake up", lockOpts);
    lockLeft->addWidget(lockOpts);
    lockLeft->addStretch();
    lockLayout->addLayout(lockLeft, 1);

    TQGroupBox *ssidGroup = new TQGroupBox(1, TQt::Horizontal, "Trusted SSIDs (Disables Locking)", lockTab);
    m_ssidsList = new TQListBox(ssidGroup);
    TQHBoxLayout *ssidBtns = new TQHBoxLayout(10);
    m_ssidAddBtn = new TQPushButton("Add SSID", ssidGroup);
    connect(m_ssidAddBtn, TQT_SIGNAL(clicked()), this, TQT_SLOT(onAddSSID()));
    m_ssidDelBtn = new TQPushButton("Remove Selected", ssidGroup);
    connect(m_ssidDelBtn, TQT_SIGNAL(clicked()), this, TQT_SLOT(onRemoveSSID()));
    ssidBtns->addWidget(m_ssidAddBtn);
    ssidBtns->addWidget(m_ssidDelBtn);
    ((TQBoxLayout*)ssidGroup->layout())->addLayout(ssidBtns);
    lockLayout->addWidget(ssidGroup, 1);

    m_widgetStack->addWidget(lockTab, 4);
    new ConfigSidebarItem(m_sidebar, "Security");

    // ==========================================
    // Tab 6: Services Freezing
    // ==========================================
    TQWidget *svcTab = new TQWidget(m_widgetStack);
    TQVBoxLayout *svcLayout = new TQVBoxLayout(svcTab, 10);

    m_freezeServicesCheck = new TQCheckBox("Freeze non-essential systemd services when battery is low", svcTab);
    svcLayout->addWidget(m_freezeServicesCheck);

    TQHBoxLayout *svcLists = new TQHBoxLayout(10);

    TQGroupBox *svcWlGroup = new TQGroupBox(1, TQt::Horizontal, "Services Whitelist (Never Frozen)", svcTab);
    m_servicesWhitelist = new TQListBox(svcWlGroup);
    TQHBoxLayout *svcWlBtns = new TQHBoxLayout(10);
    m_svcWlAddBtn = new TQPushButton("Add", svcWlGroup);
    connect(m_svcWlAddBtn, TQT_SIGNAL(clicked()), this, TQT_SLOT(onAddServiceWhitelist()));
    m_svcWlDelBtn = new TQPushButton("Remove", svcWlGroup);
    connect(m_svcWlDelBtn, TQT_SIGNAL(clicked()), this, TQT_SLOT(onRemoveServiceWhitelist()));
    svcWlBtns->addWidget(m_svcWlAddBtn);
    svcWlBtns->addWidget(m_svcWlDelBtn);
    ((TQBoxLayout*)svcWlGroup->layout())->addLayout(svcWlBtns);
    svcLists->addWidget(svcWlGroup);

    TQGroupBox *svcBlGroup = new TQGroupBox(1, TQt::Horizontal, "Services Blacklist (Always Frozen)", svcTab);
    m_servicesBlacklist = new TQListBox(svcBlGroup);
    TQHBoxLayout *svcBlBtns = new TQHBoxLayout(10);
    m_svcBlAddBtn = new TQPushButton("Add", svcBlGroup);
    connect(m_svcBlAddBtn, TQT_SIGNAL(clicked()), this, TQT_SLOT(onAddServiceBlacklist()));
    m_svcBlDelBtn = new TQPushButton("Remove", svcBlGroup);
    connect(m_svcBlDelBtn, TQT_SIGNAL(clicked()), this, TQT_SLOT(onRemoveServiceBlacklist()));
    svcBlBtns->addWidget(m_svcBlAddBtn);
    svcBlBtns->addWidget(m_svcBlDelBtn);
    ((TQBoxLayout*)svcBlGroup->layout())->addLayout(svcBlBtns);
    svcLists->addWidget(svcBlGroup);

    svcLayout->addLayout(svcLists);
    m_widgetStack->addWidget(svcTab, 5);
    new ConfigSidebarItem(m_sidebar, "Services Freezing");

    // ==========================================
    // Tab 7: Processes Freezing
    // ==========================================
    TQWidget *procTab = new TQWidget(m_widgetStack);
    TQVBoxLayout *procLayout = new TQVBoxLayout(procTab, 10);

    m_freezeProcsCheck = new TQCheckBox("Freeze non-essential user processes when battery is critical", procTab);
    procLayout->addWidget(m_freezeProcsCheck);

    TQHBoxLayout *procLists = new TQHBoxLayout(10);

    TQGroupBox *procWlGroup = new TQGroupBox(1, TQt::Horizontal, "Processes Whitelist (Never Frozen)", procTab);
    m_procsWhitelist = new TQListBox(procWlGroup);
    TQHBoxLayout *procWlBtns = new TQHBoxLayout(10);
    m_procWlAddBtn = new TQPushButton("Add", procWlGroup);
    connect(m_procWlAddBtn, TQT_SIGNAL(clicked()), this, TQT_SLOT(onAddProcWhitelist()));
    m_procWlDelBtn = new TQPushButton("Remove", procWlGroup);
    connect(m_procWlDelBtn, TQT_SIGNAL(clicked()), this, TQT_SLOT(onRemoveProcWhitelist()));
    procWlBtns->addWidget(m_procWlAddBtn);
    procWlBtns->addWidget(m_procWlDelBtn);
    ((TQBoxLayout*)procWlGroup->layout())->addLayout(procWlBtns);
    procLists->addWidget(procWlGroup);

    TQGroupBox *procBlGroup = new TQGroupBox(1, TQt::Horizontal, "Processes Blacklist (Always Frozen)", procTab);
    m_procsBlacklist = new TQListBox(procBlGroup);
    TQHBoxLayout *procBlBtns = new TQHBoxLayout(10);
    m_procBlAddBtn = new TQPushButton("Add", procBlGroup);
    connect(m_procBlAddBtn, TQT_SIGNAL(clicked()), this, TQT_SLOT(onAddProcBlacklist()));
    m_procBlDelBtn = new TQPushButton("Remove", procBlGroup);
    connect(m_procBlDelBtn, TQT_SIGNAL(clicked()), this, TQT_SLOT(onRemoveProcBlacklist()));
    procBlBtns->addWidget(m_procBlAddBtn);
    procBlBtns->addWidget(m_procBlDelBtn);
    ((TQBoxLayout*)procBlGroup->layout())->addLayout(procBlBtns);
    procLists->addWidget(procBlGroup);

    procLayout->addLayout(procLists);
    m_widgetStack->addWidget(procTab, 6);
    new ConfigSidebarItem(m_sidebar, "Processes Freezing");

    // ==========================================
    // Tab 8: Transitions & Screensavers
    // ==========================================
    TQWidget *fxTab = new TQWidget(m_widgetStack);
    TQVBoxLayout *fxTabLayout = new TQVBoxLayout(fxTab, 15, 10);

    TQGroupBox *fxGroup = new TQGroupBox(2, TQt::Horizontal, "Transitions / Screensavers", fxTab);
    new TQLabel("Sleep / Shutdown Transition Effect:", fxGroup);
    TQWidget *tvContainer = new TQWidget(fxGroup);
    TQHBoxLayout *tvLayout = new TQHBoxLayout(tvContainer, 0, 10);
    m_tvEffectCombo = new TQComboBox(false, tvContainer);
    m_tvEffectCombo->insertItem("No Effect");
    m_tvEffectCombo->insertItem("Old TV Turn-off Effect");
    m_tvEffectCombo->insertItem("Circular Wipe Effect");
    m_tvEffectCombo->insertItem("Fade Out Effect");
    m_tvEffectCombo->insertItem("Random Transition Effect");
    tvLayout->addWidget(m_tvEffectCombo, 1);
    m_testTransitionBtn = new TQPushButton("Test", tvContainer);
    connect(m_testTransitionBtn, TQT_SIGNAL(clicked()), this, TQT_SLOT(onTestTransition()));
    tvLayout->addWidget(m_testTransitionBtn);
    connect(m_tvEffectCombo, TQT_SIGNAL(activated(int)), this, TQT_SLOT(onTvEffectComboChanged(int)));

    new TQLabel("AC Mode Screensaver Selection:", fxGroup);
    TQWidget *ssContainer = new TQWidget(fxGroup);
    TQHBoxLayout *ssLayout = new TQHBoxLayout(ssContainer, 0, 10);
    m_screensaverCombo = new TQComboBox(false, ssContainer);
    m_screensaverCombo->insertItem("Random Screensaver");
    m_screensaverCombo->insertItem("Clock");
    m_screensaverCombo->insertItem("Analog Clock");
    m_screensaverCombo->insertItem("Matrix Digital Rain");
    m_screensaverCombo->insertItem("3D Pipes Screensaver");
    m_screensaverCombo->insertItem("Plasma Clouds Screensaver");
    m_screensaverCombo->insertItem("Pictures Slideshow Screensaver");
    m_screensaverCombo->insertItem("Starfield Warp Screensaver");
    m_screensaverCombo->insertItem("Disable Screensaver");
    ssLayout->addWidget(m_screensaverCombo, 1);
    m_testScreensaverBtn = new TQPushButton("Test", ssContainer);
    connect(m_testScreensaverBtn, TQT_SIGNAL(clicked()), this, TQT_SLOT(onTestScreensaver()));
    ssLayout->addWidget(m_testScreensaverBtn);

    new TQLabel("Slideshow Images Directory:", fxGroup);
    TQWidget *dirContainer = new TQWidget(fxGroup);
    TQHBoxLayout *dirHbox = new TQHBoxLayout(dirContainer, 0, 10);
    m_slideshowDirEdit = new TQLineEdit(dirContainer);
    dirHbox->addWidget(m_slideshowDirEdit, 1);
    m_slideshowBrowseBtn = new TQPushButton("Browse...", dirContainer);
    connect(m_slideshowBrowseBtn, TQT_SIGNAL(clicked()), this, TQT_SLOT(onBrowseSlideshowDir()));
    dirHbox->addWidget(m_slideshowBrowseBtn);

    new TQLabel("", fxGroup);
    TQWidget *ssOptsContainer = new TQWidget(fxGroup);
    TQVBoxLayout *ssOptsVbox = new TQVBoxLayout(ssOptsContainer, 0, 6);
    m_slideshowRandomCheck = new TQCheckBox("Random order", ssOptsContainer);
    m_slideshowZoomCheck = new TQCheckBox("Zoom effect (Ken Burns)", ssOptsContainer);
    ssOptsVbox->addWidget(m_slideshowRandomCheck);
    ssOptsVbox->addWidget(m_slideshowZoomCheck);

    connect(m_screensaverCombo, TQT_SIGNAL(activated(int)), this, TQT_SLOT(onScreensaverChanged(int)));
    fxTabLayout->addWidget(fxGroup);
    fxTabLayout->addStretch();
    m_widgetStack->addWidget(fxTab, 7);
    new ConfigSidebarItem(m_sidebar, "Transitions & Screensavers");

    // ==========================================
    // Tab 9: Appearance & Visuals
    // ==========================================
    TQWidget *gfxTab = new TQWidget(m_widgetStack);
    TQVBoxLayout *gfxLayout = new TQVBoxLayout(gfxTab, 15, 10);

    TQGroupBox *lookGroup = new TQGroupBox(1, TQt::Horizontal, "Tray Menu Style / Layout", gfxTab);

    TQWidget *darkContainer = new TQWidget(lookGroup);
    TQHBoxLayout *darkLayout = new TQHBoxLayout(darkContainer, 0, 10);
    TQLabel *darkLbl = new TQLabel("Systray Popup Dark Mode:", darkContainer);
    darkLayout->addWidget(darkLbl);
    m_darkModeCombo = new TQComboBox(false, darkContainer);
    m_darkModeCombo->insertItem("Match Desktop TDE Theme");
    m_darkModeCombo->insertItem("Force Dark Mode Visual Panel");
    darkLayout->addWidget(m_darkModeCombo);

    m_closeAnimCheck = new TQCheckBox("Enable fade-out animation when popup tray auto-closes", lookGroup);
    m_colouredIconCheck = new TQCheckBox("Use coloured battery levels in system tray icon", lookGroup);

    // Custom color sub-options layout
    m_colorOptsContainer = new TQWidget(lookGroup);
    TQHBoxLayout *indentLayout = new TQHBoxLayout(m_colorOptsContainer, 0, 0);
    indentLayout->addSpacing(25); // indent by 25px
    
    TQWidget *gridWidget = new TQWidget(m_colorOptsContainer);
    indentLayout->addWidget(gridWidget, 1);
    
    TQGridLayout *gridLayout = new TQGridLayout(gridWidget, 4, 3, 5); // 4 rows, 3 columns, spacing 5
    
    // Row 0: Full
    gridLayout->addWidget(new TQLabel("Full charge level color:", gridWidget), 0, 0);
    m_colorModeFull = new TQComboBox(false, gridWidget);
    m_colorModeFull->insertItem("Default");
    m_colorModeFull->insertItem("Custom");
    gridLayout->addWidget(m_colorModeFull, 0, 1);
    m_colorBtnFull = new TQPushButton("", gridWidget);
    m_colorBtnFull->setFixedSize(24, 24);
    gridLayout->addWidget(m_colorBtnFull, 0, 2);
    
    // Row 1: Normal
    gridLayout->addWidget(new TQLabel("Normal level color:", gridWidget), 1, 0);
    m_colorModeNormal = new TQComboBox(false, gridWidget);
    m_colorModeNormal->insertItem("Default");
    m_colorModeNormal->insertItem("Custom");
    gridLayout->addWidget(m_colorModeNormal, 1, 1);
    m_colorBtnNormal = new TQPushButton("", gridWidget);
    m_colorBtnNormal->setFixedSize(24, 24);
    gridLayout->addWidget(m_colorBtnNormal, 1, 2);
    
    // Row 2: Low
    gridLayout->addWidget(new TQLabel("Low level color:", gridWidget), 2, 0);
    m_colorModeLow = new TQComboBox(false, gridWidget);
    m_colorModeLow->insertItem("Default");
    m_colorModeLow->insertItem("Custom");
    gridLayout->addWidget(m_colorModeLow, 2, 1);
    m_colorBtnLow = new TQPushButton("", gridWidget);
    m_colorBtnLow->setFixedSize(24, 24);
    gridLayout->addWidget(m_colorBtnLow, 2, 2);
    
    // Row 3: Critical
    gridLayout->addWidget(new TQLabel("Critical level color:", gridWidget), 3, 0);
    m_colorModeCritical = new TQComboBox(false, gridWidget);
    m_colorModeCritical->insertItem("Default");
    m_colorModeCritical->insertItem("Custom");
    gridLayout->addWidget(m_colorModeCritical, 3, 1);
    m_colorBtnCritical = new TQPushButton("", gridWidget);
    m_colorBtnCritical->setFixedSize(24, 24);
    gridLayout->addWidget(m_colorBtnCritical, 3, 2);

    connect(m_colouredIconCheck, TQT_SIGNAL(toggled(bool)), this, TQT_SLOT(onColouredIconToggled(bool)));
    connect(m_colorModeFull, TQT_SIGNAL(activated(int)), this, TQT_SLOT(onColorModeChanged(int)));
    connect(m_colorModeNormal, TQT_SIGNAL(activated(int)), this, TQT_SLOT(onColorModeChanged(int)));
    connect(m_colorModeLow, TQT_SIGNAL(activated(int)), this, TQT_SLOT(onColorModeChanged(int)));
    connect(m_colorModeCritical, TQT_SIGNAL(activated(int)), this, TQT_SLOT(onColorModeChanged(int)));
    
    connect(m_colorBtnFull, TQT_SIGNAL(clicked()), this, TQT_SLOT(onChooseColorFull()));
    connect(m_colorBtnNormal, TQT_SIGNAL(clicked()), this, TQT_SLOT(onChooseColorNormal()));
    connect(m_colorBtnLow, TQT_SIGNAL(clicked()), this, TQT_SLOT(onChooseColorLow()));
    connect(m_colorBtnCritical, TQT_SIGNAL(clicked()), this, TQT_SLOT(onChooseColorCritical()));

    m_animateChargeCheck = new TQCheckBox("Animated systray icon during charge", lookGroup);
    m_blinkCritCheck = new TQCheckBox("Blink tray icon on critical battery level", lookGroup);
    m_presModeIconCheck = new TQCheckBox("Show presentation mode icon indicator in systray", lookGroup);
    m_mediaModeIconCheck = new TQCheckBox("Show media playing icon indicator in systray", lookGroup);

    TQWidget *spacer = new TQWidget(lookGroup);
    spacer->setMinimumHeight(10);
    spacer->setMaximumHeight(10);

    new TQLabel("Popup Transparency:", lookGroup);

    TQWidget *opacityContainer = new TQWidget(lookGroup);
    TQHBoxLayout *opacityHbox = new TQHBoxLayout(opacityContainer, 0, 10);
    m_opacitySlider = new TQSlider(10, 100, 10, 100, TQt::Horizontal, opacityContainer);
    m_opacityLbl = new TQLabel("100%", opacityContainer);
    connect(m_opacitySlider, TQT_SIGNAL(valueChanged(int)), this, TQT_SLOT(repaint()));
    connect(m_opacitySlider, TQT_SIGNAL(valueChanged(int)), this, TQT_SLOT(onOpacityChanged(int)));
    opacityHbox->addWidget(m_opacitySlider, 1);
    opacityHbox->addWidget(m_opacityLbl);

    gfxLayout->addWidget(lookGroup);
    gfxLayout->addStretch();
    m_widgetStack->addWidget(gfxTab, 8);
    new ConfigSidebarItem(m_sidebar, "Appearance");
    new ConfigSidebarItem(m_sidebar, "Advanced");

    m_sidebar->setSelected(0, true);
    m_widgetStack->raiseWidget(0);

    // ==========================================
    // Bottom Buttons
    // ==========================================
    TQHBoxLayout *bottom = new TQHBoxLayout(contentLayout, 10);
    TQPushButton *defaultsBtn = new TQPushButton("Reset to Defaults", this);
    connect(defaultsBtn, TQT_SIGNAL(clicked()), this, TQT_SLOT(onResetDefaults()));
    bottom->addWidget(defaultsBtn);

    bottom->addSpacing(75);

    TQPushButton *aboutBtn = new TQPushButton("About", this);
    connect(aboutBtn, TQT_SIGNAL(clicked()), this, TQT_SLOT(onAbout()));
    bottom->addWidget(aboutBtn);

    bottom->addStretch();

    TQPushButton *okBtn = new TQPushButton("OK", this);
    connect(okBtn, TQT_SIGNAL(clicked()), this, TQT_SLOT(onAccept()));
    okBtn->setDefault(true);
    bottom->addWidget(okBtn);

    TQPushButton *cancelBtn = new TQPushButton("Cancel", this);
    connect(cancelBtn, TQT_SIGNAL(clicked()), this, TQT_SLOT(reject()));
    bottom->addWidget(cancelBtn);

    resize(980, 640);
}

void ConfigDialog::loadConfigValues() {
    // General Tab
    m_powerBtnCombo->setCurrentItem(m_config->power_button);
    m_sleepBtnCombo->setCurrentItem(m_config->sleep_button);
    
    m_backlightSliderCheck->setChecked(m_config->backlight_slider == 1);
    if (!m_hasControllableBacklight) {
        m_backlightSliderCheck->setEnabled(false);
        m_backlightSliderCheck->setChecked(false); // Force unchecked visually
        // Note: we don't save the forced 0 value to config here so it remembers user preference 
        // if they move to another machine, but it won't be active anyway.
    }
    
    m_statusNotifsCheck->setChecked(m_config->status_notifs == 1);
    m_notifChargerCheck->setChecked(m_config->notif_charger);
    m_notifFullCheck->setChecked(m_config->notif_full);
    m_notifLowCheck->setChecked(m_config->notif_low);

    m_notifCriticalCheck->blockSignals(true);
    m_notifCriticalCheck->setChecked(m_config->notif_critical);
    m_notifCriticalCheck->blockSignals(false);

    m_notifChargerCheck->setEnabled(m_config->status_notifs == 1);
    m_notifFullCheck->setEnabled(m_config->status_notifs == 1);
    m_notifLowCheck->setEnabled(m_config->status_notifs == 1);
    m_notifCriticalCheck->setEnabled(m_config->status_notifs == 1);

    // Battery Profile Tab
    m_updatingTimeouts = true;
    m_batReduceSpin->setValue(m_config->bat_backlight_reduce_timeout);
    m_batSleepSpin->setValue(m_config->bat_display_sleep_timeout);
    m_batIdleSpin->setValue(m_config->bat_idle_timeout);
    if (m_batSleepSpin->value() <= m_batReduceSpin->value()) {
        m_batSleepSpin->setValue(m_batReduceSpin->value() + 1);
    }
    if (m_batIdleSpin->value() <= m_batSleepSpin->value()) {
        m_batIdleSpin->setValue(m_batSleepSpin->value() + 1);
    }
    m_batActionCombo->setCurrentItem(m_config->bat_energy_saving);
    m_batLidCombo->setCurrentItem(m_config->bat_lid_action);
    m_batProfileCombo->setCurrentItem(m_config->bat_power_profile);
    m_warnLevelSpin->setValue(m_config->warn_level);
    m_critLevelSpin->setValue(m_config->critical_level);
    m_critActionCombo->setCurrentItem(m_config->critical_action);

    // AC Profile Tab
    m_acReduceSpin->setValue(m_config->ac_backlight_reduce_timeout);
    m_acSleepSpin->setValue(m_config->ac_display_sleep_timeout);
    m_acIdleSpin->setValue(m_config->ac_idle_timeout);
    if (m_acSleepSpin->value() <= m_acReduceSpin->value()) {
        m_acSleepSpin->setValue(m_acReduceSpin->value() + 1);
    }
    if (m_acIdleSpin->value() <= m_acSleepSpin->value()) {
        m_acIdleSpin->setValue(m_acSleepSpin->value() + 1);
    }
    m_updatingTimeouts = false;
    m_acActionCombo->setCurrentItem(m_config->ac_energy_saving);
    m_acLidCombo->setCurrentItem(m_config->ac_lid_action);
    m_acProfileCombo->setCurrentItem(m_config->ac_power_profile);
    m_acFullProfileCombo->setCurrentItem(m_config->ac_full_power_profile);

    // Advanced Tab
    if (m_chargeLimitHwAvailable) {
        m_chargeLimitCheck->setChecked(m_config->charge_limit_enabled);
        m_chargeLimitSpin->setValue(m_config->charge_limit_value);
        m_chargeLimitSpin->setEnabled(m_config->charge_limit_enabled);
    }
    m_ecoFreqSlider->setValue(m_config->eco_freq_cap);
    TQString freqTxt;
    freqTxt.sprintf("%d%%", m_config->eco_freq_cap);
    m_ecoFreqLabel->setText(freqTxt);
    m_balancedUsbCheck->setChecked(m_config->balanced_usb_autosuspend);

    // Adaptive Tab
    m_idleBrightnessCheck->setChecked(m_config->reduce_brightness_more_during_idle);
    m_chargeBrightnessCheck->setChecked(m_config->reduce_brightness_when_charge_decrease);
    m_statusBrightnessCheck->setChecked(m_config->adjust_brightness_when_status_change);
    m_autoAdaptCheck->setChecked(m_config->timeouts_auto_adapt);
    m_minSuspendCheck->setChecked(m_config->minimal_state_before_suspend);

    m_disableEthCombo->setCurrentItem(m_config->disable_eth);
    m_lowbatBtCheck->setChecked(m_config->lowbat_bt_off_on_display_off);
    m_powernapCheck->setChecked(m_config->ac_lid_enable_powernap);
    m_powernapBtCheck->setChecked(m_config->ac_lid_powernap_mode_disable_bt);
    m_powernapWifiCheck->setChecked(m_config->ac_lid_powernap_mode_disable_wifi);
    m_powernapBtCheck->setEnabled(m_config->ac_lid_enable_powernap == 1);
    m_powernapWifiCheck->setEnabled(m_config->ac_lid_enable_powernap == 1);

    // Locking Tab
    m_lockDisplayCombo->setCurrentItem(m_config->lock_on_display_off);
    m_lockSleepCheck->setChecked(m_config->lock_on_sleep == 1);
    m_blinkCritCheck->setChecked(m_config->icon_blink_on_critical);

    m_ssidsList->clear();
    for (TQStringList::Iterator it = m_config->authorized_ssids.begin(); it != m_config->authorized_ssids.end(); ++it) {
        m_ssidsList->insertItem(*it);
    }

    // Services Freezing
    m_freezeServicesCheck->setChecked(m_config->lowbat_freeze_services);
    m_servicesWhitelist->clear();
    for (TQStringList::Iterator it = m_config->whitelist.begin(); it != m_config->whitelist.end(); ++it) {
        m_servicesWhitelist->insertItem(*it);
    }
    m_servicesBlacklist->clear();
    for (TQStringList::Iterator it = m_config->blacklist.begin(); it != m_config->blacklist.end(); ++it) {
        m_servicesBlacklist->insertItem(*it);
    }

    // Processes Freezing
    m_freezeProcsCheck->setChecked(m_config->critical_freeze_processes);
    m_procsWhitelist->clear();
    for (TQStringList::Iterator it = m_config->p_whitelist.begin(); it != m_config->p_whitelist.end(); ++it) {
        m_procsWhitelist->insertItem(*it);
    }
    m_procsBlacklist->clear();
    for (TQStringList::Iterator it = m_config->p_blacklist.begin(); it != m_config->p_blacklist.end(); ++it) {
        m_procsBlacklist->insertItem(*it);
    }

    // Appearance Tab
    m_tvEffectCombo->setCurrentItem(m_config->tv_effect_on_suspend_and_shutdown);
    onTvEffectComboChanged(m_tvEffectCombo->currentItem());

    int ssIdx = 0;
    if (m_config->ac_screensaver == "clock") ssIdx = 1;
    else if (m_config->ac_screensaver == "analog_clock") ssIdx = 2;
    else if (m_config->ac_screensaver == "matrix") ssIdx = 3;
    else if (m_config->ac_screensaver == "pipes") ssIdx = 4;
    else if (m_config->ac_screensaver == "plasma") ssIdx = 5;
    else if (m_config->ac_screensaver == "slideshow") ssIdx = 6;
    else if (m_config->ac_screensaver == "starfield") ssIdx = 7;
    else if (m_config->ac_screensaver == "none") ssIdx = 8;
    m_screensaverCombo->setCurrentItem(ssIdx);
    onScreensaverChanged(ssIdx);

    m_slideshowDirEdit->setText(m_config->slideshow_image_dir);
    m_slideshowRandomCheck->setChecked(m_config->slideshow_random_order);
    m_slideshowRandomCheck->setEnabled(ssIdx == 6);
    m_slideshowZoomCheck->setChecked(m_config->slideshow_zoom_effect);
    m_slideshowZoomCheck->setEnabled(ssIdx == 6);
    m_closeAnimCheck->setChecked(m_config->close_popup_animation);
    m_darkModeCombo->setCurrentItem(m_config->dark_mode);
    m_opacitySlider->setValue((int)(m_config->popup_opacity * 100));
    m_colouredIconCheck->setChecked(m_config->coloured_icon);
    onColouredIconToggled(m_config->coloured_icon);

    m_colorModeFull->setCurrentItem(m_config->custom_color_full ? 1 : 0);
    m_colorModeNormal->setCurrentItem(m_config->custom_color_normal ? 1 : 0);
    m_colorModeLow->setCurrentItem(m_config->custom_color_low ? 1 : 0);
    m_colorModeCritical->setCurrentItem(m_config->custom_color_critical ? 1 : 0);

    m_colorFull = TQColor(m_config->tint_icon_full_r, m_config->tint_icon_full_g, m_config->tint_icon_full_b);
    m_colorNormal = TQColor(m_config->tint_icon_normal_r, m_config->tint_icon_normal_g, m_config->tint_icon_normal_b);
    m_colorLow = TQColor(m_config->tint_icon_warning_r, m_config->tint_icon_warning_g, m_config->tint_icon_warning_b);
    m_colorCritical = TQColor(m_config->tint_icon_critical_r, m_config->tint_icon_critical_g, m_config->tint_icon_critical_b);

    updateColorButtonBackground(m_colorBtnFull, m_colorFull);
    updateColorButtonBackground(m_colorBtnNormal, m_colorNormal);
    updateColorButtonBackground(m_colorBtnLow, m_colorLow);
    updateColorButtonBackground(m_colorBtnCritical, m_colorCritical);

    onColorModeChanged(0);

    m_animateChargeCheck->setChecked(m_config->animate_charge_icon);
    m_presModeIconCheck->setChecked(m_config->presentation_mode_icon);
    m_mediaModeIconCheck->setChecked(m_config->media_mode_icon);

    TQString opacityText;
    opacityText.sprintf("%d%%", m_opacitySlider->value());
    m_opacityLbl->setText(opacityText);
}

void ConfigDialog::saveConfigValues() {
    m_config->power_button = m_powerBtnCombo->currentItem();
    m_config->sleep_button = m_sleepBtnCombo->currentItem();
    m_config->backlight_slider = m_backlightSliderCheck->isChecked() ? 1 : 0;
    m_config->status_notifs = m_statusNotifsCheck->isChecked() ? 1 : 0;
    m_config->notif_charger = m_notifChargerCheck->isChecked();
    m_config->notif_full = m_notifFullCheck->isChecked();
    m_config->notif_low = m_notifLowCheck->isChecked();
    m_config->notif_critical = m_notifCriticalCheck->isChecked();

    m_config->bat_backlight_reduce_timeout = m_batReduceSpin->value();
    m_config->bat_display_sleep_timeout = m_batSleepSpin->value();
    m_config->bat_idle_timeout = m_batIdleSpin->value();
    m_config->bat_energy_saving = m_batActionCombo->currentItem();
    m_config->bat_lid_action = m_batLidCombo->currentItem();
    m_config->bat_power_profile = m_batProfileCombo->currentItem();
    m_config->warn_level = m_warnLevelSpin->value();
    m_config->critical_level = m_critLevelSpin->value();
    m_config->critical_action = m_critActionCombo->currentItem();

    m_config->ac_backlight_reduce_timeout = m_acReduceSpin->value();
    m_config->ac_display_sleep_timeout = m_acSleepSpin->value();
    m_config->ac_idle_timeout = m_acIdleSpin->value();
    m_config->ac_energy_saving = m_acActionCombo->currentItem();
    m_config->ac_lid_action = m_acLidCombo->currentItem();
    m_config->ac_power_profile = m_acProfileCombo->currentItem();
    m_config->ac_full_power_profile = m_acFullProfileCombo->currentItem();

    // Advanced
    m_config->charge_limit_enabled = m_chargeLimitCheck->isChecked();
    m_config->charge_limit_value = m_chargeLimitSpin->value();
    m_config->eco_freq_cap = m_ecoFreqSlider->value();
    m_config->balanced_usb_autosuspend = m_balancedUsbCheck->isChecked();

    m_config->reduce_brightness_more_during_idle = m_idleBrightnessCheck->isChecked();
    m_config->reduce_brightness_when_charge_decrease = m_chargeBrightnessCheck->isChecked();
    m_config->adjust_brightness_when_status_change = m_statusBrightnessCheck->isChecked();
    m_config->timeouts_auto_adapt = m_autoAdaptCheck->isChecked();
    m_config->minimal_state_before_suspend = m_minSuspendCheck->isChecked();

    m_config->disable_eth = m_disableEthCombo->currentItem();
    m_config->lowbat_bt_off_on_display_off = m_lowbatBtCheck->isChecked();
    m_config->ac_lid_enable_powernap = m_powernapCheck->isChecked();
    m_config->ac_lid_powernap_mode_disable_bt = m_powernapBtCheck->isChecked();
    m_config->ac_lid_powernap_mode_disable_wifi = m_powernapWifiCheck->isChecked();

    m_config->lock_on_display_off = m_lockDisplayCombo->currentItem();
    m_config->lock_on_sleep = m_lockSleepCheck->isChecked() ? 1 : 0;
    m_config->icon_blink_on_critical = m_blinkCritCheck->isChecked();

    m_config->authorized_ssids.clear();
    for (unsigned int i = 0; i < m_ssidsList->count(); i++) {
        m_config->authorized_ssids.append(m_ssidsList->text(i));
    }

    m_config->lowbat_freeze_services = m_freezeServicesCheck->isChecked();
    m_config->whitelist.clear();
    for (unsigned int i = 0; i < m_servicesWhitelist->count(); i++) {
        m_config->whitelist.append(m_servicesWhitelist->text(i));
    }
    m_config->blacklist.clear();
    for (unsigned int i = 0; i < m_servicesBlacklist->count(); i++) {
        m_config->blacklist.append(m_servicesBlacklist->text(i));
    }

    m_config->critical_freeze_processes = m_freezeProcsCheck->isChecked();
    m_config->p_whitelist.clear();
    for (unsigned int i = 0; i < m_procsWhitelist->count(); i++) {
        m_config->p_whitelist.append(m_procsWhitelist->text(i));
    }
    m_config->p_blacklist.clear();
    for (unsigned int i = 0; i < m_procsBlacklist->count(); i++) {
        m_config->p_blacklist.append(m_procsBlacklist->text(i));
    }

    m_config->tv_effect_on_suspend_and_shutdown = m_tvEffectCombo->currentItem();

    int ssIdx = m_screensaverCombo->currentItem();
    if (ssIdx == 0) m_config->ac_screensaver = "random";
    else if (ssIdx == 1) m_config->ac_screensaver = "clock";
    else if (ssIdx == 2) m_config->ac_screensaver = "analog_clock";
    else if (ssIdx == 3) m_config->ac_screensaver = "matrix";
    else if (ssIdx == 4) m_config->ac_screensaver = "pipes";
    else if (ssIdx == 5) m_config->ac_screensaver = "plasma";
    else if (ssIdx == 6) m_config->ac_screensaver = "slideshow";
    else if (ssIdx == 7) m_config->ac_screensaver = "starfield";
    else if (ssIdx == 8) m_config->ac_screensaver = "none";

    m_config->slideshow_image_dir = m_slideshowDirEdit->text();
    m_config->slideshow_random_order = m_slideshowRandomCheck->isChecked();
    m_config->slideshow_zoom_effect = m_slideshowZoomCheck->isChecked();
    m_config->close_popup_animation = m_closeAnimCheck->isChecked();
    m_config->dark_mode = m_darkModeCombo->currentItem();
    m_config->popup_opacity = m_opacitySlider->value() / 100.0;
    m_config->coloured_icon = m_colouredIconCheck->isChecked();
    m_config->custom_color_full = (m_colorModeFull->currentItem() == 1);
    m_config->custom_color_normal = (m_colorModeNormal->currentItem() == 1);
    m_config->custom_color_low = (m_colorModeLow->currentItem() == 1);
    m_config->custom_color_critical = (m_colorModeCritical->currentItem() == 1);

    m_config->tint_icon_full_r = m_colorFull.red();
    m_config->tint_icon_full_g = m_colorFull.green();
    m_config->tint_icon_full_b = m_colorFull.blue();

    m_config->tint_icon_normal_r = m_colorNormal.red();
    m_config->tint_icon_normal_g = m_colorNormal.green();
    m_config->tint_icon_normal_b = m_colorNormal.blue();

    m_config->tint_icon_warning_r = m_colorLow.red();
    m_config->tint_icon_warning_g = m_colorLow.green();
    m_config->tint_icon_warning_b = m_colorLow.blue();

    m_config->tint_icon_critical_r = m_colorCritical.red();
    m_config->tint_icon_critical_g = m_colorCritical.green();
    m_config->tint_icon_critical_b = m_colorCritical.blue();

    m_config->animate_charge_icon = m_animateChargeCheck->isChecked();
    m_config->presentation_mode_icon = m_presModeIconCheck->isChecked();
    m_config->media_mode_icon = m_mediaModeIconCheck->isChecked();
}

void ConfigDialog::onAccept() {
    saveConfigValues();
    m_configManager->save(*m_config);
    accept();
}

void ConfigDialog::onResetDefaults() {
    if (TQMessageBox::warning(this, "Reset to Defaults",
                              "Are you sure you want to reset all configurations to default values?",
                              TQMessageBox::Yes, TQMessageBox::No) == TQMessageBox::Yes) {
        m_configManager->loadDefaults(*m_config);
        loadConfigValues();
    }
}

// List buttons actions
void ConfigDialog::onAddServiceWhitelist() {
    bool ok;
    TQString val = TQInputDialog::getText("Add Service Whitelist", "Enter service name to whitelist:",
                                         TQLineEdit::Normal, TQString::null, &ok, this);
    if (ok && !val.isEmpty()) {
        m_servicesWhitelist->insertItem(val);
    }
}

void ConfigDialog::onRemoveServiceWhitelist() {
    int idx = m_servicesWhitelist->currentItem();
    if (idx >= 0) m_servicesWhitelist->removeItem(idx);
}

void ConfigDialog::onAddServiceBlacklist() {
    bool ok;
    TQString val = TQInputDialog::getText("Add Service Blacklist", "Enter service name to blacklist:",
                                         TQLineEdit::Normal, TQString::null, &ok, this);
    if (ok && !val.isEmpty()) {
        m_servicesBlacklist->insertItem(val);
    }
}

void ConfigDialog::onRemoveServiceBlacklist() {
    int idx = m_servicesBlacklist->currentItem();
    if (idx >= 0) m_servicesBlacklist->removeItem(idx);
}

void ConfigDialog::onAddProcWhitelist() {
    bool ok;
    TQString val = TQInputDialog::getText("Add Process Whitelist", "Enter process command segment to whitelist:",
                                         TQLineEdit::Normal, TQString::null, &ok, this);
    if (ok && !val.isEmpty()) {
        m_procsWhitelist->insertItem(val);
    }
}

void ConfigDialog::onRemoveProcWhitelist() {
    int idx = m_procsWhitelist->currentItem();
    if (idx >= 0) m_procsWhitelist->removeItem(idx);
}

void ConfigDialog::onAddProcBlacklist() {
    bool ok;
    TQString val = TQInputDialog::getText("Add Process Blacklist", "Enter process command segment to blacklist:",
                                         TQLineEdit::Normal, TQString::null, &ok, this);
    if (ok && !val.isEmpty()) {
        m_procsBlacklist->insertItem(val);
    }
}

void ConfigDialog::onRemoveProcBlacklist() {
    int idx = m_procsBlacklist->currentItem();
    if (idx >= 0) m_procsBlacklist->removeItem(idx);
}

void ConfigDialog::onAddSSID() {
    AddSsidDialog dlg(this);
    if (dlg.exec() == TQDialog::Accepted) {
        TQString val = dlg.ssid();
        if (!val.isEmpty()) {
            m_ssidsList->insertItem(val);
        }
    }
}

void ConfigDialog::onRemoveSSID() {
    int idx = m_ssidsList->currentItem();
    if (idx >= 0) m_ssidsList->removeItem(idx);
}

void ConfigDialog::onBrowseSlideshowDir() {
    TQString current = m_slideshowDirEdit->text();
    if (current.isEmpty()) {
        current = TQDir::homeDirPath();
    }
    TQString dir = TQFileDialog::getExistingDirectory(current, this, "Choose Slideshow Directory", "Choose a directory containing images for the screensaver", true);
    if (!dir.isEmpty()) {
        m_slideshowDirEdit->setText(dir);
    }
}

void ConfigDialog::onBatReduceChanged(int val) {
    if (m_updatingTimeouts) return;
    m_updatingTimeouts = true;

    int sleep = m_batSleepSpin->value();
    int idle = m_batIdleSpin->value();

    if (val >= sleep) {
        sleep = val + 1;
        m_batSleepSpin->setValue(sleep);
    }
    if (sleep >= idle) {
        idle = sleep + 1;
        m_batIdleSpin->setValue(idle);
    }

    m_updatingTimeouts = false;
}

void ConfigDialog::onBatSleepChanged(int val) {
    if (m_updatingTimeouts) return;
    m_updatingTimeouts = true;

    int reduce = m_batReduceSpin->value();
    int idle = m_batIdleSpin->value();

    if (val <= reduce) {
        reduce = val - 1;
        if (reduce < m_batReduceSpin->minValue()) {
            reduce = m_batReduceSpin->minValue();
            val = reduce + 1;
            m_batSleepSpin->setValue(val);
        }
        m_batReduceSpin->setValue(reduce);
    }
    if (val >= idle) {
        idle = val + 1;
        m_batIdleSpin->setValue(idle);
    }

    m_updatingTimeouts = false;
}

void ConfigDialog::onBatIdleChanged(int val) {
    if (m_updatingTimeouts) return;
    m_updatingTimeouts = true;

    int sleep = m_batSleepSpin->value();
    int reduce = m_batReduceSpin->value();

    if (val <= sleep) {
        sleep = val - 1;
        if (sleep <= reduce) {
            reduce = sleep - 1;
            if (reduce < m_batReduceSpin->minValue()) {
                reduce = m_batReduceSpin->minValue();
                sleep = reduce + 1;
                val = sleep + 1;
                m_batIdleSpin->setValue(val);
                m_batSleepSpin->setValue(sleep);
                m_batReduceSpin->setValue(reduce);
            } else {
                m_batSleepSpin->setValue(sleep);
                m_batReduceSpin->setValue(reduce);
            }
        } else {
            m_batSleepSpin->setValue(sleep);
        }
    }

    m_updatingTimeouts = false;
}

void ConfigDialog::onAcReduceChanged(int val) {
    if (m_updatingTimeouts) return;
    m_updatingTimeouts = true;

    int sleep = m_acSleepSpin->value();
    int idle = m_acIdleSpin->value();

    if (val >= sleep) {
        sleep = val + 1;
        m_acSleepSpin->setValue(sleep);
    }
    if (sleep >= idle) {
        idle = sleep + 1;
        m_acIdleSpin->setValue(idle);
    }

    m_updatingTimeouts = false;
}

void ConfigDialog::onAcSleepChanged(int val) {
    if (m_updatingTimeouts) return;
    m_updatingTimeouts = true;

    int reduce = m_acReduceSpin->value();
    int idle = m_acIdleSpin->value();

    if (val <= reduce) {
        reduce = val - 1;
        if (reduce < m_acReduceSpin->minValue()) {
            reduce = m_acReduceSpin->minValue();
            val = reduce + 1;
            m_acSleepSpin->setValue(val);
        }
        m_acReduceSpin->setValue(reduce);
    }
    if (val >= idle) {
        idle = val + 1;
        m_acIdleSpin->setValue(idle);
    }

    m_updatingTimeouts = false;
}

void ConfigDialog::onAcIdleChanged(int val) {
    if (m_updatingTimeouts) return;
    m_updatingTimeouts = true;

    int sleep = m_acSleepSpin->value();
    int reduce = m_acReduceSpin->value();

    if (val <= sleep) {
        sleep = val - 1;
        if (sleep <= reduce) {
            reduce = sleep - 1;
            if (reduce < m_acReduceSpin->minValue()) {
                reduce = m_acReduceSpin->minValue();
                sleep = reduce + 1;
                val = sleep + 1;
                m_acIdleSpin->setValue(val);
                m_acSleepSpin->setValue(sleep);
                m_acReduceSpin->setValue(reduce);
            } else {
                m_acSleepSpin->setValue(sleep);
                m_acReduceSpin->setValue(reduce);
            }
        } else {
            m_acSleepSpin->setValue(sleep);
        }
    }

    m_updatingTimeouts = false;
}

void ConfigDialog::onScreensaverChanged(int index) {
    bool isSlideshow = (index == 6);
    m_slideshowDirEdit->setEnabled(isSlideshow);
    m_slideshowBrowseBtn->setEnabled(isSlideshow);
    m_slideshowRandomCheck->setEnabled(isSlideshow);
    m_slideshowZoomCheck->setEnabled(isSlideshow);
    m_testScreensaverBtn->setEnabled(index >= 1 && index <= 7);
}

void ConfigDialog::onTvEffectComboChanged(int index) {
    m_testTransitionBtn->setEnabled(index >= 1 && index <= 3);
}

void ConfigDialog::onColouredIconToggled(bool checked) {
    m_colorOptsContainer->setEnabled(checked);
}

void ConfigDialog::onColorModeChanged(int) {
    m_colorBtnFull->setEnabled(m_colorModeFull->currentItem() == 1);
    if (m_colorModeFull->currentItem() == 1) {
        updateColorButtonBackground(m_colorBtnFull, m_colorFull);
    } else {
        updateColorButtonBackground(m_colorBtnFull, TQColor(40, 200, 40));
    }

    m_colorBtnNormal->setEnabled(m_colorModeNormal->currentItem() == 1);
    if (m_colorModeNormal->currentItem() == 1) {
        updateColorButtonBackground(m_colorBtnNormal, m_colorNormal);
    } else {
        updateColorButtonBackground(m_colorBtnNormal, TQColor(15, 15, 245));
    }

    m_colorBtnLow->setEnabled(m_colorModeLow->currentItem() == 1);
    if (m_colorModeLow->currentItem() == 1) {
        updateColorButtonBackground(m_colorBtnLow, m_colorLow);
    } else {
        updateColorButtonBackground(m_colorBtnLow, TQColor(205, 100, 0));
    }

    m_colorBtnCritical->setEnabled(m_colorModeCritical->currentItem() == 1);
    if (m_colorModeCritical->currentItem() == 1) {
        updateColorButtonBackground(m_colorBtnCritical, m_colorCritical);
    } else {
        updateColorButtonBackground(m_colorBtnCritical, TQColor(245, 45, 45));
    }
}

void ConfigDialog::onChooseColorFull() {
    TQColor col = TQColorDialog::getColor(m_colorFull, this);
    if (col.isValid()) {
        m_colorFull = col;
        updateColorButtonBackground(m_colorBtnFull, m_colorFull);
    }
}

void ConfigDialog::onChooseColorNormal() {
    TQColor col = TQColorDialog::getColor(m_colorNormal, this);
    if (col.isValid()) {
        m_colorNormal = col;
        updateColorButtonBackground(m_colorBtnNormal, m_colorNormal);
    }
}

void ConfigDialog::onChooseColorLow() {
    TQColor col = TQColorDialog::getColor(m_colorLow, this);
    if (col.isValid()) {
        m_colorLow = col;
        updateColorButtonBackground(m_colorBtnLow, m_colorLow);
    }
}

void ConfigDialog::onChooseColorCritical() {
    TQColor col = TQColorDialog::getColor(m_colorCritical, this);
    if (col.isValid()) {
        m_colorCritical = col;
        updateColorButtonBackground(m_colorBtnCritical, m_colorCritical);
    }
}

void ConfigDialog::updateColorButtonBackground(TQPushButton *btn, const TQColor &color) {
    if (!btn) return;
    TQPalette pal = btn->palette();
    pal.setColor(TQColorGroup::Button, color);
    btn->setPalette(pal);
    btn->setPaletteBackgroundColor(color);
}

void ConfigDialog::onNotifCriticalToggled(bool checked) {
    if (!checked) {
        TQMessageBox::warning(this, "Warning",
                              "Disabling this option will no longer warn you that the action defined\n"
                              "for the critical level is about to occur if the charger is not plugged in quickly.");
    }
}

void ConfigDialog::onTestTransition() {
    int idx = m_tvEffectCombo->currentItem();
    int mode = 0;
    if (idx == 1) mode = 1;      // TV turn-off
    else if (idx == 2) mode = 2; // Circle wipe
    else if (idx == 3) mode = 0; // Fade out
    else return;

    if (m_testTransitionOverlay) {
        delete m_testTransitionOverlay;
        m_testTransitionOverlay = NULL;
    }

    showMinimized();

    m_testTransitionOverlay = new TransitionOverlay(mode, NULL);
    connect(m_testTransitionOverlay, TQT_SIGNAL(transitionComplete()), this, TQT_SLOT(onTestTransitionComplete()));
    m_testTransitionOverlay->show();
    m_testTransitionOverlay->setFocus();
}

void ConfigDialog::onTestTransitionComplete() {
    m_testTransitionTimer->start(1000, true); // 1 second, single shot
}

void ConfigDialog::onTestTransitionTimeout() {
    if (m_testTransitionOverlay) {
        m_testTransitionOverlay->hide();
        delete m_testTransitionOverlay;
        m_testTransitionOverlay = NULL;
    }
    showNormal();
    raise();
    setActiveWindow();
}

void ConfigDialog::onTestScreensaver() {
    int idx = m_screensaverCombo->currentItem();
    TQString type;
    if (idx == 1) type = "clock";
    else if (idx == 2) type = "analog_clock";
    else if (idx == 3) type = "matrix";
    else if (idx == 4) type = "pipes";
    else if (idx == 5) type = "plasma";
    else if (idx == 6) type = "slideshow";
    else if (idx == 7) type = "starfield";
    else return;

    if (m_testScreensaverWidget) {
        delete m_testScreensaverWidget;
        m_testScreensaverWidget = NULL;
    }

    showMinimized();

    m_testScreensaverWidget = new ScreensaverWidget(type, m_slideshowDirEdit->text(), m_slideshowRandomCheck->isChecked(), m_slideshowZoomCheck->isChecked(), NULL);
    connect(m_testScreensaverWidget, TQT_SIGNAL(userActivityDetected()), this, TQT_SLOT(onTestScreensaverFinished()));
    m_testScreensaverWidget->showFullScreen();

    m_testScreensaverTimer->start(5000, true); // 5 seconds, single shot
}

void ConfigDialog::onTestScreensaverTimeout() {
    onTestScreensaverFinished();
}

void ConfigDialog::onTestScreensaverFinished() {
    m_testScreensaverTimer->stop();
    if (m_testScreensaverWidget) {
        m_testScreensaverWidget->close();
        delete m_testScreensaverWidget;
        m_testScreensaverWidget = NULL;
    }
    showNormal();
    raise();
    setActiveWindow();
}

void ConfigDialog::slotSelectionChanged() {
    int idx = m_sidebar->currentItem();
    if (idx >= 0) {
        m_widgetStack->raiseWidget(idx);
    }
}

void ConfigDialog::onEcoFreqChanged(int value) {
    TQString txt;
    txt.sprintf("%d%%", value);
    m_ecoFreqLabel->setText(txt);
}

void ConfigDialog::onOpacityChanged(int value) {
    TQString txt;
    txt.sprintf("%d%%", value);
    m_opacityLbl->setText(txt);
}

AddSsidDialog::AddSsidDialog(TQWidget *parent)
    : TQDialog(parent, "AddSsidDialog", true)
{
    setCaption("Add Trusted SSID");
    TQVBoxLayout *layout = new TQVBoxLayout(this, 15, 10);

    TQLabel *label = new TQLabel("Enter SSID name:", this);
    layout->addWidget(label);

    m_lineEdit = new TQLineEdit(this);
    layout->addWidget(m_lineEdit);

    TQHBoxLayout *btns = new TQHBoxLayout(layout, 10);

    m_currentSsidBtn = new TQPushButton("Use Current SSID", this);
    connect(m_currentSsidBtn, TQT_SIGNAL(clicked()), this, TQT_SLOT(slotUseCurrentSsid()));
    btns->addWidget(m_currentSsidBtn);

    btns->addStretch();

    TQPushButton *okBtn = new TQPushButton("OK", this);
    okBtn->setDefault(true);
    connect(okBtn, TQT_SIGNAL(clicked()), this, TQT_SLOT(accept()));
    btns->addWidget(okBtn);

    TQPushButton *cancelBtn = new TQPushButton("Cancel", this);
    connect(cancelBtn, TQT_SIGNAL(clicked()), this, TQT_SLOT(reject()));
    btns->addWidget(cancelBtn);

    // Disable the button if Wi-Fi is not connected or active SSID is empty
    TQString curSsid = getActiveSsid();
    if (curSsid.isEmpty()) {
        m_currentSsidBtn->setEnabled(false);
    }

    resize(380, 140);
}

TQString AddSsidDialog::ssid() const {
    return m_lineEdit->text().stripWhiteSpace();
}

void AddSsidDialog::slotUseCurrentSsid() {
    TQString curSsid = getActiveSsid();
    if (!curSsid.isEmpty()) {
        m_lineEdit->setText(curSsid);
    }
}

extern const unsigned char about_icon_data[];
extern const size_t about_icon_size;

void ConfigDialog::onAbout() {
    TQDialog dlg(this, "AboutDialog", true);
    dlg.setCaption("About YaBatman");

    TQVBoxLayout *mainLayout = new TQVBoxLayout(&dlg, 15, 10);

    TQHBoxLayout *contentLayout = new TQHBoxLayout(mainLayout, 15);

    // Left: about.png icon
    TQLabel *leftIconLabel = new TQLabel(&dlg);
    TQPixmap aboutPixmap;
    TQImage aboutImg;
    if (aboutImg.loadFromData(about_icon_data, about_icon_size, "PNG")) {
        TQImage scaledImg = aboutImg.smoothScale(aboutImg.width() * 0.8, aboutImg.height() * 0.8);
        aboutPixmap.convertFromImage(scaledImg);
        leftIconLabel->setPixmap(aboutPixmap);
    }
    contentLayout->addWidget(leftIconLabel);

    // Right: text details
    TQVBoxLayout *textLayout = new TQVBoxLayout(contentLayout, 5);

    // Title: "YaBatman"
    TQLabel *titleLabel = new TQLabel("<font size=\"+2\"><b>YaBatman</b></font>", &dlg);
    textLayout->addWidget(titleLabel);

    // Subtitle
    TQLabel *subLabel = new TQLabel("A tqt3 battery monitor\nand power manager for TDE", &dlg);
    textLayout->addWidget(subLabel);

    // Author/Link
    TQLabel *authorLabel = new TQLabel("<font size=\"-1\" color=\"#222222\">by seb3773 - <a href=\"https://github.com/seb3773\">https://github.com/seb3773</a></font>", &dlg);
    textLayout->addWidget(authorLabel);

    // Spacer
    mainLayout->addSpacing(10);

    // Button Row: Centered OK button
    TQHBoxLayout *btnLayout = new TQHBoxLayout(mainLayout);
    btnLayout->addStretch();
    TQPushButton *okBtn = new TQPushButton("OK", &dlg);
    okBtn->setDefault(true);
    connect(okBtn, TQT_SIGNAL(clicked()), &dlg, TQT_SLOT(accept()));
    btnLayout->addWidget(okBtn);
    btnLayout->addStretch();

    // Resize to fit nicely
    dlg.adjustSize();
    dlg.exec();
}

// Embedded about.png
extern const unsigned char about_icon_data[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0xc8, 0x00, 0x00, 0x00, 0xc1, 0x04, 0x03, 0x00, 0x00, 0x00, 0x78, 0x79, 0xe2,
    0x65, 0x00, 0x00, 0x00, 0x27, 0x50, 0x4c, 0x54, 0x45, 0x00, 0x00, 0x00, 0xf3, 0xd2, 0x5f, 0x03,
    0x38, 0x76, 0x41, 0x75, 0x08, 0xff, 0xff, 0xff, 0x66, 0x6d, 0x6a, 0x25, 0xb4, 0xd7, 0x32, 0x3b,
    0x2c, 0xe4, 0x2a, 0x15, 0x74, 0xaa, 0x0a, 0x05, 0x0d, 0x09, 0x9b, 0xa0, 0xa0, 0x94, 0x1c, 0x13,
    0x76, 0xe6, 0xb8, 0x3f, 0x00, 0x00, 0x00, 0x01, 0x74, 0x52, 0x4e, 0x53, 0x00, 0x40, 0xe6, 0xd8,
    0x66, 0x00, 0x00, 0x00, 0x09, 0x70, 0x48, 0x59, 0x73, 0x00, 0x00, 0x0b, 0x13, 0x00, 0x00, 0x0b,
    0x13, 0x01, 0x00, 0x9a, 0x9c, 0x18, 0x00, 0x00, 0x0f, 0x03, 0x49, 0x44, 0x41, 0x54, 0x78, 0x5e,
    0xec, 0x96, 0xb1, 0x6a, 0xe3, 0x40, 0x10, 0x86, 0xd5, 0x84, 0x80, 0xba, 0xe1, 0x08, 0xe1, 0x7c,
    0x4f, 0x71, 0xed, 0xd5, 0x32, 0x07, 0x71, 0xa3, 0x40, 0x2a, 0x3d, 0x85, 0x0f, 0x0e, 0x0c, 0x9b,
    0x07, 0x38, 0x13, 0xd4, 0xa4, 0x32, 0x72, 0xe5, 0x90, 0x88, 0xb5, 0x7e, 0xa7, 0x09, 0x18, 0x02,
    0x7e, 0x02, 0x63, 0xfc, 0x50, 0x37, 0xb3, 0xab, 0xc5, 0xd1, 0x49, 0x29, 0x72, 0x3b, 0x65, 0xfe,
    0x6a, 0x57, 0x82, 0xf9, 0x34, 0xff, 0xfc, 0xbb, 0x28, 0x79, 0x5f, 0x9f, 0xfa, 0x54, 0x3a, 0x7d,
    0x07, 0x50, 0x4c, 0xf5, 0x20, 0xc0, 0x6a, 0x98, 0x01, 0xab, 0x07, 0x79, 0xbc, 0xc0, 0x50, 0x2f,
    0x29, 0x5e, 0xa0, 0x07, 0xb1, 0x74, 0x39, 0x54, 0x0d, 0xd7, 0xa4, 0x08, 0x01, 0x51, 0xd1, 0x37,
    0xac, 0xd8, 0x10, 0x29, 0xda, 0x65, 0x88, 0x2e, 0x7a, 0xad, 0x9c, 0x81, 0x88, 0x56, 0x8a, 0xe9,
    0xfa, 0x4e, 0xb4, 0xfb, 0xb7, 0x5e, 0xf1, 0x40, 0x34, 0x52, 0x84, 0x9c, 0x73, 0xbd, 0x6f, 0x18,
    0x68, 0x64, 0x94, 0x27, 0x7a, 0x7a, 0xa4, 0x5e, 0x2b, 0x29, 0x83, 0xc9, 0x24, 0x8a, 0x02, 0xf5,
    0x5a, 0x01, 0xb1, 0xac, 0x26, 0xa4, 0x20, 0x56, 0xe7, 0x78, 0xa7, 0x1b, 0x66, 0x7c, 0xe1, 0xe6,
    0x74, 0x27, 0x4f, 0x97, 0xf5, 0xdb, 0xc0, 0x5d, 0x93, 0xea, 0xdc, 0xc3, 0x00, 0x08, 0x6f, 0xc6,
    0x6e, 0x49, 0x20, 0x53, 0x4d, 0xc8, 0x99, 0x40, 0x64, 0xf4, 0x5d, 0xea, 0x88, 0x57, 0xca, 0xf1,
    0xa2, 0x0b, 0xf6, 0xcb, 0x00, 0xb0, 0x79, 0x62, 0x5e, 0x7c, 0x6b, 0xaa, 0x02, 0x89, 0x60, 0xd0,
    0xca, 0x52, 0x08, 0x97, 0x76, 0xbc, 0x68, 0x64, 0x2b, 0x2f, 0xc3, 0x6e, 0xf9, 0x4b, 0x45, 0x3b,
    0x5e, 0xd5, 0xfd, 0xa8, 0x69, 0x21, 0xcb, 0x5f, 0x55, 0xc5, 0x71, 0xd3, 0x86, 0x3c, 0x90, 0x14,
    0x0f, 0x10, 0xc8, 0xe6, 0x5e, 0x37, 0x5c, 0x72, 0x7b, 0x7d, 0xad, 0x7a, 0x5a, 0xe6, 0xca, 0x9d,
    0x34, 0x55, 0x5f, 0x46, 0xcb, 0x2e, 0x9f, 0xd9, 0x04, 0x18, 0x80, 0x2c, 0xe0, 0xdf, 0x6a, 0x30,
    0x44, 0x4d, 0x35, 0xa4, 0x25, 0xbc, 0x62, 0x21, 0x98, 0x8b, 0xe0, 0x2b, 0x36, 0x9d, 0xf2, 0xbc,
    0xf0, 0x6f, 0x0f, 0xb1, 0x90, 0x67, 0x61, 0x94, 0x10, 0x06, 0xfb, 0x53, 0x05, 0xcd, 0x98, 0xc3,
    0x48, 0xb3, 0x57, 0x81, 0xc0, 0x55, 0xe1, 0x7a, 0xe8, 0x9b, 0xc5, 0x20, 0xf7, 0x0d, 0xd1, 0x76,
    0xb9, 0x6f, 0x95, 0x26, 0x06, 0xa6, 0x22, 0x68, 0xe9, 0xd3, 0x46, 0x67, 0x77, 0xed, 0xdd, 0x5a,
    0xf4, 0x19, 0xf2, 0x0c, 0xec, 0xd7, 0x5d, 0x7c, 0x92, 0xc1, 0x55, 0x1a, 0x57, 0xb2, 0x13, 0x63,
    0x03, 0xeb, 0x2c, 0x5b, 0xb3, 0x99, 0x79, 0xa2, 0xe0, 0xd7, 0xc1, 0x41, 0xc0, 0xea, 0xa4, 0x8b,
    0xd1, 0x32, 0x94, 0x12, 0x0a, 0x67, 0xfd, 0x79, 0x0e, 0x57, 0xb6, 0xce, 0x26, 0xa7, 0x0c, 0xe3,
    0x36, 0x1b, 0x3b, 0x24, 0xc4, 0xad, 0x78, 0xc1, 0x43, 0x90, 0x65, 0xd9, 0xcf, 0x00, 0x59, 0xd4,
    0xbc, 0xdb, 0xc9, 0x0e, 0x7b, 0xe4, 0x0a, 0x90, 0xe2, 0xe8, 0x7c, 0x79, 0xca, 0x58, 0x08, 0x6e,
    0xfd, 0xe6, 0xcd, 0x04, 0xb2, 0x3a, 0x5a, 0x95, 0x0b, 0x18, 0x02, 0x31, 0x5b, 0x81, 0xec, 0x82,
    0x5b, 0x99, 0xa8, 0x10, 0x08, 0x74, 0xee, 0x7b, 0x07, 0xa9, 0x33, 0xd1, 0x95, 0x2c, 0x3b, 0xbb,
    0x25, 0x12, 0x35, 0xc8, 0xe2, 0xc9, 0x41, 0xc6, 0x2d, 0x64, 0xe3, 0x76, 0x93, 0x46, 0xab, 0x93,
    0x73, 0x20, 0x0c, 0xe1, 0xd5, 0x66, 0x02, 0x11, 0xe4, 0x95, 0x15, 0xfb, 0xac, 0x4b, 0xf6, 0x4a,
    0x81, 0xb1, 0x36, 0x02, 0xb9, 0xe1, 0x36, 0x80, 0x8d, 0xf5, 0x73, 0xbf, 0x05, 0xc4, 0x31, 0x79,
    0x83, 0x12, 0xab, 0x78, 0x86, 0xbf, 0xe7, 0xb7, 0x92, 0x26, 0x58, 0x0f, 0x59, 0x58, 0x0f, 0x29,
    0x04, 0x32, 0xff, 0x13, 0xeb, 0x18, 0x33, 0x3c, 0x24, 0x63, 0x19, 0xa0, 0xb5, 0x0b, 0x80, 0x0c,
    0x69, 0xe7, 0x20, 0x4c, 0x89, 0x3d, 0xee, 0x1e, 0x62, 0x04, 0x82, 0x00, 0xa9, 0x00, 0xd4, 0x27,
    0xc8, 0xfc, 0x50, 0xc7, 0x40, 0xb0, 0x7f, 0x03, 0x31, 0x1d, 0x08, 0x9e, 0x02, 0x84, 0x85, 0x3c,
    0xee, 0x9e, 0x3f, 0xd9, 0x85, 0x1e, 0xa4, 0x08, 0x90, 0xbb, 0x95, 0x0a, 0x64, 0xdb, 0x87, 0xf8,
    0x74, 0xfd, 0x10, 0x48, 0x59, 0x47, 0x8d, 0x24, 0x40, 0x6e, 0xc4, 0xae, 0x8e, 0xf8, 0x49, 0x23,
    0x78, 0x81, 0x1c, 0x62, 0x52, 0x8c, 0xe7, 0x00, 0xa9, 0x79, 0x02, 0x00, 0x66, 0x95, 0x93, 0xf0,
    0xb6, 0xd9, 0x2b, 0x5a, 0x48, 0x19, 0x75, 0x13, 0xa7, 0xd8, 0x0b, 0x44, 0x28, 0xd8, 0x8e, 0xc5,
    0xac, 0x20, 0xd4, 0x59, 0x61, 0x2b, 0x0f, 0x89, 0x3d, 0x8e, 0x29, 0x8e, 0xf3, 0xb6, 0xb4, 0xf5,
    0x17, 0x57, 0x50, 0xbd, 0xf3, 0xdb, 0xe5, 0xba, 0x8c, 0xfe, 0x87, 0x4c, 0xc3, 0xb0, 0x7b, 0xff,
    0x2b, 0xe1, 0x31, 0xfe, 0x9b, 0x71, 0x0e, 0x93, 0x87, 0x55, 0xd3, 0x29, 0xec, 0x35, 0x3b, 0xc1,
    0x10, 0xbc, 0x32, 0x98, 0x7e, 0x0c, 0x81, 0xd9, 0x0c, 0xd3, 0xfe, 0x0f, 0x3d, 0xda, 0xc5, 0x12,
    0x27, 0x48, 0x60, 0xc0, 0xce, 0x3e, 0x72, 0x23, 0xff, 0xa5, 0xcc, 0x6c, 0x5a, 0x1b, 0x47, 0xd2,
    0x38, 0xde, 0x97, 0x60, 0xf0, 0xcd, 0xd0, 0x61, 0x96, 0x9e, 0x5b, 0xb3, 0xbd, 0x87, 0xb9, 0xe6,
    0xd0, 0x58, 0xb7, 0x80, 0x3a, 0x7b, 0xc8, 0xa5, 0x27, 0xb4, 0xe2, 0x8d, 0x33, 0xa7, 0xe0, 0x16,
    0x2c, 0x73, 0xf1, 0x90, 0x35, 0x5a, 0xd7, 0xee, 0x75, 0x59, 0x61, 0x74, 0x33, 0x8d, 0x90, 0x4e,
    0x9e, 0x35, 0xa6, 0xa2, 0xa7, 0x7d, 0x59, 0x70, 0x76, 0xa1, 0x7d, 0x5e, 0x22, 0x84, 0x3e, 0xd4,
    0x3e, 0x2f, 0xaa, 0x8e, 0x5e, 0xca, 0x93, 0xf4, 0x43, 0x12, 0x27, 0x4e, 0x95, 0x7e, 0xfa, 0x3f,
    0x6f, 0x55, 0x65, 0x29, 0x18, 0xe2, 0xe4, 0xe5, 0xaa, 0x0b, 0x49, 0x2b, 0xa0, 0xaa, 0x81, 0xeb,
    0xc3, 0x96, 0x0a, 0x9e, 0xc9, 0x80, 0xb5, 0xcc, 0x76, 0x04, 0x32, 0xe6, 0x3f, 0xe3, 0x86, 0xbf,
    0x40, 0x90, 0x9a, 0xae, 0xab, 0xab, 0x61, 0x26, 0x44, 0xd3, 0x67, 0xb9, 0x6a, 0x8e, 0x33, 0xdf,
    0x82, 0x4e, 0xd2, 0xe9, 0x23, 0x33, 0xb5, 0x1c, 0x1e, 0xe4, 0xcd, 0x65, 0x55, 0x23, 0x59, 0xf2,
    0x16, 0xd6, 0x43, 0xfa, 0x7b, 0xf5, 0x0c, 0x06, 0x0e, 0x74, 0xb8, 0x83, 0x88, 0xbf, 0x7a, 0x02,
    0x51, 0x89, 0x05, 0xb2, 0xce, 0x88, 0x24, 0xc3, 0xb2, 0x25, 0xa0, 0xe9, 0xe1, 0x73, 0x28, 0x40,
    0xa3, 0x60, 0x5b, 0x20, 0x25, 0xd1, 0xec, 0xeb, 0x9c, 0xbb, 0x70, 0xd2, 0x55, 0x22, 0x6f, 0x3a,
    0xe5, 0x86, 0x87, 0x25, 0x8e, 0x4c, 0x1b, 0x52, 0xbe, 0x3d, 0xc9, 0x88, 0x01, 0xca, 0xbc, 0x80,
    0x02, 0x86, 0x29, 0x41, 0x54, 0x48, 0xf3, 0xe2, 0x04, 0xba, 0x10, 0xae, 0x78, 0xc8, 0x81, 0x87,
    0x25, 0x84, 0x08, 0xcb, 0x02, 0x85, 0x2f, 0x7f, 0x3b, 0x2e, 0x6a, 0x8e, 0xae, 0xda, 0xe6, 0x51,
    0x09, 0x77, 0x45, 0x91, 0xa5, 0xa4, 0x1b, 0xc2, 0xe2, 0x56, 0xa2, 0xdc, 0x11, 0xc2, 0xf9, 0x04,
    0x72, 0x06, 0xca, 0x96, 0x5b, 0x58, 0xc0, 0x5d, 0x19, 0x0a, 0xe5, 0x37, 0xcf, 0xb8, 0xc4, 0x08,
    0xcb, 0x7c, 0x01, 0x79, 0x51, 0x6a, 0xce, 0xce, 0x6d, 0xb8, 0x90, 0x6c, 0x6b, 0x87, 0x5e, 0x6a,
    0xe5, 0x04, 0xc2, 0xc5, 0x14, 0x67, 0x0e, 0xd3, 0x3b, 0x88, 0x60, 0x1b, 0x95, 0x61, 0x89, 0x94,
    0x54, 0x1f, 0x0e, 0x3a, 0x66, 0x14, 0x33, 0x42, 0x14, 0x5e, 0xe4, 0x30, 0x74, 0xf0, 0xbd, 0xbb,
    0x30, 0xd2, 0x02, 0x69, 0x52, 0x62, 0x81, 0x3a, 0x5b, 0x5e, 0xb1, 0x14, 0xb9, 0x8d, 0xa6, 0x45,
    0x65, 0x4e, 0x14, 0x67, 0x75, 0x38, 0x20, 0x31, 0x31, 0xa8, 0xbb, 0xdf, 0x21, 0x84, 0xfc, 0xc5,
    0xeb, 0x16, 0x0c, 0xe5, 0xce, 0x41, 0xd7, 0x75, 0x54, 0x10, 0xbc, 0x89, 0x15, 0x7b, 0x2b, 0x42,
    0x02, 0x35, 0xed, 0x32, 0x8f, 0x90, 0x0f, 0x07, 0x9d, 0xb5, 0xac, 0x18, 0x0b, 0x40, 0x31, 0x65,
    0x04, 0xc9, 0x86, 0x21, 0x45, 0xe5, 0x2f, 0xb4, 0xb9, 0x91, 0x81, 0x96, 0x70, 0x48, 0x72, 0x84,
    0x90, 0xb7, 0xb6, 0xa8, 0x24, 0xe4, 0xf5, 0x27, 0x42, 0xca, 0x7c, 0xa9, 0xed, 0x10, 0x9d, 0x20,
    0x43, 0x8e, 0xb3, 0x80, 0xdf, 0x39, 0xfa, 0x2b, 0x7d, 0xdf, 0x83, 0x32, 0x8c, 0x44, 0x8a, 0x82,
    0x86, 0x19, 0x6f, 0x45, 0xb0, 0xa2, 0xa9, 0x38, 0x9c, 0x27, 0x32, 0x85, 0x52, 0xd3, 0xea, 0x30,
    0x85, 0x41, 0x67, 0x06, 0xdd, 0x10, 0x41, 0x4a, 0x54, 0xc0, 0x9f, 0x39, 0x94, 0x90, 0x55, 0x1e,
    0xda, 0x96, 0x05, 0x03, 0xca, 0x4a, 0x14, 0x4a, 0x47, 0x1c, 0xfa, 0x20, 0xdd, 0x46, 0x21, 0xb0,
    0x0b, 0x98, 0x12, 0x16, 0x88, 0xb5, 0x45, 0x3d, 0x8b, 0x21, 0x2f, 0x99, 0x11, 0x01, 0xf9, 0x88,
    0x12, 0x39, 0x71, 0xa6, 0xd4, 0x92, 0xc1, 0x84, 0xbe, 0x2c, 0x91, 0x52, 0xf2, 0x4f, 0x60, 0x21,
    0x88, 0x08, 0x30, 0x98, 0x24, 0x24, 0xdc, 0xca, 0x4c, 0xa1, 0x40, 0xd6, 0x95, 0xc2, 0x03, 0xef,
    0x22, 0x66, 0xc8, 0xfd, 0x2c, 0xee, 0xc2, 0xbc, 0x30, 0x01, 0x34, 0x52, 0xf8, 0xfa, 0x04, 0x11,
    0x71, 0x52, 0x75, 0x28, 0x24, 0x86, 0x08, 0x21, 0xe2, 0x03, 0xb6, 0x12, 0x5d, 0xac, 0xda, 0x08,
    0x56, 0x1c, 0x56, 0x56, 0x6c, 0x43, 0x86, 0x44, 0x18, 0x7a, 0x71, 0x6d, 0x5f, 0xa2, 0x82, 0xee,
    0x8a, 0xca, 0xc5, 0x96, 0x48, 0x22, 0x44, 0xb3, 0x0f, 0xd6, 0x09, 0x55, 0x21, 0x42, 0x78, 0xaa,
    0x58, 0x5e, 0xe8, 0xb8, 0x23, 0x65, 0x4c, 0x8a, 0x9b, 0x10, 0x1a, 0x89, 0xf3, 0xa7, 0x8f, 0xed,
    0x9f, 0xef, 0x23, 0xa2, 0x34, 0xe5, 0x03, 0x71, 0x2c, 0x0d, 0x18, 0x83, 0x4c, 0x42, 0x22, 0x9e,
    0xc5, 0x91, 0x97, 0xb0, 0x64, 0x1d, 0x29, 0x99, 0x43, 0xce, 0x12, 0xe3, 0xe8, 0x45, 0x08, 0x89,
    0x50, 0x74, 0x5c, 0xa7, 0xf0, 0x7f, 0x08, 0x04, 0x8f, 0x0b, 0x2f, 0x64, 0x24, 0x44, 0xee, 0x4a,
    0x2a, 0x45, 0x28, 0x11, 0xb4, 0xa3, 0xd2, 0x5f, 0x9a, 0xcc, 0xa2, 0x81, 0x02, 0x11, 0xd1, 0x7c,
    0x96, 0x32, 0x0e, 0x33, 0x2e, 0x25, 0x21, 0xd2, 0x3b, 0x8e, 0x90, 0x8e, 0xa9, 0x25, 0x41, 0x94,
    0x32, 0x16, 0xa3, 0x88, 0xb6, 0x6a, 0x65, 0xed, 0x08, 0x42, 0x20, 0x8c, 0x62, 0xd7, 0x50, 0xf1,
    0x2e, 0x81, 0x30, 0x7d, 0x10, 0x87, 0xe5, 0x5f, 0x19, 0x80, 0xe1, 0x50, 0xd4, 0x0e, 0x25, 0x2b,
    0x05, 0x22, 0x41, 0x31, 0x52, 0x9a, 0xf9, 0xbb, 0xe6, 0xff, 0x89, 0xfd, 0x53, 0xc6, 0x31, 0xaa,
    0x5c, 0xd0, 0x02, 0xe3, 0x00, 0xe8, 0x31, 0x08, 0x05, 0xc8, 0xe4, 0x97, 0x1e, 0x80, 0x3e, 0xa1,
    0x74, 0x60, 0x47, 0x2f, 0x72, 0x33, 0xd9, 0xd8, 0x62, 0xdd, 0xf0, 0x57, 0xdf, 0xc9, 0x29, 0xf5,
    0x4b, 0xf1, 0xc5, 0x5f, 0x05, 0x62, 0x8a, 0x97, 0xd6, 0xd4, 0xa5, 0x82, 0x04, 0x29, 0x82, 0xa9,
    0xfa, 0x63, 0x0a, 0x27, 0xd2, 0x08, 0xaa, 0x36, 0x21, 0x75, 0x28, 0x90, 0x88, 0xaf, 0xd6, 0xdc,
    0x91, 0x29, 0x28, 0x40, 0x2b, 0x45, 0xad, 0x3a, 0xc7, 0xd1, 0xc2, 0x32, 0x09, 0xcf, 0x14, 0x24,
    0x08, 0x45, 0x8c, 0x88, 0x29, 0x27, 0x83, 0x63, 0x4a, 0x18, 0xe4, 0xea, 0x9a, 0x1a, 0x71, 0x24,
    0x57, 0x2b, 0xa0, 0x0e, 0x01, 0x00, 0xf3, 0x0a, 0xb9, 0xaa, 0x41, 0x84, 0x72, 0xeb, 0x00, 0x47,
    0xe1, 0x91, 0x02, 0x9a, 0xd5, 0xe8, 0x13, 0x1c, 0x1f, 0xe5, 0x35, 0x48, 0xa8, 0xa8, 0xe5, 0x48,
    0x46, 0x2a, 0x7e, 0x35, 0xd6, 0x03, 0xfd, 0xb8, 0x79, 0x02, 0x10, 0xc8, 0x42, 0x58, 0x11, 0x55,
    0x9e, 0x54, 0x7c, 0xfc, 0xbd, 0x32, 0xa5, 0xbf, 0x99, 0x9b, 0xc6, 0x8c, 0xe5, 0x19, 0xb2, 0x99,
    0xc4, 0xc3, 0x21, 0xf1, 0xa6, 0xba, 0x5c, 0x7d, 0xb5, 0xef, 0x1b, 0xdf, 0x29, 0x49, 0xd3, 0x82,
    0x40, 0x0b, 0x09, 0x4d, 0x48, 0x0b, 0xbe, 0x5c, 0x32, 0x1e, 0x0c, 0x46, 0xc0, 0x36, 0x1b, 0x8c,
    0x45, 0x51, 0xb5, 0x36, 0x70, 0xca, 0x53, 0x20, 0xb6, 0x39, 0x67, 0x9e, 0x6a, 0x5e, 0x58, 0x20,
    0x4c, 0x30, 0x2d, 0x0a, 0x0a, 0x98, 0x29, 0xb8, 0xc3, 0x59, 0x62, 0x85, 0x71, 0xd2, 0xa7, 0xc1,
    0xe0, 0x74, 0xef, 0x05, 0xc1, 0x0c, 0x9f, 0x6b, 0x7c, 0x49, 0x0c, 0xc5, 0xe4, 0x24, 0xed, 0xee,
    0x71, 0x5e, 0xc1, 0x52, 0x8d, 0x9f, 0xba, 0xad, 0x18, 0x52, 0x82, 0x98, 0x22, 0xcb, 0xab, 0xe9,
    0x0b, 0xc3, 0x48, 0x36, 0x83, 0x53, 0xd7, 0x75, 0x07, 0x6c, 0xdf, 0x25, 0x2d, 0xca, 0x22, 0x87,
    0x15, 0x3b, 0x85, 0x35, 0x67, 0x4c, 0xb1, 0x6f, 0x23, 0x18, 0x92, 0x48, 0x4f, 0xdc, 0x16, 0x55,
    0x69, 0xce, 0x0d, 0xe4, 0xf3, 0x60, 0xe7, 0xee, 0xdc, 0x53, 0x81, 0xcc, 0xfd, 0xab, 0x8a, 0x22,
    0x8a, 0x0b, 0x39, 0xd2, 0x1d, 0x49, 0xd0, 0xb2, 0x64, 0x63, 0x87, 0x40, 0x52, 0x41, 0x36, 0xc2,
    0x04, 0x86, 0x98, 0xed, 0xc3, 0x55, 0x72, 0xf1, 0x7b, 0x97, 0x8d, 0x21, 0xc7, 0x1b, 0xdf, 0xf3,
    0x18, 0x13, 0x4b, 0xa2, 0x80, 0x96, 0x59, 0x49, 0x05, 0x49, 0xed, 0x90, 0xac, 0x82, 0x00, 0xc8,
    0x04, 0x30, 0x1d, 0x84, 0xed, 0xfe, 0xd3, 0xc5, 0x64, 0xe7, 0xfa, 0xbe, 0x2b, 0x52, 0xbe, 0xff,
    0x83, 0xef, 0xfb, 0x7f, 0x96, 0x2b, 0x32, 0x85, 0x3b, 0x72, 0xcf, 0x7c, 0x34, 0x39, 0x3f, 0xf0,
    0x48, 0xe2, 0xc8, 0x40, 0xd0, 0xc9, 0x25, 0x77, 0x2b, 0x5a, 0x84, 0x75, 0xc5, 0x88, 0x2f, 0xfd,
    0x9b, 0x89, 0x7b, 0x76, 0x36, 0xf1, 0x27, 0x3b, 0x82, 0x9c, 0x13, 0xe4, 0x63, 0x62, 0x28, 0x72,
    0x92, 0x07, 0x08, 0x66, 0x8a, 0xb9, 0xe2, 0x74, 0xfb, 0xc7, 0x5a, 0x90, 0x98, 0x43, 0xf3, 0x03,
    0x60, 0x42, 0x97, 0x8f, 0xd5, 0x17, 0x5c, 0xfa, 0xfe, 0xe4, 0xe7, 0x33, 0xd7, 0x3d, 0xf3, 0xcf,
    0x4f, 0x91, 0xb1, 0x9f, 0x9c, 0x4d, 0x6e, 0x3e, 0x5e, 0x7d, 0xa5, 0x00, 0xc9, 0x98, 0x79, 0x5e,
    0xc0, 0x33, 0x88, 0x04, 0xd6, 0xb0, 0x23, 0x24, 0x65, 0x48, 0x29, 0x01, 0x5f, 0xf5, 0x21, 0x33,
    0xc9, 0x7b, 0xf1, 0x06, 0xbd, 0x43, 0xce, 0x42, 0xca, 0x64, 0xb7, 0xdf, 0xef, 0x77, 0x13, 0xd7,
    0xbd, 0xf1, 0xbd, 0xc4, 0x50, 0xf4, 0x0b, 0x40, 0x86, 0x02, 0x60, 0x88, 0x46, 0xe9, 0x6b, 0x8b,
    0x94, 0x71, 0x42, 0x10, 0xae, 0x24, 0x00, 0x6e, 0xe8, 0x91, 0xd4, 0xf7, 0x08, 0x19, 0xde, 0x0d,
    0x7a, 0x87, 0x54, 0x9c, 0xf9, 0xf8, 0x63, 0xff, 0x1f, 0x7f, 0x82, 0x9a, 0x90, 0xe9, 0x1b, 0x2d,
    0x40, 0x88, 0x91, 0xe9, 0xd0, 0x31, 0xdf, 0xaf, 0xb2, 0x85, 0x9d, 0x84, 0xca, 0xb7, 0xf3, 0x56,
    0x1a, 0xf2, 0x3f, 0x30, 0xec, 0xf1, 0x6d, 0xf2, 0xe9, 0xd2, 0x47, 0x9b, 0xb8, 0x24, 0x03, 0x5f,
    0x7c, 0x36, 0x17, 0x41, 0x48, 0xf9, 0xf9, 0x2a, 0x59, 0xce, 0xb0, 0xb4, 0x10, 0xa1, 0x00, 0x74,
    0x90, 0xa8, 0x21, 0x79, 0x8b, 0x20, 0xa9, 0x0d, 0x92, 0xf2, 0xb9, 0x26, 0x71, 0x24, 0xc5, 0x44,
    0x8c, 0x0e, 0xca, 0xa1, 0x77, 0xed, 0xbb, 0x78, 0x41, 0xfc, 0x72, 0x5d, 0x96, 0x32, 0x41, 0x1e,
    0x12, 0x90, 0x8a, 0xa8, 0xbd, 0x77, 0xf5, 0xd3, 0x83, 0x9a, 0x79, 0x41, 0x95, 0x58, 0xb1, 0x96,
    0xc5, 0x00, 0x0e, 0x40, 0xe8, 0x70, 0xc6, 0x7a, 0x87, 0x1a, 0x71, 0x84, 0x59, 0x94, 0x65, 0x19,
    0xbe, 0x74, 0xc9, 0x50, 0x01, 0xb2, 0x10, 0x41, 0x3c, 0xd6, 0x84, 0x4c, 0xb2, 0xd3, 0xdf, 0x61,
    0x0f, 0x05, 0x32, 0xcc, 0xdb, 0x13, 0x94, 0x21, 0xb0, 0xcc, 0x0a, 0xe1, 0x70, 0x91, 0x0e, 0x86,
    0xa0, 0xf1, 0x82, 0x10, 0x04, 0xe5, 0xff, 0xae, 0x77, 0x3e, 0x32, 0xd8, 0x4d, 0x4c, 0x43, 0x05,
    0x98, 0xcd, 0x18, 0x25, 0x04, 0x4f, 0xf6, 0xf7, 0xe5, 0x43, 0x60, 0xb6, 0xc9, 0xe9, 0x90, 0xb4,
    0xac, 0x97, 0x52, 0x29, 0xd6, 0x7a, 0x4f, 0xe7, 0x08, 0xe1, 0x43, 0x47, 0x56, 0x3b, 0x79, 0x26,
    0x2a, 0xbb, 0x94, 0x18, 0xec, 0x10, 0xb1, 0x43, 0x29, 0xa4, 0xc2, 0x3f, 0xa7, 0x7c, 0x3b, 0xc3,
    0xc8, 0x07, 0x38, 0x2b, 0x25, 0x84, 0x40, 0xd8, 0x53, 0x43, 0xfc, 0xb6, 0x41, 0xfa, 0xd4, 0x20,
    0x86, 0x4b, 0xa0, 0xa1, 0x08, 0x69, 0xda, 0x05, 0x95, 0xde, 0xee, 0x7c, 0xb7, 0x3b, 0x1d, 0xb8,
    0xae, 0xa4, 0x96, 0x3b, 0x43, 0xc8, 0xeb, 0x9b, 0xab, 0xc6, 0xb8, 0x78, 0x9d, 0xf1, 0xfc, 0x13,
    0x2e, 0x3a, 0xfb, 0x93, 0x44, 0x49, 0xf3, 0x54, 0x3b, 0x49, 0xcb, 0xd4, 0x9b, 0x4b, 0xff, 0xf5,
    0xe9, 0x4b, 0xee, 0x28, 0xa4, 0xe3, 0x1c, 0x21, 0xc1, 0x95, 0x77, 0xf1, 0xda, 0x6b, 0x0d, 0x5c,
    0x93, 0xbb, 0x01, 0xed, 0x40, 0xc9, 0x03, 0xf9, 0x09, 0x68, 0xc4, 0x10, 0xda, 0x10, 0x18, 0x7e,
    0xfa, 0xf8, 0x66, 0x20, 0xf6, 0x92, 0x08, 0xee, 0xe0, 0x25, 0x2d, 0x5b, 0x3f, 0x74, 0xee, 0x86,
    0x08, 0x99, 0x73, 0x3b, 0x4c, 0xe8, 0x63, 0x00, 0xbb, 0x94, 0xa5, 0x26, 0xf7, 0xa6, 0x59, 0x17,
    0x42, 0xab, 0x49, 0x65, 0xae, 0xf4, 0xc8, 0x2f, 0xa4, 0xb0, 0x7b, 0xb4, 0x5f, 0x82, 0x96, 0x9a,
    0x06, 0xeb, 0x72, 0x02, 0x6b, 0x76, 0xa5, 0xed, 0x41, 0x1c, 0x24, 0x02, 0x31, 0x46, 0x90, 0x57,
    0x94, 0x8b, 0x9d, 0x91, 0x92, 0x01, 0x70, 0x7b, 0x02, 0x2b, 0x2b, 0xa4, 0x00, 0xd0, 0xe6, 0xc4,
    0xd9, 0x32, 0xcd, 0x90, 0xa6, 0x8d, 0x09, 0xd2, 0x19, 0x1a, 0x83, 0x1e, 0xca, 0x16, 0xd0, 0xba,
    0x6a, 0xa9, 0x6a, 0x31, 0x8f, 0xe7, 0x1d, 0x06, 0x6f, 0xb0, 0x7e, 0x3d, 0xfe, 0xb1, 0x86, 0x38,
    0xf6, 0xbe, 0xcb, 0x78, 0xa3, 0xd7, 0xb1, 0x5b, 0x39, 0x1d, 0x05, 0xca, 0x06, 0xb9, 0x8b, 0x36,
    0xe3, 0x4c, 0xfc, 0xdf, 0xb2, 0x94, 0x20, 0xe9, 0x2f, 0x78, 0x61, 0x83, 0xa1, 0xdf, 0x8e, 0x09,
    0x62, 0x51, 0xad, 0xb8, 0x6e, 0xfa, 0x7a, 0x6c, 0x59, 0xb5, 0x7a, 0x77, 0x78, 0x8a, 0x05, 0xb4,
    0x2e, 0x04, 0x7a, 0x04, 0x21, 0x80, 0x67, 0x8c, 0xd6, 0x46, 0x84, 0xc4, 0x2b, 0xd5, 0xa1, 0x00,
    0xd9, 0xfb, 0x23, 0x18, 0x59, 0x82, 0xd2, 0xcf, 0x17, 0x88, 0x0e, 0x02, 0x3c, 0xef, 0xb6, 0x19,
    0x2b, 0x4a, 0xbc, 0xf4, 0xdf, 0x83, 0x96, 0x91, 0x88, 0xd5, 0x51, 0x77, 0xf4, 0x8b, 0x40, 0x53,
    0x45, 0xcc, 0x6c, 0x90, 0x70, 0x61, 0x70, 0x4d, 0x0a, 0x6c, 0x78, 0x77, 0x90, 0x0e, 0x6c, 0x90,
    0x4d, 0x67, 0xb4, 0x36, 0x21, 0x9e, 0x59, 0x22, 0xff, 0xf7, 0xf0, 0xc1, 0xf0, 0xc6, 0x75, 0x8a,
    0xc2, 0x59, 0x76, 0x08, 0x15, 0x89, 0x6e, 0x8d, 0x06, 0xf8, 0xea, 0x99, 0x8d, 0xb2, 0x3d, 0x13,
    0x17, 0xb2, 0x50, 0xb2, 0xc6, 0x2c, 0xca, 0x56, 0xdd, 0x81, 0xd0, 0xbb, 0xc0, 0xa3, 0x4d, 0x14,
    0x97, 0xa2, 0x43, 0x20, 0x7a, 0x6c, 0x49, 0xae, 0xb0, 0xe6, 0xc3, 0x1e, 0x48, 0x51, 0x29, 0x99,
    0xa5, 0x0e, 0x43, 0x44, 0x39, 0x9c, 0xa0, 0xbd, 0x6d, 0xd4, 0x46, 0xcf, 0x02, 0x39, 0xa2, 0xe4,
    0xaa, 0x99, 0x02, 0x90, 0x23, 0x80, 0x81, 0x6c, 0xec, 0x10, 0x73, 0x4f, 0x64, 0xba, 0x9e, 0xb4,
    0x47, 0x30, 0x9e, 0x76, 0x32, 0x38, 0x8f, 0x5a, 0xef, 0x79, 0xc1, 0xcc, 0x4c, 0x02, 0x2b, 0xe4,
    0x55, 0x52, 0x5b, 0x33, 0x46, 0x81, 0xd7, 0x6e, 0xb8, 0xa3, 0xa9, 0x25, 0x83, 0x5f, 0x1c, 0x34,
    0xfd, 0x04, 0xc4, 0x6a, 0x96, 0x42, 0xe9, 0x87, 0xdf, 0x0c, 0x19, 0x13, 0x64, 0x7a, 0x18, 0x12,
    0x7c, 0x13, 0x84, 0x6a, 0x31, 0xf9, 0x6c, 0x5a, 0x97, 0x31, 0x8f, 0x95, 0xac, 0x0e, 0x42, 0xd4,
    0xec, 0x69, 0x88, 0x89, 0xbe, 0x96, 0xb8, 0x27, 0xbf, 0x0e, 0xbc, 0x66, 0x83, 0x64, 0x77, 0x25,
    0x20, 0xa3, 0xe0, 0x99, 0x90, 0xc2, 0xfe, 0x64, 0x59, 0xc1, 0x51, 0xc6, 0x10, 0xba, 0xb0, 0x57,
    0x6b, 0x90, 0x02, 0x49, 0xa7, 0x72, 0xaa, 0xef, 0x52, 0xc6, 0x5d, 0xc8, 0xb8, 0x0b, 0x51, 0xc4,
    0x90, 0x9d, 0x88, 0x40, 0x08, 0xc3, 0x46, 0x88, 0x0a, 0x92, 0x40, 0x35, 0x4a, 0x3f, 0x43, 0x89,
    0xea, 0x40, 0x7a, 0xc8, 0x10, 0x8a, 0x51, 0xd2, 0x32, 0x51, 0x02, 0x66, 0xd4, 0xf4, 0x39, 0x90,
    0x87, 0x36, 0x36, 0x31, 0x06, 0x07, 0x20, 0xc3, 0xc6, 0x52, 0xad, 0x9f, 0x86, 0x6c, 0x3b, 0x10,
    0x11, 0x22, 0x52, 0x0e, 0x42, 0xd2, 0xda, 0x28, 0x0b, 0xe4, 0x70, 0x13, 0xb6, 0x3f, 0x00, 0xda,
    0x58, 0x21, 0x46, 0x88, 0x25, 0x9b, 0xc7, 0xcf, 0x88, 0xc9, 0xb8, 0xbe, 0xfe, 0xce, 0x6d, 0x90,
    0x2f, 0x08, 0xd1, 0xb5, 0x51, 0x2d, 0x88, 0x0a, 0x9e, 0x86, 0x40, 0x0d, 0x12, 0x67, 0x07, 0x2a,
    0xbe, 0xb1, 0x43, 0xd3, 0x6d, 0xc8, 0xd3, 0x29, 0xdc, 0x58, 0xbb, 0xc1, 0xda, 0xea, 0x5b, 0x3b,
    0x34, 0xe7, 0x9b, 0xdb, 0x0a, 0x56, 0x60, 0xd3, 0xb4, 0xd7, 0x62, 0x78, 0xca, 0x00, 0xec, 0x91,
    0x07, 0xf5, 0x24, 0xa4, 0xd7, 0x80, 0xa4, 0x90, 0x40, 0xb3, 0xad, 0xe0, 0x5f, 0xca, 0x08, 0xb1,
    0x57, 0x0a, 0xa8, 0xe9, 0x53, 0x90, 0xfe, 0xbc, 0xc6, 0x08, 0x82, 0x60, 0xee, 0xd0, 0x86, 0xa8,
    0xde, 0xba, 0x06, 0x10, 0xe3, 0xfb, 0x07, 0x21, 0x47, 0xda, 0x02, 0xc9, 0x5b, 0x90, 0xfa, 0xec,
    0x0f, 0x7f, 0xfa, 0xf0, 0xc5, 0xf9, 0x91, 0x2e, 0x2d, 0x6a, 0xf8, 0xe5, 0x78, 0xbd, 0x39, 0xdf,
    0x9f, 0x67, 0x87, 0x72, 0xb8, 0xa7, 0xd5, 0x8b, 0x6f, 0x80, 0xa4, 0xa3, 0xd9, 0xf5, 0xbd, 0x72,
    0x7e, 0x11, 0x2f, 0x91, 0xc9, 0x47, 0x38, 0x99, 0xde, 0xef, 0x47, 0xb3, 0x47, 0x4a, 0xdc, 0x80,
    0xf4, 0x11, 0x62, 0x5b, 0x7e, 0xed, 0x90, 0x74, 0x76, 0xaf, 0x67, 0xef, 0x02, 0xa7, 0x5d, 0xf2,
    0xaf, 0x32, 0xfd, 0x61, 0xa4, 0xfe, 0x36, 0xca, 0x1e, 0x21, 0x3d, 0x5d, 0x83, 0x04, 0x60, 0xd9,
    0x48, 0x84, 0x2b, 0x2b, 0x04, 0xfe, 0xf2, 0x0e, 0xae, 0xc7, 0xf7, 0xc1, 0xc9, 0xa6, 0x0d, 0x39,
    0xd1, 0xfb, 0xcd, 0x3d, 0x5c, 0xff, 0x17, 0x0c, 0x44, 0x43, 0x51, 0x2f, 0x13, 0xcb, 0xe6, 0xee,
    0x00, 0x44, 0x87, 0xd7, 0x88, 0x29, 0xf6, 0xea, 0xb6, 0x5d, 0x28, 0x0a, 0x21, 0x70, 0xfd, 0x47,
    0xb8, 0x9f, 0xa9, 0x0a, 0x02, 0xf9, 0x82, 0xbc, 0x11, 0x58, 0xca, 0xc4, 0xde, 0x21, 0xfb, 0x86,
    0x11, 0xc1, 0x87, 0x77, 0x81, 0x7e, 0xa7, 0x7e, 0x80, 0xb6, 0xfd, 0x4b, 0xef, 0x54, 0x70, 0x7d,
    0x0f, 0x9b, 0x91, 0x50, 0xd2, 0x2d, 0x3f, 0xde, 0xea, 0xc3, 0x7b, 0x49, 0x2e, 0x84, 0x3c, 0xd1,
    0x57, 0xfa, 0x99, 0x30, 0x16, 0x50, 0xee, 0x7e, 0x52, 0x9f, 0xdf, 0xa9, 0xff, 0xb7, 0x6f, 0xc6,
    0xaa, 0x71, 0x03, 0x41, 0x18, 0xbe, 0x26, 0x18, 0xd4, 0xa9, 0xf0, 0x0b, 0xa8, 0x08, 0xe1, 0xda,
    0x54, 0xf6, 0x53, 0x24, 0x04, 0xb1, 0xa0, 0xf4, 0xa9, 0xd3, 0x88, 0xc5, 0x93, 0x17, 0x10, 0x41,
    0x9d, 0x0b, 0x73, 0xaa, 0x1c, 0x05, 0x31, 0xa7, 0x1f, 0x95, 0x49, 0x93, 0x74, 0x69, 0x72, 0x08,
    0x3d, 0x54, 0x76, 0x47, 0xde, 0x73, 0x94, 0xd3, 0x49, 0x2b, 0x70, 0xe3, 0xa0, 0xbf, 0xb9, 0x46,
    0xcb, 0xc7, 0xcc, 0xac, 0xd8, 0xdb, 0x7f, 0x46, 0xd1, 0xa9, 0xf8, 0xdb, 0x0f, 0xd6, 0x3f, 0x09,
    0xac, 0x20, 0x10, 0x34, 0x06, 0xf2, 0x02, 0x96, 0x04, 0xc5, 0x64, 0x58, 0xd3, 0x2f, 0x8a, 0xbc,
    0x8c, 0x85, 0x8d, 0xa3, 0xc9, 0x58, 0xe1, 0x93, 0xda, 0xff, 0x3a, 0x61, 0x6c, 0x55, 0xaa, 0x90,
    0x52, 0x4c, 0xa8, 0x84, 0x52, 0x5c, 0xd9, 0x7e, 0x7c, 0x72, 0x30, 0x79, 0x0f, 0x3a, 0xe8, 0x51,
    0x2b, 0xea, 0x90, 0x0f, 0xe2, 0x33, 0x10, 0x13, 0x3e, 0x90, 0xe5, 0xbf, 0x61, 0xcd, 0x8f, 0xf2,
    0x14, 0xf2, 0x5e, 0x29, 0x0d, 0x98, 0xe2, 0x83, 0x63, 0x12, 0x1f, 0x0b, 0xd0, 0x4d, 0x96, 0xb1,
    0xc9, 0x3d, 0x14, 0x0b, 0x60, 0x6a, 0x7b, 0x09, 0x64, 0x9f, 0xb7, 0x86, 0xd3, 0xb5, 0xd6, 0xfe,
    0xcb, 0x47, 0x20, 0x55, 0x6c, 0x21, 0x48, 0x15, 0xc0, 0xc9, 0xcd, 0x4e, 0xa2, 0x01, 0xd0, 0x71,
    0xb0, 0xff, 0x5c, 0x8d, 0x95, 0x44, 0xfa, 0xaf, 0xff, 0xd8, 0x20, 0x30, 0x8c, 0xac, 0x6b, 0x4b,
    0xdb, 0x65, 0x6a, 0x47, 0x20, 0x4c, 0x30, 0x62, 0xa1, 0xe0, 0x78, 0xb6, 0x40, 0x94, 0x62, 0x14,
    0x92, 0x64, 0x83, 0x7c, 0x25, 0xbd, 0xb7, 0xd8, 0xe5, 0x88, 0x50, 0x42, 0x10, 0xa7, 0x18, 0xa0,
    0x8c, 0xc0, 0x1c, 0x27, 0xe0, 0xe1, 0x34, 0x08, 0x2b, 0x10, 0x8f, 0x16, 0x25, 0xe3, 0xc1, 0x1e,
    0x2e, 0x9a, 0x7d, 0xd6, 0x75, 0x6d, 0xf4, 0xf2, 0xc3, 0xa4, 0x22, 0x10, 0xc7, 0x34, 0xb4, 0x18,
    0x8a, 0x8a, 0x6a, 0x39, 0x8f, 0xc7, 0xf2, 0x35, 0xb8, 0xa2, 0x34, 0x59, 0x95, 0x1d, 0x5a, 0x44,
    0x73, 0x02, 0xa8, 0xa2, 0x14, 0x83, 0x0b, 0x97, 0xc2, 0xcd, 0xb8, 0xf1, 0x41, 0x83, 0xc9, 0xdf,
    0x0b, 0x53, 0x73, 0xdb, 0x64, 0x2e, 0xb7, 0x98, 0x56, 0xb9, 0xbd, 0x06, 0xa1, 0x8a, 0x31, 0x08,
    0x44, 0x90, 0x3c, 0x3a, 0x68, 0x25, 0xa1, 0xb8, 0xb8, 0x0c, 0xa4, 0xc3, 0xae, 0x8c, 0xe6, 0xb4,
    0x15, 0xbb, 0x3a, 0x7d, 0xa4, 0x14, 0xac, 0x50, 0x0b, 0x6c, 0x74, 0x0a, 0x55, 0x28, 0x2e, 0x59,
    0x07, 0xb1, 0x85, 0x05, 0x12, 0x4e, 0x41, 0xd0, 0xef, 0xa8, 0x94, 0xdc, 0x3f, 0x56, 0xc4, 0x12,
    0xc8, 0xb8, 0x1b, 0x25, 0x96, 0xb6, 0xf5, 0xb6, 0x35, 0x4c, 0x7d, 0x0e, 0xd2, 0x0a, 0xf0, 0x82,
    0x48, 0xef, 0x9c, 0x2a, 0x05, 0xb0, 0x26, 0x58, 0x86, 0x9b, 0xfc, 0x3c, 0x33, 0x4b, 0x9d, 0x77,
    0xc4, 0x9d, 0xfd, 0x6d, 0x24, 0x7c, 0x81, 0x84, 0xe1, 0x28, 0xe5, 0x55, 0x96, 0x65, 0x02, 0x79,
    0xa0, 0xa4, 0xb1, 0x76, 0xfe, 0xb0, 0x4b, 0xdc, 0xd9, 0x41, 0x3e, 0x51, 0xde, 0x32, 0x2d, 0x81,
    0xdc, 0x32, 0x01, 0x3a, 0x8d, 0x63, 0xf1, 0x87, 0xaf, 0xa6, 0x20, 0x1b, 0x34, 0x82, 0xc9, 0x6d,
    0xd6, 0x82, 0x5a, 0x7b, 0xa7, 0x8b, 0xe8, 0x4b, 0x98, 0xc0, 0xe9, 0xeb, 0xeb, 0x77, 0x0a, 0x56,
    0xe3, 0x10, 0xe9, 0xf1, 0x77, 0x80, 0xbb, 0x40, 0xc1, 0x03, 0xc2, 0x77, 0xb0, 0xaa, 0xc2, 0xcb,
    0x7e, 0x89, 0x0e, 0xc3, 0x38, 0x01, 0x9b, 0xda, 0x9e, 0x1f, 0x5f, 0xd2, 0x5a, 0xa7, 0x86, 0xd6,
    0x37, 0xfa, 0x81, 0xf9, 0x2d, 0x0c, 0xf7, 0xa8, 0x75, 0x91, 0x54, 0x6a, 0xdd, 0x24, 0xb0, 0xef,
    0xfc, 0xae, 0xa8, 0xf0, 0x80, 0xb8, 0x47, 0x11, 0x8a, 0x2e, 0x13, 0xcf, 0x31, 0xaf, 0x47, 0x1b,
    0xeb, 0x1a, 0x73, 0x12, 0x86, 0x50, 0x58, 0x18, 0xc0, 0xe2, 0xcf, 0x4d, 0x0a, 0xcc, 0x69, 0x77,
    0x14, 0xde, 0x5e, 0x6a, 0xf2, 0x0c, 0x43, 0xcc, 0x94, 0xc2, 0x37, 0x5f, 0xdb, 0xbf, 0xee, 0x63,
    0x80, 0x5f, 0x35, 0x5c, 0x7b, 0x0b, 0xb2, 0xce, 0x03, 0x82, 0x87, 0x5b, 0xd2, 0x0e, 0x62, 0x50,
    0x78, 0x2b, 0x90, 0x03, 0x5b, 0x40, 0x7e, 0x10, 0x79, 0xf2, 0x4e, 0x8e, 0x10, 0x5f, 0xb9, 0x8e,
    0xa3, 0x64, 0xec, 0xce, 0x0b, 0x52, 0xd4, 0x3d, 0xa9, 0xf2, 0x87, 0xd4, 0x2e, 0x7e, 0x6f, 0x88,
    0xcb, 0x2e, 0x79, 0x33, 0x2e, 0xea, 0x3e, 0x5b, 0xb5, 0x3f, 0xc4, 0x2d, 0x28, 0x96, 0x94, 0x84,
    0xfb, 0x6a, 0x5a, 0xf9, 0x41, 0x24, 0x5f, 0xbc, 0xa0, 0xf2, 0xc1, 0x43, 0x49, 0x84, 0x71, 0xeb,
    0x07, 0x91, 0x15, 0x24, 0x95, 0x5f, 0x04, 0xa1, 0xe7, 0x0f, 0xb9, 0x00, 0x1c, 0xc4, 0xe7, 0x3d,
    0xb9, 0x79, 0x84, 0x00, 0x8b, 0x86, 0x84, 0xdd, 0x0e, 0x2e, 0x30, 0x0b, 0xe9, 0x29, 0x58, 0xfa,
    0xdd, 0x83, 0x60, 0x24, 0x1c, 0x80, 0xe6, 0x20, 0xf7, 0x01, 0xa4, 0xf7, 0x09, 0x87, 0x58, 0x44,
    0xd9, 0xf4, 0xde, 0xe2, 0x2c, 0xa4, 0xf7, 0x9e, 0x37, 0x47, 0xc6, 0x72, 0xf9, 0x40, 0x8e, 0x7a,
    0xf6, 0x90, 0x15, 0xb2, 0x42, 0x56, 0x08, 0xa6, 0x19, 0xdb, 0x27, 0x81, 0x04, 0x98, 0xd6, 0xe6,
    0x49, 0x84, 0x49, 0x49, 0x20, 0xff, 0x9f, 0x56, 0xad, 0x5a, 0xb5, 0xea, 0x0f, 0x7d, 0x19, 0x83,
    0x87, 0xd0, 0x53, 0xa5, 0xa2, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60,
    0x82
};
extern const size_t about_icon_size = sizeof(about_icon_data);
