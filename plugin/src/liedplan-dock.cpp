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
#include "liedplan-theme.hpp"

#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSlider>
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
	setObjectName(QStringLiteral("liedplanDock"));
	setAttribute(Qt::WA_StyledBackground, true);
	setStyleSheet(liedplan_theme::stylesheet());

	auto *root_layout = new QVBoxLayout(this);
	root_layout->setContentsMargins(8, 8, 8, 8);
	root_layout->setSpacing(8);

	auto *button_row = new QHBoxLayout();
	button_row->setContentsMargins(8, 4, 8, 4);
	button_row->setSpacing(8);

	auto *add_btn = new QPushButton(QStringLiteral("＋ Neue Karte"), this);
	add_btn->setObjectName(QStringLiteral("lpBtnAccent"));

	auto *clear_btn = new QPushButton(QStringLiteral("🗑 Alle löschen"), this);
	clear_btn->setObjectName(QStringLiteral("lpBtnRed"));

	button_row->addWidget(add_btn);
	button_row->addWidget(clear_btn);
	button_row->addStretch(1);
	root_layout->addLayout(button_row);

	auto *scroll_area = new QScrollArea(this);
	scroll_area->setWidgetResizable(true);
	scroll_area->setFrameShape(QFrame::NoFrame);

	auto *cards_container = new QWidget(scroll_area);
	cards_container->setObjectName(QStringLiteral("lpCardsContainer"));
	cards_container->setAttribute(Qt::WA_StyledBackground, true);

	cards_layout_ = new QVBoxLayout(cards_container);
	cards_layout_->setContentsMargins(8, 8, 8, 8);
	cards_layout_->setSpacing(12);
	cards_layout_->addStretch(1);

	scroll_area->setWidget(cards_container);
	root_layout->addWidget(scroll_area, 1);

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
	setMinimumSize(440, 260);
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
	auto *row_widget = new QFrame(this);
	row_widget->setObjectName(QStringLiteral("lpCardOuter"));
	row_widget->setMinimumWidth(320);
	row_widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	auto *outer_layout = new QVBoxLayout(row_widget);
	outer_layout->setContentsMargins(2, 2, 2, 2);
	outer_layout->setSpacing(0);

	auto *card_inner = new QFrame(row_widget);
	card_inner->setObjectName(QStringLiteral("lpCardInner"));
	outer_layout->addWidget(card_inner);

	auto *inner_layout = new QVBoxLayout(card_inner);
	inner_layout->setContentsMargins(16, 16, 16, 16);
	inner_layout->setSpacing(10);

	auto *id_bar = new QFrame(card_inner);
	id_bar->setObjectName(QStringLiteral("lpIdBar"));
	auto *id_layout = new QHBoxLayout(id_bar);
	id_layout->setContentsMargins(12, 6, 12, 6);
	id_layout->setSpacing(8);

	auto *id_prefix = new QLabel(QStringLiteral("ID"), id_bar);
	id_prefix->setObjectName(QStringLiteral("lpIdPrefix"));

	auto *id_label = new QLabel(QString::fromUtf8(card.id.c_str()), id_bar);
	id_label->setObjectName(QStringLiteral("lpIdValue"));

	auto *del_btn = new QPushButton(QStringLiteral("✕"), id_bar);
	del_btn->setObjectName(QStringLiteral("lpDelBtn"));
	del_btn->setFixedSize(28, 28);
	del_btn->setToolTip(QStringLiteral("Karte löschen"));

	id_layout->addWidget(id_prefix);
	id_layout->addWidget(id_label);
	id_layout->addStretch(1);
	id_layout->addWidget(del_btn);
	inner_layout->addWidget(id_bar);
	inner_layout->addSpacing(2);

	auto make_field_row = [card_inner](const QString &label_text, const std::string &initial_val,
					   const QString &placeholder, QVBoxLayout *parent_layout) -> QLineEdit * {
		auto *field_layout = new QVBoxLayout();
		field_layout->setContentsMargins(0, 0, 0, 0);
		field_layout->setSpacing(4);

		auto *lbl = new QLabel(label_text, card_inner);
		lbl->setObjectName(QStringLiteral("lpFieldLabel"));

		auto *edit = new QLineEdit(QString::fromUtf8(initial_val.c_str()), card_inner);
		edit->setPlaceholderText(placeholder);
		edit->setFixedHeight(34);

		field_layout->addWidget(lbl);
		field_layout->addWidget(edit);
		parent_layout->addLayout(field_layout);
		return edit;
	};

	auto *edit_zeile1 =
		make_field_row(QStringLiteral("GL-NUMMER"), card.zeile1, QStringLiteral("GL-Nummer"), inner_layout);
	auto *edit_zeile2 =
		make_field_row(QStringLiteral("VERSE"), card.zeile2, QStringLiteral("Verse"), inner_layout);
	auto *edit_zeile3 =
		make_field_row(QStringLiteral("LIEDTITEL"), card.zeile3, QStringLiteral("Liedtitel"), inner_layout);

	auto *prof_layout = new QVBoxLayout();
	prof_layout->setContentsMargins(0, 0, 0, 0);
	prof_layout->setSpacing(4);

	auto *lbl_prof = new QLabel(QStringLiteral("PROFIL"), card_inner);
	lbl_prof->setObjectName(QStringLiteral("lpFieldLabel"));

	auto *profile_combo = new QComboBox(card_inner);
	profile_combo->setFixedHeight(34);
	int selected_index = 0;
	const auto &profiles = liedplan_profiles();
	for (size_t i = 0; i < profiles.size(); ++i) {
		profile_combo->addItem(QString::fromUtf8(profiles[i].name.c_str()));
		if (profiles[i].name == card.profile)
			selected_index = static_cast<int>(i);
	}
	profile_combo->setCurrentIndex(selected_index);

	prof_layout->addWidget(lbl_prof);
	prof_layout->addWidget(profile_combo);
	inner_layout->addLayout(prof_layout);

	const QString hex_a =
		QString::fromUtf8((card.corner_color_a.empty() ? "#fce647" : card.corner_color_a).c_str()).toUpper();
	const QString hex_b =
		QString::fromUtf8((card.corner_color_b.empty() ? "#fce647" : card.corner_color_b).c_str()).toUpper();

	auto *corner_a_layout = new QVBoxLayout();
	corner_a_layout->setContentsMargins(0, 0, 0, 0);
	corner_a_layout->setSpacing(4);
	auto *lbl_corner_a = new QLabel(QStringLiteral("ECKE A"), card_inner);
	lbl_corner_a->setObjectName(QStringLiteral("lpFieldLabel"));
	auto *color_a_btn = new QPushButton(hex_a, card_inner);
	color_a_btn->setObjectName(QStringLiteral("lpColorA"));
	color_a_btn->setFixedHeight(34);
	corner_a_layout->addWidget(lbl_corner_a);
	corner_a_layout->addWidget(color_a_btn);
	inner_layout->addLayout(corner_a_layout);

	auto *corner_b_layout = new QVBoxLayout();
	corner_b_layout->setContentsMargins(0, 0, 0, 0);
	corner_b_layout->setSpacing(4);
	auto *lbl_corner_b = new QLabel(QStringLiteral("ECKE B"), card_inner);
	lbl_corner_b->setObjectName(QStringLiteral("lpFieldLabel"));
	auto *color_b_btn = new QPushButton(hex_b, card_inner);
	color_b_btn->setObjectName(QStringLiteral("lpColorB"));
	color_b_btn->setFixedHeight(34);
	corner_b_layout->addWidget(lbl_corner_b);
	corner_b_layout->addWidget(color_b_btn);
	inner_layout->addLayout(corner_b_layout);

	auto *opacity_layout = new QVBoxLayout();
	opacity_layout->setContentsMargins(0, 0, 0, 0);
	opacity_layout->setSpacing(4);
	auto *lbl_opacity = new QLabel(QStringLiteral("ECKEN-DECKKRAFT"), card_inner);
	lbl_opacity->setObjectName(QStringLiteral("lpFieldLabel"));
	opacity_layout->addWidget(lbl_opacity);

	auto *opacity_a_row = new QHBoxLayout();
	opacity_a_row->setContentsMargins(0, 0, 0, 0);
	opacity_a_row->setSpacing(8);
	auto *lbl_op_a = new QLabel(QStringLiteral("A"), card_inner);
	lbl_op_a->setObjectName(QStringLiteral("lpFieldLabel"));
	auto *opacity_a_slider = new QSlider(Qt::Horizontal, card_inner);
	opacity_a_slider->setObjectName(QStringLiteral("lpOpacityA"));
	opacity_a_slider->setRange(0, 255);
	opacity_a_slider->setValue(std::clamp(card.corner_opacity_a, 0, 255));
	opacity_a_row->addWidget(lbl_op_a);
	opacity_a_row->addWidget(opacity_a_slider, 1);
	opacity_layout->addLayout(opacity_a_row);

	auto *opacity_b_row = new QHBoxLayout();
	opacity_b_row->setContentsMargins(0, 0, 0, 0);
	opacity_b_row->setSpacing(8);
	auto *lbl_op_b = new QLabel(QStringLiteral("B"), card_inner);
	lbl_op_b->setObjectName(QStringLiteral("lpFieldLabel"));
	auto *opacity_b_slider = new QSlider(Qt::Horizontal, card_inner);
	opacity_b_slider->setObjectName(QStringLiteral("lpOpacityB"));
	opacity_b_slider->setRange(0, 255);
	opacity_b_slider->setValue(std::clamp(card.corner_opacity_b, 0, 255));
	opacity_b_row->addWidget(lbl_op_b);
	opacity_b_row->addWidget(opacity_b_slider, 1);
	opacity_layout->addLayout(opacity_b_row);

	inner_layout->addLayout(opacity_layout);

	auto *rainbow_cb = new QCheckBox(QStringLiteral("🌈 Regenbogen-Ecken"), card_inner);
	rainbow_cb->setObjectName(QStringLiteral("lpRainbow"));
	rainbow_cb->setChecked(card.rainbow_corners);
	inner_layout->addWidget(rainbow_cb);

	auto *actions_layout = new QHBoxLayout();
	actions_layout->setContentsMargins(0, 4, 0, 0);
	actions_layout->setSpacing(8);

	auto *to_scene_btn = new QPushButton(QStringLiteral("▶ Zu Szene"), card_inner);
	to_scene_btn->setObjectName(QStringLiteral("lpBtnGreen"));
	to_scene_btn->setToolTip(
		QStringLiteral("Karte als Text-Quelle in die aktuelle Szene legen oder aktualisieren"));

	auto *clear_src_btn = new QPushButton(QStringLiteral("✕ Leeren"), card_inner);
	clear_src_btn->setObjectName(QStringLiteral("lpBtnRed"));
	clear_src_btn->setToolTip(
		QStringLiteral("Text der Quelle auf leer setzen — die Quelle bleibt in der Szene"));

	auto *remove_src_btn = new QPushButton(QStringLiteral("Entf."), card_inner);
	remove_src_btn->setObjectName(QStringLiteral("lpBtn"));
	remove_src_btn->setToolTip(QStringLiteral("Quelle aus allen Szenen entfernen"));

	actions_layout->addWidget(to_scene_btn, 1);
	actions_layout->addWidget(clear_src_btn, 1);
	actions_layout->addWidget(remove_src_btn);
	inner_layout->addLayout(actions_layout);

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

	connect(color_a_btn, &QPushButton::clicked, this, [this, card_id, color_a_btn]() {
		if (LiedplanCard *c = find_card_mut(card_id)) {
			const QString cur_hex = QString::fromUtf8(
				(c->corner_color_a.empty() ? "#fce647" : c->corner_color_a).c_str());
			const QColor chosen =
				QColorDialog::getColor(QColor(cur_hex), this, QStringLiteral("Eckfarbe A"));
			if (chosen.isValid()) {
				const QString hex = chosen.name(QColor::HexRgb);
				c->corner_color_a = hex.toUtf8().toStdString();
				color_a_btn->setText(hex.toUpper());
				save_state();
				liedplan_scene::update_everywhere(*c, false);
			}
		}
	});

	connect(color_b_btn, &QPushButton::clicked, this, [this, card_id, color_b_btn]() {
		if (LiedplanCard *c = find_card_mut(card_id)) {
			const QString cur_hex = QString::fromUtf8(
				(c->corner_color_b.empty() ? "#fce647" : c->corner_color_b).c_str());
			const QColor chosen =
				QColorDialog::getColor(QColor(cur_hex), this, QStringLiteral("Eckfarbe B"));
			if (chosen.isValid()) {
				const QString hex = chosen.name(QColor::HexRgb);
				c->corner_color_b = hex.toUtf8().toStdString();
				color_b_btn->setText(hex.toUpper());
				save_state();
				liedplan_scene::update_everywhere(*c, false);
			}
		}
	});

	connect(opacity_a_slider, &QSlider::valueChanged, this, [this, card_id](int val) {
		if (LiedplanCard *c = find_card_mut(card_id)) {
			c->corner_opacity_a = std::clamp(val, 0, 255);
			save_state();
			liedplan_scene::update_everywhere(*c, false);
		}
	});

	connect(opacity_b_slider, &QSlider::valueChanged, this, [this, card_id](int val) {
		if (LiedplanCard *c = find_card_mut(card_id)) {
			c->corner_opacity_b = std::clamp(val, 0, 255);
			save_state();
			liedplan_scene::update_everywhere(*c, false);
		}
	});

	connect(rainbow_cb, &QCheckBox::toggled, this, [this, card_id](bool checked) {
		if (LiedplanCard *c = find_card_mut(card_id)) {
			c->rainbow_corners = checked;
			save_state();
			liedplan_scene::update_everywhere(*c, false);
		}
	});

	connect(to_scene_btn, &QPushButton::clicked, this, [this, card_id]() {
		if (LiedplanCard *c = find_card_mut(card_id)) {
			liedplan_scene::send_to_current_scene(*c);
		}
	});

	connect(clear_src_btn, &QPushButton::clicked, this, [this, card_id]() {
		if (LiedplanCard *c = find_card_mut(card_id)) {
			liedplan_scene::update_everywhere(*c, true);
		}
	});

	connect(remove_src_btn, &QPushButton::clicked, this, [card_id]() {
		liedplan_scene::remove_everywhere(card_id);
	});

	connect(del_btn, &QPushButton::clicked, this, [this, card_id, row_widget]() {
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
