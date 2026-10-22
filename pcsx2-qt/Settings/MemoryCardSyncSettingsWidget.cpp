// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "MemoryCardSyncSettingsWidget.h"
#include "QtHost.h"
#include "SettingsWindow.h"

#include "pcsx2/Host.h"
#include "pcsx2/SIO/Memcard/MemcardSync.h"

#include <QtCore/QPointer>
#include <QtCore/QSysInfo>

#include <optional>
#include <string_view>
#include <thread>

static constexpr const char* SECTION = "MemoryCardSync";

MemoryCardSyncSettingsWidget::MemoryCardSyncSettingsWidget(SettingsWindow* settings_dialog, QWidget* parent)
	: SettingsWidget(settings_dialog, parent)
{
	setupTab(m_ui);

	m_ui.enabled->setChecked(Host::GetBaseBoolSettingValue(SECTION, "Enabled", false));
	m_ui.remoteUri->setText(QString::fromStdString(Host::GetBaseStringSettingValue(SECTION, "RemoteURI")));
	m_ui.accessKeyId->setText(QString::fromStdString(Host::GetBaseStringSettingValue(SECTION, "AccessKeyID")));
	m_ui.deviceName->setText(QString::fromStdString(Host::GetBaseStringSettingValue(SECTION, "DeviceName")));
	// emusync names the device after the computer when no name is set.
	m_ui.deviceName->setPlaceholderText(QSysInfo::machineHostName());

	connect(m_ui.enabled, &QCheckBox::toggled, this, &MemoryCardSyncSettingsWidget::onEnabledChanged);
	connect(m_ui.remoteUri, &QLineEdit::editingFinished, this, [this]() { onTextSettingEdited(m_ui.remoteUri, "RemoteURI"); });
	connect(m_ui.accessKeyId, &QLineEdit::editingFinished, this, [this]() { onTextSettingEdited(m_ui.accessKeyId, "AccessKeyID"); });
	connect(m_ui.deviceName, &QLineEdit::editingFinished, this, [this]() { onTextSettingEdited(m_ui.deviceName, "DeviceName"); });
	connect(m_ui.secret, &QLineEdit::editingFinished, this, &MemoryCardSyncSettingsWidget::onSecretEdited);
	connect(m_ui.secretShow, &QPushButton::toggled, this, [this](bool shown) {
		onShowToggled(m_ui.secret, m_ui.secretShow, MemcardSync::SECRET_ACCESS_KEY, shown);
	});
	connect(m_ui.passphrase, &QLineEdit::editingFinished, this, &MemoryCardSyncSettingsWidget::onPassphraseEdited);
	connect(m_ui.passphraseShow, &QPushButton::toggled, this, [this](bool shown) {
		onShowToggled(m_ui.passphrase, m_ui.passphraseShow, MemcardSync::ENCRYPTION_PASSPHRASE, shown);
	});
	// Clicking Show leaves the focus in its field: taking it would finish the edit, which saves what
	// was typed and empties the field, so there would be nothing left to show. Tab still reaches it.
	m_ui.secretShow->setFocusPolicy(Qt::TabFocus);
	m_ui.passphraseShow->setFocusPolicy(Qt::TabFocus);
	connect(m_ui.passphraseRemove, &QPushButton::clicked, this, &MemoryCardSyncSettingsWidget::onPassphraseRemoveClicked);
	connect(m_ui.syncNow, &QPushButton::clicked, this, &MemoryCardSyncSettingsWidget::onSyncNowClicked);

	m_status_timer.setInterval(2000);
	connect(&m_status_timer, &QTimer::timeout, this, &MemoryCardSyncSettingsWidget::updateStatus);

	dialog()->registerWidgetHelp(m_ui.enabled, tr("Sync Memory Cards"), tr("Unchecked"),
		tr("Newer saves from other devices are put on the cards before a game starts; your saves upload while you play "
		   "and when the game closes."));
	dialog()->registerWidgetHelp(m_ui.remoteUri, tr("Storage"), tr("Not set"),
		tr("Bucket and folder, e.g. s3://my-saves/emusync/v1?region=eu-west-1. Use the same on every device. "
		   "S3-compatible services take an endpoint: s3://bucket/folder?endpoint=https://...&region=auto."));
	dialog()->registerWidgetHelp(m_ui.accessKeyId, tr("Access Key ID"), tr("Not set"),
		tr("From your storage provider. A key that can only read and write this bucket is enough."));
	dialog()->registerWidgetHelp(m_ui.secret, tr("Secret Access Key"), tr("Not set"),
		tr("Kept in your system's credential store (Credential Manager, Keychain or Secret Service), never in PCSX2's "
		   "settings file. Show displays the saved key; type or paste a new one to replace it."));
	dialog()->registerWidgetHelp(m_ui.passphrase, tr("Encryption Passphrase"), tr("None"),
		tr("Optional. Encrypts your saves on this computer before they upload, so the storage provider can't read them. "
		   "New storage is created encrypted; for storage that is already encrypted, enter the passphrase used when it "
		   "was set up. Use the same passphrase on every device. It can't be recovered, and encrypted storage can't be "
		   "made unencrypted. Kept in your system's credential store."));
	dialog()->registerWidgetHelp(m_ui.deviceName, tr("Device Name"), tr("This computer's name"),
		tr("How this computer appears in a save's history on your other devices."));

	updateEnabledState();
	updateSecretPlaceholders();
	updateStatus();
}

MemoryCardSyncSettingsWidget::~MemoryCardSyncSettingsWidget() = default;

void MemoryCardSyncSettingsWidget::showEvent(QShowEvent* event)
{
	SettingsWidget::showEvent(event);
	updateStatus();
	m_status_timer.start();
}

void MemoryCardSyncSettingsWidget::hideEvent(QHideEvent* event)
{
	SettingsWidget::hideEvent(event);
	m_status_timer.stop();
}

void MemoryCardSyncSettingsWidget::onEnabledChanged(bool enabled)
{
	Host::SetBaseBoolSettingValue(SECTION, "Enabled", enabled);
	Host::CommitBaseSettingChanges();
	apply();
}

void MemoryCardSyncSettingsWidget::onTextSettingEdited(QLineEdit* edit, const char* key)
{
	const std::string value = edit->text().trimmed().toStdString();
	if (value == Host::GetBaseStringSettingValue(SECTION, key))
		return;
	Host::SetBaseStringSettingValue(SECTION, key, value.c_str());
	Host::CommitBaseSettingChanges();
	apply();
}

// Whether the field holds the stored secret that Show put there, unchanged: nothing to save.
static bool showsStoredSecret(const QLineEdit* edit)
{
	const QVariant revealed = edit->property("revealed");
	return revealed.isValid() && revealed.toString() == edit->text();
}

// Empties a secret's field and masks it again, once its value is handed over or removed.
static void resetSecretField(QLineEdit* edit, QPushButton* show_button)
{
	edit->clear();
	edit->setProperty("revealed", QVariant());
	show_button->setChecked(false);
}

void MemoryCardSyncSettingsWidget::onSecretEdited()
{
	const QString text = m_ui.secret->text().trimmed();
	if (text.isEmpty() || showsStoredSecret(m_ui.secret))
		return;
	resetSecretField(m_ui.secret, m_ui.secretShow);
	apply({{MemcardSync::SECRET_ACCESS_KEY, text.toStdString()}});
}

void MemoryCardSyncSettingsWidget::onPassphraseEdited()
{
	// Not trimmed: emusync's own tool keeps spaces too, and the same passphrase has to unlock the
	// storage on every device.
	const QString text = m_ui.passphrase->text();
	if (text.isEmpty() || showsStoredSecret(m_ui.passphrase))
		return;
	resetSecretField(m_ui.passphrase, m_ui.passphraseShow);
	apply({{MemcardSync::ENCRYPTION_PASSPHRASE, text.toStdString()}});
}

void MemoryCardSyncSettingsWidget::onShowToggled(QLineEdit* edit, QPushButton* show_button, const char* secret, bool shown)
{
	edit->setEchoMode(shown ? QLineEdit::Normal : QLineEdit::Password);
	if (!shown)
	{
		// Hide puts a stored secret that was shown away again, rather than only masking it.
		if (showsStoredSecret(edit))
			edit->clear();
		edit->setProperty("revealed", QVariant());
		return;
	}
	// Show unmasks what is being typed; with nothing typed, it loads the stored secret, e.g. to set
	// up another device with it (and losing the passphrase loses the saves). While settings apply,
	// e.g. a value just saved by tabbing to Show, the store may not have it yet: apply() shows it
	// once they are applied.
	if (!edit->text().isEmpty() || m_pending_applies > 0 || !MemcardSync::HasSecret(secret))
		return;

	// Reading the credential store can block (an unlock prompt), so not on the UI thread.
	std::thread([widget = QPointer<MemoryCardSyncSettingsWidget>(this), edit, show_button, secret]() {
		std::string error;
		std::optional<std::string> value = MemcardSync::LoadSecret(secret, &error);
		QtHost::RunOnUIThread([widget, edit, show_button, secret, value = std::move(value), error = std::move(error)]() mutable {
			if (widget && show_button->isChecked() && edit->text().isEmpty())
			{
				if (value.has_value())
				{
					const QString text = QString::fromStdString(*value);
					edit->setText(text);
					edit->setProperty("revealed", text);
				}
				else if (!error.empty())
				{
					widget->m_ui.status->setText(QString::fromStdString(error));
				}
				else
				{
					widget->m_ui.status->setText(std::string_view(secret) == MemcardSync::SECRET_ACCESS_KEY ?
						MemoryCardSyncSettingsWidget::tr("No secret access key is saved.") :
						MemoryCardSyncSettingsWidget::tr("No passphrase is saved."));
				}
			}
			if (value.has_value())
				MemcardSync::WipeSecret(*value);
		});
	}).detach();
}

void MemoryCardSyncSettingsWidget::onPassphraseRemoveClicked()
{
	resetSecretField(m_ui.passphrase, m_ui.passphraseShow);
	apply({{MemcardSync::ENCRYPTION_PASSPHRASE, std::string()}});
}

void MemoryCardSyncSettingsWidget::onSyncNowClicked()
{
	MemcardSync::SyncNow();
	m_ui.status->setText(tr("Syncing..."));
}

void MemoryCardSyncSettingsWidget::apply(std::vector<std::pair<const char*, std::string>> secrets)
{
	std::vector<MemcardSync::SecretUpdate> updates;
	for (auto& [name, value] : secrets)
		updates.push_back({name, std::move(value)});
	m_pending_applies++;
	updateStatus();
	MemcardSync::ApplySettingsAsync(std::move(updates), [widget = QPointer<MemoryCardSyncSettingsWidget>(this)](const std::string&) {
		QtHost::RunOnUIThread([widget]() {
			if (widget)
				widget->onApplied();
		});
	});
}

void MemoryCardSyncSettingsWidget::onApplied()
{
	m_pending_applies--;
	updateSecretPlaceholders();
	updateStatus();
	// Only once every queued change is in: an earlier one finishing doesn't mean a secret saved
	// since is stored yet.
	if (m_pending_applies > 0)
		return;
	// A Show pressed while they applied.
	if (m_ui.secretShow->isChecked())
		onShowToggled(m_ui.secret, m_ui.secretShow, MemcardSync::SECRET_ACCESS_KEY, true);
	if (m_ui.passphraseShow->isChecked())
		onShowToggled(m_ui.passphrase, m_ui.passphraseShow, MemcardSync::ENCRYPTION_PASSPHRASE, true);
}

void MemoryCardSyncSettingsWidget::updateEnabledState()
{
	if (MemcardSync::IsAvailable())
		return;
	for (QWidget* w : {static_cast<QWidget*>(m_ui.enabled), static_cast<QWidget*>(m_ui.remoteUri),
			 static_cast<QWidget*>(m_ui.accessKeyId), static_cast<QWidget*>(m_ui.secret), static_cast<QWidget*>(m_ui.secretShow),
			 static_cast<QWidget*>(m_ui.passphrase), static_cast<QWidget*>(m_ui.passphraseShow),
			 static_cast<QWidget*>(m_ui.passphraseRemove), static_cast<QWidget*>(m_ui.deviceName), static_cast<QWidget*>(m_ui.syncNow)})
	{
		w->setEnabled(false);
	}
}

void MemoryCardSyncSettingsWidget::updateSecretPlaceholders()
{
	m_ui.secret->setPlaceholderText(MemcardSync::HasSecret(MemcardSync::SECRET_ACCESS_KEY) ? tr("Saved") : tr("Not set"));
	const bool has_passphrase = MemcardSync::HasSecret(MemcardSync::ENCRYPTION_PASSPHRASE);
	m_ui.passphrase->setPlaceholderText(has_passphrase ? tr("Saved") : tr("No passphrase saved"));
	m_ui.passphraseRemove->setEnabled(has_passphrase && MemcardSync::IsAvailable());
}

void MemoryCardSyncSettingsWidget::updateStatus()
{
	QString text;
	if (!MemcardSync::IsAvailable())
		text = tr("This build of PCSX2 has no memory card sync.");
	else if (m_pending_applies > 0)
		text = tr("Connecting to the storage...");
	else if (const std::string error = MemcardSync::GetLastStartError(); !error.empty())
		text = QString::fromStdString(error);
	else if (!m_ui.enabled->isChecked())
		text = tr("Off.");
	else if (!MemcardSync::IsRunning())
		text = tr("Not syncing: fill in the storage, access key ID and secret access key.");
	else if (const std::string status = MemcardSync::GetStatus(); !status.empty())
		text = QString::fromStdString(status);
	else
		text = tr("Syncing. Nothing has changed yet.");
	m_ui.status->setText(text);
	m_ui.syncNow->setEnabled(MemcardSync::IsAvailable() && MemcardSync::IsRunning() && m_pending_applies == 0);
}

#include "moc_MemoryCardSyncSettingsWidget.cpp"
