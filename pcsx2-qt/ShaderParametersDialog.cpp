// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "ShaderParametersDialog.h"
#include "Settings/SettingsWindow.h"

#include "pcsx2/GS/ShaderChain/ShaderPresets.h"
#include "pcsx2/Host.h"
#include "pcsx2/VMManager.h"

#include "common/Error.h"
#include "common/Path.h"
#include "common/SettingsInterface.h"

#include <QtCore/QEvent>
#include <QtCore/QTimer>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>

#include <algorithm>
#include <cmath>

ShaderParametersDialog::ShaderParametersDialog(SettingsWindow* settings, QWidget* parent, std::string preset)
	: QDialog(parent)
	, m_settings(settings)
	, m_preset(std::move(preset))
{
	m_ui.setupUi(this);
	setWindowTitle(tr("Shader Parameters - %1").arg(QString::fromStdString(std::string(Path::GetFileTitle(m_preset)))));

	m_write_timer = new QTimer(this);
	m_write_timer->setSingleShot(true);
	m_write_timer->setInterval(WRITE_DELAY_MS);
	connect(m_write_timer, &QTimer::timeout, this, &ShaderParametersDialog::flushWrite);

	connect(m_ui.resetAll, &QPushButton::clicked, this, &ShaderParametersDialog::onResetAllClicked);
	connect(m_ui.close, &QPushButton::clicked, this, &QDialog::accept);

	if (m_settings->isPerGameSettings())
		m_ui.resetAll->setText(tr("Use Global Settings"));

	if (!loadParameters())
		return;

	buildRows();
	applyOverrides(ShaderChainParams::ParseOverrides(readOverrideEntries()));
}

ShaderParametersDialog::~ShaderParametersDialog() = default;

void ShaderParametersDialog::done(int r)
{
	flushWrite();
	QDialog::done(r);
}

bool ShaderParametersDialog::eventFilter(QObject* watched, QEvent* event)
{
	// Wheel events over an unfocused slider/spin box scroll the list instead of editing the parameter.
	QWidget* const widget = qobject_cast<QWidget*>(watched);
	if (event->type() == QEvent::Wheel && widget && !widget->hasFocus())
	{
		QApplication::sendEvent(m_ui.scroll->viewport(), event);
		return true;
	}
	return QDialog::eventFilter(watched, event);
}

bool ShaderParametersDialog::loadParameters()
{
	const std::string path = ShaderPresets::ResolvePresetPath(m_preset);
	std::vector<ShaderChainParams::ParameterInfo> params;
	Error error;
	if (path.empty() || !ShaderChainParams::EnumerateParameters(path, &params, &error))
	{
		m_ui.status->setText(tr("Could not load preset: %1")
								 .arg(path.empty() ? tr("invalid preset path") : QString::fromStdString(error.GetDescription())));
		m_ui.resetAll->setEnabled(false);
		return false;
	}

	if (params.empty())
	{
		m_ui.status->setText(tr("This preset has no adjustable parameters."));
		m_ui.resetAll->setEnabled(false);
		return false;
	}

	m_ui.status->hide();
	m_rows.reserve(params.size());
	for (ShaderChainParams::ParameterInfo& info : params)
	{
		Row row;
		row.value = info.initial;
		row.info = std::move(info);
		m_rows.push_back(std::move(row));
	}
	return true;
}

void ShaderParametersDialog::buildRows()
{
	QGridLayout* const grid = m_ui.grid;
	for (size_t i = 0; i < m_rows.size(); i++)
	{
		Row& row = m_rows[i];
		const ShaderChainParams::ParameterInfo& info = row.info;
		const int grid_row = static_cast<int>(i);

		QLabel* const label = new QLabel(
			QString::fromStdString(info.description.empty() ? info.name : info.description), m_ui.scrollContents);
		label->setToolTip(QString::fromStdString(info.name));
		grid->addWidget(label, grid_row, 0);

		// A slider needs at least one whole step: has_range rules out a non-positive step and an
		// inverted range, whole_steps rules out a range narrower than a single step (those have no
		// usable positions, so they get the spin box alone).
		const bool has_range = (info.step > 0.0f && info.maximum > info.minimum);
		const double whole_steps = has_range ? std::round((info.maximum - info.minimum) / info.step) : 0.0;
		const bool degenerate = (whole_steps < 1.0);
		if (!degenerate)
		{
			// One position per whole step, so a position is exactly minimum + p * step. Ranges with
			// more steps than the slider can carry are capped, which stretches the increment.
			const int steps = static_cast<int>(std::min<double>(whole_steps, static_cast<double>(MAX_SLIDER_STEPS)));
			row.slider_increment = (whole_steps <= static_cast<double>(MAX_SLIDER_STEPS)) ?
									   info.step :
									   (info.maximum - info.minimum) / static_cast<float>(MAX_SLIDER_STEPS);
			row.slider = new QSlider(Qt::Horizontal, m_ui.scrollContents);
			row.slider->setRange(0, steps);
			row.slider->setMinimumWidth(180);
			row.slider->setFocusPolicy(Qt::StrongFocus);
			row.slider->installEventFilter(this);
			grid->addWidget(row.slider, grid_row, 1);
			connect(row.slider, &QSlider::valueChanged, this, [this, i](int pos) {
				Row& r = m_rows[i];
				// The last position is the maximum exactly, even when the range is not a whole
				// number of steps.
				float v = (pos >= r.slider->maximum()) ?
							  r.info.maximum :
							  r.info.minimum + static_cast<float>(pos) * r.slider_increment;
				// The position nearest the default *is* the default: minimum + p * step accumulates
				// float noise on wide ranges, which would leave Reset enabled and persist that noise.
				if (std::abs(v - r.info.initial) < r.slider_increment * 0.5f)
					v = r.info.initial;
				onValueEdited(r, v);
			});
		}

		row.spin = new QDoubleSpinBox(m_ui.scrollContents);
		// Decimals first: QDoubleSpinBox rounds the range and the step to the current precision.
		row.spin->setDecimals(ShaderChainParams::DecimalsForStep(info.step));
		if (has_range)
		{
			// Includes the sub-step ranges that lost their slider: the preset range still applies.
			row.spin->setRange(info.minimum, info.maximum);
			row.spin->setSingleStep(info.step);
		}
		else
		{
			row.spin->setRange(-1.0e9, 1.0e9);
			row.spin->setSingleStep(info.step > 0.0f ? info.step : 1.0);
		}
		row.spin->setMinimumWidth(90);
		row.spin->setKeyboardTracking(false);
		row.spin->setFocusPolicy(Qt::StrongFocus);
		row.spin->installEventFilter(this);
		grid->addWidget(row.spin, grid_row, 2);
		connect(row.spin, &QDoubleSpinBox::valueChanged, this, [this, i](double v) {
			onValueEdited(m_rows[i], static_cast<float>(v));
		});

		row.reset = new QPushButton(tr("Reset"), m_ui.scrollContents);
		grid->addWidget(row.reset, grid_row, 3);
		connect(row.reset, &QPushButton::clicked, this, [this, i]() {
			onValueEdited(m_rows[i], m_rows[i].info.initial);
		});

		refreshRowWidgets(row);
	}
	grid->setColumnStretch(1, 1);
	grid->setRowStretch(static_cast<int>(m_rows.size()), 1);
}

std::vector<std::string> ShaderParametersDialog::readOverrideEntries() const
{
	const char* section = ShaderChainParams::SettingsSection();
	if (m_settings->isPerGameSettings())
	{
		SettingsInterface* const sif = m_settings->getSettingsInterface();
		if (sif->ContainsValue(section, m_preset.c_str()))
			return sif->GetStringList(section, m_preset.c_str());
	}
	return Host::GetBaseStringListSetting(section, m_preset.c_str());
}

void ShaderParametersDialog::applyOverrides(const ShaderChainParams::ParamList& overrides)
{
	for (Row& row : m_rows)
	{
		float value = row.info.initial;
		const auto it = std::find_if(overrides.begin(), overrides.end(),
			[&row](const auto& p) { return p.first == row.info.name; });
		if (it != overrides.end())
		{
			value = it->second;
			// Out-of-range persisted values are clamped into the widget range; the clamped value is
			// what gets pushed and, on the next write, persisted.
			if (row.info.maximum > row.info.minimum)
				value = std::clamp(value, row.info.minimum, row.info.maximum);
		}
		setRowValue(row, value);
	}
}

void ShaderParametersDialog::setRowValue(Row& row, float value)
{
	row.value = value;
	refreshRowWidgets(row);
}

void ShaderParametersDialog::refreshRowWidgets(Row& row)
{
	const bool was_updating = m_updating;
	m_updating = true;
	if (row.slider)
	{
		const int position = static_cast<int>(std::lround((row.value - row.info.minimum) / row.slider_increment));
		row.slider->setValue(std::clamp(position, 0, row.slider->maximum()));
	}
	row.spin->setValue(row.value);
	row.reset->setEnabled(!isDefault(row));
	m_updating = was_updating;
}

bool ShaderParametersDialog::isDefault(const Row& row) const
{
	return ShaderChainParams::IsDefaultValue(row.value, row.info.initial);
}

ShaderChainParams::ParamList ShaderParametersDialog::collectOverrides() const
{
	ShaderChainParams::ParamList list;
	for (const Row& row : m_rows)
	{
		if (!isDefault(row))
			list.emplace_back(row.info.name, row.value);
	}
	return list;
}

void ShaderParametersDialog::onValueEdited(Row& row, float value)
{
	if (m_updating)
		return;
	if (std::abs(value - row.value) < 1e-7f)
		return;

	setRowValue(row, value);
	pushToStore();
	scheduleWrite();
}

bool ShaderParametersDialog::isEditingEffectiveLayer() const
{
	if (m_settings->isPerGameSettings())
	{
		// Game Properties may be open for a game that is not running; its values must not leak
		// into whatever is on screen.
		return VMManager::HasValidVM() && VMManager::GetDiscSerial() == m_settings->getSerial() &&
			   VMManager::GetDiscCRC() == m_settings->getDiscCRC();
	}

	// Global window: a running game's per-game key overrides the global list wholesale.
	auto lock = Host::GetSettingsLock();
	SettingsInterface* const game_layer = Host::Internal::GetGameSettingsLayer();
	return !game_layer || !game_layer->ContainsValue(ShaderChainParams::SettingsSection(), m_preset.c_str());
}

void ShaderParametersDialog::pushToStore()
{
	if (!isEditingEffectiveLayer())
		return;

	// Push every parameter, not only the overrides: the backends only set what the list contains,
	// so a value reset to its default has to be sent explicitly to take effect on a live chain.
	ShaderChainParams::ParamList list;
	list.reserve(m_rows.size());
	for (const Row& row : m_rows)
		list.emplace_back(row.info.name, row.value);
	ShaderPresets::Params().Set(m_preset, std::move(list));
}

void ShaderParametersDialog::scheduleWrite()
{
	m_write_pending = true;
	m_write_timer->start();
}

void ShaderParametersDialog::flushWrite()
{
	if (!m_write_pending)
		return;
	m_write_pending = false;
	m_write_timer->stop();

	const char* section = ShaderChainParams::SettingsSection();
	const std::vector<std::string> entries = ShaderChainParams::FormatOverrides(collectOverrides());
	if (m_settings->isPerGameSettings())
	{
		SettingsInterface* const sif = m_settings->getSettingsInterface();
		if (entries.empty())
			sif->DeleteValue(section, m_preset.c_str());
		else
			sif->SetStringList(section, m_preset.c_str(), entries);
		m_settings->saveAndReloadGameSettings();
		// A per-game list replaces the global one wholesale, so an emptied list falls back to the
		// global values; show what is now effective.
		if (entries.empty())
			applyOverrides(ShaderChainParams::ParseOverrides(readOverrideEntries()));
	}
	else
	{
		if (entries.empty())
			Host::RemoveBaseSettingValue(section, m_preset.c_str());
		else
			Host::SetBaseStringListSettingValue(section, m_preset.c_str(), entries);
		Host::CommitBaseSettingChanges();
	}

	// Re-push after the write so a settings reload that raced the debounce cannot leave stale values live.
	pushToStore();
}

void ShaderParametersDialog::onResetAllClicked()
{
	if (m_settings->isPerGameSettings())
	{
		// "Use Global Settings": drop the per-game key and show the global values.
		m_write_pending = false;
		m_write_timer->stop();
		m_settings->getSettingsInterface()->DeleteValue(ShaderChainParams::SettingsSection(), m_preset.c_str());
		m_settings->saveAndReloadGameSettings();
		applyOverrides(ShaderChainParams::ParseOverrides(readOverrideEntries()));
		pushToStore();
		return;
	}

	for (Row& row : m_rows)
		setRowValue(row, row.info.initial);
	pushToStore();
	scheduleWrite();
}

#include "moc_ShaderParametersDialog.cpp"
