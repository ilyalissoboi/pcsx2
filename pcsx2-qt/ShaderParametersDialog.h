// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "ui_ShaderParametersDialog.h"

#include "pcsx2/GS/ShaderChain/ShaderChainParams.h"

#include <QtWidgets/QDialog>

#include <string>
#include <vector>

class QDoubleSpinBox;
class QPushButton;
class QSlider;
class QTimer;
class SettingsWindow;

/// Editor for a preset's #pragma parameters. Every change is pushed to the running shader chain
/// immediately and written to the INI (global or per-game layer) after a short debounce.
class ShaderParametersDialog final : public QDialog
{
	Q_OBJECT

public:
	ShaderParametersDialog(SettingsWindow* settings, QWidget* parent, std::string preset);
	~ShaderParametersDialog() override;

protected:
	void done(int r) override;
	bool eventFilter(QObject* watched, QEvent* event) override;

private Q_SLOTS:
	void onResetAllClicked();
	void flushWrite();

private:
	struct Row
	{
		ShaderChainParams::ParameterInfo info;
		QSlider* slider = nullptr; // null when the range is degenerate
		QDoubleSpinBox* spin = nullptr;
		QPushButton* reset = nullptr;
		float slider_increment = 0.0f; // value per slider position
		float value = 0.0f;
	};

	static constexpr int MAX_SLIDER_STEPS = 10000;
	static constexpr int WRITE_DELAY_MS = 250;

	bool loadParameters();
	void buildRows();
	std::vector<std::string> readOverrideEntries() const;
	void applyOverrides(const ShaderChainParams::ParamList& overrides);
	void setRowValue(Row& row, float value);
	void refreshRowWidgets(Row& row);
	bool isDefault(const Row& row) const;
	ShaderChainParams::ParamList collectOverrides() const;
	/// True when this dialog's settings layer is the one the running chain reads from, i.e. when a
	/// live push would be visible. Per-game windows only qualify while that game is running.
	bool isEditingEffectiveLayer() const;
	void pushToStore();
	void scheduleWrite();
	void onValueEdited(Row& row, float value);

	Ui::ShaderParametersDialog m_ui;
	SettingsWindow* m_settings;
	std::string m_preset;
	std::vector<Row> m_rows;
	QTimer* m_write_timer = nullptr;
	bool m_updating = false;
	bool m_write_pending = false;
};
