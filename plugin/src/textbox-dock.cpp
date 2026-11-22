/*
 * textbox-dock.cpp — Qt-basiertes Hauptbedienpanel für Textbox in OBS Studio (v3).
 *
 * Zweck:
 *   - Implementiert die Benutzeroberfläche TextboxDock für das OBS Studio QDockWidget.
 *   - Verwaltet die Karten-Eingabezeilen, Schnell-Profile, Farben und Deckkraft.
 *   - Steuert Import-Workflows (Google Forms API, CSV/XLSX/ZIP-Upload, AI/LLM-Analyse).
 *   - Synchronisiert Karteninhalte und Aktionen mit OBS-Szenen via textbox_scene.
 */

#ifndef _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#endif

#include "textbox-dock.hpp"
#include "textbox-events.hpp"
#include "textbox-html.hpp"
#include "textbox-profiles.hpp"
#include "textbox-scene.hpp"
#include "textbox-theme-icons.hpp"
#include "textbox-log.hpp"

#include <obs.h>

#ifdef ENABLE_FRONTEND_API
#include <obs-frontend-api.h>
#endif

#include <QAction>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QDate>
#include <QDialog>
#include <QFile>
#include <QGuiApplication>
#include <QScreen>
#include <QFileDialog>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
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

namespace {
// QScrollArea::minimumSizeHint() = qMax(widget-minimumSizeHint, own minimum):
// widgetResizable=true propagiert so den Content-min nach oben in den Dialog,
// der wächst dann über die Bildschirmhöhe. Subclass kappt das Minimum.
class CappedScrollArea : public QScrollArea {
public:
	explicit CappedScrollArea(QWidget *parent = nullptr) : QScrollArea(parent) { setMinimumSize(0, 0); }
	QSize minimumSizeHint() const override { return QSize(0, 0); }
};

// Hover-Propagation: findet den nächsten tbCardOuter-Vorfahren eines Widgets.
QWidget *find_tb_card_outer(QWidget *w)
{
	while (w) {
		if (w->objectName() == QStringLiteral("tbCardOuter"))
			return w;
		w = w->parentWidget();
	}
	return nullptr;
}

// Findet rekursiv das tiefste sichtbare Widget unter pos im viewport-Baum.
QWidget *deepest_child_at(QWidget *parent, QPoint pos)
{
	if (!parent)
		return nullptr;
	QWidget *direct = parent->childAt(pos);
	if (!direct)
		return nullptr;
	QPoint local = direct->mapFromParent(pos);
	QWidget *deeper = deepest_child_at(direct, local);
	return deeper ? deeper : direct;
}

// Event-Filter für den ScrollArea-Viewport: tracked MouseMove/Leave und setzt
// tbHover-Property auf dem tbCardOuter-Ancestor. Nur EINE Karte gleichzeitig aktiv.
class CardHoverFilter : public QObject {
	QWidget *hovered_ = nullptr;
public:
	explicit CardHoverFilter(QObject *parent = nullptr) : QObject(parent) {}
protected:
	bool eventFilter(QObject *obj, QEvent *ev) override
	{
		auto *viewport = qobject_cast<QWidget *>(obj);
		if (!viewport)
			return QObject::eventFilter(obj, ev);
		switch (ev->type()) {
		case QEvent::MouseMove: {
			QWidget *outer = nullptr;
			if (auto *me = static_cast<QMouseEvent *>(ev)) {
				QPoint local = viewport->mapFromGlobal(me->globalPosition().toPoint());
				if (viewport->rect().contains(local)) {
					if (QWidget *child = deepest_child_at(viewport, local))
						outer = find_tb_card_outer(child);
				}
			}
			if (outer == hovered_)
				break;
			// Alte Karte clearen.
			if (hovered_) {
				hovered_->setProperty("tbHover", QVariant());
				if (QStyle *st = hovered_->style()) {
					st->unpolish(hovered_);
					st->polish(hovered_);
				}
				hovered_->update();
			}
			hovered_ = outer;
			// Neue Karte markieren.
			if (hovered_) {
				hovered_->setProperty("tbHover", QStringLiteral("1"));
				if (QStyle *st = hovered_->style()) {
					st->unpolish(hovered_);
					st->polish(hovered_);
				}
				hovered_->update();
			}
			break;
		}
		case QEvent::Leave: {
			if (hovered_) {
				hovered_->setProperty("tbHover", QVariant());
				if (QStyle *st = hovered_->style()) {
					st->unpolish(hovered_);
					st->polish(hovered_);
				}
				hovered_->update();
				hovered_ = nullptr;
			}
			break;
		}
		default:
			break;
		}
		return QObject::eventFilter(obj, ev);
	}
};
}
#include <QShortcut>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>
#include <QtGlobal>

#include <QDesktopServices>
#include <QEvent>

#include <algorithm>
#include <functional>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <thread>

namespace {

constexpr int kDefaultCardCount = 12;

// M7-1: Label-Format der Karten-Suche ("id · titel", Titel getrimmt + auf 20
// Zeichen gekürzt; leer = nur id). Ein Helper für beide Aufrufstellen
// (textChanged-Lambda + refresh_top_row).
QString card_search_label(const QString &id, const QString &title)
{
	QString t = title.trimmed();
	if (t.size() > 20)
		t = t.left(20);
	return t.isEmpty() ? id : QStringLiteral("%1 · %2").arg(id, t);
}

// 15.11 (User-Vorgabe): alle Datei-Picker nutzen den NATIVEN Windows-Dialog.
// Der Qt-eigene Dialog (TEXTBOX_NO_NATIVE_DIALOG) ist entfernt — nichts Eigenes.
QFileDialog::Options dialog_options()
{
	return QFileDialog::Options();
}

// RAII-Wächter zum Zählen laufender Hintergrund-Threads für den sauberen Modul-Shutdown.

// Abschnittsüberschrift als QToolButton (einklappbare Bereiche).
static QToolButton *make_section_toolbtn(const QString &text, QWidget *parent)
{
	auto *btn = new QToolButton(parent);
	btn->setObjectName(QStringLiteral("tbFieldLabel"));
	btn->setAutoRaise(true);
	btn->setText(text);
	btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
	btn->setContentsMargins(0, 0, 0, 0);
	return btn;
}

struct ActiveJobGuard {
	ActiveJobGuard() { ++g_active_jobs; }
	~ActiveJobGuard() { --g_active_jobs; }
	ActiveJobGuard(const ActiveJobGuard &) = delete;
	ActiveJobGuard &operator=(const ActiveJobGuard &) = delete;
};

// QTextEdit frisst Tab/Backtab (focusNextPrevChild ist protected). Subklasse
// reicht die Tasten an die Dialog-Fokus-Kette weiter.
struct PromptTabEdit : QPlainTextEdit {
	explicit PromptTabEdit(const QString &text, QWidget *parent = nullptr) : QPlainTextEdit(text, parent) {}
	void keyPressEvent(QKeyEvent *ke) override {
		if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
			if (focusNextPrevChild(ke->key() == Qt::Key_Tab))
				return;
		}
		QPlainTextEdit::keyPressEvent(ke);
	}
};

// Hält die Timer- und Zustandsdaten für das verzögerte Speichern pro Dock-Instanz.
struct DockSaveScheduler {
	QPointer<QTimer> timer;         // Debounce-Timer (150 ms) für save_state()
	QPointer<QPushButton> undo_btn; // Referenz auf den Undo-Button (Enabled-State)
	std::vector<std::string> dirty_cards; // Karten-IDs, deren HTML neu geschrieben werden muss
};

// Pro Dock-Instanz ein Scheduler; überlebt das Dock kurz (via QPointer im Timer).
std::map<const TextboxDock *, DockSaveScheduler> g_dock_schedulers;

// Sichert den aktuellen Kartenstand für einstufiges Undo und aktiviert den Undo-Button.
void capture_undo_snapshot(const TextboxDock *dock, TextboxState &state)
{
	state.undo_snapshot_ = state.cards;
	state.undo_available_ = true;
	auto it = g_dock_schedulers.find(dock);
	if (it != g_dock_schedulers.end() && it->second.undo_btn) {
		it->second.undo_btn->setEnabled(true);
	}
}

// Zählt die belegten Blöcke einer Einreichung (mindestens eines der Felder nr/zeilen/titel nicht leer).
std::size_t count_service_blocks(const textbox_import::TextboxService &svc)
{
	std::size_t count = 0;
	for (const auto &pt : svc.punkte) {
		if (!pt.nr.empty() || !pt.zeilen.empty() || !pt.titel.empty())
			++count;
	}
	return count;
}

// Durchsucht alle Szenen nach einer Browser-Source zur Karten-ID und schiebt
// die aktuellen Kartendaten per push_update (CSS-Vars + Event) live in den Browser.
void push_card_live(const TextboxCard &card, const TextboxSettings &settings)
{
	const std::string name = textbox_scene::source_name(card.id);
	const std::string legacy_name = "Textbox " + card.id;
	std::vector<obs_scene_t *> scenes;
	auto cb = [](void *param, obs_source_t *source) -> bool {
		if (source && obs_source_is_scene(source)) {
			auto *list = static_cast<std::vector<obs_scene_t *> *>(param);
			if (obs_scene_t *sc = obs_scene_from_source(source))
				list->push_back(sc);
		}
		return true;
	};
	obs_enum_sources(cb, &scenes);
	if (scenes.empty())
		obs_enum_scenes(cb, &scenes);

	const std::string dir = textbox_html::cards_dir();
	for (obs_scene_t *scene : scenes) {
		obs_sceneitem_t *item = obs_scene_find_source(scene, name.c_str());
		if (!item)
			item = obs_scene_find_source(scene, legacy_name.c_str());
		if (!item)
			continue;
		obs_source_t *src = obs_sceneitem_get_source(item);
		if (!src)
			continue;
		const char *id = obs_source_get_unversioned_id(src);
		if (!id)
			id = obs_source_get_id(src);
		if (!id || std::strcmp(id, "browser_source") != 0)
			continue;
		textbox_html::push_update(src, card, false, settings, dir);
	}
}

// Markiert eine Karte als "dirty" (HTML muss neu geschrieben werden) und startet
// einen Debounce-Timer (150 ms). Der Timer sammelt mehrere Änderungen, bevor save_state() läuft.
void mark_card_dirty_and_schedule(const TextboxDock *dock, const std::string &card_id)
{
	auto it = g_dock_schedulers.find(dock);
	if (it == g_dock_schedulers.end() || !it->second.timer)
		return;
	if (!card_id.empty()) {
		auto &vec = it->second.dirty_cards;
		if (std::find(vec.begin(), vec.end(), card_id) == vec.end())
			vec.push_back(card_id);
	}
	it->second.timer->start(150);
}

std::map<std::string, std::pair<std::string, std::string>>
load_reference_table(const std::string &referenz_path)
{
	if (referenz_path.empty())
		return {};
	std::error_code ec;
	const std::filesystem::path fsp = std::filesystem::u8path(referenz_path);
	if (!std::filesystem::exists(fsp, ec) || ec)
		return {};
	std::ifstream in(fsp, std::ios::binary);
	if (!in.is_open())
		return {};
	std::ostringstream ss;
	ss << in.rdbuf();
	return textbox_ai::parse_reference_table(ss.str());
}

// Erzeugt eine Standardkarte für Index i (box1..boxN) mit Standardprofil "Karte Dunkel".
TextboxCard make_default_card(std::size_t i)
{
	TextboxCard c;
	c.id = "box" + std::to_string(i + 1);
	c.profile = "Karte Dunkel";
	if (const TextboxProfile *prof = profile_by_name(c.profile)) {
		profile_apply(*prof, c);
	}
	return c;
}

// Prüft, ob zeile1 der Karte i einem Standard-Eintrag entspricht.
bool is_standard_card(const TextboxCard &c, std::size_t i)
{
	return i < textbox_import::PROGRAM_POINTS.size() &&
	       c.zeile1 == textbox_import::PROGRAM_POINTS[i];
}

// IMP-N: Nach dem Löschen eines Farbprofils referencieren Karten evtl. einen
// nicht mehr existierenden Namen und das Combo wäre dauerhaft leer (seeded
// verhindert Nachsaat). Defaults nachsaaten, damit die Profilfunktion nicht
// tot ist; Karten mit dangling-Profilreferenz auf "" setzen (baked Farben
// bleiben sichtbar). Liefert true, wenn Defaults nachgesät wurden.
bool reconcile_profiles_after_delete(TextboxState &state)
{
	const bool reseeded = textbox_ensure_profiles_available(state.color_profiles);
	for (auto &c : state.cards) {
		if (!c.profile.empty() && !textbox_profile_lookup(c.profile, state.color_profiles))
			c.profile = "";
	}
	if (!state.active_profile.empty() && !textbox_profile_lookup(state.active_profile, state.color_profiles))
		state.active_profile.clear();
	return reseeded;
}

} // namespace

#ifdef ENABLE_FRONTEND_API
// Frontend-Event: heilt fehlende Karten-Sources nach dem Laden der
// Szenensammlung und nach Szenenwechsel (die Quelle gehört pro Szene).
void textbox_self_heal_event(obs_frontend_event event, void *data)
{
	if (event != OBS_FRONTEND_EVENT_FINISHED_LOADING &&
	    event != OBS_FRONTEND_EVENT_SCENE_CHANGED)
		return;
	auto *dock = static_cast<TextboxDock *>(data);
	if (dock)
		dock->ensure_card_sources();
}
#endif

// changeEvent entfernt — natives OBS-Styling via aktiver Style

void TextboxDock::ensure_card_sources()
{
	for (const auto &card : state_.cards) {
		if (cancel_ && *cancel_)
			break;
		bool has_text = false;
		for (const auto &r : effective_rows(card))
			if (!r.value.empty())
				has_text = true;
		if (has_text)
			textbox_scene::send_to_current_scene(card, state_.settings);
	}
}

TextboxDock::TextboxDock(const std::string &state_path, QWidget *parent)
	: QWidget(parent),
	  state_path_(state_path)
{
	setObjectName(QStringLiteral("textboxDock"));
	setAttribute(Qt::WA_StyledBackground, true);

	auto *save_timer = new QTimer(this);
	save_timer->setObjectName(QStringLiteral("tbSaveTimer"));
	save_timer->setSingleShot(true);
	save_timer->setInterval(150);
	g_dock_schedulers[this].timer = save_timer;
	connect(save_timer, &QTimer::timeout, this, &TextboxDock::save_state);
	connect(this, &QObject::destroyed, [this]() {
		g_dock_schedulers.erase(this);
	});

	/* OBS-Theme-Icons aus der DLL nach <config>/textbox-studio/icons/ schreiben. */
	if (!state_path_.empty()) {
		const std::filesystem::path icons_dir =
			std::filesystem::u8path(state_path_).parent_path() / "icons";
		std::string icon_err;
		if (textbox_theme_icons::write_icons(icons_dir, false, &icon_err)) {
			const auto u8 = icons_dir.u8string();
			icon_dir_ = QString::fromUtf8(u8.data(), static_cast<int>(u8.size()));
		} else {
			blog(LOG_WARNING, "[textbox] Icons konnten nicht geschrieben werden: %s",
			     icon_err.c_str());
		}
	}

	auto *root_layout = new QVBoxLayout(this);
	root_layout->setContentsMargins(4, 4, 4, 4);
	root_layout->setSpacing(4);

	auto *top_row = new QHBoxLayout();
	top_row->setContentsMargins(4, 0, 4, 2);
	top_row->setSpacing(8);

	profil_select_ = new QComboBox(this);
	profil_select_->setEditable(true);
	profil_select_->setInsertPolicy(QComboBox::NoInsert);
	profil_select_->setMinimumWidth(fontMetrics().averageCharWidth() * 14);
	profil_select_->setAccessibleName(QStringLiteral("Schnellprofil-Wahl"));
	profil_select_->setToolTip(
		QStringLiteral("Schnellprofile-Liste (gespeicherte Karten-Sets, nicht Kartenfarben)"));
	if (QCompleter *c = profil_select_->completer()) {
		c->setCaseSensitivity(Qt::CaseInsensitive);
		c->setFilterMode(Qt::MatchContains);
	}
	if (profil_select_->lineEdit()) {
		profil_select_->lineEdit()->setPlaceholderText(
			QStringLiteral("Schnellprofil suchen…"));
		profil_select_->lineEdit()->setAccessibleName(QStringLiteral("Schnellprofil-Wahl"));
		profil_select_->lineEdit()->setToolTip(
			QStringLiteral("Schnellprofile-Liste (gespeicherte Karten-Sets, nicht Kartenfarben)"));
	}

	card_search_ = new QComboBox(this);
	card_search_->setEditable(true);
	card_search_->setInsertPolicy(QComboBox::NoInsert);
	card_search_->setMinimumWidth(fontMetrics().averageCharWidth() * 12);
	card_search_->setAccessibleName(QStringLiteral("Karten-Suche"));
	if (QCompleter *c = card_search_->completer()) {
		c->setCaseSensitivity(Qt::CaseInsensitive);
		c->setFilterMode(Qt::MatchContains);
	}
	if (card_search_->lineEdit()) {
		card_search_->lineEdit()->setPlaceholderText(QStringLiteral("Karte suchen…"));
		card_search_->lineEdit()->setAccessibleName(QStringLiteral("Karten-Suche"));
	}

	top_row->addWidget(profil_select_, 1);
	profil_del_btn_ = new QToolButton(this);
	profil_del_btn_->setObjectName(QStringLiteral("tbProfilDelBtn"));
	const int btn_icon_sz = style()->pixelMetric(QStyle::PM_ButtonIconSize, nullptr, this);
	profil_del_btn_->setFixedSize(btn_icon_sz, btn_icon_sz);
	profil_del_btn_->setAutoRaise(true);
	profil_del_btn_->setIcon(theme_icon("trash.svg"));
	profil_del_btn_->setIconSize(QSize(16, 16));
	profil_del_btn_->setToolTip(QStringLiteral("Gewähltes Schnell-Profil löschen"));
	profil_del_btn_->setAccessibleName(QStringLiteral("Gewähltes Schnell-Profil löschen"));
	profil_del_btn_->setEnabled(false);
	profil_del_btn_->setProperty("class", "iconbtn");
	profil_del_btn_->setProperty("tbDanger", true);
	top_row->addWidget(profil_del_btn_);
	top_row->addWidget(card_search_, 1);
	root_layout->addLayout(top_row);

	scroll_area_ = new QScrollArea(this);
	scroll_area_->setWidgetResizable(true);
	scroll_area_->setFrameShape(QFrame::NoFrame);

	// Karten-Hover-Propagation: tbCardOuter :hover auch bei Maus über Kindern.
	auto *hover_filter = new CardHoverFilter(scroll_area_);
	scroll_area_->viewport()->setMouseTracking(true);
	scroll_area_->viewport()->installEventFilter(hover_filter);

	auto *cards_container = new QWidget(scroll_area_);
	cards_container->setObjectName(QStringLiteral("tbCardsContainer"));
	cards_container->setAttribute(Qt::WA_StyledBackground, true);

	cards_layout_ = new QVBoxLayout(cards_container);
	cards_layout_->setContentsMargins(4, 4, 16, 4); // rechter Rand 16px für Scrollbar-Abstand
	cards_layout_->setSpacing(8);
	cards_layout_->addStretch(1);

	scroll_area_->setWidget(cards_container);
	root_layout->addWidget(scroll_area_, 1);

	dock_status_lbl_ = new QLabel(this);
	dock_status_lbl_->setObjectName(QStringLiteral("tbDockStatus"));
	dock_status_lbl_->setContentsMargins(4, 0, 4, 0);
	dock_status_lbl_->setWordWrap(true);
	dock_status_lbl_->setVisible(false);
	dock_status_close_ = new QToolButton(this); // c1: Schließen-Button für die Statusmeldung
	dock_status_close_->setObjectName(QStringLiteral("tbDockStatusClose"));
	dock_status_close_->setAutoRaise(true);
	dock_status_close_->setIcon(theme_icon("close.svg"));
	dock_status_close_->setIconSize(QSize(12, 12));
	dock_status_close_->setToolTip(QStringLiteral("Meldung schließen"));
	dock_status_close_->setAccessibleName(QStringLiteral("Meldung schließen"));
	dock_status_close_->setVisible(false);
	connect(dock_status_close_, &QToolButton::clicked, this, [this]() {
		if (dock_status_lbl_)
			dock_status_lbl_->setVisible(false);
		if (dock_status_close_)
			dock_status_close_->setVisible(false);
		if (dock_status_timer_)
			dock_status_timer_->stop();
	});
	auto *status_row = new QHBoxLayout();
	status_row->setContentsMargins(4, 0, 4, 0);
	status_row->setSpacing(4);
	status_row->addWidget(dock_status_lbl_, 1);
	status_row->addWidget(dock_status_close_, 0);
	root_layout->addLayout(status_row);

	auto *bottom_toolbar = new QHBoxLayout();
	bottom_toolbar->setContentsMargins(4, 4, 4, 4);
	bottom_toolbar->setSpacing(6);

	auto make_tool_btn = [this, btn_icon_sz](const QString &obj_name, const char *icon_name,
				    const QString &tooltip) -> QToolButton * {
		auto *btn = new QToolButton(this);
		btn->setObjectName(obj_name);
		btn->setFixedSize(btn_icon_sz, btn_icon_sz);
		btn->setAutoRaise(true);
		btn->setIcon(theme_icon(icon_name));
		btn->setIconSize(QSize(16, 16));
		btn->setToolTip(tooltip);
		btn->setAccessibleName(tooltip);
		btn->setProperty("class", "iconbtn");
		return btn;
	};

	auto *tool_add = make_tool_btn(QStringLiteral("tbToolAdd"), "plus.svg",
				       QStringLiteral("Neue Karte"));
	// Kompakter Button für einstufiges Rückgängig (Undo)
	auto *tool_undo = new QPushButton(QStringLiteral("↶ Rückgängig"), this);
	tool_undo->setObjectName(QStringLiteral("tbToolUndo"));
	tool_undo->setFixedHeight(btn_icon_sz);
	tool_undo->setToolTip(
		QStringLiteral("Letzte destruktive Aktion rückgängig machen (Strg+Z)"));
	tool_undo->setEnabled(state_.undo_available_);
	g_dock_schedulers[this].undo_btn = tool_undo;

	auto *tool_import = make_tool_btn(
		QStringLiteral("tbToolImport"), "download.svg",
		QStringLiteral("AI-Import / Datei-Import (CSV, XLSX, ZIP, Google)"));
	auto *tool_settings = make_tool_btn(
		QStringLiteral("tbToolSettings"), "cogs.svg",
		QStringLiteral("Schnell-Profile / Einstellungen / Backup / Restore"));
	tool_settings->setPopupMode(QToolButton::InstantPopup);

	auto *settings_menu = new QMenu(tool_settings);
	QAction *act_quick = settings_menu->addAction(QStringLiteral("Schnell-Profile…"));
	settings_menu->addSeparator();
	QAction *act_settings = settings_menu->addAction(QStringLiteral("Einstellungen…"));
	// IMP-F (F2b): Karten-Vorlagen (Templates) direkt im Zahnrad-Menü.
	QAction *act_tpl_folder =
		settings_menu->addAction(QStringLiteral("Vorlagen-Ordner öffnen"));
	QAction *act_tpl_copy =
		settings_menu->addAction(QStringLiteral("Vorlage aus index.html (classic) kopieren"));
	settings_menu->addSeparator();
	QAction *act_backup = settings_menu->addAction(QStringLiteral("Backup erstellen"));
	QAction *act_restore = settings_menu->addAction(QStringLiteral("Backup wiederherstellen"));
	tool_settings->setMenu(settings_menu);

	bottom_toolbar->addWidget(tool_add);
	bottom_toolbar->addWidget(tool_undo);
	bottom_toolbar->addWidget(tool_import);
	bottom_toolbar->addWidget(tool_settings);
	bottom_toolbar->addStretch(1);
	root_layout->addLayout(bottom_toolbar);

	// Gemeinsamer Handler für den Undo-Button und das Tastenkürzel Strg+Z
	auto perform_undo = [this, tool_undo]() {
		if (!state_.undo_available_)
			return;
		std::vector<std::string> prev_ids;
		prev_ids.reserve(state_.cards.size());
		for (const auto &c : state_.cards)
			prev_ids.push_back(c.id);

		state_.cards = state_.undo_snapshot_;
		for (const auto &c : state_.cards) {
			if (c.id.rfind("box", 0) == 0 && c.id.size() > 3) {
				try {
					const int num = std::stoi(c.id.substr(3));
					if (num >= state_.next_id)
						state_.next_id = num + 1;
				} catch (...) {
				}
			}
		}
		// IMP-S: Undo stellt Karten-Daten wieder her — die Karten-HTMLs wurden
		// beim Leeren gelöscht bzw. weggepruned. Alle wiederhergestellten Karten
		// als dirty markieren, damit save_state() die HTMLs neu schreibt.
		for (const auto &c : state_.cards)
			mark_card_dirty_and_schedule(this, c.id);
		save_state();
		rebuild_rows();
		sync_all_cards_live(prev_ids);
		// IMP-L5: Undo kann Karten wieder entfernen — deren Karten-HTML wäre
		// sonst ein Orphan in cards/. Nach dem Sync dieselben Orphan-Reste
		// wegwerfen wie beim Startup-Prune.
		std::vector<std::string> live_ids;
		live_ids.reserve(state_.cards.size());
		for (const auto &c : state_.cards)
			live_ids.push_back(c.id);
		textbox_html::prune_orphan_card_html(live_ids);
		state_.undo_available_ = false;
		tool_undo->setEnabled(false);
		set_dock_status(QStringLiteral("Vorheriger Zustand wiederhergestellt"), false);
	};

	auto *undo_shortcut = new QShortcut(QKeySequence::Undo, this);
	connect(tool_undo, &QPushButton::clicked, this, perform_undo);
	connect(undo_shortcut, &QShortcut::activated, this, perform_undo);

	connect(tool_add, &QToolButton::clicked, this, &TextboxDock::on_add_card);
	connect(tool_import, &QToolButton::clicked, this, &TextboxDock::on_open_import_dialog);
	connect(act_quick, &QAction::triggered, this, &TextboxDock::on_open_quick_profiles_dialog);
	connect(act_settings, &QAction::triggered, this, &TextboxDock::on_open_settings_dialog);
	connect(act_tpl_folder, &QAction::triggered, this, &TextboxDock::on_open_templates_folder);
	connect(act_tpl_copy, &QAction::triggered, this, &TextboxDock::on_copy_template_from_classic);
	connect(act_backup, &QAction::triggered, this, &TextboxDock::on_backup_state);
	connect(act_restore, &QAction::triggered, this, &TextboxDock::on_restore_state);

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
			} else {
				// IMP-O: ohne Treffer war bisher kein Feedback sichtbar —
				// der alte Statusblinker blieb stehen.
				set_dock_status(QStringLiteral("Keine Karte gefunden für „%1“").arg(query), true);
			}
		});
	}

	bool file_exists = false;
	if (!state_path_.empty()) {
		std::error_code ec;
		file_exists = std::filesystem::exists(std::filesystem::u8path(state_path_), ec);
	}

	const std::string err = state_.load(state_path_);
	blog(LOG_INFO, "[textbox] Zustand geladen (%zu Karten, Datei: %s)", state_.cards.size(), state_path_.c_str());

	// Reste gelöschter Karten aufräumen, damit cards/ nicht mit Orphan-HTML vollläuft.
	std::vector<std::string> live_ids;
	live_ids.reserve(state_.cards.size());
	for (const auto &c : state_.cards)
		live_ids.push_back(c.id);
	const std::size_t pruned = textbox_html::prune_orphan_card_html(live_ids);
	if (pruned > 0)
		blog(LOG_INFO, "[textbox] %zu Orphan-Karten-HTML gelöscht", pruned);

	if (!err.empty()) {
		if (!state_path_.empty()) {
			std::error_code ec;
			std::filesystem::copy_file(
				std::filesystem::u8path(state_path_),
				std::filesystem::u8path(state_path_ + ".bak"),
				std::filesystem::copy_options::overwrite_existing, ec);
		}
		for (int i = 0; i < kDefaultCardCount; ++i) {
			state_.cards.push_back(make_default_card(static_cast<std::size_t>(i)));
		}
		state_.next_id = kDefaultCardCount + 1;
		set_dock_status(
			QStringLiteral("Warnung: textbox-state.json war beschädigt (Backup .bak angelegt)"),
			true);
	} else if (!file_exists) {
		for (int i = 0; i < kDefaultCardCount; ++i) {
			state_.add_card();
		}
		save_state();
	}

	/* Standardprofile einmalig in state_.color_profiles materialisieren. */
	if (!state_.profiles_seeded) {
		for (const auto &bp : textbox_profiles()) {
			bool present = false;
			for (const auto &cp : state_.color_profiles) {
				if (cp.name == bp.name) {
					present = true;
					break;
				}
			}
			if (!present)
				state_.color_profiles.push_back(bp);
		}
		state_.profiles_seeded = true;
		save_state();
	} else if (state_.color_profiles.empty()) {
		// IMP-N: seeded, aber Liste leer (alle Profile gelöscht) -> Defaults
		// nachsaaten; sonst wäre das Profil-Combo dauerhaft leer und die
		// Profilfunktion tot. seed-Flag bleibt true (einzelne gelöschte
		// Profiles kommen bewusst nicht zurück).
		textbox_ensure_profiles_available(state_.color_profiles);
		save_state();
		set_dock_status(QStringLiteral("Standardprofile nachgesät"), false);
	}
	// IMP-N: dangling Profilreferenzen (profil/active_profile zeigen auf
	// einen gelöschten Namen) beim Start bereinigen.
	reconcile_profiles_after_delete(state_);

	rebuild_rows();

	tb_event_log(tb_event_log_dir_from_state_path(state_path_),
		     "Dock geöffnet/registriert");

	// Spec F2b: Karten-HTML ab Start aus Zustand + Vorlagen neu erzeugen und die
	// Browser-Dokumente neu laden — so wirken Vorlagen (auch Vorlagen-Ordner-
	// Änderungen) ohne Dock-Interaktion und HTML bleibt konsistent mit dem State.
	for (const auto &card : state_.cards) {
		if (cancel_ && *cancel_)
			break;
		textbox_scene::reload_everywhere(card, state_.settings);
	}

	// Self-healing via Frontend-Event (Szenen nach Dock-Aufbau laden).
#ifdef ENABLE_FRONTEND_API
	obs_frontend_add_event_callback(&textbox_self_heal_event, this);
#endif

	setMinimumSize(360, 240);
}

TextboxDock::~TextboxDock()
{
#ifdef ENABLE_FRONTEND_API
	obs_frontend_remove_event_callback(&textbox_self_heal_event, this);
#endif
	// Kooperatives Abbruch-Flag vor sämtlichen weiteren Aufräumarbeiten setzen.
	if (cancel_)
		cancel_->store(true);
	auto it = g_dock_schedulers.find(this);
	if (it != g_dock_schedulers.end()) {
		if (it->second.timer && it->second.timer->isActive())
			it->second.timer->stop();
		g_dock_schedulers.erase(it);
	}
	// Verbleibende Karten-Sources aus Szenen entfernen (Shutdown-Rest).
	for (const auto &card : state_.cards) {
		textbox_scene::remove_everywhere(card.id);
	}
}

TextboxCard *TextboxDock::find_card_mut(const std::string &id)
{
	for (auto &c : state_.cards) {
		if (c.id == id)
			return &c;
	}
	return nullptr;
}

QIcon TextboxDock::theme_icon(const char *name) const
{
	if (icon_dir_.isEmpty())
		return QIcon();
	return QIcon(icon_dir_ + QLatin1Char('/') + QLatin1String(name));
}

void TextboxDock::create_row_widget(const TextboxCard &card)
{
	const int btn_sz = style()->pixelMetric(QStyle::PM_ButtonIconSize, nullptr, this);
	auto *row_widget = new QFrame(this);
	row_widget->setObjectName(QStringLiteral("tbCardOuter"));
	row_widget->setProperty("tbCardId", QString::fromUtf8(card.id.c_str()));
	row_widget->setMinimumWidth(fontMetrics().averageCharWidth() * 28);
	row_widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

	auto *outer_layout = new QVBoxLayout(row_widget);
	outer_layout->setContentsMargins(3, 3, 3, 3);
	outer_layout->setSpacing(0);

	auto *card_inner = new QFrame(row_widget);
	card_inner->setObjectName(QStringLiteral("tbCardInner"));
	outer_layout->addWidget(card_inner);

	auto *inner_layout = new QVBoxLayout(card_inner);
	inner_layout->setContentsMargins(10, 10, 10, 10);
	inner_layout->setSpacing(8);

	auto *id_bar = new QFrame(card_inner);
	id_bar->setObjectName(QStringLiteral("tbIdBar"));
	auto *id_layout = new QHBoxLayout(id_bar);
	id_layout->setContentsMargins(8, 4, 8, 4);
	id_layout->setSpacing(8);

	auto *id_prefix = new QLabel(QStringLiteral("ID"), id_bar);
	id_prefix->setObjectName(QStringLiteral("tbIdPrefix"));

	auto *id_edit = new QLineEdit(QString::fromUtf8(card.id.c_str()), id_bar);
	id_edit->setObjectName(QStringLiteral("tbIdValue"));
	id_edit->setToolTip(QStringLiteral("ID umbenennen (Enter); nur Buchstaben/Zahlen/-/_"));
	id_edit->setAccessibleName(QStringLiteral("Karten-ID"));
	id_edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

	auto *style_toggle_btn = new QToolButton(id_bar);
	style_toggle_btn->setObjectName(QStringLiteral("tbStyleToggle"));
	style_toggle_btn->setFixedSize(btn_sz, btn_sz);
	style_toggle_btn->setAutoRaise(true);
	style_toggle_btn->setIcon(theme_icon("chevron_down.svg"));
	style_toggle_btn->setIconSize(QSize(16, 16));
	style_toggle_btn->setToolTip(QStringLiteral("Stil-Optionen einklappen"));
	style_toggle_btn->setAccessibleName(QStringLiteral("Stil-Optionen einklappen"));
	style_toggle_btn->setProperty("class", "iconbtn");

	auto *dup_btn = new QToolButton(id_bar);
	dup_btn->setObjectName(QStringLiteral("tbDupBtn"));
	dup_btn->setFixedSize(btn_sz, btn_sz);
	dup_btn->setAutoRaise(true);
	dup_btn->setText(QStringLiteral("⧉"));
	dup_btn->setToolTip(QStringLiteral("Karte duplizieren (neue ID, gleicher Inhalt + Stil)"));
	dup_btn->setAccessibleName(QStringLiteral("Karte duplizieren (neue ID, gleicher Inhalt + Stil)"));
	dup_btn->setProperty("class", "iconbtn");

	auto *del_btn = new QPushButton(id_bar);
	del_btn->setObjectName(QStringLiteral("tbDelBtn"));
	del_btn->setFixedSize(btn_sz, btn_sz);
	del_btn->setIcon(theme_icon("trash.svg"));
	del_btn->setIconSize(QSize(16, 16));
	del_btn->setToolTip(QStringLiteral("Karte löschen"));
	del_btn->setAccessibleName(QStringLiteral("Karte löschen"));
	del_btn->setProperty("tbDanger", true);

	id_layout->addWidget(id_prefix);
	id_layout->addWidget(id_edit, 1);
	id_layout->addWidget(style_toggle_btn);
	id_layout->addWidget(dup_btn);
	id_layout->addWidget(del_btn);
	inner_layout->addWidget(id_bar);
	inner_layout->addSpacing(2);

	// IMP-G: Zeilen dynamisch (1..8) — Label + Wert, mit +/- Steuerung.
	const std::string card_id = card.id;
	auto schedule_save = [this, card_id]() {
		if (const TextboxCard *c = find_card_mut(card_id)) {
			push_card_live(*c, state_.settings);
		}
		mark_card_dirty_and_schedule(this, card_id);
	};

	auto *rows_frame = new QFrame(card_inner);
	auto *rows_layout = new QVBoxLayout(rows_frame);
	rows_layout->setContentsMargins(0, 0, 0, 0);
	rows_layout->setSpacing(4);

	auto *rows_bar = new QHBoxLayout();
	rows_bar->setContentsMargins(0, 0, 0, 0);
	rows_bar->setSpacing(6);
	auto rows_collapsed = std::make_shared<bool>(false);
	auto *lbl_rows = make_section_toolbtn(QStringLiteral("▼ SPEZIFISCHE CSS-REGELN"), rows_frame);
	lbl_rows->setToolTip(QStringLiteral("Spezifische CSS-Regeln ein-/ausklappen"));
	auto *row_add_btn = new QToolButton(rows_frame);
	row_add_btn->setObjectName(QStringLiteral("tbRowAdd"));
	row_add_btn->setFixedSize(btn_sz, btn_sz);
	row_add_btn->setAutoRaise(true);
	row_add_btn->setIcon(theme_icon("plus.svg"));
	row_add_btn->setIconSize(QSize(16, 16));
	row_add_btn->setToolTip(QStringLiteral("Zeile hinzufügen (max 8)"));
	row_add_btn->setAccessibleName(QStringLiteral("Zeile hinzufügen (max 8)"));
	row_add_btn->setProperty("class", "iconbtn");
	auto *row_del_btn = new QToolButton(rows_frame);
	row_del_btn->setObjectName(QStringLiteral("tbRowDel"));
	row_del_btn->setFixedSize(btn_sz, btn_sz);
	row_del_btn->setAutoRaise(true);
	row_del_btn->setIcon(theme_icon("trash.svg"));
	row_del_btn->setIconSize(QSize(16, 16));
	row_del_btn->setToolTip(QStringLiteral("Letzte Zeile entfernen (min 1)"));
	row_del_btn->setAccessibleName(QStringLiteral("Letzte Zeile entfernen (min 1)"));
	row_del_btn->setProperty("class", "iconbtn");
	row_del_btn->setProperty("tbDanger", true);
	rows_bar->addWidget(lbl_rows);
	rows_bar->addStretch(1);
	rows_bar->addWidget(row_add_btn);
	rows_bar->addWidget(row_del_btn);
	rows_layout->addLayout(rows_bar);
	inner_layout->addWidget(rows_frame);

	// shared_ptr: die connect-Lambdas leben nach Funktionsende weiter.
	auto label_edits = std::make_shared<std::vector<QLineEdit *>>();
	auto value_edits = std::make_shared<std::vector<QLineEdit *>>();
	auto move_btns = std::make_shared<std::vector<QToolButton *>>();
	auto move_row = std::make_shared<std::function<void(QLineEdit *, int)>>();

	connect(lbl_rows, &QToolButton::clicked, rows_frame,
	        [rows_collapsed, label_edits, value_edits, move_btns, lbl_rows]() {
		        *rows_collapsed = !*rows_collapsed;
		        lbl_rows->setText(*rows_collapsed ? QStringLiteral("▶ SPEZIFISCHE CSS-REGELN")
		                                          : QStringLiteral("▼ SPEZIFISCHE CSS-REGELN"));
		        for (auto *e : *label_edits)
			        e->setVisible(!*rows_collapsed);
		        for (auto *e : *value_edits)
			        e->setVisible(!*rows_collapsed);
		        for (auto *b : *move_btns)
			        b->setVisible(!*rows_collapsed);
	        });

	auto add_row_edit = [this, card_inner, rows_layout, rows_collapsed, move_row, move_btns, btn_sz](const QString &label, const QString &value) {
		auto *row = new QHBoxLayout();
		row->setContentsMargins(0, 0, 0, 0);
		row->setSpacing(6);
		auto *ledit = new QLineEdit(label, card_inner);
		ledit->setPlaceholderText(QStringLiteral("--zeile-N"));
		ledit->setToolTip(QStringLiteral(
			"CSS-Variablenname dieser Zeile (mit -- Präfix). Im gerenderten HTML als var(--…) referenzierbar; leer/ohne -- => --zeile-N."));
		ledit->setFixedWidth(fontMetrics().averageCharWidth() * 10);
		ledit->setFixedHeight(btn_sz);
		auto *vedit = new QLineEdit(value, card_inner);
		vedit->setPlaceholderText(QStringLiteral("Text dieser Zeile"));
		vedit->setFixedHeight(btn_sz);
		auto *up_btn = new QToolButton(card_inner);
		up_btn->setObjectName(QStringLiteral("tbRowUp"));
		up_btn->setFixedSize(btn_sz, btn_sz);
		up_btn->setAutoRaise(true);
		up_btn->setText(QStringLiteral("▲"));
		up_btn->setToolTip(QStringLiteral("Zeile nach oben verschieben"));
		up_btn->setAccessibleName(QStringLiteral("Zeile nach oben verschieben"));
		up_btn->setProperty("class", "iconbtn");
		auto *down_btn = new QToolButton(card_inner);
		down_btn->setObjectName(QStringLiteral("tbRowDown"));
		down_btn->setFixedSize(btn_sz, btn_sz);
		down_btn->setAutoRaise(true);
		down_btn->setText(QStringLiteral("▼"));
		down_btn->setToolTip(QStringLiteral("Zeile nach unten verschieben"));
		down_btn->setAccessibleName(QStringLiteral("Zeile nach unten verschieben"));
		down_btn->setProperty("class", "iconbtn");
		QObject::connect(up_btn, &QToolButton::clicked, card_inner,
		                 [move_row, ledit]() { (*move_row)(ledit, -1); });
		QObject::connect(down_btn, &QToolButton::clicked, card_inner,
		                 [move_row, ledit]() { (*move_row)(ledit, +1); });
		if (*rows_collapsed) {
			ledit->setVisible(false);
			vedit->setVisible(false);
			up_btn->setVisible(false);
			down_btn->setVisible(false);
		}
		row->addWidget(ledit);
		row->addWidget(vedit, 1);
		row->addWidget(up_btn);
		row->addWidget(down_btn);
		rows_layout->addLayout(row);
		move_btns->push_back(up_btn);
		move_btns->push_back(down_btn);
		return std::make_pair(ledit, vedit);
	};

	auto collect_rows = [this, card_id, label_edits, value_edits]() -> bool {
		TextboxCard *c = find_card_mut(card_id);
		if (!c)
			return false;
		std::vector<TextboxRow> new_rows;
		for (size_t i = 0; i < label_edits->size(); ++i) {
			TextboxRow r;
			r.label = label_edits->at(i)->text().toUtf8().toStdString();
			r.value = value_edits->at(i)->text().toUtf8().toStdString();
			new_rows.push_back(r);
		}
		c->rows = std::move(new_rows);
		// Legacy-Mirror synchron halten (Import-Check liest zeile1).
		c->zeile1 = c->rows.size() > 0 ? c->rows[0].value : std::string();
		c->zeile2 = c->rows.size() > 1 ? c->rows[1].value : std::string();
		c->zeile3 = c->rows.size() > 2 ? c->rows[2].value : std::string();
		return true;
	};

	auto update_search_entry = [this, card_id](const QString &text) {
		if (!card_search_)
			return;
		const QSignalBlocker blocker(card_search_);
		const QString prev_text = card_search_->currentText();
		const QString id_q = QString::fromUtf8(card_id.c_str());
		for (int j = 0; j < card_search_->count(); ++j) {
			if (card_search_->itemData(j).toString() == id_q) {
				card_search_->setItemText(j, card_search_label(id_q, text));
				break;
			}
		}
		if (card_search_->lineEdit())
			card_search_->lineEdit()->setText(prev_text);
	};

	auto wire_row_edits = [this, label_edits, value_edits, schedule_save, collect_rows, update_search_entry](
			      QLineEdit *ledit, QLineEdit *vedit) {
		connect(ledit, &QLineEdit::textChanged, this, [schedule_save, collect_rows](const QString &) {
			if (collect_rows())
				schedule_save();
		});
		connect(vedit, &QLineEdit::textChanged, this, [schedule_save, collect_rows, update_search_entry](const QString &text) {
			if (!collect_rows())
				return;
			schedule_save();
			update_search_entry(text);
		});
	};

	const auto init_rows = effective_rows(card);
	for (size_t i = 0; i < init_rows.size(); ++i) {
		const QString lbl = !init_rows[i].label.empty()
			       ? QString::fromUtf8(init_rows[i].label.c_str())
			       : QString::fromUtf8(kTextboxRowLabels[std::min(i, size_t(2))]);
		auto pr = add_row_edit(lbl, QString::fromUtf8(init_rows[i].value.c_str()));
		label_edits->push_back(pr.first);
		value_edits->push_back(pr.second);
		wire_row_edits(pr.first, pr.second);
	}

	connect(row_add_btn, &QToolButton::clicked, this, [this, label_edits, value_edits, add_row_edit, wire_row_edits, collect_rows, schedule_save]() {
		if (label_edits->size() >= kMaxCardRows)
			return;
		const size_t idx = label_edits->size();
		const QString lbl = QStringLiteral("--zeile-%1").arg(idx + 1);
		auto pr = add_row_edit(lbl, QString());
		label_edits->push_back(pr.first);
		value_edits->push_back(pr.second);
		wire_row_edits(pr.first, pr.second);
		if (collect_rows())
			schedule_save();
	});

	connect(row_del_btn, &QToolButton::clicked, this, [label_edits, value_edits, move_btns, rows_layout, collect_rows, schedule_save]() {
		if (label_edits->size() <= 1)
			return;
		// Letzte Zeilen-HLayout entfernen (Header-Layout bleibt index 0).
		QLayout *row_l = rows_layout->itemAt(rows_layout->count() - 1)->layout();
		if (row_l) {
			for (int k = row_l->count() - 1; k >= 0; --k) {
				auto *item = row_l->takeAt(k);
				if (item->widget())
					item->widget()->deleteLater();
				delete item;
			}
			delete row_l;
		}
		label_edits->pop_back();
		value_edits->pop_back();
		if (move_btns->size() >= 2) {
			move_btns->pop_back();
			move_btns->pop_back();
		}
		if (collect_rows())
			schedule_save();
	});

	// Zeilen-Umordnung: vertauscht Inhalte zweier Reihen und die Edit-Pointer.
	*move_row = [this, card_id, label_edits, value_edits, schedule_save, collect_rows](
		    QLineEdit *ledit, int dir) {
		auto &labs = *label_edits;
		auto &vals = *value_edits;
		size_t i = 0;
		bool found = false;
		for (; i < labs.size(); ++i)
			if (labs[i] == ledit) { found = true; break; }
		if (!found)
			return;
		const int j = int(i) + dir;
		if (j < 0 || j >= int(labs.size()))
			return;
		{
			const QSignalBlocker b1(labs[i]);
			const QSignalBlocker b2(labs[j]);
			const QSignalBlocker b3(vals[i]);
			const QSignalBlocker b4(vals[j]);
			const QString tl = labs[i]->text();
			const QString tv = vals[i]->text();
			labs[i]->setText(labs[j]->text());
			vals[i]->setText(vals[j]->text());
			labs[j]->setText(tl);
			vals[j]->setText(tv);
		}
		// Nur die Texte werden vertauscht (die Edit-Widgets bleiben an ihrer
		// Position); ein zusätzliches Tauschen der Zeiger würde die
		// Text-Vertauschung in collect_rows wieder aufheben.
		if (collect_rows())
			schedule_save();
	};

	auto *style_section = new QFrame(card_inner);
	style_section->setObjectName(QStringLiteral("tbStyleSection"));
	auto *style_layout = new QVBoxLayout(style_section);
	style_layout->setContentsMargins(0, 0, 0, 0);
	style_layout->setSpacing(8);

	auto *prof_layout = new QVBoxLayout();
	prof_layout->setContentsMargins(0, 0, 0, 0);
	prof_layout->setSpacing(4);

	// Einheitlicher Left-Margin für alle Abschnittsüberschriften
	auto make_section_label = [](const QString &text, QWidget *parent) -> QLabel * {
		auto *lbl = new QLabel(text, parent);
		lbl->setObjectName(QStringLiteral("tbFieldLabel"));
		lbl->setContentsMargins(0, 0, 0, 0);
		return lbl;
	};

	auto *lbl_prof = make_section_label(QStringLiteral("PROFIL"), style_section);
	lbl_prof->setToolTip(
		QStringLiteral("Karten-Farbprofil (Text/Box/Ecken/Deckkraft)"));

	auto *prof_row = new QHBoxLayout();
	prof_row->setContentsMargins(0, 0, 0, 0);
	prof_row->setSpacing(6);

	/*
	 * M6-B2 / F1: Dropdown füllt mit 6 Standard-Profilen + Custom-Profilen aus
	 * state_.color_profiles. Zeigt card.profile auf ein gelöschtes Profil,
	 * bleibt das harmlos: die auf der Karte gebakten Farben (text_color /
	 * box_color) werden NICHT automatisch zurückgesetzt.
	 */
	auto *profile_combo = new QComboBox(style_section);
	profile_combo->setFixedHeight(btn_sz);
	profile_combo->setToolTip(
		QStringLiteral("Karten-Farbprofil (Text/Box/Ecken/Deckkraft)"));
	profile_combo->setAccessibleName(
		QStringLiteral("Karten-Farbprofil %1").arg(QString::fromUtf8(card.id.c_str())));
	int selected_index = 0;
	const std::string canonical_prof = profile_name_canonical(card.profile);
	for (size_t i = 0; i < state_.color_profiles.size(); ++i) {
		const auto &cp = state_.color_profiles[i];
		profile_combo->addItem(QString::fromUtf8(cp.name.c_str()));
		if (cp.name == canonical_prof || cp.name == card.profile)
			selected_index = static_cast<int>(i);
	}
	profile_combo->setCurrentIndex(selected_index);

	auto *new_prof_btn = new QPushButton(QStringLiteral("Profil +"), style_section);
	new_prof_btn->setObjectName(QStringLiteral("tbBtnNewProfile"));
	new_prof_btn->setFixedHeight(btn_sz);
	new_prof_btn->setToolTip(
		QStringLiteral("Aktuelles Karten-Farbprofil (Text/Box/Ecken/Deckkraft) speichern"));

	auto *prof_rename_btn = new QPushButton(style_section);
	prof_rename_btn->setObjectName(QStringLiteral("tbBtnRenameProfile"));
	prof_rename_btn->setFixedSize(btn_sz, btn_sz);
	prof_rename_btn->setToolTip(QStringLiteral("Aktuelles Profil umbenennen"));
	prof_rename_btn->setAccessibleName(QStringLiteral("Aktuelles Profil umbenennen"));
	prof_rename_btn->setIcon(theme_icon("rename.svg"));
	prof_rename_btn->setIconSize(QSize(16, 16));
	prof_rename_btn->setProperty("class", "iconbtn");

	auto *prof_del_btn = new QPushButton(QStringLiteral("−"), style_section);
	prof_del_btn->setObjectName(QStringLiteral("tbBtnDeleteProfile"));
	prof_del_btn->setFixedSize(btn_sz, btn_sz);
	prof_del_btn->setToolTip(QStringLiteral("Aktuelles Profil löschen"));
	prof_del_btn->setAccessibleName(QStringLiteral("Aktuelles Profil löschen"));
	prof_del_btn->setProperty("class", "iconbtn");
	prof_del_btn->setProperty("tbDanger", true);

	prof_row->addWidget(profile_combo, 1);
	prof_row->addWidget(new_prof_btn);
	prof_row->addWidget(prof_rename_btn);
	prof_row->addWidget(prof_del_btn);

	prof_layout->addWidget(lbl_prof);
	prof_layout->addLayout(prof_row);
	style_layout->addLayout(prof_layout);

	// IMP-G: generelle CSS-Regeln (--variable = wert), einklappbar im Styling-Block.
	auto *css_frame = new QFrame(style_section);
	css_frame->setObjectName(QStringLiteral("tbCssRules"));
	auto *css_layout = new QVBoxLayout(css_frame);
	css_layout->setContentsMargins(0, 4, 0, 0);
	css_layout->setSpacing(4);
	auto *css_bar = new QHBoxLayout();
	css_bar->setContentsMargins(0, 0, 0, 0);
	css_bar->setSpacing(6);
	auto css_collapsed = std::make_shared<bool>(true);
	auto *lbl_css = make_section_toolbtn(QStringLiteral("▶ GENERELLE CSS-REGELN"), css_frame);
	lbl_css->setToolTip(QStringLiteral("CSS-Regeln ein-/ausklappen"));
	auto *css_add_btn = new QToolButton(css_frame);
	css_add_btn->setObjectName(QStringLiteral("tbCssAdd"));
	css_add_btn->setFixedSize(btn_sz, btn_sz);
	css_add_btn->setAutoRaise(true);
	css_add_btn->setIcon(theme_icon("plus.svg"));
	css_add_btn->setIconSize(QSize(16, 16));
	css_add_btn->setToolTip(QStringLiteral("CSS-Regel hinzufügen (--variable = wert)"));
	css_add_btn->setAccessibleName(QStringLiteral("CSS-Regel hinzufügen (--variable = wert)"));
	css_add_btn->setProperty("class", "iconbtn");
	auto *css_del_btn = new QToolButton(css_frame);
	css_del_btn->setObjectName(QStringLiteral("tbCssDel"));
	css_del_btn->setFixedSize(btn_sz, btn_sz);
	css_del_btn->setAutoRaise(true);
	css_del_btn->setIcon(theme_icon("trash.svg"));
	css_del_btn->setIconSize(QSize(16, 16));
	css_del_btn->setToolTip(QStringLiteral("Letzte CSS-Regel entfernen"));
	css_del_btn->setAccessibleName(QStringLiteral("Letzte CSS-Regel entfernen"));
	css_del_btn->setProperty("class", "iconbtn");
	css_del_btn->setProperty("tbDanger", true);
	css_bar->addWidget(lbl_css);
	css_bar->addStretch(1);
	css_bar->addWidget(css_add_btn);
	css_bar->addWidget(css_del_btn);
	css_layout->addLayout(css_bar);
	style_layout->addWidget(css_frame);

	// Übliche Template-Variablen als Autovervollständigung für Regel-Namen.
	auto css_var_model = std::make_shared<QStringList>();
	for (const char *v : {"--tb-radius", "--tb-padding", "--tb-padding-top", "--tb-padding-left",
	                      "--tb-gap", "--tb-min-width", "--tb-max-width", "--tb-text-align",
	                      "--tb-font-family", "--tb-font-first", "--tb-font-first-weight",
	                      "--tb-font-rest", "--tb-font-rest-weight", "--tb-corner-size",
	                      "--tb-color-text", "--tb-color-box", "--tb-corner-color-a", "--tb-corner-color-b",
	                      "--tb-corner-opacity-a", "--tb-corner-opacity-b", "--tb-text-opacity",
	                      "--tb-box-opacity", "--tb-rainbow",
	                      "--show-anim", "--anim-file", "--anim-scale", "--anim-x", "--anim-y",
	                      "--box-x", "--box-y", "--box-scale"})
		css_var_model->append(QString::fromLatin1(v));

	auto paint_color_swatch = [](QPushButton *btn, const QString &hex) {
		const QColor col(hex);
		const bool light = (col.red() + col.green() + col.blue()) > 160;
		QPalette pal = btn->palette();
		pal.setColor(QPalette::Button, col);
		pal.setColor(QPalette::ButtonText, light ? QColor(QStringLiteral("#1a1a1a")) : QColor(QStringLiteral("#ffffff")));
		btn->setPalette(pal);
		btn->setAutoFillBackground(true);
	};

	// Eine Regelzeile: Variablenname + Wert-Eingarte (Typ) + Wert-Widget.
	struct CssRuleRow {
		QLineEdit *name_edit = nullptr;
		QComboBox *type_combo = nullptr;
		QWidget *value_widget = nullptr;
		QHBoxLayout *row_layout = nullptr;
		std::function<QString()> getter;
	};
	auto css_rows = std::make_shared<std::vector<CssRuleRow>>();

// Einklappen/ausklappen der CSS-Regelzeilen.
	QObject::connect(lbl_css, &QToolButton::clicked, this,
		[this, lbl_css, css_collapsed, css_rows]() {
			*css_collapsed = !*css_collapsed;
			lbl_css->setText(*css_collapsed ? QStringLiteral("▶ GENERELLE CSS-REGELN")
			                                : QStringLiteral("▼ GENERELLE CSS-REGELN"));
			for (auto &r : *css_rows) {
				r.name_edit->setVisible(!*css_collapsed);
				r.type_combo->setVisible(!*css_collapsed);
				r.value_widget->setVisible(!*css_collapsed);
			}
		});

	auto collect_css = [this, card_id, css_rows]() -> bool {
		TextboxCard *c = find_card_mut(card_id);
		if (!c)
			return false;
		std::vector<TextboxStyleRule> rules;
		for (auto &r : *css_rows) {
			TextboxStyleRule rule;
			rule.name = textbox_html::sanitize_css_var_name(
				r.name_edit->text().toUtf8().toStdString());
			rule.value = textbox_html::sanitize_css_value(
				r.getter().toUtf8().toStdString());
			rule.type = r.type_combo->currentData().toString().toUtf8().toStdString();
			if (rule.name.empty())
				continue;
			rules.push_back(rule);
		}
		c->style = std::move(rules);
		apply_style_rules(*c); // bekannte Farb-/Deckkraft-Regeln auf Felder mappen
		return true;
	};

	const int fh = fontMetrics().height() + 8; // Zeilen-Höhe aus Schrift
	auto build_value_widget =
		[css_frame, paint_color_swatch, this, fh](const QString &type, const QString &value,
		                                const std::function<void()> &on_change)
			-> std::pair<QWidget *, std::function<QString()>> {
			if (type == QLatin1String("num")) {
				auto *row = new QWidget(css_frame);
				auto *hl = new QHBoxLayout(row);
				hl->setContentsMargins(0, 0, 0, 0);
				hl->setSpacing(6);
				auto *slider = new QSlider(Qt::Horizontal, row);
				row->setMinimumWidth(fontMetrics().averageCharWidth() * 8);
				slider->setRange(0, 255);
				bool ok = false;
				const int v = value.toInt(&ok);
				slider->setValue(ok ? std::clamp(v, 0, 255) : 255);
				auto *val = new QLabel(QString::number(slider->value()), row);
				val->setObjectName(QStringLiteral("tbFieldLabel"));
				val->setMinimumWidth(fontMetrics().averageCharWidth() * 3);
				val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
				hl->addWidget(slider, 1);
				hl->addWidget(val);
				QObject::connect(slider, &QSlider::valueChanged, row,
				                 [val, on_change](int s) {
				                 val->setText(QString::number(s));
				                 on_change();
			                 });
				return {row, [slider]() { return QString::number(slider->value()); }};
			}
			if (type == QLatin1String("hex")) {
				const QString hex = value.startsWith(QLatin1String("#")) ? value
				                                                         : QStringLiteral("#ffffff");
				auto *btn = new QPushButton(hex.toUpper(), css_frame);
				btn->setObjectName(QStringLiteral("tbTextColor"));
				btn->setFixedHeight(fh);
				btn->setMinimumWidth(fontMetrics().averageCharWidth() * 8);
				paint_color_swatch(btn, hex);
				QObject::connect(btn, &QPushButton::clicked, btn,
				                 [btn, on_change, paint_color_swatch]() {
				                 const QColor chosen = QColorDialog::getColor(QColor(btn->text()), btn);
				                 if (chosen.isValid()) {
				                 const QString h = chosen.name(QColor::HexRgb);
				                 btn->setText(h.toUpper());
				                 paint_color_swatch(btn, h);
				                 on_change();
			                 }
		                 });
				return {btn, [btn]() { return btn->text(); }};
			}
			if (type == QLatin1String("px")) {
				auto *combo = new QComboBox(css_frame);
				combo->setEditable(true);
				for (const char *p : {"0px", "2px", "4px", "6px", "8px", "10px", "12px", "14px",
				                     "16px", "20px", "24px", "28px", "32px", "40px", "48px", "60px"})
					combo->addItem(QString::fromLatin1(p));
				if (!value.isEmpty() && combo->findText(value, Qt::MatchFixedString) < 0)
					combo->addItem(value);
				combo->setCurrentText(value.isEmpty() ? QStringLiteral("8px") : value);
				combo->setFixedHeight(fh);
				combo->setMinimumWidth(fontMetrics().averageCharWidth() * 8);
				QObject::connect(combo, &QComboBox::currentTextChanged, combo,
				                 [on_change](const QString &) { on_change(); });
				return {combo, [combo]() { return combo->currentText(); }};
			}
			if (type == QLatin1String("bool")) {
				auto *cb = new QCheckBox(QStringLiteral("1 = an"), css_frame);
				cb->setChecked(value == QLatin1String("1"));
				QObject::connect(cb, &QCheckBox::toggled, cb,
				                 [on_change](bool) { on_change(); });
				return {cb, [cb]() { return cb->isChecked() ? QStringLiteral("1") : QStringLiteral("0"); }};
			}
			if (type == QLatin1String("enum")) {
				// Skalierungs-Dropdown: 1x..12x, Wert als nackte Zahl.
				auto *combo = new QComboBox(css_frame);
				combo->setFixedHeight(fh);
				combo->setMinimumWidth(fontMetrics().averageCharWidth() * 8);
				const int kScaleValues[] = {1, 2, 3, 4, 6, 8, 10, 12};
				int sidx = 2; // Default "3x"
				for (int si = 0; si < 8; ++si) {
					combo->addItem(QStringLiteral("%1x").arg(kScaleValues[si]), kScaleValues[si]);
					if (kScaleValues[si] == value.toInt())
						sidx = si;
				}
				combo->setCurrentIndex(sidx);
				QObject::connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), combo,
				                 [on_change](int) { on_change(); });
				return {combo, [combo]() { return QString::number(combo->currentData().toInt()); }};
			}
			if (type == QLatin1String("file")) {
				auto *row = new QWidget(css_frame);
				auto *hl = new QHBoxLayout(row);
				hl->setContentsMargins(0, 0, 0, 0);
				hl->setSpacing(6);
				auto *edit = new QLineEdit(value, row);
				edit->setPlaceholderText(QStringLiteral("Dateiname, Pfad oder URL"));
				edit->setFixedHeight(fh);
				auto *pick = new QPushButton(row);
				pick->setFixedSize(fh + 2, fh + 2);
				pick->setIcon(theme_icon("download.svg"));
				pick->setIconSize(QSize(16, 16));
				pick->setToolTip(QStringLiteral("Animationsdatei auswählen"));
				hl->addWidget(edit, 1);
				hl->addWidget(pick);
				QObject::connect(edit, &QLineEdit::textChanged, edit,
				                 [on_change](const QString &) { on_change(); });
				QObject::connect(pick, &QPushButton::clicked, row,
				                 [this, edit, on_change]() {
				                 const QString start_dir =
				                     QString::fromUtf8(state_.settings.animation_dir.c_str());
				                 const QString chosen = QFileDialog::getOpenFileName(
				                     this, QStringLiteral("Animationsdatei wählen"), start_dir,
				                     QStringLiteral(
				                         "Animationen (*.gif *.webm *.mp4 *.mov *.m4v);;Bilder (*.png *.jpg *.jpeg);;Alle Dateien (*.*)"),
				                     nullptr, dialog_options());
				                 if (!chosen.isEmpty()) {
				                     edit->setText(chosen);
				                     on_change();
				                 }
				                 });
				return {row, [edit]() { return edit->text(); }};
			}
			auto *edit = new QLineEdit(value, css_frame);
			edit->setPlaceholderText(QStringLiteral("wert"));
			edit->setFixedHeight(fh);
			QObject::connect(edit, &QLineEdit::textChanged, edit,
			                 [on_change](const QString &) { on_change(); });
			return {edit, [edit]() { return edit->text(); }};
		};

	auto make_rule_row =
		[this, fh, css_frame, css_layout, css_rows, css_var_model, build_value_widget, collect_css,
		 schedule_save, css_collapsed](const QString &name, const QString &type,
		                                const QString &value) {
			auto *row = new QHBoxLayout();
			row->setContentsMargins(0, 0, 0, 0);
			row->setSpacing(6);
			auto *nedit = new QLineEdit(name, css_frame);
			nedit->setPlaceholderText(QStringLiteral("--variable"));
			nedit->setMinimumWidth(fontMetrics().averageCharWidth() * 8);
			nedit->setFixedHeight(fh);
			auto *comp = new QCompleter(*css_var_model, css_frame);
			comp->setCaseSensitivity(Qt::CaseInsensitive);
			nedit->setCompleter(comp);
			nedit->setAccessibleName(QStringLiteral("Name der CSS-Regel %1").arg(name));
			auto *tcombo = new QComboBox(css_frame);
			tcombo->setFixedWidth(fontMetrics().averageCharWidth() * 7);
			tcombo->setFixedHeight(fh);
			tcombo->setToolTip(QStringLiteral(
				"Wert-Eingarte: Text, Zahl 0-255 (Regler), HEX (Farbpicker), px (Dropdown), Bool, Enum (Dropdown), Datei (Picker)"));
			tcombo->setAccessibleName(QStringLiteral("Typ der CSS-Regel %1").arg(name));
			tcombo->addItem(QStringLiteral("Text"), QStringLiteral("text"));
			tcombo->addItem(QStringLiteral("0-255"), QStringLiteral("num"));
			tcombo->addItem(QStringLiteral("HEX"), QStringLiteral("hex"));
			tcombo->addItem(QStringLiteral("px"), QStringLiteral("px"));
			tcombo->addItem(QStringLiteral("Bool"), QStringLiteral("bool"));
			tcombo->addItem(QStringLiteral("Enum"), QStringLiteral("enum"));
			tcombo->addItem(QStringLiteral("Datei"), QStringLiteral("file"));
			const int ti = tcombo->findData(type);
			tcombo->setCurrentIndex(ti >= 0 ? ti : 0);

			CssRuleRow r;
			r.name_edit = nedit;
			r.type_combo = tcombo;
			r.row_layout = row;
			auto onchange = [collect_css, schedule_save]() {
				if (collect_css())
					schedule_save();
			};
			auto built = build_value_widget(tcombo->currentData().toString(), value, onchange);
			r.value_widget = built.first;
			if (r.value_widget && r.value_widget->inherits("QComboBox"))
				r.value_widget->setAccessibleName(
					QStringLiteral("Wert der CSS-Regel %1").arg(name));
			r.getter = built.second;
			if (*css_collapsed) {
				r.name_edit->hide();
				r.type_combo->hide();
				r.value_widget->hide();
			}
			row->addWidget(r.name_edit, 3);
			row->addWidget(r.value_widget, 4);
			row->addWidget(r.type_combo);
			css_layout->addLayout(row);
			css_rows->push_back(r);
			const int idx = static_cast<int>(css_rows->size()) - 1;

			QObject::connect(nedit, &QLineEdit::textChanged, css_frame,
			                 [onchange](const QString &) { onchange(); });

			// Typ-Wechsel: Wert-Widget neu aufbauen, bisherigen Wert übernehmen.
			QObject::connect(tcombo, &QComboBox::textActivated, css_frame,
			                 [css_rows, build_value_widget, collect_css, schedule_save, idx]() {
			                 CssRuleRow &rr = css_rows->at(idx);
			                 const QString old = rr.getter();
			                 QWidget *oldw = rr.value_widget;
			                 QHBoxLayout *lay = rr.row_layout;
			                 const int pos = lay ? lay->indexOf(oldw) : -1;
			                 auto oc = [collect_css, schedule_save]() {
				                 if (collect_css())
					                 schedule_save();
			                 };
			                 auto built2 = build_value_widget(
				                 rr.type_combo->currentData().toString(), old, oc);
			                 rr.value_widget = built2.first;
			                 rr.getter = built2.second;
			                 rr.value_widget->setVisible(rr.name_edit->isVisible());
			                 if (lay && pos >= 0)
				                 lay->insertWidget(pos, built2.first);
			                 oldw->deleteLater();
		                 });
		};

	auto rebuild_css_rows = [this, card_id, css_layout, css_frame, css_rows, make_rule_row,
	                         css_collapsed]() {
		while (css_layout->count() > 1) {
			QLayoutItem *it = css_layout->takeAt(1);
			if (QWidget *w = it->widget())
				w->deleteLater();
			delete it;
		}
		css_rows->clear();
		TextboxCard *c = find_card_mut(card_id);
		if (!c)
			return;
		for (const auto &rule : c->style) {
			QString type = QString::fromUtf8(rule.type.c_str());
			if (type.isEmpty())
				type = QString::fromUtf8(infer_style_type(rule.value).c_str());
			make_rule_row(QString::fromUtf8(rule.name.c_str()), type,
			              QString::fromUtf8(rule.value.c_str()));
		}
		// Bekannte Farb-/Deckkraft-Variablen als Regelzeilen seedieren,
		// damit Farben/Deckkraft/Rainbow immer als generelle Regeln sichtbar sind.
		auto has_var = [&](const char *n) {
			for (auto &r : *css_rows)
				if (r.name_edit->text() == QString::fromLatin1(n))
					return true;
			return false;
		};
		auto seed = [&](const char *n, const QString &type, const QString &v) {
			if (!has_var(n))
				make_rule_row(QString::fromLatin1(n), type, v);
		};
		seed("--tb-color-text", "hex",
		     QString::fromUtf8((c->text_color.empty() ? "#000000" : c->text_color).c_str()));
		seed("--tb-color-box", "hex",
		     QString::fromUtf8((c->box_color.empty() ? "#ffffff" : c->box_color).c_str()));
		seed("--tb-corner-color-a", "hex",
		     QString::fromUtf8((c->corner_color_a.empty() ? "#fce647" : c->corner_color_a).c_str()));
		seed("--tb-corner-color-b", "hex",
		     QString::fromUtf8((c->corner_color_b.empty() ? "#fce647" : c->corner_color_b).c_str()));
		seed("--tb-corner-opacity-a", "num", QString::number(c->corner_opacity_a));
		seed("--tb-corner-opacity-b", "num", QString::number(c->corner_opacity_b));
		seed("--tb-text-opacity", "num", QString::number(c->text_opacity));
		seed("--tb-box-opacity", "num", QString::number(c->box_opacity));
		seed("--tb-rainbow", "bool", c->rainbow_corners ? QStringLiteral("1") : QStringLiteral("0"));
		// IMP-H6: Animation als generelle CSS-Regelzeilen (Template nutzt --show-anim).
		seed("--show-anim", "bool", c->show_anim ? QStringLiteral("1") : QStringLiteral("0"));
		seed("--anim-file", "file", QString::fromUtf8(c->anim_file.c_str()));
		seed("--anim-scale", "enum", QString::number(c->anim_scale));
		seed("--anim-x", "px", QString::number(c->anim_x) + QStringLiteral("px"));
		seed("--anim-y", "px", QString::number(c->anim_y) + QStringLiteral("px"));
		// IMP-T: Box-Position/Skalierung als generelle CSS-Regelzeilen
		seed("--box-x", "px", QString::number(c->box_x) + QStringLiteral("px"));
		seed("--box-y", "px", QString::number(c->box_y) + QStringLiteral("px"));
		seed("--box-scale", "enum", QString::number(c->box_scale));
	};

	rebuild_css_rows();

	connect(css_add_btn, &QToolButton::clicked, this,
		[make_rule_row, collect_css, schedule_save]() {
			make_rule_row(QStringLiteral("--"), QStringLiteral("text"), QString());
			if (collect_css())
				schedule_save();
		});

	connect(css_del_btn, &QToolButton::clicked, this,
		[this, card_id, css_layout, css_frame, css_rows, collect_css, schedule_save]() {
			if (css_rows->empty())
				return;
			QLayoutItem *it = css_layout->takeAt(css_layout->count() - 1);
			if (QWidget *w = it->widget())
				w->deleteLater();
			delete it;
			css_rows->pop_back();
			if (collect_css())
				schedule_save();
		});

	// IMP-H7: Karten-Vorlage per Pfad-Eingabe (statt Dropdown). leer =
	// klassischer Weg (template_id leer); einfacher Name = Vorlagen-Ordner;
	// voller Pfad = direkte .html-Datei anywhere. Wechsel -> write_card neu +
	// Browser-Source-Reload (spec: Document-Neuerstellung), gequetscht per Timer.
	auto *tpl_layout = new QVBoxLayout();
	tpl_layout->setContentsMargins(0, 0, 0, 0);
	tpl_layout->setSpacing(4);
	auto *lbl_tpl = make_section_label(QStringLiteral("VORLAGE"), style_section);
	lbl_tpl->setToolTip(QStringLiteral(
		"Pfad zur Vorlagen-HTML. Leer = index.html (classic). Einfacher Name (z.B. minimal) = "
		"Vorlagen-Ordner; voller Pfad = direkte Datei. Ordner-Icon wählt eine .html."));
	auto *tpl_row = new QHBoxLayout();
	tpl_row->setContentsMargins(0, 0, 0, 0);
	tpl_row->setSpacing(6);
	auto *tpl_edit = new QLineEdit(style_section);
	tpl_edit->setObjectName(QStringLiteral("tbTemplatePath"));
	tpl_edit->setPlaceholderText(QStringLiteral("index.html (leer = classic) oder Pfad zur Vorlage"));
	tpl_edit->setToolTip(lbl_tpl->toolTip());
	tpl_edit->setFixedHeight(btn_sz);
	tpl_edit->setText(QString::fromUtf8(card.template_id.c_str()));
	tpl_edit->setAccessibleName(
		QStringLiteral("Vorlagenpfad der Karte %1").arg(QString::fromUtf8(card.id.c_str())));
	auto *tpl_pick = new QPushButton(style_section);
	tpl_pick->setObjectName(QStringLiteral("tbTemplatePick"));
	tpl_pick->setFixedSize(btn_sz, btn_sz);
	tpl_pick->setIcon(theme_icon("download.svg"));
	tpl_pick->setIconSize(QSize(16, 16));
	tpl_pick->setToolTip(QStringLiteral("Vorlagen-Datei (.html) auswählen"));
	tpl_pick->setAccessibleName(QStringLiteral("Vorlagen-Datei (.html) auswählen"));
	tpl_pick->setProperty("class", "iconbtn");
	tpl_row->addWidget(tpl_edit, 1);
	tpl_row->addWidget(tpl_pick);
	tpl_layout->addWidget(lbl_tpl);
	tpl_layout->addLayout(tpl_row);
	style_layout->addLayout(tpl_layout);
	// Für refresh_templates_ui() (State-Änderung von außen) am Row erreichbar.
	row_widget->setProperty("tbHasTemplate", true);

	// QCompleter listing all .html files in templates_dir() for quick pick.
	{
		const std::string tpl_dir = textbox_html::templates_dir();
		if (!tpl_dir.empty()) {
			QStringList html_files;
			std::error_code ec;
			for (const auto &entry : std::filesystem::directory_iterator(
				     std::filesystem::u8path(tpl_dir), ec)) {
				if (!entry.is_regular_file())
					continue;
				const std::string name = entry.path().filename().u8string();
				if (name.size() >= 6 &&
				    name.compare(name.size() - 5, 5, ".html") == 0) {
					html_files.append(QString::fromUtf8(name.c_str()));
				}
			}
			if (!html_files.isEmpty()) {
				html_files.sort(Qt::CaseInsensitive);
				auto *completer = new QCompleter(html_files, style_section);
				completer->setCaseSensitivity(Qt::CaseInsensitive);
				completer->setFilterMode(Qt::MatchContains);
				tpl_edit->setCompleter(completer);
			}
		}
	}


	style_section->setVisible(true);
	inner_layout->addWidget(style_section);

	connect(style_toggle_btn, &QToolButton::clicked, this,
		[this, style_section, style_toggle_btn]() {
			// M7-2: isHidden() statt isVisible() — isVisible() ist auch bei
			// verstecktem Dock-Parent false und flippt das Chevron-Icon vor dem
			// echten State-Change.
			const bool expand = style_section->isHidden();
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
	to_scene_btn->setObjectName(QStringLiteral("tbBtnGreen"));
	to_scene_btn->setFixedHeight(btn_sz);
	to_scene_btn->setToolTip(
		QStringLiteral("Karte als Text-Quelle in die aktuelle Szene legen oder aktualisieren"));

	auto *clear_src_btn = new QPushButton(QStringLiteral("Leeren"), card_inner);
	clear_src_btn->setObjectName(QStringLiteral("tbBtnRed"));
	clear_src_btn->setFixedHeight(btn_sz);
	clear_src_btn->setToolTip(
		QStringLiteral("Text der Quelle auf leer setzen — die Quelle bleibt in der Szene"));
	clear_src_btn->setProperty("tbDanger", true);

	actions_layout->addWidget(to_scene_btn, 1);
	actions_layout->addWidget(clear_src_btn, 1);
	inner_layout->addLayout(actions_layout);

	// activated fires only on USER selection — programmatic setCurrentIndex
	// during UI rebuild must not re-apply the profile over baked card colors.
	connect(profile_combo, QOverload<int>::of(&QComboBox::activated), this,
		[this, card_id, schedule_save, rebuild_css_rows, profile_combo](int) {
			if (TextboxCard *c = find_card_mut(card_id)) {
				capture_undo_snapshot(this, state_); // vor der Mutation: Undo stellt alt wieder her
				c->profile = profile_combo->currentText().toUtf8().toStdString();
				if (const TextboxProfile *prof =
					textbox_profile_lookup(c->profile, state_.color_profiles))
					profile_switch(*prof, *c); // Farb-Regeln des Profils übernehmen
				rebuild_css_rows(); // Regelzeilen zeigen jetzt die Profil-Werte
				schedule_save();
			}
		});

	// IMP-H7: Pfad-Eingabe -> debounce (Tippen soll nicht jeden Character
	// reloaden), Picker -> sofort übernehmen.
	auto *tpl_timer = new QTimer(style_section);
	tpl_timer->setSingleShot(true);
	tpl_timer->setInterval(650);
	connect(tpl_edit, &QLineEdit::textChanged, tpl_timer, [tpl_timer]() {
		tpl_timer->start();
	});
	connect(tpl_timer, &QTimer::timeout, this,
		[this, card_id, tpl_guard = QPointer<QLineEdit>(tpl_edit)]() {
			// IMP-S: style_section (und damit tpl_edit) kann vor dem Timeout
			// zerstört sein — QPointer guardet den dangling Zeiger.
			if (!tpl_guard)
				return;
			const QString t = tpl_guard->text().trimmed();
		TextboxCard *c = find_card_mut(card_id);
		if (!c || c->template_id == t.toUtf8().toStdString())
			return;
		apply_template_id(card_id, t.toUtf8().toStdString());
	});
	connect(tpl_pick, &QPushButton::clicked, this, [this, card_id, tpl_edit]() {
		const QString start = QString::fromUtf8(textbox_html::templates_dir().c_str());
		const QString chosen = QFileDialog::getOpenFileName(
			this, QStringLiteral("Vorlage wählen"), start,
			QStringLiteral("Vorlagen (*.html);;Alle Dateien (*.*)"),
			nullptr, dialog_options());
		if (!chosen.isEmpty()) {
			tpl_edit->setText(chosen);
			apply_template_id(card_id, chosen.toUtf8().toStdString());
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
		TextboxCard *c = find_card_mut(card_id);
		if (!c)
			return;
		const std::string tc = c->text_color.empty() ? "#000000" : c->text_color;
		const std::string bc = c->box_color.empty() ? "#ffffff" : c->box_color;
		TextboxProfile prof{
			name_utf8,
			tc,
			bc,
			c->corner_color_a.empty() ? "#fce647" : c->corner_color_a,
			c->corner_color_b.empty() ? "#fce647" : c->corner_color_b,
			c->corner_opacity_a,
			c->corner_opacity_b,
			c->text_opacity,
			c->box_opacity,
			c->rainbow_corners,
		};
		// IMP-H5: Profil = Snapshot der generellen CSS-Regeln der Karte.
		prof.style = c->style;
		auto it = std::find_if(state_.color_profiles.begin(), state_.color_profiles.end(),
				       [&name_utf8](const TextboxProfile &p) {
					       return p.name == name_utf8;
				       });
		if (it != state_.color_profiles.end()) {
			*it = std::move(prof);
		} else {
			state_.color_profiles.push_back(std::move(prof));
		}
		c->profile = name_utf8;
		// c3 (Bugfix): Karten mit demselben Profil folgen sofort den neuen
		// Profilfarben — sonst behielten sie ihre alten baked Farben.
		const std::string canonical = profile_name_canonical(name_utf8);
		for (auto &other : state_.cards) {
			if (other.id == c->id)
				continue;
			if (other.profile == name_utf8 || profile_name_canonical(other.profile) == canonical) {
				if (const TextboxProfile *p =
					textbox_profile_lookup(other.profile, state_.color_profiles))
					profile_switch(*p, other);
				push_card_live(other, state_.settings);
			}
		}
		save_state();
		rebuild_rows();
		set_dock_status(QStringLiteral("Farbprofil \"%1\" gespeichert").arg(qname), false);
	});

	// IMP-H5: Profil löschen (aktuelle Combo-Auswahl) — auch Standardprofile.
	connect(prof_del_btn, &QPushButton::clicked, this,
		[this, card_id, profile_combo]() {
			const QString qname = profile_combo->currentText().trimmed();
			if (qname.isEmpty())
				return;
			const std::string name_utf8 = qname.toUtf8().toStdString();
			auto it = std::find_if(state_.color_profiles.begin(),
				state_.color_profiles.end(),
				[&name_utf8](const TextboxProfile &p) {
					return p.name == name_utf8;
				});
			if (it == state_.color_profiles.end()) {
				set_dock_status(QStringLiteral("Profil \"%1\" nicht gefunden.").arg(qname), true);
				return;
			}
			state_.color_profiles.erase(it);
			if (TextboxCard *c = find_card_mut(card_id))
				c->profile = ""; // baked Farben bleiben (kein Auto-Reset)
			// IMP-N: letzte Profile gelöscht -> Defaults nachsaaten, sonst ist
			// das Profil-Combo dauerhaft leer; dangling Kartenreferenzen bereinigen.
			const bool reseeded = reconcile_profiles_after_delete(state_);
			save_state();
			rebuild_rows();
			set_dock_status(reseeded
				? QStringLiteral("Profil \"%1\" gelöscht — Standardprofile nachgesät").arg(qname)
				: QStringLiteral("Profil \"%1\" gelöscht").arg(qname),
				false);
		});

	// IMP-H5: Profil umbenennen; Karten mit altem Namen folgen dem neuen.
	connect(prof_rename_btn, &QPushButton::clicked, this,
		[this, profile_combo]() {
			const QString old_name = profile_combo->currentText().trimmed();
			if (old_name.isEmpty())
				return;
			bool ok = false;
			const QString raw = QInputDialog::getText(
				this, QStringLiteral("Profil umbenennen"),
				QStringLiteral("Neuer Profilname:"), QLineEdit::Normal, old_name, &ok);
			if (!ok)
				return;
			const QString qname = raw.trimmed();
			if (qname.isEmpty() || qname == old_name)
				return;
			const std::string old_utf8 = old_name.toUtf8().toStdString();
			const std::string new_utf8 = qname.toUtf8().toStdString();
			for (const auto &cp : state_.color_profiles) {
				if (cp.name == new_utf8) {
					set_dock_status(
						QStringLiteral("\"%1\" existiert bereits als Profil.").arg(qname), true);
					return;
				}
			}
			auto it = std::find_if(state_.color_profiles.begin(),
				state_.color_profiles.end(),
				[&old_utf8](const TextboxProfile &p) {
					return p.name == old_utf8;
				});
			if (it == state_.color_profiles.end()) {
				set_dock_status(QStringLiteral("Profil \"%1\" nicht gefunden.").arg(old_name), true);
				return;
			}
			it->name = new_utf8;
			for (auto &c : state_.cards) {
				if (c.profile == old_utf8 || profile_name_canonical(c.profile) == old_utf8)
					c.profile = new_utf8;
			}
			save_state();
			rebuild_rows();
			set_dock_status(QStringLiteral("Profil \"%1\" → \"%2\" umbenannt")
				    .arg(old_name, qname), false);
		});

	connect(to_scene_btn, &QPushButton::clicked, this, [this, card_id]() {
		if (TextboxCard *c = find_card_mut(card_id)) {
			blog(LOG_INFO, "[textbox] Sende Karte '%s' an aktuelle Szene", card_id.c_str());
			textbox_scene::send_to_current_scene(*c, state_.settings);
		}
	});

	connect(clear_src_btn, &QPushButton::clicked, this, [this, card_id]() {
		if (TextboxCard *c = find_card_mut(card_id)) {
			capture_undo_snapshot(this, state_); // IMP-Q: Undo stellt den Text wieder her
			textbox_scene::update_everywhere(*c, true, state_.settings);
		}
	});

	connect(del_btn, &QPushButton::clicked, this, [this, card_id, row_widget]() {
		capture_undo_snapshot(this, state_);
		if (state_.remove_card(card_id)) {
			save_state();
		}
		// Karte weg -> HTML-Datei ebenfalls löschen, sonst bleiben Orphan-Dateien.
		std::error_code ec;
		std::filesystem::remove(
		    std::filesystem::u8path(textbox_html::cards_dir() + "/" + textbox_html::sanitize_card_id(card_id) + ".html"), ec);
		textbox_scene::remove_everywhere(card_id);
		cards_layout_->removeWidget(row_widget);
		row_widgets_.erase(std::remove(row_widgets_.begin(), row_widgets_.end(), row_widget),
			   row_widgets_.end());
		row_widget->deleteLater();
		refresh_top_row();
	});

	connect(dup_btn, &QToolButton::clicked, this, [this, card_id]() {
		capture_undo_snapshot(this, state_);
		const TextboxCard *src = find_card_mut(card_id);
		if (!src)
			return;
		TextboxCard copy = *src;
		TextboxCard &nc = state_.add_card();
		const std::string new_id = nc.id;
		nc = copy;
		nc.id = new_id;
		create_row_widget(nc);
		refresh_top_row();
		save_state();
		bool any_text = !nc.zeile1.empty() || !nc.zeile2.empty() || !nc.zeile3.empty();
		for (const auto &r : nc.rows)
			if (!r.value.empty())
				any_text = true;
		if (any_text)
			textbox_scene::send_to_current_scene(nc, state_.settings);
		else
			textbox_scene::reload_everywhere(nc, state_.settings);
	});

	// ID-Umbenennung: validate, alte HTML-Datei + Sources entfernen, Widget neu bauen.
	connect(id_edit, &QLineEdit::editingFinished, this,
	        [this, card_id, id_edit, row_widget, schedule_save]() {
		        const QString t = id_edit->text().trimmed();
		        static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9_-]{1,32}$"));
		        const std::string new_id = t.toStdString();
		        if (new_id == card_id)
			        return;
		        if (!re.match(t).hasMatch() || find_card_mut(new_id)) {
			        id_edit->setText(QString::fromUtf8(card_id.c_str()));
			        return;
		        }
		        TextboxCard *c = find_card_mut(card_id);
		        if (!c)
			        return;
		        capture_undo_snapshot(this, state_);
		        std::error_code ec;
		        std::filesystem::remove(
			        std::filesystem::u8path(textbox_html::cards_dir() + "/" + textbox_html::sanitize_card_id(card_id) + ".html"), ec);
		        const bool had_text = [&c]() {
			        if (!c->zeile1.empty() || !c->zeile2.empty() || !c->zeile3.empty())
				        return true;
			        for (const auto &r : c->rows)
				        if (!r.value.empty())
					        return true;
			        return false;
		        }();
		        c->id = new_id;
		        textbox_scene::remove_everywhere(card_id);
		        save_state();
		        if (had_text)
			        textbox_scene::send_to_current_scene(*c, state_.settings);
		        else
			        textbox_scene::reload_everywhere(*c, state_.settings);
		        // Widget mit neuer ID neu aufbauen (alte Handler capturen die alte ID).
		        cards_layout_->removeWidget(row_widget);
		        row_widgets_.erase(std::remove(row_widgets_.begin(), row_widgets_.end(), row_widget),
			           row_widgets_.end());
		        row_widget->deleteLater();
		        create_row_widget(*c);
		        refresh_top_row();
		        // IMP-R: Dirty-Eintrag mit der NEUEN ID (die alte ID existiert nicht mehr).
		        mark_card_dirty_and_schedule(this, new_id);
	        });

	const int insert_idx = std::max(0, cards_layout_->count() - 1);
	cards_layout_->insertWidget(insert_idx, row_widget);
	row_widgets_.push_back(row_widget);
}

void TextboxDock::rebuild_rows()
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

void TextboxDock::sync_profil_del_state()
{
	if (!profil_del_btn_ || !profil_select_)
		return;
	const int idx = profil_select_->currentIndex();
	const bool valid = idx > 0 && idx < profil_select_->count() &&
			   !profil_select_->itemData(idx).toString().isEmpty();
	profil_del_btn_->setEnabled(valid);
}

void TextboxDock::apply_template_id(const std::string &card_id, const std::string &template_id)
{
	TextboxCard *c = find_card_mut(card_id);
	if (!c)
		return;
	c->template_id = template_id;

	// Spec F2b: Template-Wechsel = Document-Neuerstellung -> Reload der
	// Browser-Quellen (farbliche Live-Pushes laufen über die CSS-Vars im Template).
	save_state();
	textbox_scene::reload_everywhere(*c, state_.settings);
	refresh_templates_ui();

	if (template_id.empty()) {
		set_dock_status(QStringLiteral("Karte %1: Vorlage „index.html“ (classic)")
			.arg(QString::fromUtf8(card_id.c_str())),
			false);
	} else if (textbox_html::render_template_html(*c, template_id, true).empty()) {
		// IMP-M: Pfad existiert/lesbar? Sonst fällt write_card still auf
		// classic zurück — ohne Hinweis wäre das für den Nutzer unsichtbar.
		set_dock_status(QStringLiteral("Vorlage \"%1\" nicht gefunden — Karte %2 nutzt „index.html“ (classic)")
			.arg(QString::fromUtf8(template_id.c_str()),
			     QString::fromUtf8(card_id.c_str())),
			true);
	} else {
		set_dock_status(QStringLiteral("Vorlage \"%1\" auf Karte %2 angewendet")
				.arg(QString::fromUtf8(template_id.c_str()),
				     QString::fromUtf8(card_id.c_str())),
			false);
	}
}

void TextboxDock::refresh_templates_ui()
{
	// IMP-H7: Vorlagen sind Pfad-Eingaben — spiegle nur den State-Wert in die
	// Felder (keine Ordner-Discovery mehr; der Pfad ist die Wahrheit).
	for (QWidget *w : row_widgets_) {
		if (!w || !w->property("tbHasTemplate").toBool())
			continue;
		QLineEdit *edit = w->findChild<QLineEdit *>(QStringLiteral("tbTemplatePath"));
		if (!edit)
			continue;
		const std::string card_id = w->property("tbCardId").toString().toUtf8().toStdString();
		const TextboxCard *card = find_card_mut(card_id);
		const QString cur = card ? QString::fromUtf8(card->template_id.c_str()) : QString();
		const QSignalBlocker blocker(edit);
		if (edit->text() != cur)
			edit->setText(cur);
	}
}

void TextboxDock::on_open_templates_folder()
{
	const std::string dir = textbox_html::templates_dir();
	if (dir.empty()) {
		set_dock_status(QStringLiteral("Vorlagen-Ordner konnte nicht bestimmt werden."), true);
		return;
	}

	// Beispiele seeden, wenn der user-Ordner noch leer ist (kopiert aus dem
	// builtin-Data-Verzeichnis; Überschreiben existierender Dateien = NO-OP).
	const std::string builtin = textbox_html::builtin_templates_dir();
	if (!builtin.empty()) {
		std::error_code ec;
		for (const auto &entry : std::filesystem::directory_iterator(builtin, ec)) {
			if (!entry.is_regular_file())
				continue;
			const std::filesystem::path dst =
				std::filesystem::u8path(dir) / entry.path().filename();
			if (std::filesystem::exists(dst, ec))
				continue;
			std::filesystem::copy_file(entry.path(), dst,
					    std::filesystem::copy_options::overwrite_existing, ec);
		}
	}
	refresh_templates_ui();

	const QUrl url = QUrl::fromLocalFile(QString::fromUtf8(dir.c_str()));
	if (!QDesktopServices::openUrl(url)) {
		set_dock_status(QStringLiteral("Ordner konnte nicht geöffnet werden: %1")
				.arg(QString::fromUtf8(dir.c_str())),
			true);
		return;
	}
	set_dock_status(QStringLiteral("Vorlagen-Ordner geöffnet: %1")
				.arg(QString::fromUtf8(dir.c_str())),
		false);
}

void TextboxDock::on_copy_template_from_classic()
{
	bool ok = false;
	const QString raw_name = QInputDialog::getText(
		this, QStringLiteral("Vorlage aus index.html (classic) kopieren"),
		QStringLiteral("Vorlagename (Datei <name>.html im Vorlagen-Ordner):"),
		QLineEdit::Normal, QStringLiteral("meine-vorlage"), &ok);
	if (!ok)
		return;
	QString name = raw_name.trimmed();
	name.replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_-]")), QStringLiteral("_"));
	if (name.isEmpty()) {
		set_dock_status(QStringLiteral("Bitte einen Vorlagennamen eingeben."), true);
		return;
	}

	const std::string src_path = textbox_html::resolve_textbox_path(state_.settings);
	QFile src(QString::fromUtf8(src_path.c_str()));
	if (!src.open(QIODevice::ReadOnly)) {
		set_dock_status(QStringLiteral("index.html (classic) nicht lesbar: %1")
				.arg(QString::fromUtf8(src_path.c_str())),
			true);
		return;
	}
	const QByteArray contents = src.readAll();
	src.close();
	if (contents.isEmpty()) {
		set_dock_status(QStringLiteral("index.html (classic) ist leer."), true);
		return;
	}

	const std::string dir = textbox_html::templates_dir();
	if (dir.empty()) {
		set_dock_status(QStringLiteral("Vorlagen-Ordner konnte nicht bestimmt werden."), true);
		return;
	}
	const QString dst = QString::fromUtf8(dir.c_str()) + "/" + name + ".html";
	QFile out(dst);
	if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		set_dock_status(QStringLiteral("Vorlage konnte nicht geschrieben werden: %1").arg(dst),
			true);
		return;
	}
	out.write(contents);
	out.close();

	refresh_templates_ui();
	set_dock_status(QStringLiteral("Vorlage \"%1\" aus index.html kopiert").arg(name), false);
}

void TextboxDock::refresh_top_row()
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
			card_search_->addItem(
				card_search_label(id_q, QString::fromUtf8(card.zeile3.c_str())), id_q);
		}
		card_search_->setCurrentIndex(-1);
		if (card_search_->lineEdit())
			card_search_->lineEdit()->setText(prev_text);
	}
}

void TextboxDock::apply_quick_profile(const std::string &name)
{
	if (name.empty())
		return;

	std::vector<TextboxCard> prev_cards = state_.cards;
	std::vector<std::string> prev_ids;
	prev_ids.reserve(state_.cards.size());
	for (const auto &c : state_.cards)
		prev_ids.push_back(c.id);

	if (state_.apply_quick_profile(name)) {
		state_.undo_snapshot_ = std::move(prev_cards);
		state_.undo_available_ = true;
		if (auto it = g_dock_schedulers.find(this);
		    it != g_dock_schedulers.end() && it->second.undo_btn) {
			it->second.undo_btn->setEnabled(true);
		}
		state_.active_profile = name;
		save_state();
		sync_all_cards_live(prev_ids);
		rebuild_rows();
		set_dock_status(QString("Profil \"%1\" geladen")
					.arg(QString::fromStdString(name)),
				false);
	}
}

void TextboxDock::flash_row(const std::string &card_id)
{
	if (card_id.empty())
		return;
	const QString target_id = QString::fromUtf8(card_id.c_str());
	for (QWidget *w : row_widgets_) {
		if (!w || w->property("tbCardId").toString() != target_id)
			continue;
		if (scroll_area_)
			scroll_area_->ensureWidgetVisible(w);
		w->setProperty("tbFlash", QStringLiteral("true"));
		if (QStyle *st = w->style()) {
			st->unpolish(w);
			st->polish(w);
		}
		w->update();
		QPointer<QWidget> w_ptr(w);
		QTimer::singleShot(1200, this, [w_ptr]() {
			if (!w_ptr)
				return;
			w_ptr->setProperty("tbFlash", QVariant());
			if (QStyle *st = w_ptr->style()) {
				st->unpolish(w_ptr.data());
				st->polish(w_ptr.data());
			}
			w_ptr->update();
		});
		break;
	}
}

void TextboxDock::on_add_card()
{
	const TextboxCard &card = state_.add_card();
	create_row_widget(card);
	refresh_top_row();
	save_state();
}

void TextboxDock::on_clear_all()
{
	const auto reply = QMessageBox::question(
		this, QStringLiteral("Alle löschen"),
		QStringLiteral("Sollen wirklich alle Karten gelöscht werden?"),
		QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
	if (reply != QMessageBox::Yes)
		return;

	capture_undo_snapshot(this, state_);

	std::vector<std::string> ids;
	ids.reserve(state_.cards.size());
	for (const auto &c : state_.cards) {
		ids.push_back(c.id);
	}

	state_.cards.clear();
	rebuild_rows();
	save_state();

	for (const auto &id : ids) {
		if (cancel_ && *cancel_)
			return;
		std::error_code ec;
		std::filesystem::remove(
		    std::filesystem::u8path(textbox_html::cards_dir() + "/" + textbox_html::sanitize_card_id(id) + ".html"), ec);
		textbox_scene::remove_everywhere(id);
	}
	set_dock_status(QStringLiteral("Alle Karten gelöscht"), false);
}

void TextboxDock::sync_all_cards_live(const std::vector<std::string> &previous_ids)
{
	for (const auto &old_id : previous_ids) {
		if (cancel_ && *cancel_)
			return;
		if (!state_.find(old_id)) {
			textbox_scene::remove_everywhere(old_id);
		}
	}
	for (const auto &card : state_.cards) {
		if (cancel_ && *cancel_)
			return;
		// IMP-L3: Nach Undo eines Löschens existiert die Browser-Quelle nicht
		// mehr — update_everywhere würde sie nie reanlegen. Fehlende Quelle
		// wird in der aktuellen Szene reangelegt (write_card + obs_scene_add).
		if (!textbox_scene::source_exists(card.id)) {
			textbox_scene::send_to_current_scene(card, state_.settings);
			continue;
		}
		textbox_scene::update_everywhere(card, false, state_.settings);
	}
}

void TextboxDock::set_dock_status(const QString &msg, bool is_error)
{
	if (!msg.isEmpty()) {
		tb_event_log(tb_event_log_dir_from_state_path(state_path_),
			     msg.toUtf8().toStdString());
	}
	if (!dock_status_lbl_)
		return;
	dock_status_lbl_->setText(msg);
	{
		QPalette pal = dock_status_lbl_->palette();
		if (is_error) {
			pal.setColor(QPalette::WindowText, QColor(QStringLiteral("#E85E75")));
		} else {
			pal.setColor(QPalette::WindowText, palette().color(QPalette::Mid));
		}
		dock_status_lbl_->setPalette(pal);
	}
	dock_status_lbl_->setVisible(!msg.isEmpty());
	if (dock_status_close_)
		dock_status_close_->setVisible(!msg.isEmpty());
	// c1: Auto-dismiss — Fehler bleiben 15 s sichtbar, Infos 6 s.
	if (!dock_status_timer_) {
		dock_status_timer_ = new QTimer(this);
		dock_status_timer_->setSingleShot(true);
		connect(dock_status_timer_, &QTimer::timeout, this, [this]() {
			if (dock_status_lbl_)
				dock_status_lbl_->setVisible(false);
			if (dock_status_close_)
				dock_status_close_->setVisible(false);
		});
	}
	dock_status_timer_->stop();
	dock_status_timer_->start(is_error ? 15000 : 6000);
}

void TextboxDock::apply_import_result(const textbox_import::TextboxImportResult &res)
{
	capture_undo_snapshot(this, state_);
	const auto &punkte = !res.punkte.empty() ? res.punkte : res.service.punkte;
	blog(LOG_INFO, "[textbox] Import übernommen: %zu Einträge", punkte.size());
	const std::size_t total_items = punkte.size();
	while (state_.cards.size() < total_items) {
		state_.add_card(); // IMP-T: next_id-vergabene IDs — indexbasierte IDs kollidierten mit bestehenden Karten nach Löschungen
	}
	if (static_cast<int>(state_.cards.size()) + 1 > state_.next_id)
		state_.next_id = static_cast<int>(state_.cards.size()) + 1;

	std::vector<textbox_ai::AiPoint> enriched;
	enriched.reserve(total_items);
	for (std::size_t i = 0; i < total_items; ++i) {
		enriched.push_back(textbox_ai::AiPoint{
			punkte[i].nr, punkte[i].zeilen, punkte[i].titel});
	}
	const auto ref_table = load_reference_table(state_.settings.referenz_path);
	if (!ref_table.empty()) {
		textbox_ai::enrich_titles_from_reference(enriched, ref_table);
	}

	for (std::size_t i = 0; i < total_items; ++i) {
		state_.cards[i].zeile1 = enriched[i].zeile1;
		state_.cards[i].zeile2 = enriched[i].zeile2;
		state_.cards[i].zeile3 = enriched[i].zeile3;
		// IMP-R: importierte Zeilen überschreiben alte rows (rows gewinnen
		// sonst in effective_rows und der Import wäre unsichtbar).
		state_.cards[i].rows.clear();
	}
	// Überzählige Bestandskarten bereinigen, damit keine alten Einträge stehen bleiben.
	for (std::size_t i = total_items; i < state_.cards.size(); ++i) {
		if (!is_standard_card(state_.cards[i], i))
			state_.cards[i].zeile1.clear();
		state_.cards[i].zeile2.clear();
		state_.cards[i].zeile3.clear();
		state_.cards[i].rows.clear();
	}

	save_state();
	for (std::size_t i = 0; i < state_.cards.size(); ++i) {
		if (cancel_ && *cancel_)
			return;
		textbox_scene::update_everywhere(state_.cards[i], false, state_.settings);
	}
	rebuild_rows();
}

void TextboxDock::on_open_import_dialog()
{
	set_dock_status(QStringLiteral("Datenarchiv geöffnet"), false);

	QDialog dlg(this);
	dlg.setObjectName(QStringLiteral("tbImportDialog"));
	dlg.setWindowTitle(QStringLiteral("Datenarchiv"));
	dlg.setMinimumWidth(fontMetrics().averageCharWidth() * 50);

	// Inhalt scrollbar machen: nach URL-Import wächst der Detail-Block über
	// die Bildschirmhöhe hinaus (Dialog > Monitor), Buttons wären off-screen.
	auto *dlg_layout = new QVBoxLayout(&dlg);
	dlg_layout->setContentsMargins(0, 0, 0, 0);
	dlg_layout->setSpacing(0);
	auto *dlg_scroll = new CappedScrollArea(&dlg);
	dlg_scroll->setWidgetResizable(true);
	dlg_scroll->setFrameShape(QFrame::NoFrame);
	auto *dlg_content = new QWidget();
	auto *root = new QVBoxLayout(dlg_content);
	dlg_scroll->setWidget(dlg_content);
	dlg_layout->addWidget(dlg_scroll);
	root->setContentsMargins(16, 16, 16, 16);
	root->setSpacing(12);

	// Gruppe 1: Daten von URL (+ Kopfdaten-Auswahl)
	auto *google_group = new QGroupBox(QStringLiteral("Daten von URL"), &dlg);
	auto *google_layout = new QVBoxLayout(google_group);
	google_layout->setContentsMargins(12, 16, 12, 12);
	google_layout->setSpacing(8);
	google_group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

	auto *google_info = new QLabel(
		QStringLiteral("JSON-Antworten von einer URL laden"), google_group);
	google_info->setWordWrap(true);

	auto *lbl_form_id = new QLabel(QStringLiteral("FORM-ID"), google_group);
	lbl_form_id->setObjectName(QStringLiteral("tbFieldLabel"));
	const std::string init_form_id = state_.settings.google_form_id;
	auto *edit_form_id =
		new QLineEdit(QString::fromUtf8(init_form_id.c_str()), google_group);
	edit_form_id->setPlaceholderText(QStringLiteral("Google-Form-ID"));
	edit_form_id->setAccessibleName(QStringLiteral("Google-Form-ID"));

	auto *lbl_token_path = new QLabel(QStringLiteral("TOKEN-PFAD"), google_group);
	lbl_token_path->setObjectName(QStringLiteral("tbFieldLabel"));
	const std::string init_token_path = state_.settings.token_path.empty()
						    ? textbox_import::default_token_path_str()
						    : state_.settings.token_path;
	auto *edit_token_path =
		new QLineEdit(QString::fromUtf8(init_token_path.c_str()), google_group);
	edit_token_path->setPlaceholderText(QString::fromUtf8(textbox_import::default_token_path_str()));
	edit_token_path->setAccessibleName(QStringLiteral("OAuth-Token-Pfad"));

	connect(edit_form_id, &QLineEdit::editingFinished, this, [this, edit_form_id]() {
		const std::string new_id = edit_form_id->text().trimmed().toUtf8().toStdString();
		if (state_.settings.google_form_id != new_id) {
			state_.settings.google_form_id = new_id;
			save_state();
			set_dock_status(QStringLiteral("Form-ID geändert"), false);
		}
	});

	connect(edit_token_path, &QLineEdit::editingFinished, this, [this, edit_token_path]() {
		std::string new_tp = edit_token_path->text().trimmed().toUtf8().toStdString();
		if (new_tp.empty())
			new_tp = textbox_import::default_token_path_str();
		if (state_.settings.token_path != new_tp) {
			state_.settings.token_path = new_tp;
			save_state();
			set_dock_status(QStringLiteral("Token-Pfad geändert"), false);
		}
	});

	auto *api_btn = new QPushButton(QStringLiteral("Daten von URL holen"), google_group);
	api_btn->setObjectName(QStringLiteral("tbBtnApiImport"));
	api_btn->setIcon(theme_icon("download.svg"));
	api_btn->setIconSize(QSize(16, 16));

	auto *status_lbl = new QLabel(QStringLiteral("Bereit."), google_group);
	status_lbl->setObjectName(QStringLiteral("tbImportStatus"));
	status_lbl->setWordWrap(true);

	// Kopfdaten-Container innerhalb Gruppe 1 (erst nach erfolgreichem Laden sichtbar)
	auto *kopf_group = new QWidget(google_group);
	kopf_group->setObjectName(QStringLiteral("tbKopfContainer"));
	auto *kopf_layout = new QVBoxLayout(kopf_group);
	kopf_layout->setContentsMargins(0, 6, 0, 0);
	kopf_layout->setSpacing(6);

	auto *lbl_service_select = new QLabel(QStringLiteral("EINREICHUNG AUSWÄHLEN"), kopf_group);
	lbl_service_select->setObjectName(QStringLiteral("tbFieldLabel"));
	auto *service_combo = new QComboBox(kopf_group);
	service_combo->setObjectName(QStringLiteral("tbServiceSelect"));
	service_combo->setFixedHeight(fontMetrics().height() + 12);
	service_combo->setAccessibleName(QStringLiteral("Einreichungsauswahl"));
	// Sehr lange Einreichungs-Items würden sonst das Content-Widget auf
	// mehrere tausend Pixel Breite aufblähen (QComboBox::minimumSizeHint =
	// längstes Item). Items werden vom Combo ohnehin elidiert.
	service_combo->setMinimumWidth(0);
	service_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	service_combo->setMinimumContentsLength(16);

	auto *lbl_service_preview = new QLabel(kopf_group);
	lbl_service_preview->setObjectName(QStringLiteral("tbServicePreview"));
	lbl_service_preview->setWordWrap(true);

	auto *kopf_fields_row = new QHBoxLayout();
	kopf_fields_row->setContentsMargins(0, 0, 0, 0);
	kopf_fields_row->setSpacing(6);

	auto *kopf_fields_box = new QWidget(kopf_group);
	kopf_fields_box->setObjectName(QStringLiteral("tbKopfFieldsBox"));
	auto *kopf_fields_layout = new QVBoxLayout(kopf_fields_box);
	kopf_fields_layout->setContentsMargins(0, 0, 0, 0);
	kopf_fields_layout->setSpacing(4);

	auto *kopf_edit_btn = new QPushButton(kopf_group);
	kopf_edit_btn->setObjectName(QStringLiteral("tbBtnKopfEdit"));
	kopf_edit_btn->setFixedSize(fontMetrics().height() + 12, fontMetrics().height() + 12);
	kopf_edit_btn->setIcon(theme_icon("rename.svg"));
	kopf_edit_btn->setIconSize(QSize(16, 16));
	kopf_edit_btn->setToolTip(QStringLiteral("Anzuzeigende Kopfdaten-Felder auswählen"));

	kopf_fields_row->addWidget(kopf_fields_box, 1);
	kopf_fields_row->addWidget(kopf_edit_btn, 0, Qt::AlignTop);

	auto *service_apply_btn =
		new QPushButton(QStringLiteral("In Karten übernehmen"), kopf_group);
	service_apply_btn->setObjectName(QStringLiteral("tbBtnServiceApply"));

	kopf_layout->addWidget(lbl_service_select);
	kopf_layout->addWidget(service_combo);
	kopf_layout->addWidget(lbl_service_preview);
	kopf_layout->addLayout(kopf_fields_row);
	kopf_layout->addWidget(service_apply_btn);
	kopf_group->setVisible(false);

	google_layout->addWidget(google_info);
	google_layout->addWidget(lbl_form_id);
	google_layout->addWidget(edit_form_id);
	google_layout->addWidget(lbl_token_path);
	google_layout->addWidget(edit_token_path);
	google_layout->addWidget(api_btn);
	google_layout->addWidget(status_lbl);
	google_layout->addWidget(kopf_group);
	root->addWidget(google_group);

	// Gruppe 2: Datei-Import (CSV / XLSX / ZIP)
	auto *csv_group = new QGroupBox(QStringLiteral("Datei-Import (CSV / XLSX / ZIP)"), &dlg);
	auto *csv_layout = new QVBoxLayout(csv_group);
	csv_layout->setContentsMargins(12, 16, 12, 12);
	csv_layout->setSpacing(8);
	csv_group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

	auto *csv_btn = new QPushButton(
		QStringLiteral("Datei importieren (CSV / XLSX / ZIP) …"), csv_group);
	csv_btn->setObjectName(QStringLiteral("tbBtnCsvImport"));
	csv_layout->addWidget(csv_btn);
	root->addWidget(csv_group);

	// Gruppe 3 (M5 D3): AI-Import (PDF/Dokument)
	auto *ai_group = new QGroupBox(QStringLiteral("AI-Import (PDF/Dokument)"), &dlg);
	ai_group->setObjectName(QStringLiteral("tbAiImportGroup"));
	auto *ai_layout = new QVBoxLayout(ai_group);
	ai_layout->setContentsMargins(12, 16, 12, 12);
	ai_layout->setSpacing(8);
	ai_group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

	auto *lbl_ai_file = new QLabel(QStringLiteral("DOKUMENT (PDF / DOCX)"), ai_group);
	lbl_ai_file->setObjectName(QStringLiteral("tbFieldLabel"));
	auto *ai_file_row = new QHBoxLayout();
	ai_file_row->setSpacing(6);
	auto *ai_file_edit = new QLineEdit(ai_group);
	ai_file_edit->setObjectName(QStringLiteral("tbAiFilePath"));
	ai_file_edit->setPlaceholderText(QStringLiteral("PDF- oder DOCX-Datei auswählen …"));
	ai_file_edit->setAccessibleName(QStringLiteral("AI-Import-Dokumentpfad"));
	auto *ai_file_btn = new QPushButton(QStringLiteral("Datei wählen …"), ai_group);
	ai_file_btn->setObjectName(QStringLiteral("tbBtnAiFilePick"));
	ai_file_row->addWidget(ai_file_edit, 1);
	ai_file_row->addWidget(ai_file_btn);
	ai_layout->addWidget(lbl_ai_file);
	ai_layout->addLayout(ai_file_row);

	auto *lbl_ai_key = new QLabel(QStringLiteral("API-KEY"), ai_group);
	lbl_ai_key->setObjectName(QStringLiteral("tbFieldLabel"));
	auto *ai_key_row = new QHBoxLayout();
	ai_key_row->setSpacing(6);
	std::string env_key_err;
	const std::string prefilled_key =
		textbox_ai::read_env_key(state_.settings.openrouter_key_path, env_key_err);
	auto *ai_key_edit =
		new QLineEdit(QString::fromUtf8(prefilled_key.c_str()), ai_group);
	ai_key_edit->setObjectName(QStringLiteral("tbAiApiKey"));
	ai_key_edit->setEchoMode(QLineEdit::Password);
	ai_key_edit->setAccessibleName(QStringLiteral("OpenRouter API-Key"));
	ai_key_edit->setPlaceholderText(
		QStringLiteral("OpenRouter API-Key (leer = lokale Offline-Analyse)"));
	/* Ui4: kein Auge-Emoji — natives Text-Toggle (OBS-Style: Text, kein Glyph). */
	auto *ai_key_toggle = new QPushButton(QStringLiteral("Key zeigen"), ai_group);
	ai_key_toggle->setObjectName(QStringLiteral("tbBtnAiKeyToggle"));
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

	// Einklappbarer Master-Prompt (default ausgeklappt)
	auto *prompt_group = new QGroupBox(QStringLiteral("Master-Prompt"), ai_group);
	prompt_group->setObjectName(QStringLiteral("tbAnimGroup"));
	prompt_group->setCheckable(true);
	prompt_group->setChecked(true);
	prompt_group->setAccessibleName(QStringLiteral("Master-Prompt ein/ausklappen"));
	auto *prompt_group_layout = new QVBoxLayout(prompt_group);
	prompt_group_layout->setContentsMargins(8, 8, 8, 8);
	prompt_group_layout->setSpacing(6);

	auto *prompt_body = new QWidget(prompt_group);
	auto *prompt_body_layout = new QVBoxLayout(prompt_body);
	prompt_body_layout->setContentsMargins(0, 0, 0, 0);
	prompt_body_layout->setSpacing(6);

	const std::string init_prompt = state_.settings.ai_system_prompt.empty()
						? textbox_ai::kDefaultSystemPrompt
						: state_.settings.ai_system_prompt;
	QPlainTextEdit *ai_prompt_edit =
		new PromptTabEdit(QString::fromUtf8(init_prompt.c_str()), prompt_body);
	ai_prompt_edit->setObjectName(QStringLiteral("tbAiSystemPrompt"));
	ai_prompt_edit->setAccessibleName(QStringLiteral("Master-Prompt"));
	ai_prompt_edit->setFixedHeight(fontMetrics().height() * 8);

	auto *ai_prompt_reset_btn =
		new QPushButton(QStringLiteral("Zurücksetzen"), prompt_body);
	ai_prompt_reset_btn->setObjectName(QStringLiteral("tbBtnAiPromptReset"));
	prompt_body_layout->addWidget(ai_prompt_edit);
	prompt_body_layout->addWidget(ai_prompt_reset_btn);
	prompt_body->setVisible(true);
	prompt_group_layout->addWidget(prompt_body);
	connect(prompt_group, &QGroupBox::toggled, prompt_body, &QWidget::setVisible);
	ai_layout->addWidget(prompt_group);

	connect(ai_prompt_reset_btn, &QPushButton::clicked, &dlg, [this, ai_prompt_edit]() {
		ai_prompt_edit->setPlainText(
			QString::fromUtf8(textbox_ai::kDefaultSystemPrompt));
		state_.settings.ai_system_prompt.clear();
		save_state();
	});

	auto *ai_start_btn = new QPushButton(QStringLiteral("Starten"), ai_group);
	ai_start_btn->setObjectName(QStringLiteral("tbBtnAiStart"));
	ai_layout->addWidget(ai_start_btn);

	auto *ai_status_lbl = new QLabel(
		prefilled_key.empty()
			? QStringLiteral("Kein API-Key – lokale Offline-Analyse")
			: QStringLiteral("Bereit für AI-Import."),
		ai_group);
	ai_status_lbl->setObjectName(QStringLiteral("tbImportStatus"));
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
	auto *ai_preview_group = new QGroupBox(QStringLiteral("Vorschau"), ai_group);
	auto *ai_preview_layout = new QVBoxLayout(ai_preview_group);
	ai_preview_layout->setContentsMargins(10, 14, 10, 10);
	ai_preview_layout->setSpacing(8);

	auto *ai_source_lbl = new QLabel(ai_preview_group);
	ai_source_lbl->setObjectName(QStringLiteral("tbAiSourceLabel"));
	ai_source_lbl->setWordWrap(true);

	auto *ai_table = new QTableWidget(0, 3, ai_preview_group);
	ai_table->setObjectName(QStringLiteral("tbAiPreviewTable"));
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
	ai_table->setMinimumHeight(fontMetrics().height() * 10);

	auto *ai_apply_btn = new QPushButton(QStringLiteral("Übernehmen"), ai_preview_group);
	ai_apply_btn->setObjectName(QStringLiteral("tbBtnAiApply"));

	ai_preview_layout->addWidget(ai_source_lbl);
	ai_preview_layout->addWidget(ai_table);
	ai_preview_layout->addWidget(ai_apply_btn);
	ai_preview_group->setVisible(false);
	ai_layout->addWidget(ai_preview_group);

	root->addWidget(ai_group);

	// Ergebnis-/Fehler-Label unterhalb
	auto *error_lbl = new QLabel(&dlg);
	error_lbl->setObjectName(QStringLiteral("tbImportError"));
	error_lbl->setWordWrap(true);
	error_lbl->setVisible(false);
	root->addWidget(error_lbl);
	root->addStretch(1);
	int avail_h = 780;
	if (auto *scr = QGuiApplication::primaryScreen())
		avail_h = scr->availableGeometry().height() - 80;
	dlg.resize(560, std::min(avail_h, dlg_content->sizeHint().height() + 8));

	auto pending_ai_res = std::make_shared<textbox_ai::AiResult>();
	auto loaded_services =
		std::make_shared<std::vector<textbox_import::TextboxService>>();

	auto get_service_fields =
		[](const textbox_import::TextboxService &svc)
		-> std::vector<std::pair<std::string, std::string>> {
		if (!svc.fields.empty())
			return svc.fields;
		std::vector<std::pair<std::string, std::string>> fb;
		if (!svc.zeitstempel.empty())
			fb.emplace_back("Zeitstempel", svc.zeitstempel);
		if (!svc.woche.empty())
			fb.emplace_back("Woche", svc.woche);
		if (!svc.tag.empty())
			fb.emplace_back("Tag", svc.tag);
		if (!svc.verantwortlich.empty())
			fb.emplace_back("Verantwortlich", svc.verantwortlich);
		return fb;
	};

	auto collect_all_field_names =
		[loaded_services, get_service_fields]() -> std::vector<std::string> {
		std::vector<std::string> names;
		for (const auto &svc : *loaded_services) {
			for (const auto &kv : get_service_fields(svc)) {
				if (!kv.first.empty() &&
				    std::find(names.begin(), names.end(), kv.first) ==
					    names.end()) {
					names.push_back(kv.first);
				}
			}
		}
		return names;
	};

	auto get_selected_fields_for_service =
		[this, get_service_fields, collect_all_field_names](
			const textbox_import::TextboxService &svc)
		-> std::vector<std::pair<std::string, std::string>> {
		const auto all_svc_fields = get_service_fields(svc);
		const auto &cfg = state_.settings.kopf_fields;
		if (cfg.empty())
			return all_svc_fields;
		const auto all_names = collect_all_field_names();
		bool any_match = false;
		for (const auto &n : all_names) {
			if (std::find(cfg.begin(), cfg.end(), n) != cfg.end()) {
				any_match = true;
				break;
			}
		}
		if (!any_match)
			return all_svc_fields;
		std::vector<std::pair<std::string, std::string>> out;
		for (const auto &kv : all_svc_fields) {
			if (std::find(cfg.begin(), cfg.end(), kv.first) != cfg.end())
				out.push_back(kv);
		}
		return out;
	};

	auto format_service_joined =
		[get_selected_fields_for_service](
			const textbox_import::TextboxService &svc) -> QString {
		const auto sel = get_selected_fields_for_service(svc);
		QStringList parts;
		for (const auto &kv : sel) {
			parts.append(QStringLiteral("%1: %2").arg(
				QString::fromUtf8(kv.first.c_str()),
				QString::fromUtf8(kv.second.c_str())));
		}
		return parts.join(QStringLiteral(" · "));
	};

	// Aktualisiert die dynamischen Kopfdaten-Labels für die gewählte Einreichung.
	auto update_kopf_labels =
		[kopf_fields_box, kopf_fields_layout, get_selected_fields_for_service](
			const textbox_import::TextboxService &svc) {
			while (QLayoutItem *it = kopf_fields_layout->takeAt(0)) {
				if (QWidget *w = it->widget())
					delete w;
				delete it;
			}
			const auto sel = get_selected_fields_for_service(svc);
			for (const auto &kv : sel) {
				auto *lbl = new QLabel(
					QStringLiteral("%1: %2").arg(
						QString::fromUtf8(kv.first.c_str()),
						QString::fromUtf8(kv.second.c_str())),
					kopf_fields_box);
				lbl->setWordWrap(true);
				kopf_fields_layout->addWidget(lbl);
			}
		};

	auto refresh_services_view =
		[loaded_services, service_combo, lbl_service_preview, format_service_joined,
		 update_kopf_labels](int keep_index = 0) {
			if (loaded_services->empty())
				return;
			const int clamped_idx = std::clamp(
				keep_index, 0,
				static_cast<int>(loaded_services->size()) - 1);
			{
				const QSignalBlocker blocker(service_combo);
				service_combo->clear();
				QStringList preview_lines;
				for (const auto &svc : *loaded_services) {
					const QString joined = format_service_joined(svc);
					service_combo->addItem(
						joined.isEmpty() ? QStringLiteral("—") : joined);
					const std::size_t n_entries = count_service_blocks(svc);
					if (joined.isEmpty()) {
						preview_lines.append(
							QStringLiteral("• %1 Einträge")
								.arg(n_entries));
					} else {
						preview_lines.append(
							QStringLiteral("• %1 · %2 Einträge")
								.arg(joined)
								.arg(n_entries));
					}
				}
				service_combo->setCurrentIndex(clamped_idx);
				lbl_service_preview->setText(
					preview_lines.join(QLatin1Char('\n')));
			}
			update_kopf_labels(
				(*loaded_services)[static_cast<std::size_t>(clamped_idx)]);
		};

	connect(kopf_edit_btn, &QPushButton::clicked, &dlg,
		[this, &dlg, service_combo, collect_all_field_names, refresh_services_view]() {
			const auto all_names = collect_all_field_names();
			QDialog edit_dlg(&dlg);
			edit_dlg.setObjectName(QStringLiteral("tbKopfEditDialog"));
			edit_dlg.setWindowTitle(QStringLiteral("Kopfdaten-Felder auswählen"));
			edit_dlg.setMinimumWidth(fontMetrics().averageCharWidth() * 30);

			auto *lay = new QVBoxLayout(&edit_dlg);
			lay->setContentsMargins(14, 14, 14, 14);
			lay->setSpacing(8);

			const auto &cfg = state_.settings.kopf_fields;
			bool has_overlap = false;
			for (const auto &n : all_names) {
				if (std::find(cfg.begin(), cfg.end(), n) != cfg.end()) {
					has_overlap = true;
					break;
				}
			}

			std::vector<std::pair<std::string, QCheckBox *>> checks;
			for (const auto &n : all_names) {
				auto *cb = new QCheckBox(
					QString::fromUtf8(n.c_str()), &edit_dlg);
				const bool checked =
					cfg.empty() || !has_overlap ||
					(std::find(cfg.begin(), cfg.end(), n) != cfg.end());
				cb->setChecked(checked);
				lay->addWidget(cb);
				checks.emplace_back(n, cb);
			}

			auto *btn_row = new QHBoxLayout();
			btn_row->setSpacing(8);
			auto *cancel_btn =
				new QPushButton(QStringLiteral("Abbrechen"), &edit_dlg);
			cancel_btn->setObjectName(QStringLiteral("tbBtnKopfEditCancel"));
			auto *ok_btn = new QPushButton(QStringLiteral("OK"), &edit_dlg);
			ok_btn->setObjectName(QStringLiteral("tbBtnKopfEditOk"));
			btn_row->addStretch(1);
			btn_row->addWidget(cancel_btn);
			btn_row->addWidget(ok_btn);
			lay->addLayout(btn_row);

			connect(cancel_btn, &QPushButton::clicked, &edit_dlg,
				&QDialog::reject);
			connect(ok_btn, &QPushButton::clicked, &edit_dlg, &QDialog::accept);

			if (edit_dlg.exec() == QDialog::Accepted) {
				std::vector<std::string> chosen;
				for (const auto &pr : checks) {
					if (pr.second->isChecked())
						chosen.push_back(pr.first);
				}
				state_.settings.kopf_fields = std::move(chosen);
				save_state();
				refresh_services_view(service_combo->currentIndex());
				set_dock_status(
					QStringLiteral("Kopfdaten-Felder gespeichert"), false);
			}
		});

	connect(service_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg,
		[loaded_services, update_kopf_labels](int idx) {
			if (idx >= 0 &&
			    static_cast<std::size_t>(idx) < loaded_services->size()) {
				update_kopf_labels(
					(*loaded_services)[static_cast<std::size_t>(idx)]);
			}
		});

	connect(service_apply_btn, &QPushButton::clicked, &dlg,
		[this, service_combo, loaded_services, status_lbl, update_kopf_labels,
		 format_service_joined]() {
			const int idx = service_combo->currentIndex();
			if (idx < 0 ||
			    static_cast<std::size_t>(idx) >= loaded_services->size())
				return;
			const auto &svc = (*loaded_services)[static_cast<std::size_t>(idx)];
			textbox_import::ImportResult single_res;
			single_res.service = svc;
			single_res.woche = svc.woche;
			single_res.tag = svc.tag;
			single_res.verantwortlich = svc.verantwortlich;
			single_res.zeitstempel = svc.zeitstempel;
			single_res.punkte = svc.punkte;
			single_res.services = {svc};
			apply_import_result(single_res);
			update_kopf_labels(svc);
			const QString joined = format_service_joined(svc);
			const QString msg =
				joined.isEmpty()
					? QStringLiteral("Übernommen")
					: QStringLiteral("Übernommen: %1").arg(joined);
			status_lbl->setText(msg);
			set_dock_status(msg, false);
		});

	connect(ai_file_btn, &QPushButton::clicked, &dlg, [&dlg, ai_file_edit]() {
		const QString chosen = QFileDialog::getOpenFileName(
			&dlg, QStringLiteral("Dokument auswählen"),
			ai_file_edit->text(),
			QStringLiteral("Dokumente (*.pdf *.docx);;Alle Dateien (*.*)"),
			nullptr, dialog_options());
		if (!chosen.isEmpty()) {
			ai_file_edit->setText(chosen);
		}
	});

	QPointer<QDialog> dlg_ptr(&dlg);
	QPointer<TextboxDock> self_ptr(this);
	const auto cancel = cancel_;

	auto handle_success = [cancel, self_ptr, kopf_group, loaded_services,
			       refresh_services_view, status_lbl, error_lbl](
				      const textbox_import::TextboxImportResult &res,
				      const QString &source_name = QString()) {
		if (*cancel || !self_ptr)
			return;
		self_ptr->apply_import_result(res);
		if (*cancel || !self_ptr)
			return;

		loaded_services->clear();
		if (!res.services.empty()) {
			*loaded_services = res.services;
		} else {
			textbox_import::TextboxService fallback_svc = res.service;
			if (fallback_svc.punkte.empty()) {
				fallback_svc.woche = res.woche;
				fallback_svc.tag = res.tag;
				fallback_svc.verantwortlich = res.verantwortlich;
				fallback_svc.zeitstempel = res.zeitstempel;
				fallback_svc.punkte = res.punkte;
			}
			loaded_services->push_back(std::move(fallback_svc));
		}

		refresh_services_view(0);
		kopf_group->setVisible(true);
		error_lbl->clear();
		error_lbl->setVisible(false);
		const std::size_t svc_count = loaded_services->size();
		const QString count_str =
			QStringLiteral("%1 Einreichungen gefunden").arg(svc_count);
		const QString msg =
			source_name.isEmpty()
				? QStringLiteral(
					  "%1 — 12 Karten befüllt — im Dock editierbar, „Zu Szene“ legt sie in die Szene.")
					  .arg(count_str)
				: QStringLiteral("%1 — Importiert: %2 Einträge (Quelle: %3)")
					  .arg(count_str)
					  .arg(res.punkte.size())
					  .arg(source_name);
		status_lbl->setText(msg);
		self_ptr->set_dock_status(msg, false);
	};

	auto handle_error = [cancel, self_ptr, status_lbl, error_lbl](const std::string &err_msg) {
		if (*cancel || !self_ptr)
			return;
		const QString qerr = QString::fromUtf8(err_msg.c_str());
		error_lbl->setText(qerr);
		error_lbl->setVisible(true);
		status_lbl->setText(qerr);
		self_ptr->set_dock_status(qerr, true);
	};

	connect(ai_start_btn, &QPushButton::clicked, &dlg,
		[this, &dlg, cancel, dlg_ptr, self_ptr, api_btn, csv_btn, ai_start_btn,
		 ai_file_edit, ai_key_edit, ai_prompt_edit, ai_status_lbl, status_lbl, error_lbl,
		 ai_preview_group, ai_source_lbl, ai_table, pending_ai_res]() {
			QString doc_file = ai_file_edit->text().trimmed();
			if (doc_file.isEmpty()) {
				doc_file = QFileDialog::getOpenFileName(
					&dlg, QStringLiteral("Dokument auswählen"),
					QString(),
					QStringLiteral(
						"Dokumente (*.pdf *.docx);;Alle Dateien (*.*)"),
					nullptr, dialog_options());
				if (doc_file.isEmpty())
					return;
				ai_file_edit->setText(doc_file);
			}

			const std::string prompt_str =
				ai_prompt_edit->toPlainText().trimmed().toUtf8().toStdString();
			state_.settings.ai_system_prompt =
				(prompt_str == textbox_ai::kDefaultSystemPrompt) ? "" : prompt_str;
			save_state();

			const std::string file_utf8 = doc_file.toUtf8().toStdString();
			const std::filesystem::path doc_fsp = std::filesystem::u8path(file_utf8);
			set_dock_status(
				QStringLiteral("AI-Import gestartet: %1")
					.arg(QString::fromUtf8(
						doc_fsp.filename().u8string().c_str())),
				false);
			const std::string api_key =
				ai_key_edit->text().trimmed().toUtf8().toStdString();
			const std::string sys_prompt =
				prompt_str.empty() ? textbox_ai::kDefaultSystemPrompt : prompt_str;
			const std::string python_exe = state_.settings.markitdown_python;
			const std::string referenz_path = state_.settings.referenz_path;

			api_btn->setEnabled(false);
			csv_btn->setEnabled(false);
			ai_start_btn->setEnabled(false);
			ai_preview_group->setVisible(false);
			error_lbl->clear();
			error_lbl->setVisible(false);
			ai_status_lbl->setText(QStringLiteral("Text aus Dokument extrahieren..."));
			status_lbl->setText(QStringLiteral("Text aus Dokument extrahieren..."));

			// Hintergrund-Thread erfasst nur die Kopie von cancel_ (kein this).
			std::thread([cancel, self_ptr, dlg_ptr, api_btn, csv_btn, ai_start_btn,
				     ai_status_lbl, status_lbl, error_lbl, ai_preview_group,
				     ai_source_lbl, ai_table, pending_ai_res, file_utf8, api_key,
				     sys_prompt, python_exe, referenz_path]() {
				ActiveJobGuard job_guard;
				if (*cancel)
					return;

				auto post_progress = [cancel, self_ptr, dlg_ptr, ai_status_lbl,
						      status_lbl](const std::string &msg) {
					if (*cancel || !self_ptr)
						return;
					const QString qmsg = QString::fromUtf8(msg.c_str());
					QMetaObject::invokeMethod(
						self_ptr.data(),
						[cancel, self_ptr, dlg_ptr, ai_status_lbl,
						 status_lbl, qmsg]() {
							if (*cancel || !self_ptr || !dlg_ptr)
								return;
							ai_status_lbl->setText(qmsg);
							status_lbl->setText(qmsg);
						},
						Qt::QueuedConnection);
				};

				textbox_ai::AiResult ai_res;
				std::string fatal_err;

				try {
					std::string ext_err;
					const std::string text =
						textbox_ai::extract_document_text(
							file_utf8, python_exe, ext_err, cancel);
					if (*cancel)
						return;
					if (text.empty()) {
						throw std::runtime_error(
							ext_err.empty()
								? "Kein Text im Dokument gefunden."
								: ext_err);
					}

					if (!api_key.empty()) {
						blog(LOG_INFO, "[textbox] AI-Import: Verwende OpenRouter LLM");
						try {
							ai_res = textbox_ai::process_with_llm(
								api_key, text, sys_prompt,
								referenz_path, post_progress, cancel);
							if (*cancel)
								return;
						} catch (const std::exception &llm_ex) {
							if (*cancel)
								return;
							blog(LOG_INFO, "[textbox] AI-Import: Wechsle auf Offline-Fallback-Parser");
							post_progress(
								"Free-Tier nicht erreichbar – nutze lokalen Offline-Parser...");
							auto local_items =
								textbox_ai::parse_textbox_locally(
									text);
							if (local_items.empty())
								throw std::runtime_error(
									llm_ex.what());
							ai_res.items = std::move(local_items);
							ai_res.model_label = "Offline-Fallback";
						}
					} else {
						blog(LOG_INFO, "[textbox] AI-Import: Verwende lokalen Offline-Parser");
						post_progress(
							"Kein API-Key – lokale Offline-Analyse");
						auto local_items =
							textbox_ai::parse_textbox_locally(text);
						if (local_items.empty()) {
							throw std::runtime_error(
								"Weder API-Key vorhanden noch konnte der lokale Parser Einträge erkennen.");
						}
						ai_res.items = std::move(local_items);
						ai_res.model_label = "Lokal (ohne API-Key)";
					}

					if (*cancel)
						return;

					const auto ref_table = load_reference_table(referenz_path);
					if (*cancel)
						return;
					if (!ref_table.empty()) {
						textbox_ai::enrich_titles_from_reference(
							ai_res.items, ref_table);
					}

					if (ai_res.items.empty()) {
						throw std::runtime_error(
							"Keine Einträge erkannt.");
					}
				} catch (const std::exception &ex) {
					fatal_err = ex.what();
				}

				if (*cancel || !self_ptr)
					return;

				QMetaObject::invokeMethod(
					self_ptr.data(),
					[cancel, self_ptr, dlg_ptr, api_btn, csv_btn, ai_start_btn,
					 ai_status_lbl, status_lbl, error_lbl, ai_preview_group,
					 ai_source_lbl, ai_table, pending_ai_res,
					 ai_res = std::move(ai_res),
					 fatal_err = std::move(fatal_err)]() mutable {
						if (*cancel || !self_ptr || !dlg_ptr)
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
								"Vorschau: %1 Einträge (%2)")
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

			capture_undo_snapshot(this, state_);

			const std::size_t total_items = pending_ai_res->items.size();
			while (state_.cards.size() < total_items) {
				state_.add_card(); // IMP-T: next_id-vergabene IDs — indexbasierte IDs kollidierten mit bestehenden Karten nach Löschungen
			}
			if (static_cast<int>(state_.cards.size()) + 1 > state_.next_id)
				state_.next_id = static_cast<int>(state_.cards.size()) + 1;

			const std::size_t n = std::min(total_items, state_.cards.size());
			const std::size_t ignored = (total_items > n) ? (total_items - n) : 0;

			for (std::size_t i = 0; i < n; ++i) {
				state_.cards[i].zeile1 = pending_ai_res->items[i].zeile1;
				state_.cards[i].zeile2 = pending_ai_res->items[i].zeile2;
				state_.cards[i].zeile3 = pending_ai_res->items[i].zeile3;
				// IMP-R: rows fallen auf die neuen zeile1..3 zurueck.
				state_.cards[i].rows.clear();
			}
			// Überzählige Bestandskarten bereinigen, damit keine alten Einträge stehen bleiben.
			for (std::size_t i = n; i < state_.cards.size(); ++i) {
				if (!is_standard_card(state_.cards[i], i))
					state_.cards[i].zeile1.clear();
				state_.cards[i].zeile2.clear();
				state_.cards[i].zeile3.clear();
				state_.cards[i].rows.clear();
			}

			save_state();
			for (std::size_t i = 0; i < state_.cards.size(); ++i) {
				if (cancel_ && *cancel_)
					return;
				textbox_scene::update_everywhere(
					state_.cards[i], false, state_.settings);
			}
			rebuild_rows();

			const QString label_q =
				QString::fromUtf8(pending_ai_res->model_label.c_str());
			QString dock_msg =
				QStringLiteral("AI-Import: %1 Einträge (Quelle: %2)")
					.arg(n)
					.arg(label_q);
			if (ignored > 0) {
				dock_msg += QStringLiteral(" — Übernommen: %1 Einträge (%2 weitere ignoriert)")
						    .arg(n)
						    .arg(ignored);
			}
			set_dock_status(dock_msg, false);
			dlg.accept();
		});

	connect(api_btn, &QPushButton::clicked, &dlg,
		[this, cancel, dlg_ptr, self_ptr, api_btn, csv_btn, edit_form_id, status_lbl,
		 error_lbl, handle_success, handle_error]() {
			const std::string form_id =
				edit_form_id->text().trimmed().toUtf8().toStdString();
			state_.settings.google_form_id = form_id;
			save_state();
			if (form_id.empty()) {
				const QString msg =
					QStringLiteral("Bitte zuerst eine FORM-ID eintragen");
				status_lbl->setText(msg);
				error_lbl->setText(msg);
				error_lbl->setVisible(true);
				set_dock_status(msg, true);
				return;
			}

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

			const std::filesystem::path token_path = std::filesystem::u8path(
				state_.settings.token_path.empty()
					? textbox_import::default_token_path_str()
					: state_.settings.token_path);

			// Hintergrund-Thread erfasst nur die Kopie von cancel_ (kein this).
			std::thread([cancel, self_ptr, dlg_ptr, api_btn, csv_btn, cache_path,
				     token_path, form_id, handle_success, handle_error]() {
				ActiveJobGuard job_guard;
				if (*cancel)
					return;

				std::string err_msg;
				auto res = textbox_import::import_from_api(
					&err_msg, cache_path, token_path, form_id);

				if (*cancel || !self_ptr)
					return;

				QMetaObject::invokeMethod(
					self_ptr.data(),
					[cancel, self_ptr, dlg_ptr, api_btn, csv_btn,
					 res = std::move(res), err_msg = std::move(err_msg),
					 handle_success, handle_error]() {
						if (*cancel || !self_ptr || !dlg_ptr)
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
		[&dlg, cancel, dlg_ptr, self_ptr, api_btn, csv_btn, status_lbl, error_lbl,
		 handle_success, handle_error]() {
			const QString file_name = QFileDialog::getOpenFileName(
				&dlg, QStringLiteral("Datei importieren"), QString(),
				QStringLiteral(
					"CSV-Dateien (*.csv);;Excel-Dateien (*.xlsx);;ZIP-Archive (*.zip);;Alle Dateien (*.*)"),
				nullptr, dialog_options());
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

			// Hintergrund-Thread erfasst nur die Kopie von cancel_ (kein this).
			std::thread([cancel, self_ptr, dlg_ptr, api_btn, csv_btn, path,
				     source_basename, handle_success, handle_error]() {
				ActiveJobGuard job_guard;
				if (*cancel)
					return;

				textbox_import::TextboxImportResult res;
				std::string err_msg;
				const auto err = textbox_import::parse_answers_file(
					path, res, nullptr, &err_msg);

				if (*cancel || !self_ptr)
					return;

				QMetaObject::invokeMethod(
					self_ptr.data(),
					[cancel, self_ptr, dlg_ptr, api_btn, csv_btn, err,
					 res = std::move(res), err_msg = std::move(err_msg),
					 source_basename, handle_success, handle_error]() {
						if (*cancel || !self_ptr || !dlg_ptr)
							return;
						api_btn->setEnabled(true);
						csv_btn->setEnabled(true);
						if (err == textbox_import::ImportError::None) {
							handle_success(res, source_basename);
						} else {
							const std::string base_utf8 =
								source_basename.toUtf8().toStdString();
							const std::string full_err =
								(!base_utf8.empty() &&
								 err_msg.find(base_utf8) ==
									 std::string::npos)
									? (base_utf8 + ": " + err_msg)
									: err_msg;
							handle_error(full_err);
						}
					},
					Qt::QueuedConnection);
			}).detach();
		});

	dlg.exec();
	if (dlg.result() == QDialog::Accepted) {
		tb_event_log(tb_event_log_dir_from_state_path(state_path_),
			     "Datenarchiv geschlossen");
	} else {
		set_dock_status(QStringLiteral("Datenarchiv geschlossen"), false);
	}
}

void TextboxDock::on_open_quick_profiles_dialog()
{
	QDialog dlg(this);
	dlg.setObjectName(QStringLiteral("tbQuickProfilesDialog"));
	dlg.setWindowTitle(QStringLiteral("Schnell-Profile"));
	dlg.setMinimumWidth(fontMetrics().averageCharWidth() * 42);

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
	lbl_name->setObjectName(QStringLiteral("tbFieldLabel"));

	auto *save_row = new QHBoxLayout();
	save_row->setSpacing(8);
	auto *name_edit = new QLineEdit(make_default_name(), save_group);
	name_edit->setPlaceholderText(QStringLiteral("Name des Schnell-Profils"));
	name_edit->setAccessibleName(QStringLiteral("Schnellprofil-Name"));
	auto *save_btn = new QPushButton(QStringLiteral("Speichern"), save_group);
	save_btn->setObjectName(QStringLiteral("tbBtnQuickSave"));
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
	combo->setFixedHeight(fontMetrics().height() + 12);
	combo->setAccessibleName(QStringLiteral("Schnellprofil-Auswahl"));

	auto *action_row = new QHBoxLayout();
	action_row->setSpacing(8);
	auto *apply_btn = new QPushButton(QStringLiteral("Übernehmen"), list_group);
	apply_btn->setObjectName(QStringLiteral("tbBtnQuickApply"));
	auto *del_btn = new QPushButton(QStringLiteral("Löschen"), list_group);
	del_btn->setObjectName(QStringLiteral("tbBtnQuickDelete"));
	del_btn->setIcon(theme_icon("trash.svg"));
	del_btn->setIconSize(QSize(16, 16));
	del_btn->setProperty("tbDanger", true);
	action_row->addWidget(apply_btn, 1);
	action_row->addWidget(del_btn, 1);

	list_layout->addWidget(combo);
	list_layout->addLayout(action_row);
	root->addWidget(list_group);

	auto *status_lbl = new QLabel(&dlg);
	status_lbl->setObjectName(QStringLiteral("tbQuickStatus"));
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
	tb_event_log(tb_event_log_dir_from_state_path(state_path_),
		     "Schnell-Profile geschlossen");
	refresh_top_row();
}

void TextboxDock::on_backup_state()
{
	const QString file_name = QFileDialog::getSaveFileName(
		this, QStringLiteral("JSON-Backup speichern"),
		QStringLiteral("textbox-backup.json"),
		QStringLiteral("JSON-Backup (*.json)"),
		nullptr, dialog_options());
	if (file_name.isEmpty())
		return;

	const std::string out_path = file_name.toUtf8().toStdString();
	const TextboxState snap = state_;
	const auto cancel = cancel_;
	QPointer<TextboxDock> self_ptr(this);

	// Hintergrund-Thread erfasst nur die Kopie von cancel_ (kein this).
	std::thread([cancel, self_ptr, snap, out_path, file_name]() {
		ActiveJobGuard job_guard;
		if (*cancel)
			return;
		const std::string err = snap.save(out_path);
		if (*cancel || !self_ptr)
			return;
		QMetaObject::invokeMethod(
			self_ptr.data(),
			[cancel, self_ptr, err, file_name]() {
				if (*cancel || !self_ptr)
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

void TextboxDock::on_restore_state()
{
	const QString file_name = QFileDialog::getOpenFileName(
		this, QStringLiteral("JSON-Backup wiederherstellen"), QString(),
		QStringLiteral("JSON-Backup (*.json)"),
		nullptr, dialog_options());
	if (file_name.isEmpty())
		return;

	const std::string in_path = file_name.toUtf8().toStdString();
	const auto cancel = cancel_;
	QPointer<TextboxDock> self_ptr(this);

	// Hintergrund-Thread erfasst nur die Kopie von cancel_ (kein this).
	std::thread([cancel, self_ptr, in_path, file_name]() {
		ActiveJobGuard job_guard;
		if (*cancel)
			return;
		TextboxState restored;
		const std::string err = restored.restore_backup(in_path);
		if (*cancel || !self_ptr)
			return;
		QMetaObject::invokeMethod(
			self_ptr.data(),
			[cancel, self_ptr, err, restored = std::move(restored), file_name]() mutable {
				if (*cancel || !self_ptr)
					return;
				if (!err.empty()) {
					self_ptr->set_dock_status(
						QStringLiteral("Restore fehlgeschlagen: %1")
							.arg(QString::fromUtf8(err.c_str())),
						true);
					return;
				}

				std::vector<TextboxCard> prev_cards = self_ptr->state_.cards;
				std::vector<std::string> prev_ids;
				prev_ids.reserve(self_ptr->state_.cards.size());
				for (const auto &c : self_ptr->state_.cards)
					prev_ids.push_back(c.id);

				self_ptr->state_ = std::move(restored);
				self_ptr->state_.undo_snapshot_ = std::move(prev_cards);
				self_ptr->state_.undo_available_ = true;
				if (auto it = g_dock_schedulers.find(self_ptr.data());
				    it != g_dock_schedulers.end() && it->second.undo_btn) {
					it->second.undo_btn->setEnabled(true);
				}
				self_ptr->save_state();
				self_ptr->sync_all_cards_live(prev_ids);
				if (*cancel)
					return;
				self_ptr->rebuild_rows();
				self_ptr->set_dock_status(
					QStringLiteral("Restore erfolgreich: %1").arg(file_name),
					false);
			},
			Qt::QueuedConnection);
	}).detach();
}

void TextboxDock::on_open_settings_dialog()
{
	set_dock_status(QStringLiteral("Einstellungen geöffnet"), false);

	QDialog dlg(this);
	dlg.setObjectName(QStringLiteral("tbSettingsDialog"));
	dlg.setWindowTitle(QStringLiteral("Einstellungen"));
	dlg.setMinimumWidth(fontMetrics().averageCharWidth() * 48);

	auto *root = new QVBoxLayout(&dlg);
	root->setContentsMargins(16, 16, 16, 16);
	root->setSpacing(12);

	const TextboxSettings def_settings;

	auto *oauth_group = new QGroupBox(QStringLiteral("Google-Forms-API & OAuth"), &dlg);
	auto *oauth_layout = new QVBoxLayout(oauth_group);
	oauth_layout->setContentsMargins(12, 16, 12, 12);
	oauth_layout->setSpacing(8);

	auto *lbl_form = new QLabel(QStringLiteral("GOOGLE-FORM-ID"), oauth_group);
	lbl_form->setObjectName(QStringLiteral("tbFieldLabel"));
	const std::string cur_form_id = state_.settings.google_form_id;
	auto *edit_form = new QLineEdit(QString::fromUtf8(cur_form_id.c_str()), oauth_group);
	edit_form->setPlaceholderText(QStringLiteral("Google-Form-ID"));
	edit_form->setAccessibleName(QStringLiteral("Google-Form-ID"));

	auto *lbl_token = new QLabel(QStringLiteral("OAUTH-TOKEN-PFAD"), oauth_group);
	lbl_token->setObjectName(QStringLiteral("tbFieldLabel"));
	const std::string cur_token = state_.settings.token_path.empty()
					      ? textbox_import::default_token_path_str()
					      : state_.settings.token_path;
	auto *token_row = new QHBoxLayout();
	token_row->setSpacing(8);
	auto *edit_token = new QLineEdit(QString::fromUtf8(cur_token.c_str()), oauth_group);
	edit_token->setPlaceholderText(QString::fromUtf8(textbox_import::default_token_path_str()));
	edit_token->setAccessibleName(QStringLiteral("OAuth-Token-Pfad"));
	/* Ui4: „…“ = natives OBS-Browse-Button-Idiom (Properties-Panel). */
	auto *browse_btn = new QPushButton(QStringLiteral("…"), oauth_group);
	browse_btn->setObjectName(QStringLiteral("tbBtn"));
	token_row->addWidget(edit_token, 1);
	token_row->addWidget(browse_btn);

	oauth_layout->addWidget(lbl_form);
	oauth_layout->addWidget(edit_form);
	oauth_layout->addWidget(lbl_token);
	oauth_layout->addLayout(token_row);
	root->addWidget(oauth_group);

	// Gruppe (IMP-F / F2b): Karten-Vorlagen (Templates)
	auto *tpl_group = new QGroupBox(QStringLiteral("Karten-Vorlagen"), &dlg);
	auto *tpl_group_layout = new QVBoxLayout(tpl_group);
	tpl_group_layout->setContentsMargins(12, 16, 12, 12);
	tpl_group_layout->setSpacing(8);

	auto *tpl_info = new QLabel(
		QStringLiteral("Vorlagen sind .html-Dateien im Vorlagen-Ordner des Plugins; "
				 "jede Karte wählt seine Vorlage im Feld „VORLAGE“ (leer = index.html = Standard)."),
		tpl_group);
	tpl_info->setWordWrap(true);
	tpl_info->setObjectName(QStringLiteral("tbFieldLabel"));

	auto *tpl_btn_row = new QHBoxLayout();
	tpl_btn_row->setSpacing(8);
	auto *tpl_folder_btn =
		new QPushButton(QStringLiteral("Vorlagen-Ordner öffnen"), tpl_group);
	tpl_folder_btn->setObjectName(QStringLiteral("tbBtn"));
	auto *tpl_copy_btn =
		new QPushButton(QStringLiteral("Neue Vorlage aus index.html kopieren"), tpl_group);
	tpl_copy_btn->setObjectName(QStringLiteral("tbBtn"));
	tpl_btn_row->addWidget(tpl_folder_btn);
	tpl_btn_row->addWidget(tpl_copy_btn);
	tpl_btn_row->addStretch(1);

	tpl_group_layout->addWidget(tpl_info);
	tpl_group_layout->addLayout(tpl_btn_row);
	root->addWidget(tpl_group);

	connect(tpl_folder_btn, &QPushButton::clicked, &dlg,
		[this]() { on_open_templates_folder(); });
	connect(tpl_copy_btn, &QPushButton::clicked, &dlg,
		[this]() { on_copy_template_from_classic(); });

	// Gruppe 2 (M5): AI-Import & Animationen Pfade
	auto *m5_group = new QGroupBox(QStringLiteral("AI-Import & Animationen"), &dlg);
	auto *m5_layout = new QVBoxLayout(m5_group);
	m5_layout->setContentsMargins(12, 16, 12, 12);
	m5_layout->setSpacing(8);

	auto make_path_row = [m5_group](QVBoxLayout *parent_l, const QString &label_txt,
					const std::string &cur_val,
					const std::string &def_val,
					QPushButton *&out_btn,
					const QString &acc_name) -> QLineEdit * {
		auto *lbl = new QLabel(label_txt, m5_group);
		lbl->setObjectName(QStringLiteral("tbFieldLabel"));
		auto *row = new QHBoxLayout();
		row->setSpacing(8);
		const std::string v = cur_val.empty() ? def_val : cur_val;
		auto *edit = new QLineEdit(QString::fromUtf8(v.c_str()), m5_group);
		edit->setPlaceholderText(QString::fromUtf8(def_val.c_str()));
		edit->setAccessibleName(acc_name);
		out_btn = new QPushButton(QStringLiteral("…"), m5_group);
		out_btn->setObjectName(QStringLiteral("tbBtn"));
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
		browse_key_btn,
		QStringLiteral("OpenRouter-API-Key-Pfad"));

	QPushButton *browse_py_btn = nullptr;
	auto *edit_py_path = make_path_row(
		m5_layout, QStringLiteral("MARKITDOWN-PYTHON-PFAD"),
		state_.settings.markitdown_python, def_settings.markitdown_python,
		browse_py_btn,
		QStringLiteral("Markitdown-Python-Pfad"));

	QPushButton *browse_anim_btn = nullptr;
	auto *edit_anim_dir = make_path_row(
		m5_layout, QStringLiteral("ANIMATIONS-ORDNER"),
		state_.settings.animation_dir, def_settings.animation_dir,
		browse_anim_btn,
		QStringLiteral("Animations-Ordner"));

	QPushButton *browse_referenz_btn = nullptr;
	auto *edit_referenz_path = make_path_row(
		m5_layout, QStringLiteral("REFERENZ-INDEX"),
		state_.settings.referenz_path, def_settings.referenz_path,
		browse_referenz_btn,
		QStringLiteral("Referenz-Index"));

	root->addWidget(m5_group);

	// Gruppe 3 (M6-B2/B3 / F1): Dock-Theme & Eigene Karten-Farbprofile + YAML-Export/Import
	auto *profiles_group = new QGroupBox(QStringLiteral("Farbprofile"), &dlg);
	auto *profiles_layout = new QVBoxLayout(profiles_group);
	profiles_layout->setContentsMargins(12, 16, 12, 12);
	profiles_layout->setSpacing(8);

	/* F1: Das Dock-Theme ist aktuell fest OBS Yami Dark —
	 * kein umschaltbares Preset. Statische Info-Zeile statt eines funktionslosen
	 * Combos; getrennt von den Karten-Farbprofilen, die hier darunter stehen. */
	auto *lbl_dock_theme = new QLabel(QStringLiteral("Dock-Theme: OBS Yami Dark"), profiles_group);
	lbl_dock_theme->setObjectName(QStringLiteral("tbFieldLabel"));
	lbl_dock_theme->setToolTip(
		QStringLiteral("Färbt ausschließlich die Dock-Widgets im OBS-Panel (nicht die Kartenfarben)"));

	auto *lbl_custom_profiles = new QLabel(QStringLiteral("EIGENE FARBPROFILE"), profiles_group);
	lbl_custom_profiles->setObjectName(QStringLiteral("tbFieldLabel"));

	auto *profile_list = new QListWidget(profiles_group);
	profile_list->setObjectName(QStringLiteral("tbProfileList"));
	profile_list->setMinimumHeight(fontMetrics().height() * 6);
	profile_list->setMaximumHeight(fontMetrics().height() * 10);

	auto *del_prof_btn = new QPushButton(QStringLiteral("Löschen"), profiles_group);
	del_prof_btn->setObjectName(QStringLiteral("tbBtnDeleteProfile"));
	del_prof_btn->setIcon(theme_icon("trash.svg"));
	del_prof_btn->setIconSize(QSize(16, 16));
	del_prof_btn->setProperty("tbDanger", true);
	del_prof_btn->setEnabled(false);

	auto *lbl_yaml_profiles = new QLabel(QStringLiteral("YAML-EXPORT / -IMPORT"), profiles_group);
	lbl_yaml_profiles->setObjectName(QStringLiteral("tbFieldLabel"));

	auto *yaml_combo = new QComboBox(profiles_group);
	yaml_combo->setObjectName(QStringLiteral("tbYamlProfileCombo"));
	yaml_combo->setAccessibleName(QStringLiteral("YAML-Profil-Auswahl"));
	yaml_combo->setFixedHeight(fontMetrics().height() + 12);

	auto *yaml_btn_row = new QHBoxLayout();
	yaml_btn_row->setSpacing(8);
	auto *export_yaml_btn = new QPushButton(QStringLiteral("YAML exportieren …"), profiles_group);
	export_yaml_btn->setObjectName(QStringLiteral("tbBtnExportYaml"));
	auto *import_yaml_btn = new QPushButton(QStringLiteral("YAML importieren …"), profiles_group);
	import_yaml_btn->setObjectName(QStringLiteral("tbBtnImportYaml"));
	yaml_btn_row->addWidget(export_yaml_btn, 1);
	yaml_btn_row->addWidget(import_yaml_btn, 1);

	profiles_layout->addWidget(lbl_dock_theme);
	profiles_layout->addWidget(lbl_custom_profiles);
	profiles_layout->addWidget(profile_list);
	profiles_layout->addWidget(del_prof_btn);
	profiles_layout->addWidget(lbl_yaml_profiles);
	profiles_layout->addWidget(yaml_combo);
	profiles_layout->addLayout(yaml_btn_row);
	root->addWidget(profiles_group);

	auto *status_lbl = new QLabel(&dlg);
	status_lbl->setObjectName(QStringLiteral("tbSettingsStatus"));
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
		for (const auto &sp : textbox_profiles()) {
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
			// IMP-N: letztes Profil gelöscht -> Standardprofile nachsaaten,
			// sonst bleiben Karten ohne wählbares Profil (Combo leer).
			const bool reseeded = reconcile_profiles_after_delete(state_);
			save_state();
			rebuild_rows();
			refresh_profiles_ui();
			const QString msg = reseeded
				? QStringLiteral("Farbprofil \"%1\" gelöscht — Standardprofile nachgesät").arg(qname)
				: QStringLiteral("Farbprofil \"%1\" gelöscht").arg(qname);
			status_lbl->setText(msg);
			set_dock_status(msg, false);
		});

	connect(export_yaml_btn, &QPushButton::clicked, &dlg,
		[this, &dlg, yaml_combo, status_lbl]() {
			const std::string active_name =
				yaml_combo->currentText().toUtf8().toStdString();
			const TextboxProfile *prof =
				textbox_profile_lookup(active_name, state_.color_profiles);
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
				default_file, QStringLiteral("YAML-Dateien (*.yaml *.yml)"),
				nullptr, dialog_options());
			if (out_file.isEmpty())
				return;

			const std::string yaml_str = textbox_profile_to_yaml(*prof);
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
				QString(), QStringLiteral("YAML-Dateien (*.yaml *.yml)"),
				nullptr, dialog_options());
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

			TextboxProfile imported;
			std::string parse_err;
			if (!textbox_yaml_to_profile(raw.toStdString(), imported, parse_err)) {
				const QString qerr = QString::fromUtf8(parse_err.c_str());
				status_lbl->setText(qerr);
				set_dock_status(qerr, true);
				return;
			}

			if (textbox_is_default_profile(imported.name)) {
				imported.name += " (Import)";
			}

			auto it = std::find_if(
				state_.color_profiles.begin(), state_.color_profiles.end(),
				[&imported](const TextboxProfile &p) {
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
	reset_btn->setObjectName(QStringLiteral("tbBtn"));
	auto *save_btn = new QPushButton(QStringLiteral("Speichern"), &dlg);
	save_btn->setObjectName(QStringLiteral("tbBtnAccent"));
	btn_row->addWidget(reset_btn);
	btn_row->addStretch(1);
	btn_row->addWidget(save_btn);
	root->addLayout(btn_row);

	connect(browse_btn, &QPushButton::clicked, &dlg, [&dlg, edit_token]() {
		const QString chosen = QFileDialog::getOpenFileName(
			&dlg, QStringLiteral("Google-OAuth-Token-Datei wählen"),
			edit_token->text(), QStringLiteral("JSON-Dateien (*.json);;Alle Dateien (*.*)"),
			nullptr, dialog_options());
		if (!chosen.isEmpty())
			edit_token->setText(chosen);
	});

	connect(browse_key_btn, &QPushButton::clicked, &dlg, [&dlg, edit_key_path]() {
		const QString chosen = QFileDialog::getOpenFileName(
			&dlg, QStringLiteral("OpenRouter-Key-Datei (api.env) wählen"),
			edit_key_path->text(), QStringLiteral("Env-Dateien (*.env);;Alle Dateien (*.*)"),
			nullptr, dialog_options());
		if (!chosen.isEmpty())
			edit_key_path->setText(chosen);
	});

	connect(browse_py_btn, &QPushButton::clicked, &dlg, [&dlg, edit_py_path]() {
		const QString chosen = QFileDialog::getOpenFileName(
			&dlg, QStringLiteral("MarkItDown-Python-Executable wählen"),
			edit_py_path->text(),
			QStringLiteral("Ausführbare Dateien (*.exe);;Alle Dateien (*.*)"),
			nullptr, dialog_options());
		if (!chosen.isEmpty())
			edit_py_path->setText(chosen);
	});

	connect(browse_anim_btn, &QPushButton::clicked, &dlg, [&dlg, edit_anim_dir]() {
		const QString chosen = QFileDialog::getExistingDirectory(
			&dlg, QStringLiteral("Animations-Ordner wählen"),
			edit_anim_dir->text(), dialog_options());
		if (!chosen.isEmpty())
			edit_anim_dir->setText(chosen);
	});

	connect(browse_referenz_btn, &QPushButton::clicked, &dlg, [&dlg, edit_referenz_path]() {
		const QString chosen = QFileDialog::getOpenFileName(
			&dlg, QStringLiteral("Referenz-Index-Datei wählen"),
			edit_referenz_path->text(),
			QStringLiteral("Markdown-Dateien (*.md);;Alle Dateien (*.*)"));
		if (!chosen.isEmpty())
			edit_referenz_path->setText(chosen);
	});

	connect(reset_btn, &QPushButton::clicked, &dlg,
		[edit_form, edit_token, edit_key_path, edit_py_path, edit_anim_dir,
		 edit_referenz_path, def_settings]() {
			edit_form->setText(QString());
			edit_token->setText(QString::fromUtf8(textbox_import::default_token_path_str()));
			edit_key_path->setText(
				QString::fromUtf8(def_settings.openrouter_key_path.c_str()));
			edit_py_path->setText(
				QString::fromUtf8(def_settings.markitdown_python.c_str()));
			edit_anim_dir->setText(
				QString::fromUtf8(def_settings.animation_dir.c_str()));
			edit_referenz_path->setText(
				QString::fromUtf8(def_settings.referenz_path.c_str()));
		});

	connect(save_btn, &QPushButton::clicked, &dlg,
		[this, &dlg, edit_form, edit_token, edit_key_path, edit_py_path, edit_anim_dir,
		 edit_referenz_path, def_settings]() {
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
				edit_referenz_path->text().trimmed().toUtf8().toStdString();

			state_.settings.google_form_id = fid;
			state_.settings.token_path =
				tpath.empty() ? textbox_import::default_token_path_str() : tpath;
			state_.settings.openrouter_key_path =
				kpath.empty() ? def_settings.openrouter_key_path : kpath;
			state_.settings.markitdown_python =
				pypath.empty() ? def_settings.markitdown_python : pypath;
			state_.settings.animation_dir =
				adir.empty() ? def_settings.animation_dir : adir;
			state_.settings.referenz_path =
				lpath.empty() ? def_settings.referenz_path : lpath;

			save_state();
			set_dock_status(QStringLiteral("Einstellungen gespeichert."), false);
			dlg.accept();
		});

	dlg.exec();
	tb_event_log(tb_event_log_dir_from_state_path(state_path_),
		     "Einstellungen geschlossen");
}

void TextboxDock::save_state()
{
	// IMP-S: Abbruch vor allen Writes prüfen — sonst bleibt bei Abbruch mitten
	// im Loop ein partial geschriebener Zustand (HTML ohne state.json) stehen.
	if (cancel_ && *cancel_)
		return;
	auto it = g_dock_schedulers.find(this);
	if (it != g_dock_schedulers.end()) {
		if (it->second.timer && it->second.timer->isActive())
			it->second.timer->stop();
		std::vector<std::string> ids;
		ids.swap(it->second.dirty_cards);
		for (const std::string &id : ids) {
			if (const TextboxCard *c = find_card_mut(id)) {
				std::string html_path;
				if (textbox_html::write_card(*c, html_path, state_.settings)) {
					tb_log("HTML geschrieben: " + html_path);
					// IMP-S: Live-Text/Farbe ohne Reload in alle Szenen pushen.
					textbox_scene::push_all_scenes(*c, state_.settings);
				}
			}
		}
	}
	if (!state_path_.empty()) {
		state_.save(state_path_);
		tb_log("Zustand gespeichert (" + std::to_string(state_.cards.size()) + " Karten)");
	}
}
