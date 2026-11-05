/*
 * liedplan-dock.hpp — Haupt-Bedienpanel (Qt QDockWidget) für Liedplan in OBS Studio (v3).
 *
 * Zweck:
 *   - Definiert die LiedplanDock-Klasse als Qt-Widget für das OBS-Frontend.
 *   - Ermöglicht das Bearbeiten, Senden, Leeren und Löschen von Liedplan-Karten.
 *   - Integriert Import-Dialoge (Google Forms, Datei, AI), Profilverwaltung und Einstellungen.
 */

#pragma once

#include "liedplan-ai.hpp"
#include "liedplan-import.hpp"
#include "liedplan-state.hpp"

#include <QIcon>
#include <QWidget>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

// Globaler Zähler aktiver Hintergrund-Jobs zur Synchronisation beim Entladen des Plugins.
extern std::atomic<int> g_active_jobs;

namespace liedplan_ai {
// Überladung mit kooperativem Abbruch-Flag für den markitdown-Subprozess.
std::string extract_document_text(const std::string &file_path, const std::string &python_exe,
				  std::string &err,
				  std::shared_ptr<std::atomic<bool>> cancel);

// Überladung mit kooperativem Abbruch-Flag für OpenRouter-Anfragen und Retries.
AiResult process_with_llm(const std::string &api_key, const std::string &document_text,
			  const std::string &system_prompt, const std::string &lieder_path,
			  std::function<void(const std::string &)> on_progress,
			  std::shared_ptr<std::atomic<bool>> cancel);
} // namespace liedplan_ai

class QComboBox;
class QLabel;
class QMenu;
class QScrollArea;
class QToolButton;
class QVBoxLayout;

/**
 * Haupt-UI-Panel des Plugins, eingebettet als QDockWidget in OBS Studio.
 */
class LiedplanDock : public QWidget {
	Q_OBJECT

public:
	explicit LiedplanDock(const std::string &state_path, QWidget *parent = nullptr);
	~LiedplanDock() override;

	// Self-healing: Karten mit Inhalt garantiert eine Browser-Quelle in der
	// aktuellen Szene haben (läuft, sobald das Frontend geladen ist).
	void ensure_card_sources();

private:
	LiedplanCard *find_card_mut(const std::string &id);
	void create_row_widget(const LiedplanCard &card);
	void rebuild_rows();
	void refresh_top_row();
	void sync_profil_del_state();
	void apply_quick_profile(const std::string &name);
	void flash_row(const std::string &card_id);
	void on_add_card();
	void on_clear_all();
	void on_open_import_dialog();
	void on_open_quick_profiles_dialog();
	void on_backup_state();
	void on_restore_state();
	void on_open_settings_dialog();
	void on_open_templates_folder();
	void on_copy_template_from_classic();
	void refresh_templates_ui();
	void apply_template_id(const std::string &card_id, const std::string &template_id);
	void apply_import_result(const liedplan_import::LiedplanImportResult &res);
	void sync_all_cards_live(const std::vector<std::string> &previous_ids);
	void set_dock_status(const QString &msg, bool is_error = false);
	void save_state();

	/* Ui4: OBS-Theme-Icon laden (leeres QIcon, falls kein Icon-Dir vorhanden). */
	QIcon theme_icon(const char *name) const;

	std::string state_path_;
	QString icon_dir_;
	LiedplanState state_;
	QComboBox *profil_select_ = nullptr;
	QToolButton *profil_del_btn_ = nullptr;
	QComboBox *card_search_ = nullptr;
	QScrollArea *scroll_area_ = nullptr;
	QVBoxLayout *cards_layout_ = nullptr;
	QLabel *dock_status_lbl_ = nullptr;
	std::vector<QWidget *> row_widgets_;
	// Kooperatives Abbruch-Flag für laufende Hintergrund-Threads beim Schließen des Docks.
	std::shared_ptr<std::atomic<bool>> cancel_ = std::make_shared<std::atomic<bool>>(false);
};
