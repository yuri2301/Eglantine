// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <map>
#include <vector>
#include "common/settings.h"
#include "core/core.h"
#include "qt_common/config/shared_translation.h"
#include "ui_configure_nx2_rig.h"
#include "yuzu/configuration/configuration_shared.h"
#include "yuzu/configuration/configure_nx2_rig.h"
#include "yuzu/configuration/shared_widget.h"

ConfigureNx2Rig::ConfigureNx2Rig(const Core::System& system_,
                                 std::shared_ptr<std::vector<ConfigurationShared::Tab*>> group_,
                                 const ConfigurationShared::Builder& builder, QWidget* parent)
    : Tab(group_, parent), ui{std::make_unique<Ui::ConfigureNx2Rig>()}, system{system_} {
    ui->setupUi(this);
    Setup(builder);
    SetConfiguration();
}

ConfigureNx2Rig::~ConfigureNx2Rig() = default;

void ConfigureNx2Rig::SetConfiguration() {}

void ConfigureNx2Rig::Setup(const ConfigurationShared::Builder& builder) {
    auto& layout = *ui->populate_target->layout();
    std::map<u32, QWidget*> hold{}; // Sorted by id: declaration order in settings.h

    for (auto* setting : Settings::values.linkage.by_category[Settings::Category::Nx2Rig]) {
        ConfigurationShared::Widget* widget = [&]() {
            if (setting->Id() == Settings::values.nx2_texture_vram_share.Id()) {
                return builder.BuildWidget(setting, apply_funcs,
                                           ConfigurationShared::RequestType::Slider, true, 1.0f,
                                           nullptr, tr("%", "Share of VRAM (e.g. 75%)"));
            }
            return builder.BuildWidget(setting, apply_funcs);
        }();
        if (widget == nullptr) {
            continue;
        }
        if (!widget->Valid()) {
            widget->deleteLater();
            continue;
        }
        hold.emplace(setting->Id(), widget);
    }

    for (const auto& [id, widget] : hold) {
        layout.addWidget(widget);
    }
}

void ConfigureNx2Rig::ApplyConfiguration() {
    const bool is_powered_on = system.IsPoweredOn();
    for (const auto& func : apply_funcs) {
        func(is_powered_on);
    }
}

void ConfigureNx2Rig::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }
    QWidget::changeEvent(event);
}

void ConfigureNx2Rig::RetranslateUI() {
    ui->retranslateUi(this);
}
