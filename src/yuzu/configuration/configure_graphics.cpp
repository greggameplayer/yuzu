// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QColorDialog>
#include <QComboBox>
#ifdef HAS_VULKAN
#include <QVulkanInstance>
#endif

#include "common/common_types.h"
#include "common/logging/log.h"
#include "core/core.h"
#include "core/settings.h"
#include "ui_configure_graphics.h"
#include "yuzu/configuration/configuration_shared.h"
#include "yuzu/configuration/configure_graphics.h"

#ifdef HAS_VULKAN
#include "video_core/renderer_vulkan/renderer_vulkan.h"
#endif

ConfigureGraphics::ConfigureGraphics(QWidget* parent)
    : QWidget(parent), ui(new Ui::ConfigureGraphics) {
    vulkan_device = Settings::values.vulkan_device;
    RetrieveVulkanDevices();

    ui->setupUi(this);

    SetupPerGameUI();

    SetConfiguration();

    connect(ui->api, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            [this] { UpdateDeviceComboBox(); });
    connect(ui->device, static_cast<void (QComboBox::*)(int)>(&QComboBox::activated), this,
            [this](int device) { UpdateDeviceSelection(device); });

    ui->bg_label->setVisible(Settings::configuring_global);
    ui->bg_combobox->setVisible(!Settings::configuring_global);
}

void ConfigureGraphics::UpdateDeviceSelection(int device) {
    if (device == -1) {
        return;
    }
    if (GetCurrentGraphicsBackend() == Settings::RendererBackend::Vulkan) {
        vulkan_device = device;
    }
}

ConfigureGraphics::~ConfigureGraphics() = default;

void ConfigureGraphics::SetConfiguration() {
    const bool runtime_lock = !Core::System::GetInstance().IsPoweredOn();

    ui->api->setEnabled(runtime_lock);
    ui->use_asynchronous_gpu_emulation->setEnabled(runtime_lock);
    ui->use_disk_shader_cache->setEnabled(runtime_lock);

    if (Settings::configuring_global) {
        ui->api->setCurrentIndex(static_cast<int>(Settings::values.renderer_backend.GetValue()));
        ui->aspect_ratio_combobox->setCurrentIndex(Settings::values.aspect_ratio);
        ui->use_disk_shader_cache->setChecked(Settings::values.use_disk_shader_cache);
        ui->use_asynchronous_gpu_emulation->setChecked(
            Settings::values.use_asynchronous_gpu_emulation);
    } else {
        ConfigurationShared::SetPerGameSetting(ui->use_disk_shader_cache,
                                               &Settings::values.use_disk_shader_cache);
        ConfigurationShared::SetPerGameSetting(ui->use_asynchronous_gpu_emulation,
                                               &Settings::values.use_asynchronous_gpu_emulation);

        ConfigurationShared::SetPerGameSetting(ui->api, &Settings::values.renderer_backend);
        ConfigurationShared::SetPerGameSetting(ui->aspect_ratio_combobox,
                                               &Settings::values.aspect_ratio);

        ui->bg_combobox->setCurrentIndex(Settings::values.bg_red.UsingGlobal() ? 0 : 1);
        ui->bg_button->setEnabled(!Settings::values.bg_red.UsingGlobal());
    }

    UpdateBackgroundColorButton(QColor::fromRgbF(Settings::values.bg_red, Settings::values.bg_green,
                                                 Settings::values.bg_blue));
    UpdateDeviceComboBox();
}

void ConfigureGraphics::ApplyConfiguration() {
    if (Settings::configuring_global) {
        Settings::values.renderer_backend = GetCurrentGraphicsBackend();
        Settings::values.vulkan_device = vulkan_device;
        Settings::values.aspect_ratio = ui->aspect_ratio_combobox->currentIndex();
        Settings::values.use_disk_shader_cache = ui->use_disk_shader_cache->isChecked();
        Settings::values.use_asynchronous_gpu_emulation =
            ui->use_asynchronous_gpu_emulation->isChecked();
        Settings::values.bg_red = static_cast<float>(bg_color.redF());
        Settings::values.bg_green = static_cast<float>(bg_color.greenF());
        Settings::values.bg_blue = static_cast<float>(bg_color.blueF());
    } else {
        if (ui->api->currentIndex() == ConfigurationShared::USE_GLOBAL_INDEX)
            Settings::values.renderer_backend.SetGlobal(true);
        else {
            Settings::values.renderer_backend.SetGlobal(false);
            Settings::values.renderer_backend = GetCurrentGraphicsBackend();
            if (GetCurrentGraphicsBackend() == Settings::RendererBackend::Vulkan) {
                Settings::values.vulkan_device.SetGlobal(false);
                Settings::values.vulkan_device = vulkan_device;
            } else {
                Settings::values.vulkan_device.SetGlobal(true);
            }
        }

        ConfigurationShared::ApplyPerGameSetting(&Settings::values.aspect_ratio,
                                                 ui->aspect_ratio_combobox);

        ConfigurationShared::ApplyPerGameSetting(&Settings::values.use_disk_shader_cache,
                                                 ui->use_disk_shader_cache);
        ConfigurationShared::ApplyPerGameSetting(&Settings::values.use_asynchronous_gpu_emulation,
                                                 ui->use_asynchronous_gpu_emulation);

        if (ui->bg_combobox->currentIndex() == ConfigurationShared::USE_GLOBAL_INDEX) {
            Settings::values.bg_red.SetGlobal(true);
            Settings::values.bg_green.SetGlobal(true);
            Settings::values.bg_blue.SetGlobal(true);
        } else {
            Settings::values.bg_red.SetGlobal(false);
            Settings::values.bg_green.SetGlobal(false);
            Settings::values.bg_blue.SetGlobal(false);
            Settings::values.bg_red = static_cast<float>(bg_color.redF());
            Settings::values.bg_green = static_cast<float>(bg_color.greenF());
            Settings::values.bg_blue = static_cast<float>(bg_color.blueF());
        }
    }
}

void ConfigureGraphics::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void ConfigureGraphics::RetranslateUI() {
    ui->retranslateUi(this);
}

void ConfigureGraphics::UpdateBackgroundColorButton(QColor color) {
    bg_color = color;

    QPixmap pixmap(ui->bg_button->size());
    pixmap.fill(bg_color);

    const QIcon color_icon(pixmap);
    ui->bg_button->setIcon(color_icon);
}

void ConfigureGraphics::UpdateDeviceComboBox() {
    ui->device->clear();

    bool enabled = false;
    switch (GetCurrentGraphicsBackend()) {
    case Settings::RendererBackend::OpenGL:
        ui->device->addItem(tr("OpenGL Graphics Device"));
        enabled = false;
        break;
    case Settings::RendererBackend::Vulkan:
        for (const auto device : vulkan_devices) {
            ui->device->addItem(device);
        }
        ui->device->setCurrentIndex(vulkan_device);
        enabled = !vulkan_devices.empty();
        break;
    }
    ui->device->setEnabled(enabled && !Core::System::GetInstance().IsPoweredOn());
}

void ConfigureGraphics::RetrieveVulkanDevices() {
#ifdef HAS_VULKAN
    vulkan_devices.clear();
    for (auto& name : Vulkan::RendererVulkan::EnumerateDevices()) {
        vulkan_devices.push_back(QString::fromStdString(name));
    }
#endif
}

Settings::RendererBackend ConfigureGraphics::GetCurrentGraphicsBackend() const {
    if (Settings::configuring_global) {
        return static_cast<Settings::RendererBackend>(ui->api->currentIndex());
    }

    if (ui->api->currentIndex() == 0) {
        Settings::values.renderer_backend.SetGlobal(true);
        return Settings::values.renderer_backend;
    }
    Settings::values.renderer_backend.SetGlobal(false);
    return static_cast<Settings::RendererBackend>(ui->api->currentIndex() - 2);
}

void ConfigureGraphics::SetupPerGameUI() {
    if (Settings::configuring_global) {
        return;
    }

    connect(ui->bg_combobox, static_cast<void (QComboBox::*)(int)>(&QComboBox::activated), this,
            [this](int index) { ui->bg_button->setEnabled(index == 1); });

    connect(ui->bg_button, &QPushButton::clicked, this, [this] {
        const QColor new_bg_color = QColorDialog::getColor(bg_color);
        if (!new_bg_color.isValid()) {
            return;
        }
        UpdateBackgroundColorButton(new_bg_color);
    });

    ui->use_disk_shader_cache->setTristate(true);
    ui->use_asynchronous_gpu_emulation->setTristate(true);
    ConfigurationShared::InsertGlobalItem(ui->aspect_ratio_combobox);
    ConfigurationShared::InsertGlobalItem(ui->api);
}
