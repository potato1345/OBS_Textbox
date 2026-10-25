/*
Liedplan — Standalone OBS-Plugin (T7)
Copyright (C) 2026 Johann

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#ifndef _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#endif

#include "liedplan-dock.hpp"
#include "liedplan-profiles.hpp"
#include "liedplan-scene.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>

namespace {

constexpr int kDefaultCardCount = 12;

} // namespace

LiedplanDock::LiedplanDock(const std::string &state_path, QWidget *parent)
	: QWidget(parent),
	  state_path_(state_path)
{
	auto *root_layout = new QVBoxLayout(this);
	root_layout->setContentsMargins(6, 6, 6, 6);
	root_layout->setSpacing(6);

	auto *scroll_area = new QScrollArea(this);
	scroll_area->setWidgetResizable(true);
	scroll_area->setFrameShape(QFrame::NoFrame);

	auto *cards_container = new QWidget(scroll_area);
	cards_layout_ = new QVBoxLayout(cards_container);
	cards_layout_->setContentsMargins(0, 0, 0, 0);
	cards_layout_->setSpacing(4);
	cards_layout_->addStretch(1);

	scroll_area->setWidget(cards_container);
	root_layout->addWidget(scroll_area, 1);

	auto *button_row = new QHBoxLayout();
	button_row->setContentsMargins(0, 0, 0, 0);
	button_row->setSpacing(6);

	auto *add_btn = new QPushButton(QStringLiteral("+ Neue Karte"), this);
	auto *clear_btn = new QPushButton(QStringLiteral("Alle löschen"), this);

	button_row->addWidget(add_btn);
	button_row->addWidget(clear_btn);
	button_row->addStretch(1);
	root_layout->addLayout(button_row);

	connect(add_btn, &QPushButton::clicked, this, &LiedplanDock::on_add_card);
	connect(clear_btn, &QPushButton::clicked, this, &LiedplanDock::on_clear_all);

	bool file_exists = false;
	if (!state_path_.empty()) {
		std::error_code ec;
		file_exists = std::filesystem::exists(std::filesystem::u8path(state_path_), ec);
	}

	state_.load(state_path_);

	if (!file_exists) {
		for (int i = 0; i < kDefaultCardCount; ++i) {
			state_.add_card();
		}
		save_state();
	}

	rebuild_rows();
	setMinimumSize(420, 220);
}

LiedplanCard *LiedplanDock::find_card_mut(const std::string &id)
{
	for (auto &c : state_.cards) {
		if (c.id == id)
			return &c;
	}
	return nullptr;
}

void LiedplanDock::create_row_widget(const LiedplanCard &card)
{
	auto *row_widget = new QWidget(this);
	auto *row_layout = new QHBoxLayout(row_widget);
	row_layout->setContentsMargins(0, 0, 0, 0);
	row_layout->setSpacing(4);

	auto *id_label = new QLabel(QString::fromUtf8(card.id.c_str()), row_widget);
	id_label->setMinimumWidth(42);

	auto *edit_zeile1 = new QLineEdit(QString::fromUtf8(card.zeile1.c_str()), row_widget);
	edit_zeile1->setPlaceholderText(QStringLiteral("GL-Nummer"));
	edit_zeile1->setMaximumWidth(80);

	auto *edit_zeile2 = new QLineEdit(QString::fromUtf8(card.zeile2.c_str()), row_widget);
	edit_zeile2->setPlaceholderText(QStringLiteral("Verse"));
	edit_zeile2->setMaximumWidth(80);

	auto *edit_zeile3 = new QLineEdit(QString::fromUtf8(card.zeile3.c_str()), row_widget);
	edit_zeile3->setPlaceholderText(QStringLiteral("Liedtitel"));

	auto *profile_combo = new QComboBox(row_widget);
	int selected_index = 0;
	const auto &profiles = liedplan_profiles();
	for (size_t i = 0; i < profiles.size(); ++i) {
		profile_combo->addItem(QString::fromUtf8(profiles[i].name.c_str()));
		if (profiles[i].name == card.profile)
			selected_index = static_cast<int>(i);
	}
	profile_combo->setCurrentIndex(selected_index);

	auto *to_scene_btn = new QToolButton(row_widget);
	to_scene_btn->setText(QStringLiteral("Zu Szene"));
	to_scene_btn->setToolTip(
		QStringLiteral("Karte als Text-Quelle in die aktuelle Szene legen oder aktualisieren"));

	auto *clear_src_btn = new QToolButton(row_widget);
	clear_src_btn->setText(QStringLiteral("Leeren"));
	clear_src_btn->setToolTip(
		QStringLiteral("Text der Quelle auf leer setzen — die Quelle bleibt in der Szene"));

	auto *remove_src_btn = new QToolButton(row_widget);
	remove_src_btn->setText(QStringLiteral("Entf."));
	remove_src_btn->setToolTip(QStringLiteral("Quelle aus allen Szenen entfernen"));

	auto *del_btn = new QToolButton(row_widget);
	del_btn->setText(QStringLiteral("✕"));
	del_btn->setToolTip(QStringLiteral("Karte löschen"));

	row_layout->addWidget(id_label);
	row_layout->addWidget(edit_zeile1);
	row_layout->addWidget(edit_zeile2);
	row_layout->addWidget(edit_zeile3, 1);
	row_layout->addWidget(profile_combo);
	row_layout->addWidget(to_scene_btn);
	row_layout->addWidget(clear_src_btn);
	row_layout->addWidget(remove_src_btn);
	row_layout->addWidget(del_btn);

	const std::string card_id = card.id;

	connect(edit_zeile1, &QLineEdit::textChanged, this, [this, card_id](const QString &text) {
		if (LiedplanCard *c = find_card_mut(card_id)) {
			c->zeile1 = text.toUtf8().toStdString();
			save_state();
			liedplan_scene::update_everywhere(*c, false);
		}
	});

	connect(edit_zeile2, &QLineEdit::textChanged, this, [this, card_id](const QString &text) {
		if (LiedplanCard *c = find_card_mut(card_id)) {
			c->zeile2 = text.toUtf8().toStdString();
			save_state();
			liedplan_scene::update_everywhere(*c, false);
		}
	});

	connect(edit_zeile3, &QLineEdit::textChanged, this, [this, card_id](const QString &text) {
		if (LiedplanCard *c = find_card_mut(card_id)) {
			c->zeile3 = text.toUtf8().toStdString();
			save_state();
			liedplan_scene::update_everywhere(*c, false);
		}
	});

	connect(profile_combo, &QComboBox::currentTextChanged, this,
		[this, card_id](const QString &profile_text) {
			if (LiedplanCard *c = find_card_mut(card_id)) {
				c->profile = profile_text.toUtf8().toStdString();
				if (const LiedplanProfile *prof = profile_by_name(c->profile)) {
					c->text_color = prof->text_color;
					c->box_color = prof->box_color;
				}
				save_state();
				liedplan_scene::update_everywhere(*c, false);
			}
		});

	connect(to_scene_btn, &QToolButton::clicked, this, [this, card_id]() {
		if (LiedplanCard *c = find_card_mut(card_id)) {
			liedplan_scene::send_to_current_scene(*c);
		}
	});

	connect(clear_src_btn, &QToolButton::clicked, this, [this, card_id]() {
		if (LiedplanCard *c = find_card_mut(card_id)) {
			liedplan_scene::update_everywhere(*c, true);
		}
	});

	connect(remove_src_btn, &QToolButton::clicked, this, [card_id]() {
		liedplan_scene::remove_everywhere(card_id);
	});

	connect(del_btn, &QToolButton::clicked, this, [this, card_id, row_widget]() {
		if (state_.remove_card(card_id)) {
			save_state();
		}
		liedplan_scene::remove_everywhere(card_id);
		cards_layout_->removeWidget(row_widget);
		row_widgets_.erase(std::remove(row_widgets_.begin(), row_widgets_.end(), row_widget),
				   row_widgets_.end());
		row_widget->deleteLater();
	});

	const int insert_idx = std::max(0, cards_layout_->count() - 1);
	cards_layout_->insertWidget(insert_idx, row_widget);
	row_widgets_.push_back(row_widget);
}

void LiedplanDock::rebuild_rows()
{
	for (QWidget *w : row_widgets_) {
		cards_layout_->removeWidget(w);
		w->deleteLater();
	}
	row_widgets_.clear();

	for (const auto &card : state_.cards) {
		create_row_widget(card);
	}
}

void LiedplanDock::on_add_card()
{
	const LiedplanCard &card = state_.add_card();
	create_row_widget(card);
	save_state();
}

void LiedplanDock::on_clear_all()
{
	const auto reply = QMessageBox::question(
		this, QStringLiteral("Alle löschen"),
		QStringLiteral("Sollen wirklich alle Karten gelöscht werden?"),
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
	if (reply != QMessageBox::Yes)
		return;

	std::vector<std::string> ids;
	ids.reserve(state_.cards.size());
	for (const auto &c : state_.cards) {
		ids.push_back(c.id);
	}

	state_.cards.clear();
	rebuild_rows();
	save_state();

	for (const auto &id : ids) {
		liedplan_scene::remove_everywhere(id);
	}
}

void LiedplanDock::save_state()
{
	if (!state_path_.empty())
		state_.save(state_path_);
}
