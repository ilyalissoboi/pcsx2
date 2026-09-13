// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "ShaderPackDownloadDialog.h"

#include "pcsx2/Config.h"
#include "pcsx2/Host.h"

#include "common/Assertions.h"
#include "common/Error.h"
#include "common/HTTPDownloader.h"

#include <QtWidgets/QHeaderView>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QTableWidgetItem>

#include <algorithm>

namespace
{
	enum Column
	{
		COLUMN_PACK = 0,
		COLUMN_DESCRIPTION,
		COLUMN_LICENSE,
		COLUMN_STATUS,
	};
} // namespace

ShaderPackDownloadDialog::ShaderPackDownloadDialog(QWidget* parent /*= nullptr*/)
	: QDialog(parent)
{
	m_ui.setupUi(this);
	m_ui.packs->horizontalHeader()->setSectionResizeMode(COLUMN_DESCRIPTION, QHeaderView::Stretch);

	connect(m_ui.install, &QPushButton::clicked, this, &ShaderPackDownloadDialog::onInstallClicked);
	connect(m_ui.uninstall, &QPushButton::clicked, this, &ShaderPackDownloadDialog::onUninstallClicked);
	connect(m_ui.close, &QPushButton::clicked, this, &ShaderPackDownloadDialog::onCloseClicked);

	populateTable();
	refreshStatuses();

	// Look up the latest versions in the background; statuses update when it finishes.
	m_ui.progress->setVisible(false);
	std::vector<std::string> all_ids;
	for (const ShaderPacks::PackInfo& pack : ShaderPacks::GetPacks())
		all_ids.emplace_back(pack.id);
	startWorker(Mode::Resolve, std::move(all_ids));
}

ShaderPackDownloadDialog::~ShaderPackDownloadDialog()
{
	pxAssert(!m_worker);
}

void ShaderPackDownloadDialog::done(int r)
{
	cancelWorker();
	QDialog::done(r);
}

void ShaderPackDownloadDialog::populateTable()
{
	const auto packs = ShaderPacks::GetPacks();
	m_ui.packs->setRowCount(static_cast<int>(packs.size()));
	int row = 0;
	for (const ShaderPacks::PackInfo& pack : packs)
	{
		QTableWidgetItem* name = new QTableWidgetItem(QString::fromUtf8(pack.display_name));
		name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
		name->setCheckState(Qt::Unchecked);
		name->setData(Qt::UserRole, QString::fromUtf8(pack.id));
		m_ui.packs->setItem(row, COLUMN_PACK, name);
		m_ui.packs->setItem(row, COLUMN_DESCRIPTION, new QTableWidgetItem(QString::fromUtf8(pack.description)));
		m_ui.packs->setItem(row, COLUMN_LICENSE, new QTableWidgetItem(QString::fromUtf8(pack.license)));
		m_ui.packs->setItem(row, COLUMN_STATUS, new QTableWidgetItem(tr("Checking...")));
		row++;
	}
	m_ui.packs->resizeColumnsToContents();
}

void ShaderPackDownloadDialog::refreshStatuses()
{
	for (int row = 0; row < m_ui.packs->rowCount(); row++)
	{
		QTableWidgetItem* name = m_ui.packs->item(row, COLUMN_PACK);
		const std::string id = name->data(Qt::UserRole).toString().toStdString();
		const std::optional<ShaderPacks::InstalledPack> installed = ShaderPacks::GetInstalled(id);
		const auto resolved = m_resolved.find(id);

		QString text;
		bool check = false;
		// Install writes a marker with an empty version when extraction aborted part-way.
		const bool incomplete = installed.has_value() && installed->version.empty();
		if (incomplete)
		{
			text = tr("Installed (incomplete, reinstall recommended)");
			check = true;
		}
		else if (resolved == m_resolved.end())
		{
			text = installed ? tr("Installed %1 (checking for updates...)").arg(QString::fromStdString(installed->version)) : tr("Not installed (checking...)");
		}
		else if (!resolved->second.version.has_value())
		{
			text = installed ? tr("Installed %1 (could not check for updates)").arg(QString::fromStdString(installed->version)) :
			                   tr("Not installed (could not check: %1)").arg(QString::fromStdString(resolved->second.error));
			check = !installed;
		}
		else if (!installed)
		{
			text = tr("Not installed (latest %1)").arg(QString::fromStdString(resolved->second.version->version));
			check = true;
		}
		else if (installed->version != resolved->second.version->version)
		{
			text = tr("Update available: %1 → %2").arg(QString::fromStdString(installed->version), QString::fromStdString(resolved->second.version->version));
			check = true;
		}
		else
		{
			text = tr("Installed %1 (up to date)").arg(QString::fromStdString(installed->version));
		}

		m_ui.packs->item(row, COLUMN_STATUS)->setText(text);
		if (incomplete || resolved != m_resolved.end())
			name->setCheckState(check ? Qt::Checked : Qt::Unchecked);
	}
	m_ui.packs->resizeColumnToContents(COLUMN_STATUS);
}

std::vector<std::string> ShaderPackDownloadDialog::checkedIds(bool installed_only) const
{
	std::vector<std::string> ids;
	for (int row = 0; row < m_ui.packs->rowCount(); row++)
	{
		const QTableWidgetItem* name = m_ui.packs->item(row, COLUMN_PACK);
		if (name->checkState() != Qt::Checked)
			continue;
		const std::string id = name->data(Qt::UserRole).toString().toStdString();
		if (installed_only && !ShaderPacks::GetInstalled(id).has_value())
			continue;
		ids.push_back(id);
	}
	return ids;
}

void ShaderPackDownloadDialog::onWorkerStatus(const QString& text)
{
	m_ui.status->setText(text);
}

void ShaderPackDownloadDialog::onWorkerProgress(int value, int range)
{
	if (range != m_ui.progress->maximum())
		m_ui.progress->setMaximum(range);
	m_ui.progress->setValue(value);
}

void ShaderPackDownloadDialog::onWorkerFinished()
{
	if (!m_worker)
		return;

	m_worker->join();
	const Mode mode = m_worker->mode();

	if (mode == Mode::Resolve)
	{
		m_resolved = m_worker->resolved();
		m_ui.status->setText(tr("Select the packs to install or update."));
	}
	else
	{
		QStringList failed;
		bool cancelled = false;
		for (const ShaderPacks::InstallResult& result : m_worker->results())
		{
			cancelled |= result.cancelled;
			if (!result.success && !result.cancelled)
			{
				const ShaderPacks::PackInfo* pack = ShaderPacks::FindPack(result.id);
				failed.append(QStringLiteral("%1: %2").arg(pack ? QString::fromUtf8(pack->display_name) : QString::fromStdString(result.id), QString::fromStdString(result.message)));
			}
		}
		if (cancelled)
			m_ui.status->setText(tr("Cancelled."));
		else if (!failed.isEmpty())
			m_ui.status->setText(tr("Completed with errors: %1").arg(failed.join(QStringLiteral("; "))));
		else
			m_ui.status->setText(mode == Mode::Install ? tr("Done.") : tr("Uninstalled."));
		m_ui.progress->setValue(m_ui.progress->maximum());
	}

	m_worker.reset();
	m_ui.progress->setVisible(false);
	refreshStatuses();
	updateEnabled();
}

void ShaderPackDownloadDialog::onInstallClicked()
{
	if (m_worker)
	{
		cancelWorker();
		return;
	}

	std::vector<std::string> ids = checkedIds(false);
	if (ids.empty())
	{
		m_ui.status->setText(tr("Select at least one pack."));
		return;
	}

	const std::vector<std::string> expanded = ShaderPacks::ExpandDependencies(EmuFolders::Shaders, ids);
	if (expanded.size() != ids.size())
	{
		QStringList names;
		for (const std::string& id : expanded)
		{
			const ShaderPacks::PackInfo* pack = ShaderPacks::FindPack(id);
			names.append(pack ? QString::fromUtf8(pack->display_name) : QString::fromStdString(id));
		}
		m_ui.status->setText(tr("Installing: %1").arg(names.join(QStringLiteral(", "))));
	}
	startWorker(Mode::Install, expanded);
}

void ShaderPackDownloadDialog::onUninstallClicked()
{
	if (m_worker)
		return;

	const std::vector<std::string> ids = checkedIds(true);
	if (ids.empty())
	{
		m_ui.status->setText(tr("Select at least one installed pack."));
		return;
	}

	QStringList names;
	for (const std::string& id : ids)
	{
		const ShaderPacks::PackInfo* pack = ShaderPacks::FindPack(id);
		names.append(pack ? QString::fromUtf8(pack->display_name) : QString::fromStdString(id));
	}
	QString question = tr("Remove the files installed by the following packs?\n\n%1").arg(names.join(QStringLiteral("\n")));
	const auto is_checked = [&ids](const char* id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };
	if (is_checked("shaders_slang") && !is_checked("retro-crisis-gdv-ntsc") &&
		ShaderPacks::GetInstalled("retro-crisis-gdv-ntsc").has_value())
	{
		question += tr("\n\nThe Retro Crisis presets require the libretro slang shaders and will stop working.");
	}
	if (QMessageBox::question(this, tr("Uninstall Shader Packs"), question) != QMessageBox::Yes)
		return;

	startWorker(Mode::Uninstall, ids);
}

void ShaderPackDownloadDialog::onCloseClicked()
{
	cancelWorker();
	done(0);
}

void ShaderPackDownloadDialog::startWorker(Mode mode, std::vector<std::string> ids)
{
	m_worker = std::make_unique<Worker>(this, mode, std::move(ids));
	connect(m_worker.get(), &Worker::statusUpdated, this, &ShaderPackDownloadDialog::onWorkerStatus);
	connect(m_worker.get(), &Worker::progressUpdated, this, &ShaderPackDownloadDialog::onWorkerProgress);
	connect(m_worker.get(), &Worker::threadFinished, this, &ShaderPackDownloadDialog::onWorkerFinished);
	m_ui.progress->setValue(0);
	m_ui.progress->setVisible(mode != Mode::Resolve);
	m_worker->start();
	updateEnabled();
}

void ShaderPackDownloadDialog::cancelWorker()
{
	if (!m_worker)
		return;

	m_worker->requestInterruption();
	m_worker->join();
	m_worker.reset();
	m_ui.progress->setVisible(false);
	m_ui.status->setText(tr("Cancelled."));
	refreshStatuses();
	updateEnabled();
}

void ShaderPackDownloadDialog::updateEnabled()
{
	const bool running = static_cast<bool>(m_worker);
	const bool installing = running && m_worker->mode() == Mode::Install;
	m_ui.install->setText(installing ? tr("Cancel") : tr("Install"));
	m_ui.install->setEnabled(!running || installing);
	m_ui.uninstall->setEnabled(!running);
	m_ui.close->setEnabled(!running);
	m_ui.packs->setEnabled(!running);
}

ShaderPackDownloadDialog::Worker::Worker(QWidget* parent, Mode mode, std::vector<std::string> ids)
	: QtAsyncProgressThread(parent)
	, m_mode(mode)
	, m_ids(std::move(ids))
{
}

ShaderPackDownloadDialog::Worker::~Worker() = default;

void ShaderPackDownloadDialog::Worker::runAsync()
{
	switch (m_mode)
	{
		case Mode::Resolve:
		{
			std::unique_ptr<HTTPDownloader> http = HTTPDownloader::Create(Host::GetHTTPUserAgent());
			for (const std::string& id : m_ids)
			{
				const ShaderPacks::PackInfo* pack = ShaderPacks::FindPack(id);
				if (!pack)
					continue;
				ResolveOutcome& outcome = m_resolved[id];
				Error error;
				if (http)
					outcome.version = ShaderPacks::ResolveLatest(*pack, *http, this, &error);
				else
					error.SetStringView("Failed to create HTTP downloader.");
				if (!outcome.version.has_value())
					outcome.error = error.GetDescription();
				if (IsCancelled())
					break;
			}
			break;
		}

		case Mode::Install:
			m_results = ShaderPacks::Install(m_ids, this);
			break;

		case Mode::Uninstall:
			for (const std::string& id : m_ids)
			{
				ShaderPacks::InstallResult& result = m_results.emplace_back();
				result.id = id;
				Error error;
				result.success = ShaderPacks::Uninstall(id, &error);
				if (!result.success)
					result.message = error.GetDescription();
			}
			break;
	}
}

#include "moc_ShaderPackDownloadDialog.cpp"
