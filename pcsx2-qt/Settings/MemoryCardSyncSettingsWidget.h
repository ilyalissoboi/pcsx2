// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "ui_MemoryCardSyncSettingsWidget.h"

#include "SettingsWidget.h"

#include <QtCore/QTimer>

#include <string>
#include <utility>
#include <vector>

/// Settings > Memory Card Sync (pcsx2/SIO/Memcard/MemcardSync.h). Global only: the storage and keys
/// belong to the computer, not to a game. Every change is stored at once and sync is restarted with
/// it on a background thread, since starting checks the storage over the network.
class MemoryCardSyncSettingsWidget : public SettingsWidget
{
	Q_OBJECT

public:
	MemoryCardSyncSettingsWidget(SettingsWindow* dialog, QWidget* parent);
	~MemoryCardSyncSettingsWidget();

protected:
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;

private:
	void onEnabledChanged(bool enabled);
	void onTextSettingEdited(QLineEdit* edit, const char* key);
	void onSecretEdited();
	void onPassphraseEdited();
	/// A Show button for the field of `secret` (MemcardSync::SECRET_ACCESS_KEY or ENCRYPTION_PASSPHRASE).
	void onShowToggled(QLineEdit* edit, QPushButton* show_button, const char* secret, bool shown);
	void onPassphraseRemoveClicked();
	void onSyncNowClicked();

	/// Restarts sync with the stored settings, storing `secrets` first (off the UI thread).
	void apply(std::vector<std::pair<const char*, std::string>> secrets = {});
	void updateEnabledState();
	void updateSecretPlaceholders();
	void updateStatus();

	Ui::MemoryCardSyncSettingsWidget m_ui;
	QTimer m_status_timer;
	bool m_starting = false;
};
