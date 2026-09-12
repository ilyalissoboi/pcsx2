// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "QtProgressCallback.h"
#include "ui_ShaderPackDownloadDialog.h"

#include "pcsx2/ShaderPacks.h"

#include <QtWidgets/QDialog>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

/// Installs, updates and removes the supported shader packs. Network and file work run on a worker thread.
class ShaderPackDownloadDialog final : public QDialog
{
	Q_OBJECT

public:
	explicit ShaderPackDownloadDialog(QWidget* parent = nullptr);
	~ShaderPackDownloadDialog();

	/// Every exit path goes through here (button, Escape, window close), so the worker is always
	/// stopped before the caller destroys the dialog.
	void done(int r) override;

private Q_SLOTS:
	void onWorkerStatus(const QString& text);
	void onWorkerProgress(int value, int range);
	void onWorkerFinished();
	void onInstallClicked();
	void onUninstallClicked();
	void onCloseClicked();

private:
	enum class Mode
	{
		Resolve,
		Install,
		Uninstall,
	};

	struct ResolveOutcome
	{
		std::optional<ShaderPacks::ResolvedVersion> version;
		std::string error;
	};

	class Worker final : public QtAsyncProgressThread
	{
	public:
		Worker(QWidget* parent, Mode mode, std::vector<std::string> ids);
		~Worker() override;

		Mode mode() const { return m_mode; }
		const std::map<std::string, ResolveOutcome>& resolved() const { return m_resolved; }
		const std::vector<ShaderPacks::InstallResult>& results() const { return m_results; }

	protected:
		void runAsync() override;

	private:
		Mode m_mode;
		std::vector<std::string> m_ids;
		std::map<std::string, ResolveOutcome> m_resolved;
		std::vector<ShaderPacks::InstallResult> m_results;
	};

	void populateTable();
	void refreshStatuses();
	std::vector<std::string> checkedIds(bool installed_only) const;
	void startWorker(Mode mode, std::vector<std::string> ids);
	void cancelWorker();
	void updateEnabled();

	Ui::ShaderPackDownloadDialog m_ui;
	std::unique_ptr<Worker> m_worker;
	std::map<std::string, ResolveOutcome> m_resolved;
};
