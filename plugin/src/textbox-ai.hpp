/*
 * textbox-ai.hpp — LLM-gestützte und deterministische Dokument-Extraktion (v3).
 *
 * Zweck:
 *   - Definiert AiPoint und AiResult zur Repräsentation strukturierter Kartendaten.
 *   - Deklariert Methoden für Dokument-Textextraktion (via markitdown Python-Subprozess).
 *   - Deklariert OpenRouter-API-Kommunikation und kostenlose Modell-Erkennung (:free).
 *   - Deklariert deterministischen Offline-Fallback-Parser und Referenz-Titel-Anreicherung.
 *
 * Hinweis: Qt- und libobs-frei; wird auch in Standalone-Tests (test-textbox-ai) verwendet.
 */

#pragma once

#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

namespace textbox_ai {

/** Ein einzelner erkannter Eintrag mit bis zu 3 Textzeilen. */
struct AiPoint {
	std::string zeile1;
	std::string zeile2;
	std::string zeile3;

	bool operator==(const AiPoint &other) const
	{
		return zeile1 == other.zeile1 && zeile2 == other.zeile2 && zeile3 == other.zeile3;
	}
};

struct AiResult {
	std::vector<AiPoint> items;
	std::string model_label; // z. B. "google/gemma-…", "Offline-Fallback"
};

constexpr const char *kDefaultSystemPrompt =
	"Du bist ein Parser für strukturierte Dokumente. Du erhältst Dokumenttext und erzeugst daraus "
	"JSON für mehrere Textbox-Karten in Dokumentreihenfolge. Regeln: ignoriere Kopfzeilen/Meta "
	"(Titel, Datum, Personen). Erzeuge pro Karte einen Eintrag mit bis zu drei Zeilen: "
	"zeile1 = Bezeichnung/Label des Eintrags, zeile2 = Nummer oder Kürzel, sonst leer, "
	"zeile3 = Titel/Beschreibung, sonst leer. Zerlege Bezeichnung/Nummer/Titel auch, wenn sie "
	"auf einer Zeile stehen. Gib AUSSCHLIESSLICH gültiges JSON zurück, ohne Markdown: "
	"{\"items\":[{\"zeile1\":\"...\",\"zeile2\":\"...\",\"zeile3\":\"...\"}]}";

constexpr const char *kModelPriorities[] = {
	"gemma-4-31b",           "gemma-4-26b", "nemotron-3-super-120b",
	"llama-3.3-70b",         "llama-4",     "deepseek",
	"qwen-2.5",              "qwen3",       "mistral",
	"gemini",                "phi-4",
};

// Liest den OPENROUTER_API_KEY aus einer .env-Datei (key=value, UTF-8, BOM-tolerant).
// err enthält die Fehlermeldung, falls der Key nicht gelesen werden konnte.
std::string read_env_key(const std::string &env_file_path, std::string &err);

// Extrahiert Text aus einer PDF-/DOCX-Datei via markitdown Python-Subprozess (Win32 CreateProcess).
// file_path = Pfad zum Dokument; python_exe = Python-Interpreter (mit installiertem markitdown).
// Rückgabe: extrahierter Text als UTF-8; err bei Fehlern.
std::string extract_document_text(const std::string &file_path, const std::string &python_exe,
				  std::string &err);

// Parst eine JSON-Zeichenkette aus einer LLM-Antwort: entfernt Markdown-Code-Blöcke,
// sucht das erste {…}-Objekt. Wirft std::runtime_error bei ungültigem JSON.
nlohmann::json parse_json_clean(const std::string &raw);

// Durchsucht die OpenRouter-Modell-Liste nach :free-Modellen, bewertet sie nach
// kModelPriorities + Kontextlänge, und gibt sortierte IDs zurück (beste zuerst).
std::vector<std::string> discover_free_models(const nlohmann::json &models_json);

AiResult process_with_llm(const std::string &api_key, const std::string &document_text,
			  const std::string &system_prompt, const std::string &referenz_path,
			  std::function<void(const std::string &)> on_progress = nullptr);

// Parst eine optionale Markdown-Referenztabelle (Nummer | Titel) aus referenz.md.
// Ergebnis: Map<Nummer, (Titel, Rubrik)>. Leere Map bei fehlender/ungültiger Datei.
std::map<std::string, std::pair<std::string, std::string>>
parse_reference_table(const std::string &md);

// Erweitert AiPoint-Items um Titel aus der optionalen Referenztabelle: falls zeile3 leer
// ist und zeile2 eine Nummer enthält, wird der passende Titel aus ref eingesetzt.
void enrich_titles_from_reference(
	std::vector<AiPoint> &items,
	const std::map<std::string, std::pair<std::string, std::string>> &ref);

// Deterministischer Offline-Parser: durchläuft Text zeilenweise, erkennt
// Nummern und Block-Schlüsselwörter, extrahiert zeile1/zeile2/zeile3.
// Ohne API-Key oder als Fallback nach fehlgeschlagenem LLM-Aufruf.
std::vector<AiPoint> parse_textbox_locally(const std::string &text);

} // namespace textbox_ai
