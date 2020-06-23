// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QCheckBox>
#include <QSpinBox>
#include "core/core.h"
#include "core/settings.h"
#include "ui_configure_general.h"
#include "yuzu/configuration/configure_general.h"
#include "yuzu/uisettings.h"

ConfigureGeneral::ConfigureGeneral(QWidget* parent)
    : QWidget(parent), ui(new Ui::ConfigureGeneral) {
    ui->setupUi(this);

    SetupPerGameUI();

    SetConfiguration();

    connect(ui->toggle_frame_limit, &QCheckBox::stateChanged, ui->frame_limit, [this]() {
        ui->frame_limit->setEnabled(ui->toggle_frame_limit->checkState() == Qt::Checked);
    });
}

ConfigureGeneral::~ConfigureGeneral() = default;

void ConfigureGeneral::SetConfiguration() {
    const bool runtime_lock = !Core::System::GetInstance().IsPoweredOn();

    ui->use_multi_core->setEnabled(runtime_lock);
    ui->use_multi_core->setChecked(Settings::values.use_multi_core);

    ui->toggle_check_exit->setChecked(UISettings::values.confirm_before_closing);
    ui->toggle_user_on_boot->setChecked(UISettings::values.select_user_on_boot);
    ui->toggle_background_pause->setChecked(UISettings::values.pause_when_in_background);
    ui->toggle_hide_mouse->setChecked(UISettings::values.hide_mouse);

    ui->toggle_frame_limit->setChecked(Settings::values.use_frame_limit);
    ui->frame_limit->setValue(Settings::values.frame_limit);

    if (!Settings::configuring_global && Settings::values.use_frame_limit.UsingGlobal()) {
        ui->toggle_frame_limit->setCheckState(Qt::PartiallyChecked);
    }

    ui->frame_limit->setEnabled(ui->toggle_frame_limit->checkState() == Qt::Checked);
}

void ConfigureGeneral::ApplyConfiguration() {
    if (Settings::configuring_global) {
        UISettings::values.confirm_before_closing = ui->toggle_check_exit->isChecked();
        UISettings::values.select_user_on_boot = ui->toggle_user_on_boot->isChecked();
        UISettings::values.pause_when_in_background = ui->toggle_background_pause->isChecked();
        UISettings::values.hide_mouse = ui->toggle_hide_mouse->isChecked();
    }

    Settings::values.use_multi_core = ui->use_multi_core->isChecked();
    if (ui->toggle_frame_limit->checkState() != Qt::PartiallyChecked) {
        if (!Settings::configuring_global) {
            Settings::values.use_frame_limit.SetGlobal(false);
            Settings::values.frame_limit.SetGlobal(false);
        }
        Settings::values.use_frame_limit = ui->toggle_frame_limit->checkState() == Qt::Checked;
        Settings::values.frame_limit = ui->frame_limit->value();
    } else {
        Settings::values.use_frame_limit.SetGlobal(true);
        Settings::values.frame_limit.SetGlobal(true);
    }
}

void ConfigureGeneral::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void ConfigureGeneral::RetranslateUI() {
    ui->retranslateUi(this);
}

void ConfigureGeneral::SetupPerGameUI() {
    if (Settings::configuring_global) {
        return;
    }

    ui->toggle_check_exit->setVisible(false);
    ui->toggle_user_on_boot->setVisible(false);
    ui->toggle_background_pause->setVisible(false);
    ui->toggle_hide_mouse->setVisible(false);

    ui->toggle_frame_limit->setTristate(true);
}
