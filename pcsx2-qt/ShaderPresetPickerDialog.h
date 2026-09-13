// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "ui_ShaderPresetPickerDialog.h"

#include <QtWidgets/QDialog>

class QModelIndex;
class QSortFilterProxyModel;
class QStandardItem;
class QStandardItemModel;

/// Tree view over the Shaders folder (packs > folders > presets) with a live search box.
class ShaderPresetPickerDialog final : public QDialog
{
	Q_OBJECT

public:
	ShaderPresetPickerDialog(QWidget* parent, const QString& current_preset);
	~ShaderPresetPickerDialog();

	/// Relative preset path ('/' separators) chosen by the user; empty if none.
	const QString& selectedPreset() const { return m_selected; }

private Q_SLOTS:
	void onFilterChanged(const QString& text);
	void onCurrentChanged(const QModelIndex& current);
	void onActivated(const QModelIndex& index);

private:
	static constexpr int ROLE_PATH = Qt::UserRole; // relative path for leaves, folder path for folders
	static constexpr int ROLE_IS_PRESET = Qt::UserRole + 1;

	void buildModel();
	QStandardItem* findPresetItem(const QString& preset) const;
	void selectPreset(const QString& preset);

	Ui::ShaderPresetPickerDialog m_ui;
	QStandardItemModel* m_model = nullptr;
	QSortFilterProxyModel* m_proxy = nullptr;
	QString m_selected;
};
