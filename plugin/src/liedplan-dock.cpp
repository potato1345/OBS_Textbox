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
#include <QDate>
#include <QDialog>
#include <QFileDialog>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>
#include <thread>

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
	root_layout->setSpacing(6);

	auto *button_row = new QHBoxLayout();
	button_row->setContentsMargins(8, 2, 8, 2);
	button_row->setSpacing(8);

	auto *add_btn = new QPushButton(QStringLiteral("＋ Neue Karte"), this);
	add_btn->setObjectName(QStringLiteral("lpBtnAccent"));

	auto *clear_btn = new QPushButton(QStringLiteral("🗑 Alle löschen"), this);
	clear_btn->setObjectName(QStringLiteral("lpBtnRed"));

	auto *import_btn = new QPushButton(QStringLiteral("📥 Import"), this);
	import_btn->setObjectName(QStringLiteral("lpBtnImport"));

	button_row->addWidget(add_btn);
	button_row->addWidget(clear_btn);
	button_row->addWidget(import_btn);
	button_row->addStretch(1);
	root_layout->addLayout(button_row);

	auto *button_row_m4 = new QHBoxLayout();
	button_row_m4->setContentsMargins(8, 2, 8, 2);
	button_row_m4->setSpacing(8);

	auto *quick_btn = new QPushButton(QStringLiteral("⭐ Schnell-Profile"), this);
	quick_btn->setObjectName(QStringLiteral("lpBtnQuickProfiles"));

	auto *backup_btn = new QPushButton(QStringLiteral("💾 Backup"), this);
	backup_btn->setObjectName(QStringLiteral("lpBtnBackup"));

	auto *restore_btn = new QPushButton(QStringLiteral("♻️ Restore"), this);
	restore_btn->setObjectName(QStringLiteral("lpBtnRestore"));

	auto *settings_btn = new QPushButton(QStringLiteral("⚙ Einstellungen"), this);
	settings_btn->setObjectName(QStringLiteral("lpBtnSettings"));

	button_row_m4->addWidget(quick_btn);
	button_row_m4->addWidget(backup_btn);
	button_row_m4->addWidget(restore_btn);
	button_row_m4->addWidget(settings_btn);
	button_row_m4->addStretch(1);
	root_layout->addLayout(button_row_m4);

	dock_status_lbl_ = new QLabel(this);
	dock_status_lbl_->setObjectName(QStringLiteral("lpDockStatus"));
	dock_status_lbl_->setContentsMargins(8, 0, 8, 0);
	dock_status_lbl_->setWordWrap(true);
	dock_status_lbl_->setVisible(false);
	root_layout->addWidget(dock_status_lbl_);

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
	connect(import_btn, &QPushButton::clicked, this, &LiedplanDock::on_open_import_dialog);
	connect(quick_btn, &QPushButton::clicked, this, &LiedplanDock::on_open_quick_profiles_dialog);
	connect(backup_btn, &QPushButton::clicked, this, &LiedplanDock::on_backup_state);
	connect(restore_btn, &QPushButton::clicked, this, &LiedplanDock::on_restore_state);
	connect(settings_btn, &QPushButton::clicked, this, &LiedplanDock::on_open_settings_dialog);

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

void LiedplanDock::sync_all_cards_live(const std::vector<std::string> &previous_ids)
{
	for (const auto &old_id : previous_ids) {
		if (!state_.find(old_id)) {
			liedplan_scene::remove_everywhere(old_id);
		}
	}
	for (const auto &card : state_.cards) {
		liedplan_scene::update_everywhere(card, false);
	}
}

void LiedplanDock::set_dock_status(const QString &msg, bool is_error)
{
	if (!dock_status_lbl_)
		return;
	dock_status_lbl_->setText(msg);
	dock_status_lbl_->setStyleSheet(
		is_error ? QStringLiteral("color: #E85D5D; font-size: 12px; font-weight: 600;")
			 : QStringLiteral("color: #B0B6BD; font-size: 12px;"));
	dock_status_lbl_->setVisible(!msg.isEmpty());
}

void LiedplanDock::apply_import_result(const liedplan_import::LiedplanImportResult &res)
{
	while (state_.cards.size() < static_cast<std::size_t>(kDefaultCardCount)) {
		state_.add_card();
	}

	const std::size_t count =
		std::min<std::size_t>(static_cast<std::size_t>(kDefaultCardCount), res.punkte.size());
	for (std::size_t i = 0; i < count; ++i) {
		state_.cards[i].zeile1 = res.punkte[i].gl;
		state_.cards[i].zeile2 = res.punkte[i].verse;
		state_.cards[i].zeile3 = res.punkte[i].titel;
	}

	save_state();
	for (std::size_t i = 0; i < count; ++i) {
		liedplan_scene::update_everywhere(state_.cards[i], false);
	}
	rebuild_rows();
}

void LiedplanDock::on_open_import_dialog()
{
	QDialog dlg(this);
	dlg.setObjectName(QStringLiteral("lpImportDialog"));
	dlg.setWindowTitle(QStringLiteral("Liedplan-Import"));
	dlg.setStyleSheet(liedplan_theme::stylesheet());
	dlg.setMinimumWidth(500);

	auto *root = new QVBoxLayout(&dlg);
	root->setContentsMargins(16, 16, 16, 16);
	root->setSpacing(12);

	// Gruppe 1: Von Google holen (+ Form-ID & Token-Pfad Einstellungen)
	auto *google_group = new QGroupBox(QStringLiteral("Von Google holen"), &dlg);
	auto *google_layout = new QVBoxLayout(google_group);
	google_layout->setContentsMargins(12, 16, 12, 12);
	google_layout->setSpacing(8);

	auto *google_info = new QLabel(
		QStringLiteral("Neueste Antwort aus dem Google-Formular laden"), google_group);
	google_info->setWordWrap(true);

	auto *lbl_form_id = new QLabel(QStringLiteral("FORM-ID"), google_group);
	lbl_form_id->setObjectName(QStringLiteral("lpFieldLabel"));
	const std::string init_form_id = state_.settings.google_form_id.empty()
						 ? liedplan_import::kDefaultFormId
						 : state_.settings.google_form_id;
	auto *edit_form_id =
		new QLineEdit(QString::fromUtf8(init_form_id.c_str()), google_group);
	edit_form_id->setPlaceholderText(QString::fromUtf8(liedplan_import::kDefaultFormId));

	auto *lbl_token_path = new QLabel(QStringLiteral("TOKEN-PFAD"), google_group);
	lbl_token_path->setObjectName(QStringLiteral("lpFieldLabel"));
	const std::string init_token_path = state_.settings.token_path.empty()
						    ? liedplan_import::kDefaultTokenPath
						    : state_.settings.token_path;
	auto *edit_token_path =
		new QLineEdit(QString::fromUtf8(init_token_path.c_str()), google_group);
	edit_token_path->setPlaceholderText(QString::fromUtf8(liedplan_import::kDefaultTokenPath));

	connect(edit_form_id, &QLineEdit::textChanged, this, [this](const QString &t) {
		state_.settings.google_form_id = t.trimmed().toUtf8().toStdString();
		if (state_.settings.google_form_id.empty())
			state_.settings.google_form_id = liedplan_import::kDefaultFormId;
		save_state();
	});

	connect(edit_token_path, &QLineEdit::textChanged, this, [this](const QString &t) {
		state_.settings.token_path = t.trimmed().toUtf8().toStdString();
		if (state_.settings.token_path.empty())
			state_.settings.token_path = liedplan_import::kDefaultTokenPath;
		save_state();
	});

	auto *api_btn = new QPushButton(QStringLiteral("📥 Neueste Antwort laden"), google_group);
	api_btn->setObjectName(QStringLiteral("lpBtnApiImport"));

	auto *status_lbl = new QLabel(QStringLiteral("Bereit."), google_group);
	status_lbl->setObjectName(QStringLiteral("lpImportStatus"));
	status_lbl->setWordWrap(true);

	google_layout->addWidget(google_info);
	google_layout->addWidget(lbl_form_id);
	google_layout->addWidget(edit_form_id);
	google_layout->addWidget(lbl_token_path);
	google_layout->addWidget(edit_token_path);
	google_layout->addWidget(api_btn);
	google_layout->addWidget(status_lbl);
	root->addWidget(google_group);

	// Gruppe 2: Datei-Import (CSV / XLSX / ZIP)
	auto *csv_group = new QGroupBox(QStringLiteral("Datei-Import (CSV / XLSX / ZIP)"), &dlg);
	auto *csv_layout = new QVBoxLayout(csv_group);
	csv_layout->setContentsMargins(12, 16, 12, 12);
	csv_layout->setSpacing(8);

	auto *csv_btn = new QPushButton(
		QStringLiteral("📂 Datei importieren (CSV / XLSX / ZIP) …"), csv_group);
	csv_btn->setObjectName(QStringLiteral("lpBtnCsvImport"));
	csv_layout->addWidget(csv_btn);
	root->addWidget(csv_group);

	// Gruppe 3: Kopfdaten (erst nach erfolgreichem Laden sichtbar)
	auto *kopf_group = new QGroupBox(QStringLiteral("Kopfdaten"), &dlg);
	auto *kopf_layout = new QVBoxLayout(kopf_group);
	kopf_layout->setContentsMargins(12, 16, 12, 12);
	kopf_layout->setSpacing(4);

	auto *lbl_woche = new QLabel(QStringLiteral("Woche: —"), kopf_group);
	lbl_woche->setObjectName(QStringLiteral("lpKopfWochen"));
	auto *lbl_tag = new QLabel(QStringLiteral("Tag: —"), kopf_group);
	lbl_tag->setObjectName(QStringLiteral("lpKopfTag"));
	auto *lbl_organist = new QLabel(QStringLiteral("Organist: —"), kopf_group);
	lbl_organist->setObjectName(QStringLiteral("lpKopfOrganist"));
	auto *lbl_zeitstempel = new QLabel(QStringLiteral("Zeitstempel: —"), kopf_group);
	lbl_zeitstempel->setObjectName(QStringLiteral("lpKopfZeitstempel"));

	kopf_layout->addWidget(lbl_woche);
	kopf_layout->addWidget(lbl_tag);
	kopf_layout->addWidget(lbl_organist);
	kopf_layout->addWidget(lbl_zeitstempel);
	kopf_group->setVisible(false);
	root->addWidget(kopf_group);

	// Ergebnis-/Fehler-Label unterhalb
	auto *error_lbl = new QLabel(&dlg);
	error_lbl->setObjectName(QStringLiteral("lpImportError"));
	error_lbl->setWordWrap(true);
	error_lbl->setVisible(false);
	root->addWidget(error_lbl);

	auto handle_success = [this, kopf_group, lbl_woche, lbl_tag, lbl_organist, lbl_zeitstempel,
			       status_lbl, error_lbl](
				      const liedplan_import::LiedplanImportResult &res,
				      const QString &source_name = QString()) {
		apply_import_result(res);
		lbl_woche->setText(QStringLiteral("Woche: ") +
				   QString::fromUtf8(res.woche.c_str()));
		lbl_tag->setText(QStringLiteral("Tag: ") + QString::fromUtf8(res.tag.c_str()));
		lbl_organist->setText(QStringLiteral("Organist: ") +
				      QString::fromUtf8(res.organist.c_str()));
		lbl_zeitstempel->setText(QStringLiteral("Zeitstempel: ") +
					 QString::fromUtf8(res.zeitstempel.c_str()));
		kopf_group->setVisible(true);
		error_lbl->clear();
		error_lbl->setVisible(false);
		const QString msg =
			source_name.isEmpty()
				? QStringLiteral(
					  "12 Karten befüllt — im Dock editierbar, „▶ Zu Szene“ legt sie in die Szene.")
				: QStringLiteral("Importiert: %1 Punkte (Quelle: %2)")
					  .arg(res.punkte.size())
					  .arg(source_name);
		status_lbl->setText(msg);
		set_dock_status(msg, false);
	};

	auto handle_error = [this, status_lbl, error_lbl](const std::string &err_msg) {
		const QString qerr = QString::fromUtf8(err_msg.c_str());
		error_lbl->setText(qerr);
		error_lbl->setVisible(true);
		status_lbl->setText(qerr);
		set_dock_status(qerr, true);
	};

	QPointer<QDialog> dlg_ptr(&dlg);
	QPointer<LiedplanDock> self_ptr(this);

	connect(api_btn, &QPushButton::clicked, &dlg,
		[this, dlg_ptr, self_ptr, api_btn, csv_btn, status_lbl, error_lbl, handle_success,
		 handle_error]() {
			api_btn->setEnabled(false);
			csv_btn->setEnabled(false);
			error_lbl->clear();
			error_lbl->setVisible(false);
			status_lbl->setText(QStringLiteral("Lade …"));

			std::filesystem::path cache_path;
			if (!state_path_.empty()) {
				std::error_code ec;
				cache_path = std::filesystem::u8path(state_path_).parent_path() /
					     "google_access_token.json";
			}

			const std::string form_id =
				state_.settings.google_form_id.empty()
					? liedplan_import::kDefaultFormId
					: state_.settings.google_form_id;
			const std::filesystem::path token_path = std::filesystem::u8path(
				state_.settings.token_path.empty()
					? liedplan_import::kDefaultTokenPath
					: state_.settings.token_path);

			std::thread([self_ptr, dlg_ptr, api_btn, csv_btn, cache_path, token_path,
				     form_id, handle_success, handle_error]() {
				std::string err_msg;
				auto res = liedplan_import::import_from_api(
					&err_msg, cache_path, token_path, form_id);

				if (!self_ptr)
					return;

				QMetaObject::invokeMethod(
					self_ptr.data(),
					[self_ptr, dlg_ptr, api_btn, csv_btn, res = std::move(res),
					 err_msg = std::move(err_msg), handle_success,
					 handle_error]() {
						if (!self_ptr || !dlg_ptr)
							return;
						api_btn->setEnabled(true);
						csv_btn->setEnabled(true);
						if (res) {
							handle_success(*res);
						} else {
							handle_error(err_msg);
						}
					},
					Qt::QueuedConnection);
			}).detach();
		});

	connect(csv_btn, &QPushButton::clicked, &dlg,
		[&dlg, dlg_ptr, self_ptr, api_btn, csv_btn, status_lbl, error_lbl, handle_success,
		 handle_error]() {
			const QString file_name = QFileDialog::getOpenFileName(
				&dlg, QStringLiteral("Liedplan-Datei importieren"), QString(),
				QStringLiteral(
					"CSV-Dateien (*.csv);;Excel-Dateien (*.xlsx);;ZIP-Archive (*.zip);;Alle Dateien (*.*)"));
			if (file_name.isEmpty())
				return;

			api_btn->setEnabled(false);
			csv_btn->setEnabled(false);
			error_lbl->clear();
			error_lbl->setVisible(false);
			status_lbl->setText(QStringLiteral("Importiere Datei …"));

			const std::filesystem::path path =
				std::filesystem::u8path(file_name.toUtf8().toStdString());
			const QString source_basename =
				QString::fromUtf8(path.filename().u8string().c_str());

			std::thread([self_ptr, dlg_ptr, api_btn, csv_btn, path, source_basename,
				     handle_success, handle_error]() {
				liedplan_import::LiedplanImportResult res;
				std::string err_msg;
				const auto err = liedplan_import::parse_answers_file(
					path, res, nullptr, &err_msg);

				if (!self_ptr)
					return;

				QMetaObject::invokeMethod(
					self_ptr.data(),
					[self_ptr, dlg_ptr, api_btn, csv_btn, err,
					 res = std::move(res), err_msg = std::move(err_msg),
					 source_basename, handle_success, handle_error]() {
						if (!self_ptr || !dlg_ptr)
							return;
						api_btn->setEnabled(true);
						csv_btn->setEnabled(true);
						if (err == liedplan_import::ImportError::None) {
							handle_success(res, source_basename);
						} else {
							handle_error(err_msg);
						}
					},
					Qt::QueuedConnection);
			}).detach();
		});

	dlg.exec();
}

void LiedplanDock::on_open_quick_profiles_dialog()
{
	QDialog dlg(this);
	dlg.setObjectName(QStringLiteral("lpQuickProfilesDialog"));
	dlg.setWindowTitle(QStringLiteral("Schnell-Profile"));
	dlg.setStyleSheet(liedplan_theme::stylesheet());
	dlg.setMinimumWidth(460);

	auto *root = new QVBoxLayout(&dlg);
	root->setContentsMargins(16, 16, 16, 16);
	root->setSpacing(12);

	auto make_default_name = [this]() -> QString {
		const int next_num = static_cast<int>(state_.quick_profiles.size()) + 1;
		const QString date_str = QDate::currentDate().toString(QStringLiteral("dd.MM."));
		return QStringLiteral("Profil %1 (%2)").arg(next_num).arg(date_str);
	};

	// Gruppe 1: Aktuellen Stand als Schnell-Profil speichern
	auto *save_group = new QGroupBox(QStringLiteral("Aktuellen Stand speichern"), &dlg);
	auto *save_layout = new QVBoxLayout(save_group);
	save_layout->setContentsMargins(12, 16, 12, 12);
	save_layout->setSpacing(8);

	auto *lbl_name = new QLabel(QStringLiteral("NAME"), save_group);
	lbl_name->setObjectName(QStringLiteral("lpFieldLabel"));

	auto *save_row = new QHBoxLayout();
	save_row->setSpacing(8);
	auto *name_edit = new QLineEdit(make_default_name(), save_group);
	name_edit->setPlaceholderText(QStringLiteral("Name des Schnell-Profils"));
	auto *save_btn = new QPushButton(QStringLiteral("Speichern"), save_group);
	save_btn->setObjectName(QStringLiteral("lpBtnQuickSave"));
	save_row->addWidget(name_edit, 1);
	save_row->addWidget(save_btn);

	save_layout->addWidget(lbl_name);
	save_layout->addLayout(save_row);
	root->addWidget(save_group);

	// Gruppe 2: Gespeicherte Schnell-Profile verwalten
	auto *list_group = new QGroupBox(QStringLiteral("Gespeicherte Schnell-Profile"), &dlg);
	auto *list_layout = new QVBoxLayout(list_group);
	list_layout->setContentsMargins(12, 16, 12, 12);
	list_layout->setSpacing(8);

	auto *combo = new QComboBox(list_group);
	combo->setFixedHeight(34);

	auto *action_row = new QHBoxLayout();
	action_row->setSpacing(8);
	auto *apply_btn = new QPushButton(QStringLiteral("▶ Übernehmen"), list_group);
	apply_btn->setObjectName(QStringLiteral("lpBtnQuickApply"));
	auto *del_btn = new QPushButton(QStringLiteral("🗑 Löschen"), list_group);
	del_btn->setObjectName(QStringLiteral("lpBtnQuickDelete"));
	action_row->addWidget(apply_btn, 1);
	action_row->addWidget(del_btn, 1);

	list_layout->addWidget(combo);
	list_layout->addLayout(action_row);
	root->addWidget(list_group);

	auto *status_lbl = new QLabel(&dlg);
	status_lbl->setObjectName(QStringLiteral("lpQuickStatus"));
	status_lbl->setWordWrap(true);
	root->addWidget(status_lbl);

	auto refresh_combo = [this, combo, apply_btn, del_btn]() {
		combo->clear();
		for (const auto &qp : state_.quick_profiles) {
			const QString label = QStringLiteral("%1 (%2 Karten)")
						      .arg(QString::fromUtf8(qp.name.c_str()))
						      .arg(qp.cards.size());
			combo->addItem(label);
		}
		const bool has_items = !state_.quick_profiles.empty();
		combo->setEnabled(has_items);
		apply_btn->setEnabled(has_items);
		del_btn->setEnabled(has_items);
	};

	refresh_combo();

	connect(save_btn, &QPushButton::clicked, &dlg,
		[this, name_edit, status_lbl, refresh_combo, make_default_name]() {
			const QString qname = name_edit->text().trimmed();
			const std::string name_utf8 =
				(qname.isEmpty() ? make_default_name() : qname).toUtf8().toStdString();
			state_.save_quick_profile(name_utf8);
			save_state();
			refresh_combo();
			name_edit->setText(make_default_name());
			const QString msg = QStringLiteral("Schnell-Profil gespeichert: %1")
						    .arg(QString::fromUtf8(name_utf8.c_str()));
			status_lbl->setText(msg);
			set_dock_status(msg, false);
		});

	connect(apply_btn, &QPushButton::clicked, &dlg,
		[this, combo, status_lbl]() {
			const int idx = combo->currentIndex();
			if (idx < 0 || static_cast<std::size_t>(idx) >= state_.quick_profiles.size())
				return;

			std::vector<std::string> prev_ids;
			prev_ids.reserve(state_.cards.size());
			for (const auto &c : state_.cards)
				prev_ids.push_back(c.id);

			const std::string prof_name =
				state_.quick_profiles[static_cast<std::size_t>(idx)].name;
			if (state_.apply_quick_profile(static_cast<std::size_t>(idx))) {
				save_state();
				sync_all_cards_live(prev_ids);
				rebuild_rows();
				const QString msg =
					QStringLiteral("Schnell-Profil übernommen: %1")
						.arg(QString::fromUtf8(prof_name.c_str()));
				status_lbl->setText(msg);
				set_dock_status(msg, false);
			}
		});

	connect(del_btn, &QPushButton::clicked, &dlg,
		[this, combo, status_lbl, refresh_combo]() {
			const int idx = combo->currentIndex();
			if (idx < 0 || static_cast<std::size_t>(idx) >= state_.quick_profiles.size())
				return;

			const std::string prof_name =
				state_.quick_profiles[static_cast<std::size_t>(idx)].name;
			if (state_.remove_quick_profile(static_cast<std::size_t>(idx))) {
				save_state();
				refresh_combo();
				const QString msg =
					QStringLiteral("Schnell-Profil gelöscht: %1")
						.arg(QString::fromUtf8(prof_name.c_str()));
				status_lbl->setText(msg);
				set_dock_status(msg, false);
			}
		});

	dlg.exec();
}

void LiedplanDock::on_backup_state()
{
	const QString file_name = QFileDialog::getSaveFileName(
		this, QStringLiteral("JSON-Backup speichern"),
		QStringLiteral("liedplan-backup.json"),
		QStringLiteral("JSON-Backup (*.json)"));
	if (file_name.isEmpty())
		return;

	const std::string out_path = file_name.toUtf8().toStdString();
	const LiedplanState snap = state_;
	QPointer<LiedplanDock> self_ptr(this);

	std::thread([self_ptr, snap, out_path, file_name]() {
		const std::string err = snap.save(out_path);
		if (!self_ptr)
			return;
		QMetaObject::invokeMethod(
			self_ptr.data(),
			[self_ptr, err, file_name]() {
				if (!self_ptr)
					return;
				if (err.empty()) {
					self_ptr->set_dock_status(
						QStringLiteral("Backup nach %1").arg(file_name),
						false);
				} else {
					self_ptr->set_dock_status(
						QStringLiteral("Backup fehlgeschlagen: %1")
							.arg(QString::fromUtf8(err.c_str())),
						true);
				}
			},
			Qt::QueuedConnection);
	}).detach();
}

void LiedplanDock::on_restore_state()
{
	const QString file_name = QFileDialog::getOpenFileName(
		this, QStringLiteral("JSON-Backup wiederherstellen"), QString(),
		QStringLiteral("JSON-Backup (*.json)"));
	if (file_name.isEmpty())
		return;

	const std::string in_path = file_name.toUtf8().toStdString();
	QPointer<LiedplanDock> self_ptr(this);

	std::thread([self_ptr, in_path, file_name]() {
		LiedplanState restored;
		const std::string err = restored.restore_backup(in_path);
		if (!self_ptr)
			return;
		QMetaObject::invokeMethod(
			self_ptr.data(),
			[self_ptr, err, restored = std::move(restored), file_name]() mutable {
				if (!self_ptr)
					return;
				if (!err.empty()) {
					self_ptr->set_dock_status(
						QStringLiteral("Restore fehlgeschlagen: %1")
							.arg(QString::fromUtf8(err.c_str())),
						true);
					return;
				}

				std::vector<std::string> prev_ids;
				prev_ids.reserve(self_ptr->state_.cards.size());
				for (const auto &c : self_ptr->state_.cards)
					prev_ids.push_back(c.id);

				self_ptr->state_ = std::move(restored);
				self_ptr->save_state();
				self_ptr->sync_all_cards_live(prev_ids);
				self_ptr->rebuild_rows();
				self_ptr->set_dock_status(
					QStringLiteral("Restore erfolgreich: %1").arg(file_name),
					false);
			},
			Qt::QueuedConnection);
	}).detach();
}

void LiedplanDock::on_open_settings_dialog()
{
	QDialog dlg(this);
	dlg.setObjectName(QStringLiteral("lpSettingsDialog"));
	dlg.setWindowTitle(QStringLiteral("Einstellungen"));
	dlg.setStyleSheet(liedplan_theme::stylesheet());
	dlg.setMinimumWidth(480);

	auto *root = new QVBoxLayout(&dlg);
	root->setContentsMargins(16, 16, 16, 16);
	root->setSpacing(12);

	auto *oauth_group = new QGroupBox(QStringLiteral("Google-Forms-API & OAuth"), &dlg);
	auto *oauth_layout = new QVBoxLayout(oauth_group);
	oauth_layout->setContentsMargins(12, 16, 12, 12);
	oauth_layout->setSpacing(8);

	auto *lbl_form = new QLabel(QStringLiteral("GOOGLE-FORM-ID"), oauth_group);
	lbl_form->setObjectName(QStringLiteral("lpFieldLabel"));
	const std::string cur_form_id = state_.settings.google_form_id.empty()
						? liedplan_import::kDefaultFormId
						: state_.settings.google_form_id;
	auto *edit_form = new QLineEdit(QString::fromUtf8(cur_form_id.c_str()), oauth_group);
	edit_form->setPlaceholderText(QString::fromUtf8(liedplan_import::kDefaultFormId));

	auto *lbl_token = new QLabel(QStringLiteral("OAUTH-TOKEN-PFAD"), oauth_group);
	lbl_token->setObjectName(QStringLiteral("lpFieldLabel"));
	const std::string cur_token = state_.settings.token_path.empty()
					      ? liedplan_import::kDefaultTokenPath
					      : state_.settings.token_path;
	auto *token_row = new QHBoxLayout();
	token_row->setSpacing(8);
	auto *edit_token = new QLineEdit(QString::fromUtf8(cur_token.c_str()), oauth_group);
	edit_token->setPlaceholderText(QString::fromUtf8(liedplan_import::kDefaultTokenPath));
	auto *browse_btn = new QPushButton(QStringLiteral("📂 …"), oauth_group);
	browse_btn->setObjectName(QStringLiteral("lpBtn"));
	token_row->addWidget(edit_token, 1);
	token_row->addWidget(browse_btn);

	oauth_layout->addWidget(lbl_form);
	oauth_layout->addWidget(edit_form);
	oauth_layout->addWidget(lbl_token);
	oauth_layout->addLayout(token_row);
	root->addWidget(oauth_group);

	auto *status_lbl = new QLabel(&dlg);
	status_lbl->setObjectName(QStringLiteral("lpSettingsStatus"));
	status_lbl->setWordWrap(true);
	root->addWidget(status_lbl);

	auto *btn_row = new QHBoxLayout();
	btn_row->setSpacing(8);
	auto *reset_btn = new QPushButton(QStringLiteral("Standardwerte"), &dlg);
	reset_btn->setObjectName(QStringLiteral("lpBtn"));
	auto *save_btn = new QPushButton(QStringLiteral("Speichern"), &dlg);
	save_btn->setObjectName(QStringLiteral("lpBtnAccent"));
	btn_row->addWidget(reset_btn);
	btn_row->addStretch(1);
	btn_row->addWidget(save_btn);
	root->addLayout(btn_row);

	connect(browse_btn, &QPushButton::clicked, &dlg, [&dlg, edit_token]() {
		const QString chosen = QFileDialog::getOpenFileName(
			&dlg, QStringLiteral("Google-OAuth-Token-Datei wählen"),
			edit_token->text(), QStringLiteral("JSON-Dateien (*.json);;Alle Dateien (*.*)"));
		if (!chosen.isEmpty())
			edit_token->setText(chosen);
	});

	connect(reset_btn, &QPushButton::clicked, &dlg, [edit_form, edit_token]() {
		edit_form->setText(QString::fromUtf8(liedplan_import::kDefaultFormId));
		edit_token->setText(QString::fromUtf8(liedplan_import::kDefaultTokenPath));
	});

	connect(save_btn, &QPushButton::clicked, &dlg,
		[this, &dlg, edit_form, edit_token]() {
			std::string fid = edit_form->text().trimmed().toUtf8().toStdString();
			std::string tpath = edit_token->text().trimmed().toUtf8().toStdString();
			state_.settings.google_form_id =
				fid.empty() ? liedplan_import::kDefaultFormId : fid;
			state_.settings.token_path =
				tpath.empty() ? liedplan_import::kDefaultTokenPath : tpath;
			save_state();
			set_dock_status(QStringLiteral("Einstellungen gespeichert."), false);
			dlg.accept();
		});

	dlg.exec();
}

void LiedplanDock::save_state()
{
	if (!state_path_.empty())
		state_.save(state_path_);
}
