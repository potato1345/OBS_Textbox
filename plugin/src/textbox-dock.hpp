/*
 * textbox-dock.hpp — Haupt-Bedienpanel (Qt QDockWidget) für Textbox in OBS Studio (v3).
 *
 * Zweck:
 *   - Definiert die TextboxDock-Klasse als Qt-Widget für das OBS-Frontend.
 *   - Ermöglicht das Bearbeiten, Senden, Leeren und Löschen von Textbox-Karten.
 *   - Integriert Import-Dialoge (Google Forms, Datei, AI), Profilverwaltung und Einstellungen.
 */

#pragma once

#include "textbox-ai.hpp"
#include "textbox-import.hpp"
#include "textbox-state.hpp"

#include <QIcon>
#include <QWidget>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <QListWidget>

// Globaler Zähler aktiver Hintergrund-Jobs zur Synchronisation beim Entladen des Plugins.
extern std::atomic<int> g_active_jobs;

namespace textbox_ai {
// Überladung mit kooperativem Abbruch-Flag für den markitdown-Subprozess.
std::string extract_document_text(const std::string &file_path, const std::string &python_exe, std::string &err,
				  std::shared_ptr<std::atomic<bool>> cancel);

// Überladung mit kooperativem Abbruch-Flag für OpenRouter-Anfragen und Retries.
AiResult process_with_llm(const std::string &api_key, const std::string &document_text,
			  const std::string &system_prompt, const std::string &referenz_path,
			  std::function<void(const std::string &)> on_progress,
			  std::shared_ptr<std::atomic<bool>> cancel);
} // namespace textbox_ai

class QComboBox;
class QLabel;
class QListWidgetItem;
class QMenu;
class QTimer;
class QToolButton;

/**
 * CardListWidget — sicherer Drag-Reorder ohne Qt InternalMove.
 *
 * Qt's InternalMove mit setItemWidget ist unsicher und verursacht
 * Zugriffsverletzungen. Diese Subklasse implementiert einen eigenen
 * Drag-Reorder via mousePressEvent / mouseMoveEvent / mouseReleaseEvent
 * und einem benutzerdefinierten MIME-Typ. Interaktive Kinder (Buttons,
 * Textfelder) akzeptieren ihre Events selbst, sodass der Drag nur auf
 * leeren Karten-/GroupBox-Flächen startet — das ist beabsichtigt.
 */
class CardListWidget : public QListWidget {
	Q_OBJECT
public:
	explicit CardListWidget(QWidget *parent = nullptr) : QListWidget(parent) {}
	std::function<void(const std::string &card_id, int target_row)> reorder_cb;
	bool eventFilter(QObject *obj, QEvent *event) override;

protected:
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;

private:
	int drag_row_ = -1;
	QPoint drag_press_pos;
};

/**
 * Triviale Zwischenklasse — Theme-QSS matcht per MetaObject-Klassename OBSDock
 * (wie OBS-eigene Docks), sodass der Yami-Theme-Rand (bg_base, border, radius)
 * auf das a??ere Dock-Widget gemalt wird.
 */
class OBSDock : public QWidget {
	Q_OBJECT
public:
	explicit OBSDock(QWidget *parent = nullptr) : QWidget(parent) {}
};

/**
 * Haupt-UI-Panel des Plugins, eingebettet als QDockWidget in OBS Studio.
 */
class TextboxDock : public OBSDock {
	Q_OBJECT

public:
	explicit TextboxDock(const std::string &state_path, QWidget *parent = nullptr);
	~TextboxDock() override;

	// Self-healing: Karten mit Inhalt garantiert eine Browser-Quelle in der
	// aktuellen Szene haben (läuft, sobald das Frontend geladen ist).
	void ensure_card_sources();

private:
	// Durchsucht state_.cards nach der ID; nullptr bei Nichtfund (mutable access).
	TextboxCard *find_card_mut(const std::string &id);
	// Baut die komplette Qt-Widget-Zeile für eine Karte (ID-Bar, Zeilen, CSS-Regeln, Profil, Aktionen)
	// und fügt sie als QListWidgetItem in tbCardList_ ein. Gibt das Item zurück.
	QListWidgetItem *create_row_item(const TextboxCard &card);
	// Entfernt alle Zeilen-Widgets und baut sie aus state_.cards neu auf.
	void rebuild_rows();
	// Aktualisiert die beiden Top-Combos (Schnellprofile + Kartensuche).
	void refresh_top_row();
	// Aktiviert/deaktiviert den Löschen-Button des Schnellprofil-Combos.
	void sync_profil_del_state();
	// Wendet ein Schnellprofil an (setzt Karten + Zustand + live-Sync).
	void apply_quick_profile(const std::string &name);
	// Hebt eine Kartenzeile kurz farblich hervor (scrollt zu ihr).
	void flash_row(const std::string &card_id);
	// Karte hinzufügen, Import-/Einstellungs-/Profil-Dialoge öffnen.
	void on_add_card();

	void on_open_import_dialog();
	void on_open_quick_profiles_dialog();
	void on_backup_state();
	void on_restore_state();
	void on_open_settings_dialog();
	void on_open_templates_folder();
	void on_copy_template_from_classic();
	// Spiegelt die template_id aller Karten in die Edit-Felder (nach externer Änderung).
	void refresh_templates_ui();
	// Setzt template_id auf einer Karte, löst write_card + Browser-Reload aus.
	void apply_template_id(const std::string &card_id, const std::string &template_id);
	// Überschreibt Karten-Inhalte mit einer importierten Einreichung (Google/CSV/AI).
	void apply_import_result(const textbox_import::TextboxImportResult &res);
	// Synchronisiert live: alte IDs entfernen, neue/aktualisierte pushen (nach Schnellprofil/Restore).
	void sync_all_cards_live(const std::vector<std::string> &previous_ids);
	// Setzt die kurze Statustext-Zeile unterhalb des Scrollbereichs (Auto-Dismiss per Timer).
	void set_dock_status(const QString &msg, bool is_error = false);
	// Serialisiert state_ als JSON nach state_path_ + schreibt dirty Card-HTMLs.
	void save_state();

	/* Ui4: OBS-Theme-Icon laden (leeres QIcon, falls kein Icon-Dir vorhanden). */
	QIcon theme_icon(const char *name) const;

	std::string state_path_;
	QString icon_dir_;
	TextboxState state_;
	QComboBox *profil_select_ = nullptr;
	QToolButton *profil_del_btn_ = nullptr;
	QComboBox *card_search_ = nullptr;
	CardListWidget *tbCardList_ = nullptr;
	QLabel *dock_status_lbl_ = nullptr;
	QToolButton *dock_status_close_ = nullptr; // c1: Fehlermeldung per Hand schließen
	QTimer *dock_status_timer_ = nullptr;      // c1: Auto-dismiss der Statusmeldung
	QToolButton *tool_up_ = nullptr;
	QToolButton *tool_down_ = nullptr;
	// Kooperatives Abbruch-Flag für laufende Hintergrund-Threads beim Schließen des Docks.
	std::shared_ptr<std::atomic<bool>> cancel_ = std::make_shared<std::atomic<bool>>(false);
};
