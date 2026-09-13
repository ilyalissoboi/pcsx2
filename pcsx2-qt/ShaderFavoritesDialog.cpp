// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "ShaderFavoritesDialog.h"
#include "ShaderPresetPickerDialog.h"

#include "pcsx2/GS/ShaderChain/ShaderPresets.h"
#include "pcsx2/Host.h"

#include "common/FileSystem.h"

#include <QtWidgets/QListWidgetItem>
#include <QtWidgets/QPushButton>

ShaderFavoritesDialog::ShaderFavoritesDialog(QWidget* parent, std::string current_preset)
	: QDialog(parent)
	, m_current(std::move(current_preset))
{
	m_ui.setupUi(this);

	connect(m_ui.addCurrent, &QPushButton::clicked, this, &ShaderFavoritesDialog::onAddCurrentClicked);
	connect(m_ui.add, &QPushButton::clicked, this, &ShaderFavoritesDialog::onAddClicked);
	connect(m_ui.remove, &QPushButton::clicked, this, &ShaderFavoritesDialog::onRemoveClicked);
	connect(m_ui.moveUp, &QPushButton::clicked, this, &ShaderFavoritesDialog::onMoveUpClicked);
	connect(m_ui.moveDown, &QPushButton::clicked, this, &ShaderFavoritesDialog::onMoveDownClicked);
	connect(m_ui.list, &QListWidget::itemSelectionChanged, this, &ShaderFavoritesDialog::updateButtons);

	load();
	updateButtons();
}

ShaderFavoritesDialog::~ShaderFavoritesDialog() = default;

void ShaderFavoritesDialog::load()
{
	m_ui.list->clear();
	for (const std::string& preset : Host::GetBaseStringListSetting(SECTION, KEY))
	{
		QListWidgetItem* const item = new QListWidgetItem(QString::fromStdString(preset));
		decorateItem(item);
		m_ui.list->addItem(item);
	}
}

void ShaderFavoritesDialog::decorateItem(QListWidgetItem* item)
{
	const std::string full = ShaderPresets::ResolvePresetPath(item->text().toStdString());
	const bool exists = !full.empty() && FileSystem::FileExists(full.c_str());
	QFont font = item->font();
	font.setItalic(!exists);
	item->setFont(font);
	item->setToolTip(exists ? QString() : tr("File not found"));
}

std::vector<std::string> ShaderFavoritesDialog::entries() const
{
	std::vector<std::string> result;
	result.reserve(static_cast<size_t>(m_ui.list->count()));
	for (int i = 0; i < m_ui.list->count(); i++)
		result.push_back(m_ui.list->item(i)->text().toStdString());
	return result;
}

bool ShaderFavoritesDialog::contains(const std::string& preset) const
{
	for (int i = 0; i < m_ui.list->count(); i++)
	{
		if (m_ui.list->item(i)->text().toStdString() == preset)
			return true;
	}
	return false;
}

void ShaderFavoritesDialog::save()
{
	const std::vector<std::string> list = entries();
	if (list.empty())
		Host::RemoveBaseSettingValue(SECTION, KEY);
	else
		Host::SetBaseStringListSettingValue(SECTION, KEY, list);
	Host::CommitBaseSettingChanges();
}

void ShaderFavoritesDialog::addPreset(const std::string& preset)
{
	if (preset.empty() || contains(preset))
		return;

	QListWidgetItem* const item = new QListWidgetItem(QString::fromStdString(preset));
	decorateItem(item);
	m_ui.list->addItem(item);
	m_ui.list->setCurrentItem(item);
	save();
	updateButtons();
}

void ShaderFavoritesDialog::onAddCurrentClicked()
{
	addPreset(m_current);
}

void ShaderFavoritesDialog::onAddClicked()
{
	ShaderPresetPickerDialog dlg(this, QString::fromStdString(m_current));
	if (dlg.exec() != QDialog::Accepted || dlg.selectedPreset().isEmpty())
		return;
	addPreset(dlg.selectedPreset().toStdString());
}

void ShaderFavoritesDialog::onRemoveClicked()
{
	const int row = m_ui.list->currentRow();
	if (row < 0)
		return;
	delete m_ui.list->takeItem(row);
	save();
	updateButtons();
}

void ShaderFavoritesDialog::moveSelected(int delta)
{
	const int row = m_ui.list->currentRow();
	const int target = row + delta;
	if (row < 0 || target < 0 || target >= m_ui.list->count())
		return;

	QListWidgetItem* const item = m_ui.list->takeItem(row);
	m_ui.list->insertItem(target, item);
	m_ui.list->setCurrentItem(item);
	save();
	updateButtons();
}

void ShaderFavoritesDialog::onMoveUpClicked()
{
	moveSelected(-1);
}

void ShaderFavoritesDialog::onMoveDownClicked()
{
	moveSelected(1);
}

void ShaderFavoritesDialog::updateButtons()
{
	const int row = m_ui.list->currentRow();
	const int count = m_ui.list->count();
	m_ui.addCurrent->setEnabled(!m_current.empty() && !contains(m_current));
	m_ui.remove->setEnabled(row >= 0);
	m_ui.moveUp->setEnabled(row > 0);
	m_ui.moveDown->setEnabled(row >= 0 && row + 1 < count);
}

#include "moc_ShaderFavoritesDialog.cpp"
