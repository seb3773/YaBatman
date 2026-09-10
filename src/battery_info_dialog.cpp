#include "battery_info_dialog.h"
#include "inactivity_manager.h"
#include "calibration_manager.h"
#include "battery_icons.h"
#include "theme_utils.h"
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
    m_selectedBatteryIndex = 0;
    m_batteryCombo = NULL;
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

void BatteryInfoDialog::onBatterySelected(int index) {
    m_selectedBatteryIndex = index;
    getBatterySysfsInfo();
    updateUIValues();
}

void BatteryInfoDialog::getBatterySysfsInfo() {
    int count = m_inactivity->getBatteryCount();
    if (count == 0) {
        m_batteryPath = "";
        m_batteryName = "";
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

    const TQValueList<BatteryDevice> &bats = m_inactivity->getBatteries();

    if (count > 1 && m_selectedBatteryIndex == 0) {
        // Combined Overview
        m_batteryPath = "";
        m_batteryName = "Combined";
        m_manufacturer = "Multiple Vendors";
        m_model = TQString("System Batteries (%1 units)").arg(count);
        m_serialNumber = "Combined";
        m_technology = bats.first().technology.isEmpty() ? "N/A" : bats.first().technology;
        m_status = (m_inactivity->getChargingState() == 1 ? "Charging" : (m_inactivity->getChargingState() == 2 ? "Full" : "Discharging"));
        m_capacityPercent = m_inactivity->getBatteryPercentage();

        m_designCapacity = 0;
        m_fullCapacity = 0;
        m_currentCapacity = 0;
        m_powerNow = 0;
        m_voltageNow = 0;
        m_voltageMin = 0;
        m_cycleCount = 0;

        for (TQValueList<BatteryDevice>::ConstIterator it = bats.begin(); it != bats.end(); ++it) {
            m_designCapacity += (*it).energyDesign;
            m_fullCapacity += (*it).energyFull;
            m_currentCapacity += (*it).energyNow;
            m_powerNow += (*it).powerNow;
            m_voltageNow += (*it).voltageNow;
            m_voltageMin += (*it).voltageMin;
            m_cycleCount += (*it).cycleCount;
        }
        if (count > 0) {
            m_voltageNow /= count;
            m_voltageMin /= count;
        }
    } else {
        // Specific battery (or only 1 battery exists)
        int idx = (count == 1 || m_selectedBatteryIndex == 0) ? 0 : (m_selectedBatteryIndex - 1);
        if (idx < 0 || idx >= count) idx = 0;
        const BatteryDevice &dev = bats[idx];

        m_batteryPath = dev.path;
        m_batteryName = dev.name;
        m_manufacturer = dev.vendor.isEmpty() ? "Unknown" : dev.vendor;
        m_model = dev.model.isEmpty() ? "Unknown" : dev.model;
        m_serialNumber = dev.serial.isEmpty() ? "N/A" : dev.serial;
        m_technology = dev.technology.isEmpty() ? "N/A" : dev.technology;
        m_status = dev.status;
        m_capacityPercent = dev.percentage;
        m_designCapacity = dev.energyDesign;
        m_fullCapacity = dev.energyFull;
        m_currentCapacity = dev.energyNow;
        m_powerNow = dev.powerNow;
        m_voltageNow = dev.voltageNow;
        m_voltageMin = dev.voltageMin;
        m_cycleCount = dev.cycleCount;
    }

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

    YabatmanTheme theme = resolveTheme(m_inactivity->getConfig());
    m_isDark = theme.isDark;

    // Title Block
    TQFrame *headerFrame = new TQFrame(this);
    TQHBoxLayout *titleLayout = new TQHBoxLayout(headerFrame, 10, 10);
    
    // Icon
    TQLabel *iconLabel = new TQLabel(headerFrame);
    iconLabel->setPixmap(getThemedPixmap(info_data, info_size, 32, 32, theme.isDark));
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

    applyDialogTheme(this, theme, headerFrame, titleText);

    TQString secTitleColor = theme.isDark ? "#5294e2" : "#1a5fb4";

    TQVBoxLayout *contentLayout = new TQVBoxLayout(mainLayout, 12);
    contentLayout->setMargin(15);
    contentLayout->addSpacing(8);

    if (m_inactivity->getBatteryCount() > 1) {
        TQHBoxLayout *comboLayout = new TQHBoxLayout(contentLayout, 8);
        TQLabel *selectLbl = new TQLabel("<b>Battery Device:</b>", this);
        m_batteryCombo = new TQComboBox(false, this);
        m_batteryCombo->insertItem("All Batteries (Combined Overview)");
        const TQValueList<BatteryDevice> &bats = m_inactivity->getBatteries();
        int bidx = 1;
        for (TQValueList<BatteryDevice>::ConstIterator it = bats.begin(); it != bats.end(); ++it, ++bidx) {
            TQString label;
            label.sprintf("Battery %d (%s) - %d%%", bidx, (*it).name.latin1(), (*it).percentage);
            m_batteryCombo->insertItem(label);
        }
        m_batteryCombo->setCurrentItem(m_selectedBatteryIndex);
        comboLayout->addWidget(selectLbl);
        comboLayout->addWidget(m_batteryCombo, 1);
        comboLayout->addStretch();
        connect(m_batteryCombo, TQT_SIGNAL(activated(int)), this, TQT_SLOT(onBatterySelected(int)));
        contentLayout->addSpacing(6);
    } else {
        m_batteryCombo = NULL;
    }

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
    sec5Icon->setPixmap(getThemedPixmap(model_data, model_size, 32, 32, theme.isDark));
    TQLabel *sec5 = new TQLabel(TQString("<b><font size=\"+0.5\" color=\"%1\">Model Details</font></b>").arg(secTitleColor), this);
    sec5Layout->addWidget(sec5Icon);
    sec5Layout->addWidget(sec5);
    sec5Layout->addStretch();
    leftGrid->addMultiCellLayout(sec5Layout, rl, rl, 0, 2);
    rl++;
    m_vendorVal = new TQLabel(this);
    m_modelVal = new TQLabel(this);
    m_serialNumberVal = new TQLabel(this);
    m_technologyVal = new TQLabel(this);
    ADD_GRID_ROW(leftGrid, "Vendor:", m_vendorVal, rl);
    ADD_GRID_ROW(leftGrid, "Device:", m_modelVal, rl);
    ADD_GRID_ROW(leftGrid, "Serial Number:", m_serialNumberVal, rl);
    ADD_GRID_ROW(leftGrid, "Technology:", m_technologyVal, rl);

    // Spacer
    TQWidget *spacerL1 = new TQWidget(this);
    spacerL1->setFixedHeight(20);
    leftGrid->addWidget(spacerL1, rl, 0);
    rl++;

    // 3. Energy Indicators
    TQHBoxLayout *sec3Layout = new TQHBoxLayout(6);
    TQLabel *sec3Icon = new TQLabel(this);
    sec3Icon->setPixmap(getThemedPixmap(indicators_data, indicators_size, 32, 32, theme.isDark));
    TQLabel *sec3 = new TQLabel(TQString("<b><font size=\"+0.5\" color=\"%1\">Energy Indicators</font></b>").arg(secTitleColor), this);
    sec3Layout->addWidget(sec3Icon);
    sec3Layout->addWidget(sec3);
    sec3Layout->addStretch();
    leftGrid->addMultiCellLayout(sec3Layout, rl, rl, 0, 2);
    rl++;
    m_remainingEnergyVal = new TQLabel(this);
    m_powerNowVal = new TQLabel(this);
    m_currentRateVal = new TQLabel(this);
    m_currentRateLbl = new TQLabel("<b>Current discharging rate:</b>", this);
    m_designCapacityVal = new TQLabel(this);
    m_fullCapacityVal = new TQLabel(this);

    ADD_GRID_ROW(leftGrid, "Design Capacity:", m_designCapacityVal, rl);
    ADD_GRID_ROW(leftGrid, "Full Charged Capacity:", m_fullCapacityVal, rl);
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
    TQLabel *sec1 = new TQLabel(TQString("<b><font size=\"+0.5\" color=\"%1\">State of Charge</font></b>").arg(secTitleColor), this);
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
    sec2Icon->setPixmap(getThemedPixmap(times_data, times_size, 32, 32, theme.isDark));
    TQLabel *sec2 = new TQLabel(TQString("<b><font size=\"+0.5\" color=\"%1\">Time Calculations</font></b>").arg(secTitleColor), this);
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
    sec4Icon->setPixmap(getThemedPixmap(charge_data, charge_size, 32, 32, theme.isDark));
    TQLabel *sec4 = new TQLabel(TQString("<b><font size=\"+0.5\" color=\"%1\">Voltage Statistics</font></b>").arg(secTitleColor), this);
    sec4Layout->addWidget(sec4Icon);
    sec4Layout->addWidget(sec4);
    sec4Layout->addStretch();
    rightGrid->addMultiCellLayout(sec4Layout, rr, rr, 0, 2);
    rr++;
    m_voltageVal = new TQLabel(this);
    m_voltageMinVal = new TQLabel(this);
    ADD_GRID_ROW(rightGrid, "Current Voltage:", m_voltageVal, rr);
    ADD_GRID_ROW(rightGrid, "Design Minimum Voltage:", m_voltageMinVal, rr);

    // Spacer
    TQWidget *spacerR1 = new TQWidget(this);
    spacerR1->setFixedHeight(20);
    rightGrid->addWidget(spacerR1, rr, 0);
    rr++;

    // 6. Health Evaluations
    TQHBoxLayout *sec6Layout = new TQHBoxLayout(6);
    TQLabel *sec6Icon = new TQLabel(this);
    sec6Icon->setPixmap(getThemedPixmap(health_data, health_size, 32, 32, theme.isDark));
    TQLabel *sec6 = new TQLabel(TQString("<b><font size=\"+0.5\" color=\"%1\">Health Evaluations</font></b>").arg(secTitleColor), this);
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
    // Model Details
    m_vendorVal->setText(m_manufacturer.isEmpty() ? "Unknown" : m_manufacturer);
    m_modelVal->setText(m_model.isEmpty() ? "Unknown" : m_model);
    m_serialNumberVal->setText(m_serialNumber.isEmpty() ? "Unknown" : m_serialNumber);
    m_technologyVal->setText(m_technology.isEmpty() ? "Unknown" : m_technology);

    // Design Capacities
    TQString designCapStr = "N/A";
    TQString fullCapStr = "N/A";
    if (m_designCapacity > 0) {
        designCapStr.sprintf("%.3f Wh", m_designCapacity / 1000000.0);
        fullCapStr.sprintf("%.3f Wh", m_fullCapacity / 1000000.0);
    }
    m_designCapacityVal->setText(designCapStr);
    m_fullCapacityVal->setText(fullCapStr);

    TQString minVoltageStr = "N/A";
    if (m_voltageMin > 0) {
        minVoltageStr.sprintf("%.3f V", m_voltageMin / 1000000.0);
    }
    m_voltageMinVal->setText(minVoltageStr);

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
        m_sec1Icon->setPixmap(getThemedPixmap(battery_level_60_charging_symbolic_data, battery_level_60_charging_symbolic_size, 32, 32, m_isDark));
    } else {
        m_sec1Icon->setPixmap(getThemedPixmap(battery_level_60_symbolic_data, battery_level_60_symbolic_size, 32, 32, m_isDark));
    }
}

void BatteryInfoDialog::onBatteryStatusChanged(int /*pct*/, int /*chg*/) {
    getBatterySysfsInfo();
    if (m_batteryCombo && m_inactivity->getBatteryCount() > 1) {
        const TQValueList<BatteryDevice> &bats = m_inactivity->getBatteries();
        int bidx = 1;
        for (TQValueList<BatteryDevice>::ConstIterator it = bats.begin(); it != bats.end(); ++it, ++bidx) {
            TQString label;
            label.sprintf("Battery %d (%s) - %d%%", bidx, (*it).name.latin1(), (*it).percentage);
            m_batteryCombo->changeItem(label, bidx);
        }
    }
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
