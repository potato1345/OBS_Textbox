/*
 * liedplan-dock.cpp — Qt-basiertes Hauptbedienpanel für Liedplan in OBS Studio (v3).
 *
 * Zweck:
 *   - Implementiert die Benutzeroberfläche LiedplanDock für das OBS Studio QDockWidget.
 *   - Verwaltet die Karten-Eingabezeilen, Schnell-Profile, Farben und Deckkraft.
 *   - Steuert Import-Workflows (Google Forms API, CSV/XLSX/ZIP-Upload, AI/LLM-Analyse).
 *   - Synchronisiert Karteninhalte und Aktionen mit OBS-Szenen via liedplan_scene.
 */

#ifndef _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#endif

#include "liedplan-dock.hpp"
#include "liedplan-profiles.hpp"
#include "liedplan-scene.hpp"
#include "liedplan-theme-icons.hpp"
#include "liedplan-theme.hpp"
#include "liedplan-log.hpp"

#include <obs.h>

#include <QAction>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QDate>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <filesystem>
#include <memory>
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

	/*
	Ui4: OBS-Theme-Icons ausliefern — in der DLL eingebettet, beim Dock-Build
	nach <config>/liedplan-for-obs/icons/ (neben liedplan-state.json) geschrieben.
	Version-Gated: No-Op, wenn der Satz aktuell ist (s. liedplan-theme-icons.hpp).
	Bei Fehlschlag: Dock ohne Icons (QSS ohne Icon-Regeln) — nicht fatal.
	*/
	if (!state_path_.empty()) {
		const std::filesystem::path icons_dir =
			std::filesystem::u8path(state_path_).parent_path() / "icons";
		std::string icon_err;
		if (liedplan_theme_icons::write_icons(icons_dir, false, &icon_err)) {
			const auto u8 = icons_dir.u8string();
			icon_dir_ = QString::fromUtf8(u8.data(), static_cast<int>(u8.size()));
		} else {
			blog(LOG_WARNING, "Liedplan: Icons konnten nicht geschrieben werden: %s",
			     icon_err.c_str());
		}
	}
	setStyleSheet(liedplan_theme::stylesheet(icon_dir_));

	auto *root_layout = new QVBoxLayout(this);
	root_layout->setContentsMargins(4, 4, 4, 4);
	root_layout->setSpacing(4);

	auto *top_row = new QHBoxLayout();
	top_row->setContentsMargins(4, 0, 4, 2);
	top_row->setSpacing(8);

	profil_select_ = new QComboBox(this);
	profil_select_->setEditable(true);
	profil_select_->setInsertPolicy(QComboBox::NoInsert);
	profil_select_->setMinimumWidth(160);
	if (QCompleter *c = profil_select_->completer()) {
		c->setCaseSensitivity(Qt::CaseInsensitive);
		c->setFilterMode(Qt::MatchContains);
	}
	if (profil_select_->lineEdit()) {
		profil_select_->lineEdit()->setPlaceholderText(QStringLiteral("Profil suchen…"));
	}

	card_search_ = new QComboBox(this);
	card_search_->setEditable(true);
	card_search_->setInsertPolicy(QComboBox::NoInsert);
	card_search_->setMinimumWidth(140);
	if (QCompleter *c = card_search_->completer()) {
		c->setCaseSensitivity(Qt::CaseInsensitive);
		c->setFilterMode(Qt::MatchContains);
	}
	if (card_search_->lineEdit()) {
		card_search_->lineEdit()->setPlaceholderText(QStringLiteral("Karte suchen…"));
	}

	top_row->addWidget(profil_select_, 1);
	profil_del_btn_ = new QToolButton(this);
	profil_del_btn_->setObjectName(QStringLiteral("lpProfilDelBtn"));
	profil_del_btn_->setFixedSize(28, 28);
	profil_del_btn_->setAutoRaise(true);
	profil_del_btn_->setIcon(theme_icon("trash.svg"));
	profil_del_btn_->setIconSize(QSize(16, 16));
	profil_del_btn_->setToolTip(QStringLiteral("Gewähltes Schnell-Profil löschen"));
	profil_del_btn_->setEnabled(false);
	top_row->addWidget(profil_del_btn_);
	top_row->addWidget(card_search_, 1);
	root_layout->addLayout(top_row);

	scroll_area_ = new QScrollArea(this);
	scroll_area_->setWidgetResizable(true);
	scroll_area_->setFrameShape(QFrame::NoFrame);

	auto *cards_container = new QWidget(scroll_area_);
	cards_container->setObjectName(QStringLiteral("lpCardsContainer"));
	cards_container->setAttribute(Qt::WA_StyledBackground, true);

	cards_layout_ = new QVBoxLayout(cards_container);
	cards_layout_->setContentsMargins(4, 4, 4, 4);
	cards_layout_->setSpacing(8);
	cards_layout_->addStretch(1);

	scroll_area_->setWidget(cards_container);
	root_layout->addWidget(scroll_area_, 1);

	dock_status_lbl_ = new QLabel(this);
	dock_status_lbl_->setObjectName(QStringLiteral("lpDockStatus"));
	dock_status_lbl_->setContentsMargins(4, 0, 4, 0);
	dock_status_lbl_->setWordWrap(true);
	dock_status_lbl_->setVisible(false);
	root_layout->addWidget(dock_status_lbl_);

	auto *bottom_toolbar = new QHBoxLayout();
	bottom_toolbar->setContentsMargins(4, 2, 4, 4);
	bottom_toolbar->setSpacing(6);

	auto make_tool_btn = [this](const QString &obj_name, const char *icon_name,
				    const QString &tooltip) -> QToolButton * {
		auto *btn = new QToolButton(this);
		btn->setObjectName(obj_name);
		btn->setFixedSize(32, 32);
		btn->setAutoRaise(true);
		btn->setIcon(theme_icon(icon_name));
		btn->setIconSize(QSize(16, 16));
		btn->setToolTip(tooltip);
		return btn;
	};

	auto *tool_add = make_tool_btn(QStringLiteral("lpToolAdd"), "plus.svg",
				       QStringLiteral("Neue Karte"));
	auto *tool_clear = make_tool_btn(QStringLiteral("lpToolClear"), "trash.svg",
					 QStringLiteral("Alle Karten löschen"));
	auto *tool_import = make_tool_btn(
		QStringLiteral("lpToolImport"), "download.svg",
		QStringLiteral("AI-Import / Datei-Import (CSV, XLSX, ZIP, Google)"));
	auto *tool_settings = make_tool_btn(
		QStringLiteral("lpToolSettings"), "cogs.svg",
		QStringLiteral("Schnell-Profile / Einstellungen / Backup / Restore"));
	tool_settings->setPopupMode(QToolButton::InstantPopup);

	auto *settings_menu = new QMenu(tool_settings);
	QAction *act_quick = settings_menu->addAction(QStringLiteral("Schnell-Profile…"));
	settings_menu->addSeparator();
	QAction *act_settings = settings_menu->addAction(QStringLiteral("Einstellungen…"));
	settings_menu->addSeparator();
	QAction *act_backup = settings_menu->addAction(QStringLiteral("Backup"));
	QAction *act_restore = settings_menu->addAction(QStringLiteral("Restore"));
	tool_settings->setMenu(settings_menu);

	bottom_toolbar->addWidget(tool_add);
	bottom_toolbar->addWidget(tool_clear);
	bottom_toolbar->addWidget(tool_import);
	bottom_toolbar->addWidget(tool_settings);
	bottom_toolbar->addStretch(1);
	root_layout->addLayout(bottom_toolbar);

	connect(tool_add, &QToolButton::clicked, this, &LiedplanDock::on_add_card);
	connect(tool_clear, &QToolButton::clicked, this, &LiedplanDock::on_clear_all);
	connect(tool_import, &QToolButton::clicked, this, &LiedplanDock::on_open_import_dialog);
	connect(act_quick, &QAction::triggered, this, &LiedplanDock::on_open_quick_profiles_dialog);
	connect(act_settings, &QAction::triggered, this, &LiedplanDock::on_open_settings_dialog);
	connect(act_backup, &QAction::triggered, this, &LiedplanDock::on_backup_state);
	connect(act_restore, &QAction::triggered, this, &LiedplanDock::on_restore_state);

	connect(profil_select_, QOverload<int>::of(&QComboBox::activated), this, [this](int idx) {
		if (idx <= 0)
			return;
		const QString prof_name = profil_select_->itemData(idx).toString();
		if (!prof_name.isEmpty())
			apply_quick_profile(prof_name.toUtf8().toStdString());
	});
	if (profil_select_->lineEdit()) {
		connect(profil_select_->lineEdit(), &QLineEdit::returnPressed, this, [this]() {
			const QString query = profil_select_->currentText().trimmed();
			if (query.isEmpty() || query == QStringLiteral("(keines)"))
				return;
			for (const auto &qp : state_.quick_profiles) {
				if (QString::fromUtf8(qp.name.c_str())
					    .compare(query, Qt::CaseInsensitive) == 0) {
					apply_quick_profile(qp.name);
					return;
				}
			}
		});
	}

	sync_profil_del_state();
	connect(profil_select_, QOverload<int>::of(&QComboBox::currentIndexChanged),
		this, [this](int) { sync_profil_del_state(); });
	connect(profil_del_btn_, &QToolButton::clicked, this, [this]() {
		const int idx = profil_select_->currentIndex();
		if (idx <= 0 || idx >= profil_select_->count())
			return;
		const QString prof_name = profil_select_->itemData(idx).toString();
		if (prof_name.isEmpty())
			return;
		if (state_.remove_quick_profile(static_cast<std::size_t>(idx) - 1)) {
			if (state_.active_profile == prof_name.toUtf8().toStdString())
				state_.active_profile.clear();
			save_state();
			refresh_top_row();
			const QString msg =
				QStringLiteral("Schnell-Profil gelöscht: %1")
					.arg(prof_name);
			set_dock_status(msg, false);
		}
	});

	connect(card_search_, QOverload<int>::of(&QComboBox::activated), this, [this](int idx) {
		if (idx < 0)
			return;
		const QString id_q = card_search_->itemData(idx).toString();
		if (!id_q.isEmpty())
			flash_row(id_q.toUtf8().toStdString());
	});
	if (card_search_->lineEdit()) {
		connect(card_search_->lineEdit(), &QLineEdit::returnPressed, this, [this]() {
			const QString query = card_search_->currentText().trimmed();
			if (query.isEmpty())
				return;
			int match_idx = -1;
			for (int i = 0; i < card_search_->count(); ++i) {
				if (card_search_->itemData(i).toString().compare(
					    query, Qt::CaseInsensitive) == 0 ||
				    card_search_->itemText(i).compare(query, Qt::CaseInsensitive) ==
					    0) {
					match_idx = i;
					break;
				}
			}
			if (match_idx < 0) {
				for (int i = 0; i < card_search_->count(); ++i) {
					if (card_search_->itemText(i).contains(
						    query, Qt::CaseInsensitive)) {
						match_idx = i;
						break;
					}
				}
			}
			if (match_idx >= 0) {
				const QString id_q = card_search_->itemData(match_idx).toString();
				flash_row(id_q.toUtf8().toStdString());
			}
		});
	}

	bool file_exists = false;
	if (!state_path_.empty()) {
		std::error_code ec;
		file_exists = std::filesystem::exists(std::filesystem::u8path(state_path_), ec);
	}

	state_.load(state_path_);
	blog(LOG_INFO, "[liedplan] Zustand geladen (%zu Karten, Datei: %s)", state_.cards.size(), state_path_.c_str());

	if (!file_exists) {
		for (int i = 0; i < kDefaultCardCount; ++i) {
			state_.add_card();
		}
		save_state();
	}

	rebuild_rows();
	setMinimumSize(360, 240);
}

LiedplanCard *LiedplanDock::find_card_mut(const std::string &id)
{
	for (auto &c : state_.cards) {
		if (c.id == id)
			return &c;
	}
	return nullptr;
}

QIcon LiedplanDock::theme_icon(const char *name) const
{
	if (icon_dir_.isEmpty())
		return QIcon();
	return QIcon(icon_dir_ + QLatin1Char('/') + QLatin1String(name));
}

void LiedplanDock::create_row_widget(const LiedplanCard &card)
{
	auto *row_widget = new QFrame(this);
	row_widget->setObjectName(QStringLiteral("lpCardOuter"));
	row_widget->setProperty("lpCardId", QString::fromUtf8(card.id.c_str()));
	row_widget->setMinimumWidth(320);
	row_widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	auto *outer_layout = new QVBoxLayout(row_widget);
	outer_layout->setContentsMargins(0, 0, 0, 0);
	outer_layout->setSpacing(0);

	auto *card_inner = new QFrame(row_widget);
	card_inner->setObjectName(QStringLiteral("lpCardInner"));
	outer_layout->addWidget(card_inner);

	auto *inner_layout = new QVBoxLayout(card_inner);
	inner_layout->setContentsMargins(10, 10, 10, 10);
	inner_layout->setSpacing(8);

	auto *id_bar = new QFrame(card_inner);
	id_bar->setObjectName(QStringLiteral("lpIdBar"));
	auto *id_layout = new QHBoxLayout(id_bar);
	id_layout->setContentsMargins(8, 4, 8, 4);
	id_layout->setSpacing(8);

	auto *id_prefix = new QLabel(QStringLiteral("ID"), id_bar);
	id_prefix->setObjectName(QStringLiteral("lpIdPrefix"));

	auto *id_label = new QLabel(QString::fromUtf8(card.id.c_str()), id_bar);
	id_label->setObjectName(QStringLiteral("lpIdValue"));

	auto *style_toggle_btn = new QToolButton(id_bar);
	style_toggle_btn->setObjectName(QStringLiteral("lpStyleToggle"));
	style_toggle_btn->setFixedSize(28, 28);
	style_toggle_btn->setAutoRaise(true);
	style_toggle_btn->setIcon(theme_icon("chevron_down.svg"));
	style_toggle_btn->setIconSize(QSize(16, 16));
	style_toggle_btn->setToolTip(QStringLiteral("Stil-Optionen einklappen"));

	auto *del_btn = new QPushButton(id_bar);
	del_btn->setObjectName(QStringLiteral("lpDelBtn"));
	del_btn->setFixedSize(28, 28);
	del_btn->setIcon(theme_icon("trash.svg"));
	del_btn->setIconSize(QSize(16, 16));
	del_btn->setToolTip(QStringLiteral("Karte löschen"));

	id_layout->addWidget(id_prefix);
	id_layout->addWidget(id_label);
	id_layout->addStretch(1);
	id_layout->addWidget(style_toggle_btn);
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

	auto *style_section = new QFrame(card_inner);
	style_section->setObjectName(QStringLiteral("lpStyleSection"));
	auto *style_layout = new QVBoxLayout(style_section);
	style_layout->setContentsMargins(0, 0, 0, 0);
	style_layout->setSpacing(8);

	auto *prof_layout = new QVBoxLayout();
	prof_layout->setContentsMargins(0, 0, 0, 0);
	prof_layout->setSpacing(4);

	auto *lbl_prof = new QLabel(QStringLiteral("PROFIL"), style_section);
	lbl_prof->setObjectName(QStringLiteral("lpFieldLabel"));

	auto *prof_row = new QHBoxLayout();
	prof_row->setContentsMargins(0, 0, 0, 0);
	prof_row->setSpacing(6);

	/*
	 * M6-B2: Dropdown füllt mit 6 Standard-Profilen + Custom-Profilen aus
	 * state_.color_profiles. Zeigt card.profile auf ein gelöschtes Profil,
	 * bleibt das harmlos: die auf der Karte gebakten Farben (text_color /
	 * box_color) werden NICHT automatisch zurückgesetzt.
	 */
	auto *profile_combo = new QComboBox(style_section);
	profile_combo->setFixedHeight(34);
	int selected_index = 0;
	const auto &profiles = liedplan_profiles();
	for (size_t i = 0; i < profiles.size(); ++i) {
		profile_combo->addItem(QString::fromUtf8(profiles[i].name.c_str()));
		if (profiles[i].name == card.profile)
			selected_index = static_cast<int>(i);
	}
	for (size_t i = 0; i < state_.color_profiles.size(); ++i) {
		const auto &cp = state_.color_profiles[i];
		profile_combo->addItem(QString::fromUtf8(cp.name.c_str()));
		if (cp.name == card.profile)
			selected_index = static_cast<int>(profiles.size() + i);
	}
	profile_combo->setCurrentIndex(selected_index);

	auto *new_prof_btn = new QPushButton(QStringLiteral("Profil +"), style_section);
	new_prof_btn->setObjectName(QStringLiteral("lpBtnNewProfile"));
	new_prof_btn->setFixedHeight(34);
	new_prof_btn->setToolTip(
		QStringLiteral("Aktuelle Text-/Box-Farbe dieser Karte als Profil speichern"));

	prof_row->addWidget(profile_combo, 1);
	prof_row->addWidget(new_prof_btn);

	prof_layout->addWidget(lbl_prof);
	prof_layout->addLayout(prof_row);
	style_layout->addLayout(prof_layout);

	// M6-A8: Freie Text-/Box-Farbe (Color-Picker). PROFIL-Dropdown bleibt
	// "Reset auf Profilsatz" und aktualisiert die Button-Labels (s. Connect unten).
	const QString hex_text =
		QString::fromUtf8((card.text_color.empty() ? "#000000" : card.text_color).c_str()).toUpper();
	const QString hex_box =
		QString::fromUtf8((card.box_color.empty() ? "#ffffff" : card.box_color).c_str()).toUpper();

	auto *text_color_layout = new QVBoxLayout();
	text_color_layout->setContentsMargins(0, 0, 0, 0);
	text_color_layout->setSpacing(4);
	auto *lbl_text_color = new QLabel(QStringLiteral("TEXT-FARBE"), style_section);
	lbl_text_color->setObjectName(QStringLiteral("lpFieldLabel"));
	auto *text_color_btn = new QPushButton(hex_text, style_section);
	text_color_btn->setObjectName(QStringLiteral("lpTextColor"));
	text_color_btn->setFixedHeight(34);
	text_color_layout->addWidget(lbl_text_color);
	text_color_layout->addWidget(text_color_btn);
	style_layout->addLayout(text_color_layout);

	auto *box_color_layout = new QVBoxLayout();
	box_color_layout->setContentsMargins(0, 0, 0, 0);
	box_color_layout->setSpacing(4);
	auto *lbl_box_color = new QLabel(QStringLiteral("BOX-FARBE"), style_section);
	lbl_box_color->setObjectName(QStringLiteral("lpFieldLabel"));
	auto *box_color_btn = new QPushButton(hex_box, style_section);
	box_color_btn->setObjectName(QStringLiteral("lpBoxColor"));
	box_color_btn->setFixedHeight(34);
	box_color_layout->addWidget(lbl_box_color);
	box_color_layout->addWidget(box_color_btn);
	style_layout->addLayout(box_color_layout);

	const QString hex_a =
		QString::fromUtf8((card.corner_color_a.empty() ? "#fce647" : card.corner_color_a).c_str()).toUpper();
	const QString hex_b =
		QString::fromUtf8((card.corner_color_b.empty() ? "#fce647" : card.corner_color_b).c_str()).toUpper();

	auto *corner_a_layout = new QVBoxLayout();
	corner_a_layout->setContentsMargins(0, 0, 0, 0);
	corner_a_layout->setSpacing(4);
	auto *lbl_corner_a = new QLabel(QStringLiteral("ECKE A"), style_section);
	lbl_corner_a->setObjectName(QStringLiteral("lpFieldLabel"));
	auto *color_a_btn = new QPushButton(hex_a, style_section);
	color_a_btn->setObjectName(QStringLiteral("lpColorA"));
	color_a_btn->setFixedHeight(34);
	corner_a_layout->addWidget(lbl_corner_a);
	corner_a_layout->addWidget(color_a_btn);
	style_layout->addLayout(corner_a_layout);

	auto *corner_b_layout = new QVBoxLayout();
	corner_b_layout->setContentsMargins(0, 0, 0, 0);
	corner_b_layout->setSpacing(4);
	auto *lbl_corner_b = new QLabel(QStringLiteral("ECKE B"), style_section);
	lbl_corner_b->setObjectName(QStringLiteral("lpFieldLabel"));
	auto *color_b_btn = new QPushButton(hex_b, style_section);
	color_b_btn->setObjectName(QStringLiteral("lpColorB"));
	color_b_btn->setFixedHeight(34);
	corner_b_layout->addWidget(lbl_corner_b);
	corner_b_layout->addWidget(color_b_btn);
	style_layout->addLayout(corner_b_layout);

	auto *opacity_layout = new QVBoxLayout();
	opacity_layout->setContentsMargins(0, 0, 0, 0);
	opacity_layout->setSpacing(4);
	auto *lbl_opacity = new QLabel(QStringLiteral("ECKEN-DECKKRAFT"), style_section);
	lbl_opacity->setObjectName(QStringLiteral("lpFieldLabel"));
	opacity_layout->addWidget(lbl_opacity);

	auto *opacity_a_row = new QHBoxLayout();
	opacity_a_row->setContentsMargins(0, 0, 0, 0);
	opacity_a_row->setSpacing(8);
	auto *lbl_op_a = new QLabel(QStringLiteral("A"), style_section);
	lbl_op_a->setObjectName(QStringLiteral("lpFieldLabel"));
	auto *opacity_a_slider = new QSlider(Qt::Horizontal, style_section);
	opacity_a_slider->setObjectName(QStringLiteral("lpOpacityA"));
	opacity_a_slider->setRange(0, 255);
	opacity_a_slider->setValue(std::clamp(card.corner_opacity_a, 0, 255));
	opacity_a_row->addWidget(lbl_op_a);
	opacity_a_row->addWidget(opacity_a_slider, 1);
	opacity_layout->addLayout(opacity_a_row);

	auto *opacity_b_row = new QHBoxLayout();
	opacity_b_row->setContentsMargins(0, 0, 0, 0);
	opacity_b_row->setSpacing(8);
	auto *lbl_op_b = new QLabel(QStringLiteral("B"), style_section);
	lbl_op_b->setObjectName(QStringLiteral("lpFieldLabel"));
	auto *opacity_b_slider = new QSlider(Qt::Horizontal, style_section);
	opacity_b_slider->setObjectName(QStringLiteral("lpOpacityB"));
	opacity_b_slider->setRange(0, 255);
	opacity_b_slider->setValue(std::clamp(card.corner_opacity_b, 0, 255));
	opacity_b_row->addWidget(lbl_op_b);
	opacity_b_row->addWidget(opacity_b_slider, 1);
	opacity_layout->addLayout(opacity_b_row);

	style_layout->addLayout(opacity_layout);

	// M6-A7: Text-/Box-Deckkraft (0–255). Alpha wandert über die rgba()-Werte von
	// --box-bg/--text-color in build_inject_css/push_update; --box-opacity (Show/Hide)
	// bleibt has_any_text-gesteuert und wird HIER nicht verändert.
	auto *textbox_opacity_layout = new QVBoxLayout();
	textbox_opacity_layout->setContentsMargins(0, 0, 0, 0);
	textbox_opacity_layout->setSpacing(4);
	auto *lbl_textbox_opacity = new QLabel(QStringLiteral("TEXT-/BOX-DECKKRAFT"), style_section);
	lbl_textbox_opacity->setObjectName(QStringLiteral("lpFieldLabel"));
	textbox_opacity_layout->addWidget(lbl_textbox_opacity);

	auto *text_opacity_row = new QHBoxLayout();
	text_opacity_row->setContentsMargins(0, 0, 0, 0);
	text_opacity_row->setSpacing(8);
	auto *lbl_op_text = new QLabel(QStringLiteral("TEXT"), style_section);
	lbl_op_text->setObjectName(QStringLiteral("lpFieldLabel"));
	auto *text_opacity_slider = new QSlider(Qt::Horizontal, style_section);
	text_opacity_slider->setObjectName(QStringLiteral("lpOpacityText"));
	text_opacity_slider->setRange(0, 255);
	text_opacity_slider->setValue(std::clamp(card.text_opacity, 0, 255));
	text_opacity_row->addWidget(lbl_op_text);
	text_opacity_row->addWidget(text_opacity_slider, 1);
	textbox_opacity_layout->addLayout(text_opacity_row);

	auto *box_opacity_row = new QHBoxLayout();
	box_opacity_row->setContentsMargins(0, 0, 0, 0);
	box_opacity_row->setSpacing(8);
	auto *lbl_op_box = new QLabel(QStringLiteral("BOX"), style_section);
	lbl_op_box->setObjectName(QStringLiteral("lpFieldLabel"));
	auto *box_opacity_slider = new QSlider(Qt::Horizontal, style_section);
	box_opacity_slider->setObjectName(QStringLiteral("lpOpacityBox"));
	box_opacity_slider->setRange(0, 255);
	box_opacity_slider->setValue(std::clamp(card.box_opacity, 0, 255));
	box_opacity_slider->setToolTip(
		QStringLiteral("0 = Box-Hintergrund unsichtbar (Text und Ecken bleiben sichtbar)"));
	box_opacity_row->addWidget(lbl_op_box);
	box_opacity_row->addWidget(box_opacity_slider, 1);
	textbox_opacity_layout->addLayout(box_opacity_row);

	style_layout->addLayout(textbox_opacity_layout);

	auto *rainbow_cb = new QCheckBox(QStringLiteral("Regenbogen-Ecken"), style_section);
	rainbow_cb->setObjectName(QStringLiteral("lpRainbow"));
	rainbow_cb->setChecked(card.rainbow_corners);
	style_layout->addWidget(rainbow_cb);

	auto *anim_group = new QGroupBox(style_section);
	anim_group->setObjectName(QStringLiteral("lpAnimGroup"));
	anim_group->setTitle(QStringLiteral("Animation"));
	anim_group->setCheckable(true);
	anim_group->setChecked(false);

	auto *anim_group_layout = new QVBoxLayout(anim_group);
	anim_group_layout->setContentsMargins(10, 8, 10, 10);
	anim_group_layout->setSpacing(6);

	auto *anim_body = new QWidget(anim_group);
	auto *anim_body_layout = new QVBoxLayout(anim_body);
	anim_body_layout->setContentsMargins(0, 0, 0, 0);
	anim_body_layout->setSpacing(8);

	// 1. Datei + Icon-Picker
	auto *anim_file_lbl = new QLabel(QStringLiteral("DATEI"), anim_body);
	anim_file_lbl->setObjectName(QStringLiteral("lpFieldLabel"));
	auto *anim_file_row = new QHBoxLayout();
	anim_file_row->setContentsMargins(0, 0, 0, 0);
	anim_file_row->setSpacing(6);
	auto *anim_file_edit =
		new QLineEdit(QString::fromUtf8(card.anim_file.c_str()), anim_body);
	anim_file_edit->setObjectName(QStringLiteral("lpAnimFile"));
	anim_file_edit->setReadOnly(false);
	anim_file_edit->setPlaceholderText(QStringLiteral("Dateiname, Pfad oder URL"));
	anim_file_edit->setFixedHeight(34);
	auto *anim_file_pick = new QPushButton(anim_body);
	anim_file_pick->setObjectName(QStringLiteral("lpAnimFilePick"));
	anim_file_pick->setFixedSize(34, 34); /* Ui4: Icon-only-Button, OBS-„…“-Format */
	anim_file_pick->setIcon(theme_icon("download.svg"));
	anim_file_pick->setIconSize(QSize(16, 16));
	anim_file_pick->setToolTip(QStringLiteral("Animationsdatei auswählen"));
	anim_file_row->addWidget(anim_file_edit, 1);
	anim_file_row->addWidget(anim_file_pick);
	anim_body_layout->addWidget(anim_file_lbl);
	anim_body_layout->addLayout(anim_file_row);

	// 2. Skalierung (1x, 2x, 3x, 4x, 6x, 8x, 10x, 12x)
	auto *anim_scale_lbl = new QLabel(QStringLiteral("SKALIERUNG"), anim_body);
	anim_scale_lbl->setObjectName(QStringLiteral("lpFieldLabel"));
	auto *anim_scale_combo = new QComboBox(anim_body);
	anim_scale_combo->setObjectName(QStringLiteral("lpAnimScale"));
	anim_scale_combo->setFixedHeight(34);
	constexpr int kScaleValues[] = {1, 2, 3, 4, 6, 8, 10, 12};
	int anim_scale_idx = 2; // Default "3x"
	for (int si = 0; si < 8; ++si) {
		const int sv = kScaleValues[si];
		anim_scale_combo->addItem(QStringLiteral("%1x").arg(sv), sv);
		if (sv == card.anim_scale)
			anim_scale_idx = si;
	}
	anim_scale_combo->setCurrentIndex(anim_scale_idx);
	anim_body_layout->addWidget(anim_scale_lbl);
	anim_body_layout->addWidget(anim_scale_combo);

	// 3. X-Versatz (-1200..1200, step 5)
	const int init_anim_x = std::clamp(card.anim_x, -1200, 1200);
	auto *anim_x_hdr = new QHBoxLayout();
	anim_x_hdr->setContentsMargins(0, 0, 0, 0);
	auto *anim_x_lbl = new QLabel(QStringLiteral("X-VERSATZ"), anim_body);
	anim_x_lbl->setObjectName(QStringLiteral("lpFieldLabel"));
	auto *anim_x_val =
		new QLabel(QStringLiteral("%1px").arg(init_anim_x), anim_body);
	anim_x_val->setObjectName(QStringLiteral("lpFieldLabel"));
	anim_x_hdr->addWidget(anim_x_lbl);
	anim_x_hdr->addStretch(1);
	anim_x_hdr->addWidget(anim_x_val);
	auto *anim_x_slider = new QSlider(Qt::Horizontal, anim_body);
	anim_x_slider->setObjectName(QStringLiteral("lpAnimX"));
	anim_x_slider->setRange(-1200, 1200);
	anim_x_slider->setSingleStep(5);
	anim_x_slider->setPageStep(25);
	anim_x_slider->setValue(init_anim_x);
	anim_body_layout->addLayout(anim_x_hdr);
	anim_body_layout->addWidget(anim_x_slider);

	// 4. Y-Versatz (-1200..1200, step 5)
	const int init_anim_y = std::clamp(card.anim_y, -1200, 1200);
	auto *anim_y_hdr = new QHBoxLayout();
	anim_y_hdr->setContentsMargins(0, 0, 0, 0);
	auto *anim_y_lbl = new QLabel(QStringLiteral("Y-VERSATZ"), anim_body);
	anim_y_lbl->setObjectName(QStringLiteral("lpFieldLabel"));
	auto *anim_y_val =
		new QLabel(QStringLiteral("%1px").arg(init_anim_y), anim_body);
	anim_y_val->setObjectName(QStringLiteral("lpFieldLabel"));
	anim_y_hdr->addWidget(anim_y_lbl);
	anim_y_hdr->addStretch(1);
	anim_y_hdr->addWidget(anim_y_val);
	auto *anim_y_slider = new QSlider(Qt::Horizontal, anim_body);
	anim_y_slider->setObjectName(QStringLiteral("lpAnimY"));
	anim_y_slider->setRange(-1200, 1200);
	anim_y_slider->setSingleStep(5);
	anim_y_slider->setPageStep(25);
	anim_y_slider->setValue(init_anim_y);
	anim_body_layout->addLayout(anim_y_hdr);
	anim_body_layout->addWidget(anim_y_slider);

	// 5. Aktiv-Checkbox
	auto *anim_show_cb = new QCheckBox(QStringLiteral("Aktiv"), anim_body);
	anim_show_cb->setObjectName(QStringLiteral("lpAnimShow"));
	anim_show_cb->setChecked(card.show_anim);
	anim_body_layout->addWidget(anim_show_cb);

	anim_body->setVisible(false);
	anim_group_layout->addWidget(anim_body);
	connect(anim_group, &QGroupBox::toggled, anim_body, &QWidget::setVisible);
	style_layout->addWidget(anim_group);

	style_section->setVisible(true);
	inner_layout->addWidget(style_section);

	connect(style_toggle_btn, &QToolButton::clicked, this,
		[this, style_section, style_toggle_btn]() {
			const bool expand = !style_section->isVisible();
			style_section->setVisible(expand);
			style_toggle_btn->setIcon(
				theme_icon(expand ? "chevron_down.svg" : "chevron_right.svg"));
			style_toggle_btn->setToolTip(
				expand ? QStringLiteral("Stil-Optionen einklappen")
				       : QStringLiteral("Stil-Optionen ausklappen"));
		});

	auto *actions_layout = new QHBoxLayout();
	actions_layout->setContentsMargins(0, 4, 0, 0);
	actions_layout->setSpacing(8);

	auto *to_scene_btn = new QPushButton(QStringLiteral("Zu Szene"), card_inner);
	to_scene_btn->setObjectName(QStringLiteral("lpBtnGreen"));
	to_scene_btn->setToolTip(
		QStringLiteral("Karte als Text-Quelle in die aktuelle Szene legen oder aktualisieren"));

	auto *clear_src_btn = new QPushButton(QStringLiteral("Leeren"), card_inner);
	clear_src_btn->setObjectName(QStringLiteral("lpBtnRed"));
	clear_src_btn->setToolTip(
		QStringLiteral("Text der Quelle auf leer setzen — die Quelle bleibt in der Szene"));

	actions_layout->addWidget(to_scene_btn, 1);
	actions_layout->addWidget(clear_src_btn, 1);
	inner_layout->addLayout(actions_layout);

	const std::string card_id = card.id;

	connect(edit_zeile1, &QLineEdit::textChanged, this, [this, card_id](const QString &text) {
		if (LiedplanCard *c = find_card_mut(card_id)) {
			c->zeile1 = text.toUtf8().toStdString();
			save_state();
			liedplan_scene::update_everywhere(*c, false, state_.settings);
		}
	});

	connect(edit_zeile2, &QLineEdit::textChanged, this, [this, card_id](const QString &text) {
		if (LiedplanCard *c = find_card_mut(card_id)) {
			c->zeile2 = text.toUtf8().toStdString();
			save_state();
			liedplan_scene::update_everywhere(*c, false, state_.settings);
		}
	});

	connect(edit_zeile3, &QLineEdit::textChanged, this, [this, card_id](const QString &text) {
		if (LiedplanCard *c = find_card_mut(card_id)) {
			c->zeile3 = text.toUtf8().toStdString();
			save_state();
			liedplan_scene::update_everywhere(*c, false, state_.settings);
		}
		if (card_search_) {
			const QSignalBlocker blocker(card_search_);
			const QString prev_text = card_search_->currentText();
			const QString id_q = QString::fromUtf8(card_id.c_str());
			for (int i = 0; i < card_search_->count(); ++i) {
				if (card_search_->itemData(i).toString() == id_q) {
					QString t3 = text.trimmed();
					if (t3.size() > 20)
						t3 = t3.left(20);
					card_search_->setItemText(
						i, t3.isEmpty()
							   ? id_q
							   : QStringLiteral("%1 · %2").arg(id_q, t3));
					break;
				}
			}
			if (card_search_->lineEdit())
				card_search_->lineEdit()->setText(prev_text);
		}
	});

	connect(profile_combo, &QComboBox::currentTextChanged, this,
		[this, card_id, text_color_btn, box_color_btn](const QString &profile_text) {
			if (LiedplanCard *c = find_card_mut(card_id)) {
				c->profile = profile_text.toUtf8().toStdString();
				if (const LiedplanProfile *prof =
					    liedplan_profile_lookup(c->profile, state_.color_profiles)) {
					c->text_color = prof->text_color;
					c->box_color = prof->box_color;
				}
				// M6-A8: Profil-Dropdown = Reset auf Profilsatz → Button-Labels
				// auf die (neuen) Werte aktualisieren.
				text_color_btn->setText(QString::fromUtf8(
					(c->text_color.empty() ? "#000000" : c->text_color).c_str()).toUpper());
				box_color_btn->setText(QString::fromUtf8(
					(c->box_color.empty() ? "#ffffff" : c->box_color).c_str()).toUpper());
				save_state();
				liedplan_scene::update_everywhere(*c, false, state_.settings);
			}
		});

	connect(new_prof_btn, &QPushButton::clicked, this, [this, card_id]() {
		bool ok = false;
		const QString raw_name = QInputDialog::getText(
			this, QStringLiteral("Farbprofil speichern"),
			QStringLiteral("Profilname:"), QLineEdit::Normal, QString(), &ok);
		if (!ok)
			return;
		const QString qname = raw_name.trimmed();
		if (qname.isEmpty()) {
			set_dock_status(QStringLiteral("Bitte einen Profilnamen eingeben."), true);
			return;
		}
		const std::string name_utf8 = qname.toUtf8().toStdString();
		if (liedplan_is_default_profile(name_utf8)) {
			set_dock_status(
				QStringLiteral("\"%1\" ist ein geschütztes Standard-Profil.").arg(qname),
				true);
			return;
		}
		LiedplanCard *c = find_card_mut(card_id);
		if (!c)
			return;
		const std::string tc = c->text_color.empty() ? "#000000" : c->text_color;
		const std::string bc = c->box_color.empty() ? "#ffffff" : c->box_color;
		LiedplanProfile prof{name_utf8, tc, bc};
		auto it = std::find_if(state_.color_profiles.begin(), state_.color_profiles.end(),
				       [&name_utf8](const LiedplanProfile &p) {
					       return p.name == name_utf8;
				       });
		if (it != state_.color_profiles.end()) {
			*it = std::move(prof);
		} else {
			state_.color_profiles.push_back(std::move(prof));
		}
		c->profile = name_utf8;
		save_state();
		rebuild_rows();
		set_dock_status(QStringLiteral("Farbprofil \"%1\" gespeichert").arg(qname), false);
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
				liedplan_scene::update_everywhere(*c, false, state_.settings);
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
				liedplan_scene::update_everywhere(*c, false, state_.settings);
			}
		}
	});

	// M6-A8: Freie Text-/Box-Farbe (Color-Picker, Muster = ECKE A/B).
	connect(text_color_btn, &QPushButton::clicked, this, [this, card_id, text_color_btn]() {
		if (LiedplanCard *c = find_card_mut(card_id)) {
			const QString cur_hex = QString::fromUtf8(
				(c->text_color.empty() ? "#000000" : c->text_color).c_str());
			const QColor chosen =
				QColorDialog::getColor(QColor(cur_hex), this, QStringLiteral("Textfarbe"));
			if (chosen.isValid()) {
				const QString hex = chosen.name(QColor::HexRgb);
				c->text_color = hex.toUtf8().toStdString();
				text_color_btn->setText(hex.toUpper());
				save_state();
				liedplan_scene::update_everywhere(*c, false, state_.settings);
			}
		}
	});

	connect(box_color_btn, &QPushButton::clicked, this, [this, card_id, box_color_btn]() {
		if (LiedplanCard *c = find_card_mut(card_id)) {
			const QString cur_hex = QString::fromUtf8(
				(c->box_color.empty() ? "#ffffff" : c->box_color).c_str());
			const QColor chosen =
				QColorDialog::getColor(QColor(cur_hex), this, QStringLiteral("Boxfarbe"));
			if (chosen.isValid()) {
				const QString hex = chosen.name(QColor::HexRgb);
				c->box_color = hex.toUtf8().toStdString();
				box_color_btn->setText(hex.toUpper());
				save_state();
				liedplan_scene::update_everywhere(*c, false, state_.settings);
			}
		}
	});

	connect(opacity_a_slider, &QSlider::valueChanged, this, [this, card_id](int val) {
		if (LiedplanCard *c = find_card_mut(card_id)) {
			c->corner_opacity_a = std::clamp(val, 0, 255);
			save_state();
			liedplan_scene::update_everywhere(*c, false, state_.settings);
		}
	});

	connect(opacity_b_slider, &QSlider::valueChanged, this, [this, card_id](int val) {
		if (LiedplanCard *c = find_card_mut(card_id)) {
			c->corner_opacity_b = std::clamp(val, 0, 255);
			save_state();
			liedplan_scene::update_everywhere(*c, false, state_.settings);
		}
	});

	// M6-A7: Text-/Box-Deckkraft (0–255 → Alpha von --text-color/--box-bg;
	// --box-opacity (Show/Hide, has_any_text) bleibt davon unberührt).
	connect(text_opacity_slider, &QSlider::valueChanged, this, [this, card_id](int val) {
		if (LiedplanCard *c = find_card_mut(card_id)) {
			c->text_opacity = std::clamp(val, 0, 255);
			save_state();
			liedplan_scene::update_everywhere(*c, false, state_.settings);
		}
	});

	connect(box_opacity_slider, &QSlider::valueChanged, this, [this, card_id](int val) {
		if (LiedplanCard *c = find_card_mut(card_id)) {
			c->box_opacity = std::clamp(val, 0, 255);
			save_state();
			liedplan_scene::update_everywhere(*c, false, state_.settings);
		}
	});

	connect(rainbow_cb, &QCheckBox::toggled, this, [this, card_id](bool checked) {
		if (LiedplanCard *c = find_card_mut(card_id)) {
			c->rainbow_corners = checked;
			save_state();
			liedplan_scene::update_everywhere(*c, false, state_.settings);
		}
	});

	connect(anim_file_edit, &QLineEdit::textChanged, this,
		[this, card_id](const QString &text) {
			if (LiedplanCard *c = find_card_mut(card_id)) {
				c->anim_file = text.toUtf8().toStdString();
				save_state();
				liedplan_scene::update_everywhere(*c, false, state_.settings);
			}
		});

	connect(anim_file_pick, &QPushButton::clicked, this, [this, anim_file_edit]() {
		const QString start_dir =
			QString::fromUtf8(state_.settings.animation_dir.c_str());
		const QString chosen = QFileDialog::getOpenFileName(
			this, QStringLiteral("Animationsdatei wählen"), start_dir,
			QStringLiteral(
				"Animationen (*.gif *.webm *.mp4 *.mov *.m4v);;Bilder (*.png *.jpg *.jpeg);;Alle Dateien (*.*)"));
		if (!chosen.isEmpty()) {
			anim_file_edit->setText(chosen);
		}
	});

	connect(anim_scale_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		[this, card_id, anim_scale_combo](int idx) {
			if (LiedplanCard *c = find_card_mut(card_id)) {
				const int sc = anim_scale_combo->itemData(idx).toInt();
				c->anim_scale = sc > 0 ? sc : 3;
				save_state();
				liedplan_scene::update_everywhere(*c, false, state_.settings);
			}
		});

	connect(anim_x_slider, &QSlider::valueChanged, this,
		[this, card_id, anim_x_val](int val) {
			const int clamped = std::clamp(val, -1200, 1200);
			anim_x_val->setText(QStringLiteral("%1px").arg(clamped));
			if (LiedplanCard *c = find_card_mut(card_id)) {
				c->anim_x = clamped;
				save_state();
				liedplan_scene::update_everywhere(*c, false, state_.settings);
			}
		});

	connect(anim_y_slider, &QSlider::valueChanged, this,
		[this, card_id, anim_y_val](int val) {
			const int clamped = std::clamp(val, -1200, 1200);
			anim_y_val->setText(QStringLiteral("%1px").arg(clamped));
			if (LiedplanCard *c = find_card_mut(card_id)) {
				c->anim_y = clamped;
				save_state();
				liedplan_scene::update_everywhere(*c, false, state_.settings);
			}
		});

	connect(anim_show_cb, &QCheckBox::toggled, this, [this, card_id](bool checked) {
		if (LiedplanCard *c = find_card_mut(card_id)) {
			c->show_anim = checked;
			save_state();
			liedplan_scene::update_everywhere(*c, false, state_.settings);
		}
	});

	connect(to_scene_btn, &QPushButton::clicked, this, [this, card_id]() {
		if (LiedplanCard *c = find_card_mut(card_id)) {
			blog(LOG_INFO, "[liedplan] Sende Karte '%s' an aktuelle Szene", card_id.c_str());
			liedplan_scene::send_to_current_scene(*c, state_.settings);
		}
	});

	connect(clear_src_btn, &QPushButton::clicked, this, [this, card_id]() {
		if (LiedplanCard *c = find_card_mut(card_id)) {
			liedplan_scene::update_everywhere(*c, true, state_.settings);
		}
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
		refresh_top_row();
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
	refresh_top_row();
}

void LiedplanDock::sync_profil_del_state()
{
	if (!profil_del_btn_ || !profil_select_)
		return;
	const int idx = profil_select_->currentIndex();
	const bool valid = idx > 0 && idx < profil_select_->count() &&
			   !profil_select_->itemData(idx).toString().isEmpty();
	profil_del_btn_->setEnabled(valid);
}

void LiedplanDock::refresh_top_row()
{
	if (profil_select_) {
		const QSignalBlocker blocker(profil_select_);
		profil_select_->clear();
		profil_select_->addItem(QStringLiteral("(keines)"), QString());
		int active_idx = 0;
		for (std::size_t i = 0; i < state_.quick_profiles.size(); ++i) {
			const QString qname =
				QString::fromUtf8(state_.quick_profiles[i].name.c_str());
			profil_select_->addItem(qname, qname);
			if (state_.quick_profiles[i].name == state_.active_profile)
				active_idx = static_cast<int>(i) + 1;
		}
		profil_select_->setCurrentIndex(active_idx);
		sync_profil_del_state();
	}

	if (card_search_) {
		const QSignalBlocker blocker(card_search_);
		const QString prev_text = card_search_->currentText();
		card_search_->clear();
		for (const auto &card : state_.cards) {
			const QString id_q = QString::fromUtf8(card.id.c_str());
			QString t3 = QString::fromUtf8(card.zeile3.c_str()).trimmed();
			if (t3.size() > 20)
				t3 = t3.left(20);
			const QString label =
				t3.isEmpty() ? id_q : QStringLiteral("%1 · %2").arg(id_q, t3);
			card_search_->addItem(label, id_q);
		}
		card_search_->setCurrentIndex(-1);
		if (card_search_->lineEdit())
			card_search_->lineEdit()->setText(prev_text);
	}
}

void LiedplanDock::apply_quick_profile(const std::string &name)
{
	if (name.empty())
		return;

	std::vector<std::string> prev_ids;
	prev_ids.reserve(state_.cards.size());
	for (const auto &c : state_.cards)
		prev_ids.push_back(c.id);

	if (state_.apply_quick_profile(name)) {
		state_.active_profile = name;
		save_state();
		sync_all_cards_live(prev_ids);
		rebuild_rows();
		set_dock_status(QStringLiteral("Schnell-Profil übernommen: %1")
					.arg(QString::fromUtf8(name.c_str())),
				false);
	}
}

void LiedplanDock::flash_row(const std::string &card_id)
{
	if (card_id.empty())
		return;
	const QString target_id = QString::fromUtf8(card_id.c_str());
	for (QWidget *w : row_widgets_) {
		if (!w || w->property("lpCardId").toString() != target_id)
			continue;
		if (scroll_area_)
			scroll_area_->ensureWidgetVisible(w);
		w->setProperty("lpFlash", QStringLiteral("true"));
		if (QStyle *st = w->style()) {
			st->unpolish(w);
			st->polish(w);
		}
		w->update();
		QPointer<QWidget> w_ptr(w);
		QTimer::singleShot(1200, this, [w_ptr]() {
			if (!w_ptr)
				return;
			w_ptr->setProperty("lpFlash", QVariant());
			if (QStyle *st = w_ptr->style()) {
				st->unpolish(w_ptr.data());
				st->polish(w_ptr.data());
			}
			w_ptr->update();
		});
		break;
	}
}

void LiedplanDock::on_add_card()
{
	const LiedplanCard &card = state_.add_card();
	create_row_widget(card);
	refresh_top_row();
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
		liedplan_scene::update_everywhere(card, false, state_.settings);
	}
}

void LiedplanDock::set_dock_status(const QString &msg, bool is_error)
{
	if (!dock_status_lbl_)
		return;
	dock_status_lbl_->setText(msg);
	dock_status_lbl_->setStyleSheet(
		is_error ? QStringLiteral("color: #E85E75; font-size: 10pt; font-weight: bold;")
		 : QStringLiteral("color: #969696; font-size: 10pt;"));
	dock_status_lbl_->setVisible(!msg.isEmpty());
}

void LiedplanDock::apply_import_result(const liedplan_import::LiedplanImportResult &res)
{
	blog(LOG_INFO, "[liedplan] Import übernommen: %zu Programmpunkte", res.punkte.size());
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
		liedplan_scene::update_everywhere(state_.cards[i], false, state_.settings);
	}
	rebuild_rows();
}

void LiedplanDock::on_open_import_dialog()
{
	QDialog dlg(this);
	dlg.setObjectName(QStringLiteral("lpImportDialog"));
	dlg.setWindowTitle(QStringLiteral("Liedplan-Import"));
	dlg.setStyleSheet(liedplan_theme::stylesheet(icon_dir_));
	dlg.setMinimumWidth(540);

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

	auto *api_btn = new QPushButton(QStringLiteral("Neueste Antwort laden"), google_group);
	api_btn->setObjectName(QStringLiteral("lpBtnApiImport"));
	api_btn->setIcon(theme_icon("download.svg"));
	api_btn->setIconSize(QSize(16, 16));

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
		QStringLiteral("Datei importieren (CSV / XLSX / ZIP) …"), csv_group);
	csv_btn->setObjectName(QStringLiteral("lpBtnCsvImport"));
	csv_layout->addWidget(csv_btn);
	root->addWidget(csv_group);

	// Gruppe 3 (M5 D3): AI-Import (PDF/Dokument)
	auto *ai_group = new QGroupBox(QStringLiteral("AI-Import (PDF/Dokument)"), &dlg);
	ai_group->setObjectName(QStringLiteral("lpAiImportGroup"));
	auto *ai_layout = new QVBoxLayout(ai_group);
	ai_layout->setContentsMargins(12, 16, 12, 12);
	ai_layout->setSpacing(8);

	auto *lbl_ai_file = new QLabel(QStringLiteral("DOKUMENT (PDF / DOCX)"), ai_group);
	lbl_ai_file->setObjectName(QStringLiteral("lpFieldLabel"));
	auto *ai_file_row = new QHBoxLayout();
	ai_file_row->setSpacing(6);
	auto *ai_file_edit = new QLineEdit(ai_group);
	ai_file_edit->setObjectName(QStringLiteral("lpAiFilePath"));
	ai_file_edit->setPlaceholderText(QStringLiteral("PDF- oder DOCX-Datei auswählen …"));
	auto *ai_file_btn = new QPushButton(QStringLiteral("Datei wählen …"), ai_group);
	ai_file_btn->setObjectName(QStringLiteral("lpBtnAiFilePick"));
	ai_file_row->addWidget(ai_file_edit, 1);
	ai_file_row->addWidget(ai_file_btn);
	ai_layout->addWidget(lbl_ai_file);
	ai_layout->addLayout(ai_file_row);

	auto *lbl_ai_key = new QLabel(QStringLiteral("API-KEY"), ai_group);
	lbl_ai_key->setObjectName(QStringLiteral("lpFieldLabel"));
	auto *ai_key_row = new QHBoxLayout();
	ai_key_row->setSpacing(6);
	std::string env_key_err;
	const std::string prefilled_key =
		liedplan_ai::read_env_key(state_.settings.openrouter_key_path, env_key_err);
	auto *ai_key_edit =
		new QLineEdit(QString::fromUtf8(prefilled_key.c_str()), ai_group);
	ai_key_edit->setObjectName(QStringLiteral("lpAiApiKey"));
	ai_key_edit->setEchoMode(QLineEdit::Password);
	ai_key_edit->setPlaceholderText(
		QStringLiteral("OpenRouter API-Key (leer = lokale Offline-Analyse)"));
	/* Ui4: kein Auge-Emoji — natives Text-Toggle (OBS-Style: Text, kein Glyph). */
	auto *ai_key_toggle = new QPushButton(QStringLiteral("Key zeigen"), ai_group);
	ai_key_toggle->setObjectName(QStringLiteral("lpBtnAiKeyToggle"));
	ai_key_toggle->setToolTip(QStringLiteral("API-Key ein-/ausblenden"));
	ai_key_row->addWidget(ai_key_edit, 1);
	ai_key_row->addWidget(ai_key_toggle);
	ai_layout->addWidget(lbl_ai_key);
	ai_layout->addLayout(ai_key_row);

	connect(ai_key_toggle, &QPushButton::clicked, &dlg, [ai_key_edit, ai_key_toggle]() {
		const bool now_visible = ai_key_edit->echoMode() != QLineEdit::Password;
		ai_key_edit->setEchoMode(now_visible ? QLineEdit::Password : QLineEdit::Normal);
		ai_key_toggle->setText(now_visible ? QStringLiteral("Key zeigen")
		                                  : QStringLiteral("Key verbergen"));
	});

	// Einklappbarer System-Prompt
	auto *prompt_group = new QGroupBox(QStringLiteral("System-Prompt"), ai_group);
	prompt_group->setObjectName(QStringLiteral("lpAnimGroup"));
	prompt_group->setCheckable(true);
	prompt_group->setChecked(false);
	auto *prompt_group_layout = new QVBoxLayout(prompt_group);
	prompt_group_layout->setContentsMargins(8, 8, 8, 8);
	prompt_group_layout->setSpacing(6);

	auto *prompt_body = new QWidget(prompt_group);
	auto *prompt_body_layout = new QVBoxLayout(prompt_body);
	prompt_body_layout->setContentsMargins(0, 0, 0, 0);
	prompt_body_layout->setSpacing(6);

	const std::string init_prompt = state_.settings.ai_system_prompt.empty()
						? liedplan_ai::kDefaultSystemPrompt
						: state_.settings.ai_system_prompt;
	auto *ai_prompt_edit =
		new QPlainTextEdit(QString::fromUtf8(init_prompt.c_str()), prompt_body);
	ai_prompt_edit->setObjectName(QStringLiteral("lpAiSystemPrompt"));
	ai_prompt_edit->setFixedHeight(110);

	auto *ai_prompt_reset_btn =
		new QPushButton(QStringLiteral("Zurücksetzen"), prompt_body);
	ai_prompt_reset_btn->setObjectName(QStringLiteral("lpBtnAiPromptReset"));
	prompt_body_layout->addWidget(ai_prompt_edit);
	prompt_body_layout->addWidget(ai_prompt_reset_btn);
	prompt_body->setVisible(false);
	prompt_group_layout->addWidget(prompt_body);
	connect(prompt_group, &QGroupBox::toggled, prompt_body, &QWidget::setVisible);
	ai_layout->addWidget(prompt_group);

	connect(ai_prompt_reset_btn, &QPushButton::clicked, &dlg, [this, ai_prompt_edit]() {
		ai_prompt_edit->setPlainText(
			QString::fromUtf8(liedplan_ai::kDefaultSystemPrompt));
		state_.settings.ai_system_prompt.clear();
		save_state();
	});

	auto *ai_start_btn = new QPushButton(QStringLiteral("Starten"), ai_group);
	ai_start_btn->setObjectName(QStringLiteral("lpBtnAiStart"));
	ai_layout->addWidget(ai_start_btn);

	auto *ai_status_lbl = new QLabel(
		prefilled_key.empty()
			? QStringLiteral("Kein API-Key – lokale Offline-Analyse")
			: QStringLiteral("Bereit für AI-Import."),
		ai_group);
	ai_status_lbl->setObjectName(QStringLiteral("lpImportStatus"));
	ai_status_lbl->setWordWrap(true);
	ai_layout->addWidget(ai_status_lbl);

	connect(ai_key_edit, &QLineEdit::textChanged, &dlg,
		[ai_status_lbl](const QString &txt) {
			if (txt.trimmed().isEmpty()) {
				ai_status_lbl->setText(
					QStringLiteral("Kein API-Key – lokale Offline-Analyse"));
			} else {
				ai_status_lbl->setText(QStringLiteral("Bereit für AI-Import."));
			}
		});

	// AI-Preview-Box (Tabelle + Quellen-Label + Übernehmen)
	auto *ai_preview_group = new QGroupBox(QStringLiteral("Programmvorschau"), ai_group);
	auto *ai_preview_layout = new QVBoxLayout(ai_preview_group);
	ai_preview_layout->setContentsMargins(10, 14, 10, 10);
	ai_preview_layout->setSpacing(8);

	auto *ai_source_lbl = new QLabel(ai_preview_group);
	ai_source_lbl->setObjectName(QStringLiteral("lpAiSourceLabel"));
	ai_source_lbl->setWordWrap(true);

	auto *ai_table = new QTableWidget(0, 3, ai_preview_group);
	ai_table->setObjectName(QStringLiteral("lpAiPreviewTable"));
	ai_table->setHorizontalHeaderLabels({
		QStringLiteral("Zeile 1"),
		QStringLiteral("Zeile 2"),
		QStringLiteral("Zeile 3"),
	});
	ai_table->horizontalHeader()->setStretchLastSection(true);
	ai_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
	ai_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
	ai_table->verticalHeader()->setVisible(false);
	ai_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
	ai_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	ai_table->setMinimumHeight(160);

	auto *ai_apply_btn = new QPushButton(QStringLiteral("Übernehmen"), ai_preview_group);
	ai_apply_btn->setObjectName(QStringLiteral("lpBtnAiApply"));

	ai_preview_layout->addWidget(ai_source_lbl);
	ai_preview_layout->addWidget(ai_table);
	ai_preview_layout->addWidget(ai_apply_btn);
	ai_preview_group->setVisible(false);
	ai_layout->addWidget(ai_preview_group);

	root->addWidget(ai_group);

	// Gruppe 4: Kopfdaten (erst nach erfolgreichem Laden sichtbar)
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

	auto pending_ai_res = std::make_shared<liedplan_ai::AiResult>();

	connect(ai_file_btn, &QPushButton::clicked, &dlg, [&dlg, ai_file_edit]() {
		const QString chosen = QFileDialog::getOpenFileName(
			&dlg, QStringLiteral("Liedplan-Dokument auswählen"),
			ai_file_edit->text(),
			QStringLiteral("Dokumente (*.pdf *.docx);;Alle Dateien (*.*)"));
		if (!chosen.isEmpty()) {
			ai_file_edit->setText(chosen);
		}
	});

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
					  "12 Karten befüllt — im Dock editierbar, „Zu Szene“ legt sie in die Szene.")
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

	connect(ai_start_btn, &QPushButton::clicked, &dlg,
		[this, &dlg, dlg_ptr, self_ptr, api_btn, csv_btn, ai_start_btn, ai_file_edit,
		 ai_key_edit, ai_prompt_edit, ai_status_lbl, status_lbl, error_lbl,
		 ai_preview_group, ai_source_lbl, ai_table, pending_ai_res]() {
			QString doc_file = ai_file_edit->text().trimmed();
			if (doc_file.isEmpty()) {
				doc_file = QFileDialog::getOpenFileName(
					&dlg, QStringLiteral("Liedplan-Dokument auswählen"),
					QString(),
					QStringLiteral(
						"Dokumente (*.pdf *.docx);;Alle Dateien (*.*)"));
				if (doc_file.isEmpty())
					return;
				ai_file_edit->setText(doc_file);
			}

			const std::string prompt_str =
				ai_prompt_edit->toPlainText().trimmed().toUtf8().toStdString();
			state_.settings.ai_system_prompt =
				(prompt_str == liedplan_ai::kDefaultSystemPrompt) ? "" : prompt_str;
			save_state();

			const std::string file_utf8 = doc_file.toUtf8().toStdString();
			const std::string api_key =
				ai_key_edit->text().trimmed().toUtf8().toStdString();
			const std::string sys_prompt =
				prompt_str.empty() ? liedplan_ai::kDefaultSystemPrompt : prompt_str;
			const std::string python_exe = state_.settings.markitdown_python;
			const std::string lieder_path = state_.settings.lieder_path;

			api_btn->setEnabled(false);
			csv_btn->setEnabled(false);
			ai_start_btn->setEnabled(false);
			ai_preview_group->setVisible(false);
			error_lbl->clear();
			error_lbl->setVisible(false);
			ai_status_lbl->setText(QStringLiteral("Text aus Dokument extrahieren..."));
			status_lbl->setText(QStringLiteral("Text aus Dokument extrahieren..."));

			std::thread([self_ptr, dlg_ptr, api_btn, csv_btn, ai_start_btn,
				     ai_status_lbl, status_lbl, error_lbl, ai_preview_group,
				     ai_source_lbl, ai_table, pending_ai_res, file_utf8, api_key,
				     sys_prompt, python_exe, lieder_path]() {
				auto post_progress = [self_ptr, dlg_ptr, ai_status_lbl,
						      status_lbl](const std::string &msg) {
					if (!self_ptr)
						return;
					const QString qmsg = QString::fromUtf8(msg.c_str());
					QMetaObject::invokeMethod(
						self_ptr.data(),
						[self_ptr, dlg_ptr, ai_status_lbl, status_lbl,
						 qmsg]() {
							if (!self_ptr || !dlg_ptr)
								return;
							ai_status_lbl->setText(qmsg);
							status_lbl->setText(qmsg);
						},
						Qt::QueuedConnection);
				};

				liedplan_ai::AiResult ai_res;
				std::string fatal_err;

				try {
					std::string ext_err;
					const std::string text =
						liedplan_ai::extract_document_text(
							file_utf8, python_exe, ext_err);
					if (text.empty()) {
						throw std::runtime_error(
							ext_err.empty()
								? "Kein Text im Dokument gefunden."
								: ext_err);
					}

					if (!api_key.empty()) {
						blog(LOG_INFO, "[liedplan] AI-Import: Verwende OpenRouter LLM");
						try {
							ai_res = liedplan_ai::process_with_llm(
								api_key, text, sys_prompt,
								lieder_path, post_progress);
						} catch (const std::exception &llm_ex) {
							blog(LOG_INFO, "[liedplan] AI-Import: Wechsle auf Offline-Fallback-Parser");
							post_progress(
								"Free-Tier nicht erreichbar – nutze lokalen Offline-Parser...");
							auto local_items =
								liedplan_ai::parse_liedplan_locally(
									text);
							if (local_items.empty())
								throw std::runtime_error(
									llm_ex.what());
							ai_res.items = std::move(local_items);
							ai_res.model_label = "Offline-Fallback";
						}
					} else {
						blog(LOG_INFO, "[liedplan] AI-Import: Verwende lokalen Offline-Parser");
						post_progress(
							"Kein API-Key – lokale Offline-Analyse");
						auto local_items =
							liedplan_ai::parse_liedplan_locally(text);
						if (local_items.empty()) {
							throw std::runtime_error(
								"Weder API-Key vorhanden noch konnte der lokale Parser Programmpunkte erkennen.");
						}
						ai_res.items = std::move(local_items);
						ai_res.model_label = "Lokal (ohne API-Key)";
					}

					if (ai_res.items.empty()) {
						throw std::runtime_error(
							"Keine Programmpunkte erkannt.");
					}
				} catch (const std::exception &ex) {
					fatal_err = ex.what();
				}

				if (!self_ptr)
					return;

				QMetaObject::invokeMethod(
					self_ptr.data(),
					[self_ptr, dlg_ptr, api_btn, csv_btn, ai_start_btn,
					 ai_status_lbl, status_lbl, error_lbl, ai_preview_group,
					 ai_source_lbl, ai_table, pending_ai_res,
					 ai_res = std::move(ai_res),
					 fatal_err = std::move(fatal_err)]() mutable {
						if (!self_ptr || !dlg_ptr)
							return;
						api_btn->setEnabled(true);
						csv_btn->setEnabled(true);
						ai_start_btn->setEnabled(true);

						if (!fatal_err.empty()) {
							const QString qerr =
								QString::fromUtf8(fatal_err.c_str());
							error_lbl->setText(qerr);
							error_lbl->setVisible(true);
							ai_status_lbl->setText(qerr);
							status_lbl->setText(qerr);
							self_ptr->set_dock_status(qerr, true);
							return;
						}

						*pending_ai_res = std::move(ai_res);
						const QString src_tag =
							QStringLiteral("AI-Import (%1)")
								.arg(QString::fromUtf8(
									pending_ai_res->model_label
										.c_str()));
						ai_source_lbl->setText(src_tag);

						ai_table->setRowCount(static_cast<int>(
							pending_ai_res->items.size()));
						for (std::size_t r = 0;
						     r < pending_ai_res->items.size(); ++r) {
							const auto &pt = pending_ai_res->items[r];
							ai_table->setItem(
								static_cast<int>(r), 0,
								new QTableWidgetItem(
									QString::fromUtf8(
										pt.zeile1.c_str())));
							ai_table->setItem(
								static_cast<int>(r), 1,
								new QTableWidgetItem(
									QString::fromUtf8(
										pt.zeile2.c_str())));
							ai_table->setItem(
								static_cast<int>(r), 2,
								new QTableWidgetItem(
									QString::fromUtf8(
										pt.zeile3.c_str())));
						}
						ai_preview_group->setVisible(true);
						const QString ok_msg =
							QStringLiteral(
								"Vorschau: %1 Punkte (%2)")
								.arg(pending_ai_res->items.size())
								.arg(src_tag);
						ai_status_lbl->setText(ok_msg);
						status_lbl->setText(ok_msg);
					},
					Qt::QueuedConnection);
			}).detach();
		});

	connect(ai_apply_btn, &QPushButton::clicked, &dlg,
		[this, &dlg, pending_ai_res]() {
			if (pending_ai_res->items.empty())
				return;

			if (state_.cards.empty()) {
				for (int i = 0; i < kDefaultCardCount; ++i)
					state_.add_card();
			}

			const std::size_t total_items = pending_ai_res->items.size();
			const std::size_t n = std::min(total_items, state_.cards.size());
			const std::size_t ignored = (total_items > n) ? (total_items - n) : 0;

			for (std::size_t i = 0; i < n; ++i) {
				state_.cards[i].zeile1 = pending_ai_res->items[i].zeile1;
				state_.cards[i].zeile2 = pending_ai_res->items[i].zeile2;
				state_.cards[i].zeile3 = pending_ai_res->items[i].zeile3;
			}

			save_state();
			for (std::size_t i = 0; i < n; ++i) {
				liedplan_scene::update_everywhere(
					state_.cards[i], false, state_.settings);
			}
			rebuild_rows();

			const QString label_q =
				QString::fromUtf8(pending_ai_res->model_label.c_str());
			QString dock_msg =
				QStringLiteral("AI-Import: %1 Punkte (Quelle: %2)")
					.arg(n)
					.arg(label_q);
			if (ignored > 0) {
				dock_msg += QStringLiteral(" — Übernommen: %1 Punkte (%2 weitere ignoriert)")
						    .arg(n)
						    .arg(ignored);
			}
			set_dock_status(dock_msg, false);
			dlg.accept();
		});

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
	dlg.setStyleSheet(liedplan_theme::stylesheet(icon_dir_));
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
	auto *apply_btn = new QPushButton(QStringLiteral("Übernehmen"), list_group);
	apply_btn->setObjectName(QStringLiteral("lpBtnQuickApply"));
	auto *del_btn = new QPushButton(QStringLiteral("Löschen"), list_group);
	del_btn->setObjectName(QStringLiteral("lpBtnQuickDelete"));
	del_btn->setIcon(theme_icon("trash.svg"));
	del_btn->setIconSize(QSize(16, 16));
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

			const std::string prof_name =
				state_.quick_profiles[static_cast<std::size_t>(idx)].name;
			apply_quick_profile(prof_name);
			const QString msg =
				QStringLiteral("Schnell-Profil übernommen: %1")
					.arg(QString::fromUtf8(prof_name.c_str()));
			status_lbl->setText(msg);
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
				refresh_top_row();
				const QString msg =
					QStringLiteral("Schnell-Profil gelöscht: %1")
						.arg(QString::fromUtf8(prof_name.c_str()));
				status_lbl->setText(msg);
				set_dock_status(msg, false);
			}
		});

	dlg.exec();
	refresh_top_row();
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
	dlg.setStyleSheet(liedplan_theme::stylesheet(icon_dir_));
	dlg.setMinimumWidth(520);

	auto *root = new QVBoxLayout(&dlg);
	root->setContentsMargins(16, 16, 16, 16);
	root->setSpacing(12);

	const LiedplanSettings def_settings;

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
	/* Ui4: „…“ = natives OBS-Browse-Button-Idiom (Properties-Panel). */
	auto *browse_btn = new QPushButton(QStringLiteral("…"), oauth_group);
	browse_btn->setObjectName(QStringLiteral("lpBtn"));
	token_row->addWidget(edit_token, 1);
	token_row->addWidget(browse_btn);

	oauth_layout->addWidget(lbl_form);
	oauth_layout->addWidget(edit_form);
	oauth_layout->addWidget(lbl_token);
	oauth_layout->addLayout(token_row);
	root->addWidget(oauth_group);

	// Gruppe 2 (M5): AI-Import & Animationen Pfade
	auto *m5_group = new QGroupBox(QStringLiteral("AI-Import & Animationen"), &dlg);
	auto *m5_layout = new QVBoxLayout(m5_group);
	m5_layout->setContentsMargins(12, 16, 12, 12);
	m5_layout->setSpacing(8);

	auto make_path_row = [m5_group](QVBoxLayout *parent_l, const QString &label_txt,
					const std::string &cur_val,
					const std::string &def_val,
					QPushButton *&out_btn) -> QLineEdit * {
		auto *lbl = new QLabel(label_txt, m5_group);
		lbl->setObjectName(QStringLiteral("lpFieldLabel"));
		auto *row = new QHBoxLayout();
		row->setSpacing(8);
		const std::string v = cur_val.empty() ? def_val : cur_val;
		auto *edit = new QLineEdit(QString::fromUtf8(v.c_str()), m5_group);
		edit->setPlaceholderText(QString::fromUtf8(def_val.c_str()));
		out_btn = new QPushButton(QStringLiteral("…"), m5_group);
		out_btn->setObjectName(QStringLiteral("lpBtn"));
		row->addWidget(edit, 1);
		row->addWidget(out_btn);
		parent_l->addWidget(lbl);
		parent_l->addLayout(row);
		return edit;
	};

	QPushButton *browse_key_btn = nullptr;
	auto *edit_key_path = make_path_row(
		m5_layout, QStringLiteral("OPENROUTER-API-KEY-PFAD (api.env)"),
		state_.settings.openrouter_key_path, def_settings.openrouter_key_path,
		browse_key_btn);

	QPushButton *browse_py_btn = nullptr;
	auto *edit_py_path = make_path_row(
		m5_layout, QStringLiteral("MARKITDOWN-PYTHON-PFAD"),
		state_.settings.markitdown_python, def_settings.markitdown_python,
		browse_py_btn);

	QPushButton *browse_anim_btn = nullptr;
	auto *edit_anim_dir = make_path_row(
		m5_layout, QStringLiteral("ANIMATIONS-ORDNER"),
		state_.settings.animation_dir, def_settings.animation_dir,
		browse_anim_btn);

	QPushButton *browse_lieder_btn = nullptr;
	auto *edit_lieder_path = make_path_row(
		m5_layout, QStringLiteral("LIEDER-REFERENZ-PFAD (lieder.md)"),
		state_.settings.lieder_path, def_settings.lieder_path,
		browse_lieder_btn);

	root->addWidget(m5_group);

	// Gruppe 3 (M6-B2/B3): Eigene Farbprofile + YAML-Export/Import
	auto *profiles_group = new QGroupBox(QStringLiteral("Farbprofile"), &dlg);
	auto *profiles_layout = new QVBoxLayout(profiles_group);
	profiles_layout->setContentsMargins(12, 16, 12, 12);
	profiles_layout->setSpacing(8);

	auto *lbl_custom_profiles = new QLabel(QStringLiteral("EIGENE FARBPROFILE"), profiles_group);
	lbl_custom_profiles->setObjectName(QStringLiteral("lpFieldLabel"));

	auto *profile_list = new QListWidget(profiles_group);
	profile_list->setObjectName(QStringLiteral("lpProfileList"));
	profile_list->setMinimumHeight(80);
	profile_list->setMaximumHeight(130);

	auto *del_prof_btn = new QPushButton(QStringLiteral("Löschen"), profiles_group);
	del_prof_btn->setObjectName(QStringLiteral("lpBtnDeleteProfile"));
	del_prof_btn->setIcon(theme_icon("trash.svg"));
	del_prof_btn->setIconSize(QSize(16, 16));
	del_prof_btn->setEnabled(false);

	auto *lbl_yaml_profiles = new QLabel(QStringLiteral("YAML-EXPORT / -IMPORT"), profiles_group);
	lbl_yaml_profiles->setObjectName(QStringLiteral("lpFieldLabel"));

	auto *yaml_combo = new QComboBox(profiles_group);
	yaml_combo->setObjectName(QStringLiteral("lpYamlProfileCombo"));
	yaml_combo->setFixedHeight(34);

	auto *yaml_btn_row = new QHBoxLayout();
	yaml_btn_row->setSpacing(8);
	auto *export_yaml_btn = new QPushButton(QStringLiteral("YAML exportieren …"), profiles_group);
	export_yaml_btn->setObjectName(QStringLiteral("lpBtnExportYaml"));
	auto *import_yaml_btn = new QPushButton(QStringLiteral("YAML importieren …"), profiles_group);
	import_yaml_btn->setObjectName(QStringLiteral("lpBtnImportYaml"));
	yaml_btn_row->addWidget(export_yaml_btn, 1);
	yaml_btn_row->addWidget(import_yaml_btn, 1);

	profiles_layout->addWidget(lbl_custom_profiles);
	profiles_layout->addWidget(profile_list);
	profiles_layout->addWidget(del_prof_btn);
	profiles_layout->addWidget(lbl_yaml_profiles);
	profiles_layout->addWidget(yaml_combo);
	profiles_layout->addLayout(yaml_btn_row);
	root->addWidget(profiles_group);

	auto *status_lbl = new QLabel(&dlg);
	status_lbl->setObjectName(QStringLiteral("lpSettingsStatus"));
	status_lbl->setWordWrap(true);
	root->addWidget(status_lbl);

	auto refresh_profiles_ui = [this, profile_list, del_prof_btn, yaml_combo]() {
		profile_list->clear();
		for (const auto &cp : state_.color_profiles) {
			const QString tc_hex = QString::fromUtf8(
				(cp.text_color.empty() ? "#000000" : cp.text_color).c_str()).toUpper();
			const QString bc_hex = QString::fromUtf8(
				(cp.box_color.empty() ? "#ffffff" : cp.box_color).c_str()).toUpper();
			const QString item_txt = QStringLiteral("%1  —  TEXT %2 · BOX %3")
							 .arg(QString::fromUtf8(cp.name.c_str()),
							      tc_hex, bc_hex);
			profile_list->addItem(item_txt);
		}
		del_prof_btn->setEnabled(profile_list->currentRow() >= 0);

		const QString prev_yaml = yaml_combo->currentText();
		yaml_combo->clear();
		for (const auto &sp : liedplan_profiles()) {
			yaml_combo->addItem(QString::fromUtf8(sp.name.c_str()));
		}
		for (const auto &cp : state_.color_profiles) {
			yaml_combo->addItem(QString::fromUtf8(cp.name.c_str()));
		}
		const int prev_idx = yaml_combo->findText(prev_yaml);
		if (prev_idx >= 0)
			yaml_combo->setCurrentIndex(prev_idx);
	};

	refresh_profiles_ui();

	connect(profile_list, &QListWidget::currentRowChanged, &dlg,
		[del_prof_btn](int row) {
			del_prof_btn->setEnabled(row >= 0);
		});

	connect(del_prof_btn, &QPushButton::clicked, &dlg,
		[this, profile_list, status_lbl, refresh_profiles_ui]() {
			const int row = profile_list->currentRow();
			if (row < 0 ||
			    static_cast<std::size_t>(row) >= state_.color_profiles.size())
				return;
			const QString qname = QString::fromUtf8(
				state_.color_profiles[static_cast<std::size_t>(row)].name.c_str());
			state_.color_profiles.erase(
				state_.color_profiles.begin() +
				static_cast<std::ptrdiff_t>(row));
			save_state();
			rebuild_rows();
			refresh_profiles_ui();
			const QString msg = QStringLiteral("Farbprofil \"%1\" gelöscht").arg(qname);
			status_lbl->setText(msg);
			set_dock_status(msg, false);
		});

	connect(export_yaml_btn, &QPushButton::clicked, &dlg,
		[this, &dlg, yaml_combo, status_lbl]() {
			const std::string active_name =
				yaml_combo->currentText().toUtf8().toStdString();
			const LiedplanProfile *prof =
				liedplan_profile_lookup(active_name, state_.color_profiles);
			if (!prof)
				return;
			const QString qprof_name = QString::fromUtf8(prof->name.c_str());
			QString sanitized = qprof_name;
			sanitized.replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_-]")),
					  QStringLiteral("_"));
			const QString default_file =
				QStringLiteral("farbprofil_%1.yaml").arg(sanitized);
			const QString out_file = QFileDialog::getSaveFileName(
				&dlg, QStringLiteral("Farbprofil als YAML exportieren"),
				default_file, QStringLiteral("YAML-Dateien (*.yaml *.yml)"));
			if (out_file.isEmpty())
				return;

			const std::string yaml_str = liedplan_profile_to_yaml(*prof);
			QFile f(out_file);
			if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
				const QString err_msg = QStringLiteral("Export fehlgeschlagen: %1")
								.arg(f.errorString());
				status_lbl->setText(err_msg);
				set_dock_status(err_msg, true);
				return;
			}
			const qint64 written =
				f.write(yaml_str.data(), static_cast<qint64>(yaml_str.size()));
			f.close();
			if (written != static_cast<qint64>(yaml_str.size())) {
				const QString err_msg =
					QStringLiteral("Export fehlgeschlagen: Schreibfehler.");
				status_lbl->setText(err_msg);
				set_dock_status(err_msg, true);
				return;
			}

			const QString msg =
				QStringLiteral("Profil \"%1\" exportiert").arg(qprof_name);
			status_lbl->setText(msg);
			set_dock_status(msg, false);
		});

	connect(import_yaml_btn, &QPushButton::clicked, &dlg,
		[this, &dlg, yaml_combo, status_lbl, refresh_profiles_ui]() {
			const QString in_file = QFileDialog::getOpenFileName(
				&dlg, QStringLiteral("Farbprofil aus YAML importieren"),
				QString(), QStringLiteral("YAML-Dateien (*.yaml *.yml)"));
			if (in_file.isEmpty())
				return;

			QFile f(in_file);
			if (!f.open(QIODevice::ReadOnly)) {
				const QString err_msg = QStringLiteral("Import fehlgeschlagen: %1")
								.arg(f.errorString());
				status_lbl->setText(err_msg);
				set_dock_status(err_msg, true);
				return;
			}
			const QByteArray raw = f.readAll();
			f.close();

			LiedplanProfile imported;
			std::string parse_err;
			if (!liedplan_yaml_to_profile(raw.toStdString(), imported, parse_err)) {
				const QString qerr = QString::fromUtf8(parse_err.c_str());
				status_lbl->setText(qerr);
				set_dock_status(qerr, true);
				return;
			}

			if (liedplan_is_default_profile(imported.name)) {
				imported.name += " (Import)";
			}

			auto it = std::find_if(
				state_.color_profiles.begin(), state_.color_profiles.end(),
				[&imported](const LiedplanProfile &p) {
					return p.name == imported.name;
				});
			if (it != state_.color_profiles.end()) {
				*it = imported;
			} else {
				state_.color_profiles.push_back(imported);
			}

			save_state();
			rebuild_rows();
			refresh_profiles_ui();
			const QString qimp_name = QString::fromUtf8(imported.name.c_str());
			const int imp_idx = yaml_combo->findText(qimp_name);
			if (imp_idx >= 0)
				yaml_combo->setCurrentIndex(imp_idx);

			const QString msg =
				QStringLiteral("Profil \"%1\" importiert").arg(qimp_name);
			status_lbl->setText(msg);
			set_dock_status(msg, false);
		});

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

	connect(browse_key_btn, &QPushButton::clicked, &dlg, [&dlg, edit_key_path]() {
		const QString chosen = QFileDialog::getOpenFileName(
			&dlg, QStringLiteral("OpenRouter-Key-Datei (api.env) wählen"),
			edit_key_path->text(), QStringLiteral("Env-Dateien (*.env);;Alle Dateien (*.*)"));
		if (!chosen.isEmpty())
			edit_key_path->setText(chosen);
	});

	connect(browse_py_btn, &QPushButton::clicked, &dlg, [&dlg, edit_py_path]() {
		const QString chosen = QFileDialog::getOpenFileName(
			&dlg, QStringLiteral("MarkItDown-Python-Executable wählen"),
			edit_py_path->text(),
			QStringLiteral("Ausführbare Dateien (*.exe);;Alle Dateien (*.*)"));
		if (!chosen.isEmpty())
			edit_py_path->setText(chosen);
	});

	connect(browse_anim_btn, &QPushButton::clicked, &dlg, [&dlg, edit_anim_dir]() {
		const QString chosen = QFileDialog::getExistingDirectory(
			&dlg, QStringLiteral("Animations-Ordner wählen"),
			edit_anim_dir->text());
		if (!chosen.isEmpty())
			edit_anim_dir->setText(chosen);
	});

	connect(browse_lieder_btn, &QPushButton::clicked, &dlg, [&dlg, edit_lieder_path]() {
		const QString chosen = QFileDialog::getOpenFileName(
			&dlg, QStringLiteral("Lieder-Referenzdatei (lieder.md) wählen"),
			edit_lieder_path->text(),
			QStringLiteral("Markdown-Dateien (*.md);;Alle Dateien (*.*)"));
		if (!chosen.isEmpty())
			edit_lieder_path->setText(chosen);
	});

	connect(reset_btn, &QPushButton::clicked, &dlg,
		[edit_form, edit_token, edit_key_path, edit_py_path, edit_anim_dir,
		 edit_lieder_path, def_settings]() {
			edit_form->setText(QString::fromUtf8(liedplan_import::kDefaultFormId));
			edit_token->setText(QString::fromUtf8(liedplan_import::kDefaultTokenPath));
			edit_key_path->setText(
				QString::fromUtf8(def_settings.openrouter_key_path.c_str()));
			edit_py_path->setText(
				QString::fromUtf8(def_settings.markitdown_python.c_str()));
			edit_anim_dir->setText(
				QString::fromUtf8(def_settings.animation_dir.c_str()));
			edit_lieder_path->setText(
				QString::fromUtf8(def_settings.lieder_path.c_str()));
		});

	connect(save_btn, &QPushButton::clicked, &dlg,
		[this, &dlg, edit_form, edit_token, edit_key_path, edit_py_path, edit_anim_dir,
		 edit_lieder_path, def_settings]() {
			const std::string fid = edit_form->text().trimmed().toUtf8().toStdString();
			const std::string tpath =
				edit_token->text().trimmed().toUtf8().toStdString();
			const std::string kpath =
				edit_key_path->text().trimmed().toUtf8().toStdString();
			const std::string pypath =
				edit_py_path->text().trimmed().toUtf8().toStdString();
			const std::string adir =
				edit_anim_dir->text().trimmed().toUtf8().toStdString();
			const std::string lpath =
				edit_lieder_path->text().trimmed().toUtf8().toStdString();

			state_.settings.google_form_id =
				fid.empty() ? liedplan_import::kDefaultFormId : fid;
			state_.settings.token_path =
				tpath.empty() ? liedplan_import::kDefaultTokenPath : tpath;
			state_.settings.openrouter_key_path =
				kpath.empty() ? def_settings.openrouter_key_path : kpath;
			state_.settings.markitdown_python =
				pypath.empty() ? def_settings.markitdown_python : pypath;
			state_.settings.animation_dir =
				adir.empty() ? def_settings.animation_dir : adir;
			state_.settings.lieder_path =
				lpath.empty() ? def_settings.lieder_path : lpath;

			save_state();
			set_dock_status(QStringLiteral("Einstellungen gespeichert."), false);
			dlg.accept();
		});

	dlg.exec();
}

void LiedplanDock::save_state()
{
	if (!state_path_.empty()) {
		state_.save(state_path_);
		lp_log("Zustand gespeichert (" + std::to_string(state_.cards.size()) + " Karten)");
	}
}
