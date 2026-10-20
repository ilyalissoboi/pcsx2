// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Types.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

// Memory card sync through the cloud, with emusync (https://github.com/ilyalissoboi/emusync).
//
// The slot 1 and 2 cards are synced, per save, with every other device using the same storage:
// PCSX2 on other computers, ARMSX2 on Android, and anything running `emusync watch`. A save made on
// one device shows up on the others; two devices playing different games never conflict, and when
// the same save was changed on two devices the newest version wins and the other stays in emusync's
// history.
//
// Which cards: the ones in slots 1 and 2, file cards (.ps2) and folder cards. A card's identity on
// every device is its name without the extension, with the default names ("Mcd001.ps2",
// "mcd001.ps2", a folder "Mcd001") all meaning the card "Mcd001", so this computer's Mcd001.ps2 and
// ARMSX2's mcd001.ps2 are the same card. PS1 cards and the multitap slots are not synced.
//
// When cards are read and written (with the hooks below in place):
//   Boot: FileMcd_Reopen calls OnCardsReopening() with the cards closed. Newer saves from other
//   devices are written into them before the game opens them, waiting at most 3 s for the network;
//   offline, the game starts with the cards as they are. From then until OnVMShutdown(), emusync
//   never writes to those cards; changes that arrive meanwhile wait and are applied after the game.
//   Play: OnCardWritten() (every card write) schedules a snapshot once writes have been quiet for
//   2 s. File cards are read while the game runs (PCSX2 opens them shared for reading), so saves
//   upload as they are made. Folder cards are written back by PCSX2 when they close, so their saves
//   upload then. Snapshots go to an on-disk queue first, so a save is not lost if PCSX2 exits
//   before the upload.
//   Shutdown: OnCardsFlushed() (cards closed) snapshots at once; OnVMShutdown() ends the session,
//   uploads, and applies waiting changes.
//
// Settings: [MemoryCardSync] in the base settings (Enabled, RemoteURI, AccessKeyID, DeviceName),
// with the secret access key and the optional encryption passphrase in the system's credential
// store (Credential Manager, Keychain, Secret Service) through emusync, never in the INI.
//
// Without PCSX2_HAS_EMUSYNC (emusync not found at build time) every function here is a no-op,
// IsAvailable() is false and starting fails, saying the build has no sync.
//
// Threading: everything is thread-safe. Start() and the boot hook block (local database open, and
// the network as described); every other hook returns quickly.
namespace MemcardSync
{
	/// Whether this build includes memory card sync.
	bool IsAvailable();

	struct Config
	{
		/// Private folder for emusync's database and queue. Bound to one storage for good, so use one
		/// per remote_uri (DataDirFor()).
		std::string data_dir;
		/// Shown on other devices, e.g. in a save's history. Empty: the computer's name.
		std::string device_name;
		std::string emulator_version;
		/// The storage, e.g. "s3://my-saves/emusync/v1?region=eu-west-1".
		std::string remote_uri;
		std::string access_key_id;
		std::string secret_access_key;
		/// Optional: the storage's end-to-end encryption passphrase. Set, it creates the storage
		/// encrypted if it is new, or unlocks it if it already is; storage that is not encrypted is
		/// then refused. Empty, unencrypted storage is used as it is, and encrypted storage this
		/// computer has already unlocked keeps working (emusync keeps its key in data_dir).
		std::string passphrase;
	};

	/// Starts syncing, replacing a client that is already running. Blocks while the local database
	/// opens and for up to 10 s while the storage is checked. Returns false with `error` set when
	/// sync cannot start (wrong address, keys rejected, unreadable data folder). Storage that cannot
	/// be reached right now is not an error: sync starts working once it can be reached. Does not
	/// keep config.passphrase; the caller should WipeSecret() its own copy afterwards.
	bool Start(const Config& config, std::string* error);

	/// Stops syncing. Cards are left as they are. Waits up to `timeout_ms` for queued uploads.
	void Stop(u32 timeout_ms);

	bool IsRunning();

	/// One line about the last sync, for the settings page.
	std::string GetStatus();

	/// Queues a full sync: download, apply to cards no game has open, upload.
	void SyncNow();

	// ---- Settings (base settings layer and the system's credential store) ------------------

	/// The emusync data folder for `remote_uri`: <DataRoot>/emusync/<hash of the address>.
	std::string DataDirFor(const std::string& remote_uri);

	/// Names of the secrets in the system's credential store (service "PCSX2").
	static constexpr const char* SECRET_ACCESS_KEY = "s3-secret-access-key";
	static constexpr const char* ENCRYPTION_PASSPHRASE = "encryption-passphrase";

	/// Stores a secret in the system's credential store; an empty value deletes it. Records whether
	/// one is stored in the base settings (so the settings page can say so without asking the
	/// store). May block while the system asks to unlock the keychain: not on the UI thread.
	bool StoreSecret(const char* name, const std::string& value, std::string* error);

	/// The stored secret, if any. May block like StoreSecret().
	std::optional<std::string> LoadSecret(const char* name, std::string* error);

	/// Whether a secret is recorded as stored (from the base settings; does not ask the store).
	bool HasSecret(const char* name);

	/// Applies the settings on a background thread: optionally stores new secrets first, then starts
	/// sync with the base settings, or stops it when it is switched off or incomplete. Requests
	/// made while one runs are folded into one more run with the latest settings. `done`, if set,
	/// is called on that thread with an empty string when sync runs or is deliberately off, else a
	/// sentence for the user.
	struct SecretUpdate
	{
		const char* name;
		std::string value;
	};
	void ApplySettingsAsync(std::vector<SecretUpdate> secrets = {}, std::function<void(const std::string& error)> done = {});

	/// The error of the last ApplySettingsAsync() run, empty if it started (or stopped) cleanly.
	std::string GetLastStartError();

	/// Starts sync from the settings at startup (VMManager::Internal::CPUThreadInitialize) and stops
	/// it, giving queued uploads up to a few seconds, at exit (CPUThreadShutdown).
	void Initialize();
	void Shutdown();

	/// Overwrites a secret's characters, then empties the string and frees its buffer. Plain
	/// clear() only changes the size, leaving the characters readable in memory.
	void WipeSecret(std::string& secret);

	// ---- core hooks ----------------------------------------------------------------------

	/// FileMcd_Reopen, with the cards closed and their types known (after FileMcd_SetType()).
	/// While the VM is starting, begins the session (see above). At any other time it only follows
	/// a change of the cards in the slots.
	void OnCardsReopening();

	/// Whether sync tracks the card in the combined slot `slot` (0-7) right now. FileMcd_Save and
	/// FileMcd_EraseBlock flush a tracked file card's writes before OnCardWritten().
	bool IsSyncing(uint slot);

	/// FileMcd_Save and FileMcd_EraseBlock, after a successful (and flushed) write, with the combined
	/// slot number (0-7).
	void OnCardWritten(uint slot);

	/// FileMcd_EmuClose: the cards are on disk now.
	void OnCardsFlushed();

	/// VMManager::Shutdown after FileMcd_EmuClose(), and a boot that failed after
	/// OnCardsReopening(). Ends the session; safe to call when none is open.
	void OnVMShutdown();
} // namespace MemcardSync
