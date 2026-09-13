// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "ui_ShaderFavoritesDialog.h"

#include <QtWidgets/QDialog>

#include <string>
#include <vector>

class QListWidgetItem;

/// Ordered, global list of presets cycled by the Next/Previous Shader Preset hotkeys.
/// Every change is written to EmuCore/GS/ShaderChainFavorites immediately.
class ShaderFavoritesDialog final : public QDialog
{
	Q_OBJECT

public:
	ShaderFavoritesDialog(QWidget* parent, std::string current_preset);
	~ShaderFavoritesDialog() override;

private Q_SLOTS:
	void onAddCurrentClicked();
	void onAddClicked();
	void onRemoveClicked();
	void onMoveUpClicked();
	void onMoveDownClicked();
	void updateButtons();

private:
	static constexpr const char* SECTION = "EmuCore/GS";
	static constexpr const char* KEY = "ShaderChainFavorites";

	void load();
	void save();
	void addPreset(const std::string& preset);
	void moveSelected(int delta);
	bool contains(const std::string& preset) const;
	std::vector<std::string> entries() const;
	static void decorateItem(QListWidgetItem* item);

	Ui::ShaderFavoritesDialog m_ui;
	std::string m_current;
};
