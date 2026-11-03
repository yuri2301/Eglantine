// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <QWidget>
#include "yuzu/configuration/configuration_shared.h"

namespace Core {
class System;
}

namespace Ui {
class ConfigureNx2Rig;
}

namespace ConfigurationShared {
class Builder;
}

/// This fork's page for Switch 2 games on its target machine (RTX 3060 12 GB, i5-10400F, 16 GB):
/// every setting of Settings::Category::Nx2Rig, in the global and the per-game dialogs.
class ConfigureNx2Rig : public ConfigurationShared::Tab {
    Q_OBJECT

public:
    explicit ConfigureNx2Rig(const Core::System& system_,
                             std::shared_ptr<std::vector<ConfigurationShared::Tab*>> group,
                             const ConfigurationShared::Builder& builder,
                             QWidget* parent = nullptr);
    ~ConfigureNx2Rig() override;

    void ApplyConfiguration() override;
    void SetConfiguration() override;

private:
    void Setup(const ConfigurationShared::Builder& builder);
    void changeEvent(QEvent* event) override;
    void RetranslateUI();

    std::unique_ptr<Ui::ConfigureNx2Rig> ui;

    const Core::System& system;

    std::vector<std::function<void(bool)>> apply_funcs;
};
