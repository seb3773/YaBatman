#include "battery_info_dialog.h"
#include "inactivity_manager.h"
#include "calibration_manager.h"
#include "battery_icons.h"
#include <tqfile.h>
#include <tqtextstream.h>
#include <tqdir.h>
#include <tqlayout.h>
#include <tqpushbutton.h>
#include <tqimage.h>
#include <tqpixmap.h>
#include <tqfont.h>
#include <tqmessagebox.h>

static TQPixmap getScaledIcon(const unsigned char *data, size_t size, int width, int height) {
    TQImage img;
    if (img.loadFromData(data, size, "PNG")) {
        TQImage scaled = img.smoothScale(width, height);
        TQPixmap pm;
        pm.convertFromImage(scaled);
        return pm;
    }
    return TQPixmap();
}

BatteryInfoDialog::BatteryInfoDialog(InactivityManager *inactivity, CalibrationManager *calibration, TQWidget *parent)
    : TQDialog(parent, "BatteryInfoDialog", true)
{
    m_inactivity = inactivity;
    m_calibration = calibration;
    m_logger = m_inactivity->getBatteryLogger();
    setCaption("Battery Information");
    setWFlags(WStyle_Customize | WStyle_DialogBorder | WStyle_Title);

    // Set window icon from embedded yabatman icon data
    TQImage iconImg;
    if (iconImg.loadFromData(yabatman_data, yabatman_size, "PNG")) {
        TQPixmap pm;
        pm.convertFromImage(iconImg);
        setIcon(pm);
    }

    getBatterySysfsInfo();
    setupUI();

    connect(m_inactivity, TQT_SIGNAL(batteryStatusChanged(int, int)),
            this, TQT_SLOT(onBatteryStatusChanged(int, int)));
}

BatteryInfoDialog::~BatteryInfoDialog() {}

void BatteryInfoDialog::keyPressEvent(TQKeyEvent *e) {
    if (e->key() == Key_Escape) {
        accept();
    } else {
        TQDialog::keyPressEvent(e);
    }
}

void BatteryInfoDialog::getBatterySysfsInfo() {
    m_batteryPath = m_inactivity->getBatteryPath();
    m_batteryName = "";
    if (!m_batteryPath.isEmpty()) {
        int idx = m_batteryPath.findRev('/');
        if (idx != -1) {
            m_batteryName = m_batteryPath.mid(idx + 1);
        }
    }

    if (m_batteryPath.isEmpty()) {
        m_manufacturer = "Unknown";
        m_model = "No Battery Detected";
        m_technology = "N/A";
        m_designCapacity = 0;
        m_fullCapacity = 0;
        m_currentCapacity = 0;
        m_capacityPercent = 0;
        m_voltageNow = 0;
        m_voltageMin = 0;
        m_powerNow = 0;
        m_status = "N/A";
        m_cycleCount = 0;
        m_serialNumber = "N/A";
        m_healthPercent = 0.0;
        m_healthDesc = "N/A";
        m_avgChargeRate = 0.0;
        m_avgDischargeRate = 0.0;
        m_currentRate = 0.0;
        return;
    }

    m_manufacturer = readSysfsString(m_batteryPath + "/manufacturer");
    m_model = readSysfsString(m_batteryPath + "/model_name");
    m_technology = readSysfsString(m_batteryPath + "/technology");
    m_status = readSysfsString(m_batteryPath + "/status");

    m_designCapacity = readSysfsInt(m_batteryPath + "/energy_full_design");
    if (m_designCapacity == 0) m_designCapacity = readSysfsInt(m_batteryPath + "/charge_full_design");

    m_fullCapacity = readSysfsInt(m_batteryPath + "/energy_full");
    if (m_fullCapacity == 0) m_fullCapacity = readSysfsInt(m_batteryPath + "/charge_full");

    m_currentCapacity = readSysfsInt(m_batteryPath + "/energy_now");
    if (m_currentCapacity == 0) m_currentCapacity = readSysfsInt(m_batteryPath + "/charge_now");

    m_capacityPercent = readSysfsInt(m_batteryPath + "/capacity");
    m_voltageNow = readSysfsInt(m_batteryPath + "/voltage_now");
    m_voltageMin = readSysfsInt(m_batteryPath + "/voltage_min_design");

    m_powerNow = readSysfsInt(m_batteryPath + "/power_now");
    if (m_powerNow == 0) m_powerNow = readSysfsInt(m_batteryPath + "/current_now");

    m_cycleCount = readSysfsInt(m_batteryPath + "/cycle_count");
    m_serialNumber = readSysfsString(m_batteryPath + "/serial_number");
    if (m_serialNumber.isEmpty()) m_serialNumber = "N/A";

    // Health percentage calculation
    if (m_designCapacity > 0) {
        m_healthPercent = (100.0 * m_fullCapacity) / m_designCapacity;
        if (m_healthPercent > 100.0) m_healthPercent = 100.0;
        if (m_healthPercent < 0.0) m_healthPercent = 0.0;
    } else {
        m_healthPercent = 0.0;
    }

    // Health description mapping
    if (m_designCapacity <= 0) {
        m_healthDesc = "No design capacity information available to determine battery health.";
    } else if (m_healthPercent >= 95.0) {
        m_healthDesc = "The battery is near or at its maximum rated capacity. It is in excellent condition.";
    } else if (m_healthPercent >= 90.0) {
        m_healthDesc = "The battery performs close to its original capacity. There is little noticeable difference from its optimal state.";
    } else if (m_healthPercent >= 80.0) {
        m_healthDesc = "The battery has lost some capacity, but it should not be of much concern. Consider limiting its charge and using power-saving settings.";
    } else if (m_healthPercent >= 70.0) {
        m_healthDesc = "The battery has noticeably degraded in capacity, but is still usable. Runtime may be shorter than at its original capacity. Use power-optimizing settings to extend longevity.";
    } else if (m_healthPercent >= 60.0) {
        m_healthDesc = "The battery has experienced a significant drop in capacity. Mobility can be more difficult due to decreased runtime. Replacement may be necessary in the future.";
    } else {
        m_healthDesc = "The battery has undergone substantial deterioration. Power instability and potential overheating can damage other components. Replace the battery to avoid damage.";
    }

    // Fetch averages from logger
    m_logger->getAverageRates(m_avgChargeRate, m_avgDischargeRate);
    m_currentRate = m_inactivity->getCurrentRate();
}

void BatteryInfoDialog::setupUI() {
    TQVBoxLayout *mainLayout = new TQVBoxLayout(this, 0, 0);

    // Title Block
    TQFrame *headerFrame = new TQFrame(this);
    headerFrame->setPaletteBackgroundColor(TQColor(215, 215, 215)); // Darker gray background
    
    TQHBoxLayout *titleLayout = new TQHBoxLayout(headerFrame, 10, 10);
    
    // Icon
    TQLabel *iconLabel = new TQLabel(headerFrame);
    TQImage img;
    if (img.loadFromData(info_data, info_size, "PNG")) {
        TQPixmap pm;
        pm.convertFromImage(img);
        iconLabel->setPixmap(pm);
    }
    titleLayout->addWidget(iconLabel, 0, AlignVCenter);

    // Title Text
    TQLabel *titleText = new TQLabel("Battery Information", headerFrame);
    TQFont f = titleText->font();
    f.setPointSize(f.pointSize() + 3);
    f.setBold(true);
    titleText->setFont(f);
    titleLayout->addWidget(titleText, 0, AlignVCenter);
    titleLayout->addStretch();
    
    mainLayout->addWidget(headerFrame);

    TQVBoxLayout *contentLayout = new TQVBoxLayout(mainLayout, 12);
    contentLayout->setMargin(15);
    contentLayout->addSpacing(8);

    // Layout Columns
    TQHBoxLayout *columnsLayout = new TQHBoxLayout(contentLayout, 15);
    TQVBoxLayout *leftCol = new TQVBoxLayout(columnsLayout);
    TQVBoxLayout *midCol = new TQVBoxLayout(columnsLayout);
    TQVBoxLayout *rightCol = new TQVBoxLayout(columnsLayout);

    // Helper macro to add label & value to a grid
    #define ADD_GRID_ROW(grid, labelText, widget, r) \
        { \
            TQLabel* lbl = new TQLabel("<b>" labelText "</b>", this); \
            grid->addWidget(lbl, r, 0); \
            grid->addWidget(widget, r, 1); \
            r++; \
        }

    #define ADD_GRID_ROW_STATIC(grid, labelText, valueText, r) \
        { \
            TQLabel* lbl = new TQLabel("<b>" labelText "</b>", this); \
            TQLabel* val = new TQLabel(valueText, this); \
            grid->addWidget(lbl, r, 0); \
            grid->addWidget(val, r, 1); \
            r++; \
        }

    // Capacity formatters
    TQString designCapStr = "N/A";
    TQString fullCapStr = "N/A";
    if (m_designCapacity > 0) {
        if (readSysfsInt(m_batteryPath + "/energy_full_design") > 0 || readSysfsInt(m_batteryPath + "/energy_full") > 0) {
            designCapStr.sprintf("%.3f Wh", m_designCapacity / 1000000.0);
            fullCapStr.sprintf("%.3f Wh", m_fullCapacity / 1000000.0);
        } else {
            designCapStr.sprintf("%.3f Ah", m_designCapacity / 1000000.0);
            fullCapStr.sprintf("%.3f Ah", m_fullCapacity / 1000000.0);
        }
    }

    TQString minVoltageStr = "N/A";
    if (m_voltageMin > 0) {
        minVoltageStr.sprintf("%.3f V", m_voltageMin / 1000000.0);
    }

    // --- LEFT COLUMN ---

    TQGridLayout *leftGrid = new TQGridLayout(leftCol, 15, 3, 4);
    leftGrid->setColSpacing(0, 15);
    leftGrid->setColStretch(0, 0);
    leftGrid->setColStretch(1, 0);
    leftGrid->setColStretch(2, 1);
    int rl = 0;

    // 5. Model Details
    TQHBoxLayout *sec5Layout = new TQHBoxLayout(6);
    TQLabel *sec5Icon = new TQLabel(this);
    sec5Icon->setPixmap(getScaledIcon(model_data, model_size, 32, 32));
    TQLabel *sec5 = new TQLabel("<b><font size=\"+0.5\" color=\"#1a5fb4\">Model Details</font></b>", this);
    sec5Layout->addWidget(sec5Icon);
    sec5Layout->addWidget(sec5);
    sec5Layout->addStretch();
    leftGrid->addMultiCellLayout(sec5Layout, rl, rl, 0, 2);
    rl++;
    ADD_GRID_ROW_STATIC(leftGrid, "Vendor:", m_manufacturer.isEmpty() ? "Unknown" : m_manufacturer, rl);
    ADD_GRID_ROW_STATIC(leftGrid, "Device:", m_model.isEmpty() ? "Unknown" : m_model, rl);
    ADD_GRID_ROW_STATIC(leftGrid, "Serial Number:", m_serialNumber.isEmpty() ? "Unknown" : m_serialNumber, rl);
    ADD_GRID_ROW_STATIC(leftGrid, "Technology:", m_technology.isEmpty() ? "Unknown" : m_technology, rl);

    // Spacer
    TQWidget *spacerL1 = new TQWidget(this);
    spacerL1->setFixedHeight(20);
    leftGrid->addWidget(spacerL1, rl, 0);
    rl++;

    // 3. Energy Indicators
    TQHBoxLayout *sec3Layout = new TQHBoxLayout(6);
    TQLabel *sec3Icon = new TQLabel(this);
    sec3Icon->setPixmap(getScaledIcon(indicators_data, indicators_size, 32, 32));
    TQLabel *sec3 = new TQLabel("<b><font size=\"+0.5\" color=\"#1a5fb4\">Energy Indicators</font></b>", this);
    sec3Layout->addWidget(sec3Icon);
    sec3Layout->addWidget(sec3);
    sec3Layout->addStretch();
    leftGrid->addMultiCellLayout(sec3Layout, rl, rl, 0, 2);
    rl++;
    m_remainingEnergyVal = new TQLabel(this);
    m_powerNowVal = new TQLabel(this);
    m_currentRateVal = new TQLabel(this);
    m_currentRateLbl = new TQLabel("<b>Current discharging rate:</b>", this);

    ADD_GRID_ROW_STATIC(leftGrid, "Design Capacity:", designCapStr, rl);
    ADD_GRID_ROW_STATIC(leftGrid, "Full Charged Capacity:", fullCapStr, rl);
    ADD_GRID_ROW(leftGrid, "Remaining Energy:", m_remainingEnergyVal, rl);
    ADD_GRID_ROW(leftGrid, "Net Energy Rate:", m_powerNowVal, rl);

    leftGrid->addWidget(m_currentRateLbl, rl, 0);
    leftGrid->addWidget(m_currentRateVal, rl, 1);
    rl++;

    leftCol->addStretch();

    // --- MIDDLE COLUMN ---
    TQGridLayout *midGrid = new TQGridLayout(midCol, 15, 3, 4);
    midGrid->setColSpacing(0, 15);
    midGrid->setColStretch(0, 0);
    midGrid->setColStretch(1, 0);
    midGrid->setColStretch(2, 1);
    int rm = 0;

    // 1. State of Charge
    TQHBoxLayout *sec1Layout = new TQHBoxLayout(6);
    m_sec1Icon = new TQLabel(this);
    TQLabel *sec1 = new TQLabel("<b><font size=\"+0.5\" color=\"#1a5fb4\">State of Charge</font></b>", this);
    sec1Layout->addWidget(m_sec1Icon);
    sec1Layout->addWidget(sec1);
    sec1Layout->addStretch();
    midGrid->addMultiCellLayout(sec1Layout, rm, rm, 0, 2);
    rm++;
    m_statusVal = new TQLabel(this);
    m_currentCapVal = new TQLabel(this);
    m_cycleCountVal = new TQLabel(this);
    ADD_GRID_ROW(midGrid, "State:", m_statusVal, rm);
    ADD_GRID_ROW(midGrid, "Current Charge Percentage:", m_currentCapVal, rm);
    ADD_GRID_ROW(midGrid, "Charge Cycles:", m_cycleCountVal, rm);

    // Spacer
    TQWidget *spacerM1 = new TQWidget(this);
    spacerM1->setFixedHeight(20);
    midGrid->addWidget(spacerM1, rm, 0);
    rm++;

    // 2. Time Calculations
    TQHBoxLayout *sec2Layout = new TQHBoxLayout(6);
    TQLabel *sec2Icon = new TQLabel(this);
    sec2Icon->setPixmap(getScaledIcon(times_data, times_size, 32, 32));
    TQLabel *sec2 = new TQLabel("<b><font size=\"+0.5\" color=\"#1a5fb4\">Time Calculations</font></b>", this);
    sec2Layout->addWidget(sec2Icon);
    sec2Layout->addWidget(sec2);
    sec2Layout->addStretch();
    midGrid->addMultiCellLayout(sec2Layout, rm, rm, 0, 2);
    rm++;
    m_remainingTimeVal = new TQLabel(this);
    m_avgChargeRateVal = new TQLabel(this);
    m_avgDischargeRateVal = new TQLabel(this);
    ADD_GRID_ROW(midGrid, "Estimated Time:", m_remainingTimeVal, rm);
    ADD_GRID_ROW(midGrid, "Average Charging Rate:", m_avgChargeRateVal, rm);
    ADD_GRID_ROW(midGrid, "Average Discharging Rate:", m_avgDischargeRateVal, rm);

    midCol->addStretch();

    // --- RIGHT COLUMN ---
    TQGridLayout *rightGrid = new TQGridLayout(rightCol, 15, 3, 4);
    rightGrid->setColSpacing(0, 15);
    rightGrid->setColStretch(0, 0);
    rightGrid->setColStretch(1, 0);
    rightGrid->setColStretch(2, 1);
    int rr = 0;

    // 4. Voltage Statistics
    TQHBoxLayout *sec4Layout = new TQHBoxLayout(6);
    TQLabel *sec4Icon = new TQLabel(this);
    sec4Icon->setPixmap(getScaledIcon(charge_data, charge_size, 32, 32));
    TQLabel *sec4 = new TQLabel("<b><font size=\"+0.5\" color=\"#1a5fb4\">Voltage Statistics</font></b>", this);
    sec4Layout->addWidget(sec4Icon);
    sec4Layout->addWidget(sec4);
    sec4Layout->addStretch();
    rightGrid->addMultiCellLayout(sec4Layout, rr, rr, 0, 2);
    rr++;
    m_voltageVal = new TQLabel(this);
    ADD_GRID_ROW(rightGrid, "Current Voltage:", m_voltageVal, rr);
    ADD_GRID_ROW_STATIC(rightGrid, "Design Minimum Voltage:", minVoltageStr, rr);

    // Spacer
    TQWidget *spacerR1 = new TQWidget(this);
    spacerR1->setFixedHeight(20);
    rightGrid->addWidget(spacerR1, rr, 0);
    rr++;

    // 6. Health Evaluations
    TQHBoxLayout *sec6Layout = new TQHBoxLayout(6);
    TQLabel *sec6Icon = new TQLabel(this);
    sec6Icon->setPixmap(getScaledIcon(health_data, health_size, 32, 32));
    TQLabel *sec6 = new TQLabel("<b><font size=\"+0.5\" color=\"#1a5fb4\">Health Evaluations</font></b>", this);
    sec6Layout->addWidget(sec6Icon);
    sec6Layout->addWidget(sec6);
    sec6Layout->addStretch();
    rightGrid->addMultiCellLayout(sec6Layout, rr, rr, 0, 2);
    rr++;
    m_healthPercentVal = new TQLabel(this);
    m_lastCalibVal = new TQLabel(this);
    TQString lastCalib = m_inactivity->getConfig()->last_calibration;
    if (lastCalib.isEmpty()) {
        m_lastCalibVal->setText("Never");
    } else {
        m_lastCalibVal->setText(lastCalib);
    }
    m_healthDescVal = new TQLabel(this);
    m_healthDescVal->setAlignment(AlignLeft | WordBreak);
    ADD_GRID_ROW(rightGrid, "State of Health:", m_healthPercentVal, rr);
    ADD_GRID_ROW(rightGrid, "Last Calibration:", m_lastCalibVal, rr);
    ADD_GRID_ROW(rightGrid, "Device Condition:", m_healthDescVal, rr);

    rightCol->addStretch();

    #undef ADD_GRID_ROW
    #undef ADD_GRID_ROW_STATIC

    updateUIValues();

    contentLayout->addSpacing(15);

    // Bottom close button
    TQHBoxLayout *buttonLayout = new TQHBoxLayout(contentLayout, 10);
    TQPushButton *calibBtn = new TQPushButton("Calibrate Battery...", this);
    connect(calibBtn, TQT_SIGNAL(clicked()), this, TQT_SLOT(onCalibrateBattery()));
    buttonLayout->addWidget(calibBtn);

    buttonLayout->addStretch();

    TQPushButton *closeBtn = new TQPushButton("Close", this);
    closeBtn->setDefault(true);
    connect(closeBtn, TQT_SIGNAL(clicked()), this, TQT_SLOT(accept()));
    buttonLayout->addWidget(closeBtn);

    resize(960, 680);
}

void BatteryInfoDialog::updateUIValues() {
    // Current capacity percentage
    TQString currentCapStr;
    currentCapStr.sprintf("%d%%", m_capacityPercent);
    m_currentCapVal->setText(currentCapStr);

    // Status
    m_statusVal->setText(m_status);

    // Cycles
    m_cycleCountVal->setText(m_cycleCount > 0 ? TQString::number(m_cycleCount) : "N/A");

    // Remaining time
    TQString durationStr = "";
    if (m_status != "Full" && m_powerNow > 0) {
        int remain = (m_status == "Charging") ? (m_fullCapacity - m_currentCapacity) : m_currentCapacity;
        if (remain > 0) {
            int hours = remain / m_powerNow;
            int mins = (int)((double)remain * 60.0 / m_powerNow) % 60;
            if (m_status == "Charging") {
                durationStr.sprintf("%02d:%02d:00 until full", hours, mins);
            } else {
                durationStr.sprintf("%02d:%02d:00 remaining", hours, mins);
            }
        }
    }
    m_remainingTimeVal->setText(durationStr.isEmpty() ? "N/A" : durationStr);

    // Voltage
    TQString voltageStr;
    voltageStr.sprintf("%.3f V", m_voltageNow / 1000000.0);
    m_voltageVal->setText(voltageStr);

    // Average rates
    TQString chargeRateStr = "N/A";
    TQString dischargeRateStr = "N/A";
    if (m_avgChargeRate > 0.0) {
        chargeRateStr.sprintf("%.2f %%/h", m_avgChargeRate);
        double eta = 100.0 / m_avgChargeRate;
        TQString etaStr;
        etaStr.sprintf(" (ETA full: %dh %02dm)", (int)eta, (int)((eta - (int)eta) * 60.0));
        chargeRateStr += etaStr;
    }
    if (m_avgDischargeRate > 0.0) {
        dischargeRateStr.sprintf("%.2f %%/h", m_avgDischargeRate);
        double eta = 100.0 / m_avgDischargeRate;
        TQString etaStr;
        etaStr.sprintf(" (ETA empty: %dh %02dm)", (int)eta, (int)((eta - (int)eta) * 60.0));
        dischargeRateStr += etaStr;
    }
    m_avgChargeRateVal->setText(chargeRateStr);
    m_avgDischargeRateVal->setText(dischargeRateStr);

    // Remaining Energy
    TQString remainingEnergyStr = "N/A";
    if (m_currentCapacity > 0) {
        if (readSysfsInt(m_batteryPath + "/energy_now") > 0) {
            remainingEnergyStr.sprintf("%.3f Wh", m_currentCapacity / 1000000.0);
        } else {
            remainingEnergyStr.sprintf("%.3f Ah", m_currentCapacity / 1000000.0);
        }
    }
    m_remainingEnergyVal->setText(remainingEnergyStr);

    // Net Energy Rate
    TQString powerNowStr = "N/A";
    if (m_powerNow > 0) {
        if (readSysfsInt(m_batteryPath + "/power_now") > 0) {
            powerNowStr.sprintf("%.3f W", m_powerNow / 1000000.0);
        } else {
            powerNowStr.sprintf("%.3f A", m_powerNow / 1000000.0);
        }
    }
    m_powerNowVal->setText(powerNowStr);

    // Current rate
    TQString currentRateStr = "N/A";
    if (m_currentRate != 0.0) {
        double absRate = m_currentRate < 0.0 ? -m_currentRate : m_currentRate;
        currentRateStr.sprintf("%.2f %%/h", absRate);
        if (m_currentRate > 0.0) {
            double eta = (100.0 - m_capacityPercent) / m_currentRate;
            TQString etaStr;
            etaStr.sprintf(" (ETA full: %dh %02dm)", (int)eta, (int)((eta - (int)eta) * 60.0));
            currentRateStr += etaStr;
        } else {
            double eta = m_capacityPercent / absRate;
            TQString etaStr;
            etaStr.sprintf(" (ETA empty: %dh %02dm)", (int)eta, (int)((eta - (int)eta) * 60.0));
            currentRateStr += etaStr;
        }
    }
    if (m_status == "Charging") {
        m_currentRateLbl->setText("<b>Current charging rate:</b>");
    } else {
        m_currentRateLbl->setText("<b>Current discharging rate:</b>");
    }
    m_currentRateVal->setText(currentRateStr);

    // Health
    TQString healthPercentStr = "N/A";
    if (m_designCapacity > 0) {
        healthPercentStr.sprintf("%.3f%%", m_healthPercent);
    }
    m_healthPercentVal->setText(healthPercentStr);
    m_healthDescVal->setText(m_healthDesc);

    // Update State of Charge Header Icon dynamically
    if (m_inactivity->getChargingState() != 0) {
        m_sec1Icon->setPixmap(getScaledIcon(battery_level_60_charging_symbolic_data, battery_level_60_charging_symbolic_size, 32, 32));
    } else {
        m_sec1Icon->setPixmap(getScaledIcon(battery_level_60_symbolic_data, battery_level_60_symbolic_size, 32, 32));
    }
}

void BatteryInfoDialog::onBatteryStatusChanged(int /*pct*/, int /*chg*/) {
    getBatterySysfsInfo();
    updateUIValues();
}

void BatteryInfoDialog::onCalibrateBattery() {
    TQString msg = 
        "<b>Battery Calibration</b> resets the battery charge controller limits to improve accuracy of remaining lifetime estimations.<br><br>"
        "<b>Process Steps:</b><br>"
        "1. <b>Charging Phase:</b> The battery will charge fully to 100% (charger must be connected).<br>"
        "2. <b>Discharging Phase:</b> You will be prompted to unplug the charger. The CPU will run at maximum load to drain the battery to 3%.<br>"
        "3. <b>Final Charging:</b> You will be prompted to connect the charger to charge back to 100% and finish.<br><br>"
        "<i>Important: Please do not leave your computer unattended during the discharging phase. If the battery level drops below 2% before you reconnect the charger, the system will automatically suspend to protect your battery from sudden shutdown and hardware damage.</i><br><br>"
        "<i>Note: A full screen overlay will cover your screen during this process to ensure continuous execution. You can cancel at any time.</i><br><br>"
        "Do you want to start the calibration process now?";

    int result = TQMessageBox::information(this, "Battery Calibration", msg, "Start calibration", "Cancel");
    if (result == 0) {
        accept(); // Close the info dialog
        if (m_calibration) {
            m_calibration->startCalibration();
        }
    }
}
