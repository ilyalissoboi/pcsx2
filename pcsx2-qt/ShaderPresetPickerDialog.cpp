// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "ShaderPresetPickerDialog.h"

#include "pcsx2/GS/ShaderChain/ShaderPresets.h"

#include <QtCore/QItemSelectionModel>
#include <QtCore/QSortFilterProxyModel>
#include <QtGui/QStandardItemModel>
#include <QtWidgets/QPushButton>

#include <map>

ShaderPresetPickerDialog::ShaderPresetPickerDialog(QWidget* parent, const QString& current_preset)
	: QDialog(parent)
{
	m_ui.setupUi(this);

	m_model = new QStandardItemModel(this);
	buildModel();

	m_proxy = new QSortFilterProxyModel(this);
	m_proxy->setSourceModel(m_model);
	m_proxy->setRecursiveFilteringEnabled(true);
	m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
	m_proxy->setFilterRole(ROLE_PATH);
	m_ui.tree->setModel(m_proxy);

	connect(m_ui.filter, &QLineEdit::textChanged, this, &ShaderPresetPickerDialog::onFilterChanged);
	connect(m_ui.tree->selectionModel(), &QItemSelectionModel::currentChanged, this,
		[this](const QModelIndex& current, const QModelIndex&) { onCurrentChanged(current); });
	connect(m_ui.tree, &QTreeView::activated, this, &ShaderPresetPickerDialog::onActivated);

	m_ui.buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
	selectPreset(current_preset);
	m_ui.filter->setFocus();
}

ShaderPresetPickerDialog::~ShaderPresetPickerDialog() = default;

void ShaderPresetPickerDialog::buildModel()
{
	std::map<QString, QStandardItem*> folders; // folder path -> item
	QStandardItem* const root = m_model->invisibleRootItem();

	for (const std::string& preset : ShaderPresets::Enumerate())
	{
		const QString path = QString::fromStdString(preset);
		const QStringList parts = path.split(QChar('/'), Qt::SkipEmptyParts);
		if (parts.isEmpty())
			continue;

		QStandardItem* parent = root;
		QString folder_path;
		for (qsizetype i = 0; i + 1 < parts.size(); i++)
		{
			folder_path = folder_path.isEmpty() ? parts[i] : (folder_path + QChar('/') + parts[i]);
			auto it = folders.find(folder_path);
			if (it == folders.end())
			{
				QStandardItem* folder = new QStandardItem(parts[i]);
				folder->setEditable(false);
				folder->setSelectable(false);
				folder->setData(folder_path, ROLE_PATH);
				folder->setData(false, ROLE_IS_PRESET);
				parent->appendRow(folder);
				it = folders.emplace(folder_path, folder).first;
			}
			parent = it->second;
		}

		QStandardItem* leaf = new QStandardItem(parts.last());
		leaf->setEditable(false);
		leaf->setData(path, ROLE_PATH);
		leaf->setData(true, ROLE_IS_PRESET);
		leaf->setToolTip(path);
		parent->appendRow(leaf);
	}
}

QStandardItem* ShaderPresetPickerDialog::findPresetItem(const QString& preset) const
{
	if (preset.isEmpty())
		return nullptr;
	const QModelIndexList matches = m_model->match(m_model->index(0, 0), ROLE_PATH, preset, 1, Qt::MatchExactly | Qt::MatchRecursive);
	return matches.isEmpty() ? nullptr : m_model->itemFromIndex(matches.first());
}

void ShaderPresetPickerDialog::selectPreset(const QString& preset)
{
	QStandardItem* item = findPresetItem(preset);
	if (!item)
	{
		m_ui.tree->collapseAll();
		return;
	}

	const QModelIndex proxy_index = m_proxy->mapFromSource(item->index());
	for (QModelIndex parent = proxy_index.parent(); parent.isValid(); parent = parent.parent())
		m_ui.tree->expand(parent);
	m_ui.tree->setCurrentIndex(proxy_index);
	m_ui.tree->scrollTo(proxy_index, QAbstractItemView::PositionAtCenter);
}

void ShaderPresetPickerDialog::onFilterChanged(const QString& text)
{
	m_proxy->setFilterFixedString(text);
	if (!text.isEmpty())
	{
		m_ui.tree->expandAll();
		return;
	}
	m_ui.tree->collapseAll();
	selectPreset(m_selected);
}

void ShaderPresetPickerDialog::onCurrentChanged(const QModelIndex& current)
{
	const bool is_preset = current.isValid() && current.data(ROLE_IS_PRESET).toBool();
	m_selected = is_preset ? current.data(ROLE_PATH).toString() : QString();
	m_ui.selection->setText(is_preset ? m_selected : tr("No preset selected."));
	m_ui.buttons->button(QDialogButtonBox::Ok)->setEnabled(is_preset);
}

void ShaderPresetPickerDialog::onActivated(const QModelIndex& index)
{
	if (index.isValid() && index.data(ROLE_IS_PRESET).toBool())
	{
		m_selected = index.data(ROLE_PATH).toString();
		accept();
	}
}

#include "moc_ShaderPresetPickerDialog.cpp"
