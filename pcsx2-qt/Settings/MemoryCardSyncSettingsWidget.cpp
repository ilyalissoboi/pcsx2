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
		// The stored secret access key is never read back: Show only unmasks what is being typed.
		m_ui.secret->setEchoMode(shown ? QLineEdit::Normal : QLineEdit::Password);
	});
	connect(m_ui.passphrase, &QLineEdit::editingFinished, this, &MemoryCardSyncSettingsWidget::onPassphraseEdited);
	connect(m_ui.passphraseShow, &QPushButton::toggled, this, &MemoryCardSyncSettingsWidget::onPassphraseShowToggled);
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
		   "settings file, and never shown again. Type or paste a new one to replace it."));
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

void MemoryCardSyncSettingsWidget::onSecretEdited()
{
	const std::string value = m_ui.secret->text().trimmed().toStdString();
	if (value.empty())
		return;
	// The field never holds the stored secret: empty it again once it is handed over.
	m_ui.secret->clear();
	m_ui.secretShow->setChecked(false);
	apply({{MemcardSync::SECRET_ACCESS_KEY, value}});
}

void MemoryCardSyncSettingsWidget::onPassphraseEdited()
{
	// Not trimmed: emusync's own tool keeps spaces too, and the same passphrase has to unlock the
	// storage on every device.
	const std::string value = m_ui.passphrase->text().toStdString();
	const bool revealed_and_unchanged = m_ui.passphraseShow->isChecked() && m_ui.passphrase->property("revealed").toString() == m_ui.passphrase->text();
	if (value.empty() || revealed_and_unchanged)
		return;
	m_ui.passphrase->clear();
	m_ui.passphrase->setProperty("revealed", QVariant());
	m_ui.passphraseShow->setChecked(false);
	apply({{MemcardSync::ENCRYPTION_PASSPHRASE, value}});
}

void MemoryCardSyncSettingsWidget::onPassphraseShowToggled(bool shown)
{
	m_ui.passphrase->setEchoMode(shown ? QLineEdit::Normal : QLineEdit::Password);
	if (!shown)
	{
		// Hide puts a stored passphrase that was shown away again, rather than only masking it.
		if (m_ui.passphrase->property("revealed").toString() == m_ui.passphrase->text())
			m_ui.passphrase->clear();
		m_ui.passphrase->setProperty("revealed", QVariant());
		return;
	}
	if (!m_ui.passphrase->text().isEmpty() || !MemcardSync::HasSecret(MemcardSync::ENCRYPTION_PASSPHRASE))
		return;

	// Show loads the stored passphrase: it's needed to set up other devices, and losing it loses the
	// saves. Reading the credential store can block (an unlock prompt), so not on the UI thread.
	std::thread([widget = QPointer<MemoryCardSyncSettingsWidget>(this)]() {
		std::string error;
		std::optional<std::string> value = MemcardSync::LoadSecret(MemcardSync::ENCRYPTION_PASSPHRASE, &error);
		QtHost::RunOnUIThread([widget, value = std::move(value), error = std::move(error)]() {
			if (!widget || !widget->m_ui.passphraseShow->isChecked())
				return;
			if (!value.has_value())
			{
				widget->m_ui.status->setText(error.empty() ? MemoryCardSyncSettingsWidget::tr("No passphrase is saved.") : QString::fromStdString(error));
				return;
			}
			const QString text = QString::fromStdString(*value);
			widget->m_ui.passphrase->setText(text);
			widget->m_ui.passphrase->setProperty("revealed", text);
		});
	}).detach();
}

void MemoryCardSyncSettingsWidget::onPassphraseRemoveClicked()
{
	m_ui.passphrase->clear();
	m_ui.passphrase->setProperty("revealed", QVariant());
	m_ui.passphraseShow->setChecked(false);
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
	m_starting = true;
	updateStatus();
	MemcardSync::ApplySettingsAsync(std::move(updates), [widget = QPointer<MemoryCardSyncSettingsWidget>(this)](const std::string&) {
		QtHost::RunOnUIThread([widget]() {
			if (!widget)
				return;
			widget->m_starting = false;
			widget->updateSecretPlaceholders();
			widget->updateStatus();
		});
	});
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
	else if (m_starting)
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
	m_ui.syncNow->setEnabled(MemcardSync::IsAvailable() && MemcardSync::IsRunning() && !m_starting);
}

#include "moc_MemoryCardSyncSettingsWidget.cpp"
