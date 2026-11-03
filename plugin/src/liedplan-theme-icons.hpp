/*
 * liedplan-theme-icons.hpp — Eingebettete OBS-Theme-Icons für das Dock-Interface (v3).
 *
 * Zweck:
 *   - Liefert SVG-Icons des OBS Studio Yami/Dark Themes direkt aus der DLL aus.
 *   - Schreibt die Icons in den Konfigurationsordner (<config>/icons/), falls noch nicht vorhanden
 *     oder veraltet (Version-Gating über icons-version.txt).
 *
 * Hinweis: Header-only, libobs-frei; wird in Standalone-Tests (test-theme-icons) verwendet.
 */

#pragma once
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace liedplan_theme_icons {

static const int kVersion = 2;

inline const std::vector<std::pair<std::string, std::string>> &icons();

/*
Ui4: OBS-Theme-Icons aus der Plugin-DLL ausliefern.
Dir = <config>/icons/ (neben liedplan-state.json). Schreiblogik:
- icons-version.txt == kVersion UND alle Dateien vorhanden (>=1 Byte)
  -> No-Op (OBS-Neustarts bleiben billig).
- Andernfalls (inkl. force=true): alles neu schreiben.
Dateinamen sind ASCII; std::filesystem::path-Operatoren mit narrow
Strings sind hier daher sicher (CP1252-Locale-Pitfall betrifft nur
Non-ASCII).
*/
inline bool write_icons(const std::filesystem::path &dir, bool force = false, std::string *err = nullptr)
{
	auto fail = [&](const std::string &msg) {
		if (err)
			*err = msg;
		return false;
	};
	const std::string version_name = "icons-version.txt";
	std::error_code ec;
	if (!force) {
		const std::filesystem::path vpath = dir / version_name;
		if (std::filesystem::exists(vpath, ec) && !ec) {
			std::ifstream in(vpath);
			std::string v;
			if (std::getline(in, v) && v == std::to_string(kVersion)) {
				bool all_present = true;
				for (const auto &item : icons()) {
					std::error_code fec;
					const auto sz = std::filesystem::file_size(dir / item.first, fec);
					if (fec || sz == 0) {
						all_present = false;
						break;
					}
				}
				if (all_present)
					return true; // aktueller Stand, nichts zu tun
			}
		}
	}
	if (!std::filesystem::create_directories(dir, ec) && !std::filesystem::is_directory(dir, ec))
		return fail("create_directories fehlgeschlagen: " + dir.string());
	for (const auto &item : icons()) {
		std::ofstream out(dir / item.first, std::ios::binary | std::ios::trunc);
		if (!out)
			return fail("nicht beschreibbar: " + item.first);
		out.write(item.second.data(), static_cast<std::streamsize>(item.second.size()));
		if (!out)
			return fail("Write fehlgeschlagen: " + item.first);
	}
	std::ofstream vout(dir / version_name, std::ios::binary | std::ios::trunc);
	if (!vout)
		return fail("nicht beschreibbar: " + version_name);
	vout << kVersion;
	if (!vout)
		return fail("Write fehlgeschlagen: " + version_name);
	return true;
}

inline const std::vector<std::pair<std::string, std::string>> &icons()
{
	static const std::vector<std::pair<std::string, std::string>> items = {
		{"plus.svg", R"SVGICON(<?xml version="1.0" encoding="UTF-8"?>
<svg height="16px" viewBox="0 0 16 16" width="16px" xmlns="http://www.w3.org/2000/svg">
    <path d="m 7 1 v 6 h -6 v 2 h 6 v 6 h 2 v -6 h 6 v -2 h -6 v -6 z m 0 0" fill="#fefefe"/>
</svg> )SVGICON"},
		{"trash.svg", R"SVGICON(<?xml version="1.0" encoding="UTF-8"?>
<svg height="16px" viewBox="0 0 16 16" width="16px" xmlns="http://www.w3.org/2000/svg">
    <path d="m 5.984375 0.00390625 l -1 0.99609375 l -3 0.003906 s -1 0 -1 1 l 0.015625 0.996094 h 15 l -0.015625 -0.996094 c 0 -1 -1 -1 -1 -1 l -3 -0.003906 l -1 -0.99609375 z m -3.984375 3.99609375 v 9 c 0 3 3 3 3 3 h 7 c 3 0 3 -3 3 -3 v -9 h -2 v 9 c 0 1 -1 1 -1 1 h -7 s -1 0 -1 -1 v -9 z m 4 1 v 7 h 1 v -7 z m 2 0 v 7 h 1 v -7 z m 2 0 v 7 h 1 v -7 z m 0 0" fill="#fefefe"/>
</svg> )SVGICON"},
		{"download.svg", R"SVGICON(<?xml version="1.0" encoding="UTF-8"?>
<svg height="16px" viewBox="0 0 16 16" width="16px" xmlns="http://www.w3.org/2000/svg">
    <path d="M7 0 h 2 v 8 h 3 l -4 4 l -4 -4 h 3 z M 0 13 h 16 v 2 h -16 z M 0 0" fill="#fefefe"/>
</svg> )SVGICON"},
		{"refresh.svg", R"SVGICON(<?xml version="1.0" encoding="UTF-8"?>
<svg height="16px" viewBox="0 0 16 16" width="16px" xmlns="http://www.w3.org/2000/svg">
    <path d="m 7.40625 1 c -0.613281 0.007812 -1.234375 0.089844 -1.847656 0.253906 c -3.273438 0.878906 -5.558594 3.855469 -5.558594 7.246094 s 2.285156 6.367188 5.558594 7.242188 c 3.273437 0.878906 6.742187 -0.558594 8.4375 -3.492188 c 0.277344 -0.480469 0.109375 -1.089844 -0.367188 -1.367188 c -0.476562 -0.273437 -1.089844 -0.109374 -1.367187 0.367188 c -1.246094 2.160156 -3.777344 3.207031 -6.1875 2.5625 c -2.40625 -0.644531 -4.074219 -2.820312 -4.074219 -5.3125 c 0 -2.496094 1.667969 -4.667969 4.074219 -5.3125 c 2.410156 -0.644531 4.941406 0.402344 6.1875 2.5625 c 0.058593 0.085938 0.125 0.164062 0.203125 0.226562 l -0.019532 0.015626 l -0.007812 0.007812 h -1.4375 c -0.550781 0 -1 0.449219 -1 1 c 0 0 0 1 1 1 h 5 v -5 s 0.003906 -1 -1 -1 c -0.550781 0 -1 0.449219 -1 1 v 1.6875 l -0.015625 0.011719 l -0.011719 0.011719 c -1.277344 -2.179688 -3.53125 -3.519532 -5.953125 -3.691407 c -0.203125 -0.015625 -0.40625 -0.019531 -0.613281 -0.019531 z m 0 0" fill="#fefefe"/>
</svg> )SVGICON"},
		{"save.svg", R"SVGICON(<?xml version="1.0" encoding="UTF-8"?>
<svg height="16px" viewBox="0 0 16 16" width="16px" xmlns="http://www.w3.org/2000/svg">
    <path d="m 8 0 c -0.550781 0 -1 0.449219 -1 1 v 8.585938 l -1.292969 -1.292969 c -0.1875 -0.1875 -0.441406 -0.292969 -0.707031 -0.292969 s -0.519531 0.105469 -0.707031 0.292969 c -0.390625 0.390625 -0.390625 1.023437 0 1.414062 l 3 3 c 0.390625 0.390625 1.023437 0.390625 1.414062 0 l 3 -3 c 0.390625 -0.390625 0.390625 -1.023437 0 -1.414062 s -1.023437 -0.390625 -1.414062 0 l -1.292969 1.292969 v -8.585938 c 0 -0.550781 -0.449219 -1 -1 -1 z m -7 14 v 2 h 14 v -2 z m 0 0" fill="#fefefe"/>
</svg> )SVGICON"},
		{"revert.svg", R"SVGICON(<?xml version="1.0" encoding="UTF-8"?>
<svg height="16px" viewBox="0 0 16 16" width="16px" xmlns="http://www.w3.org/2000/svg">
    <path d="m 5 2 c -0.265625 0 -0.519531 0.105469 -0.707031 0.292969 l -4 4 c -0.3906252 0.390625 -0.3906252 1.023437 0 1.414062 l 4 4 c 0.390625 0.390625 1.023437 0.390625 1.414062 0 s 0.390625 -1.023437 0 -1.414062 l -2.292969 -2.292969 h 8.585938 c 1.117188 0 2 0.882812 2 2 s -0.882812 2 -2 2 c -0.550781 0 -1 0.449219 -1 1 s 0.449219 1 1 1 c 2.199219 0 4 -1.800781 4 -4 s -1.800781 -4 -4 -4 h -8.585938 l 2.292969 -2.292969 c 0.390625 -0.390625 0.390625 -1.023437 0 -1.414062 c -0.1875 -0.1875 -0.441406 -0.292969 -0.707031 -0.292969 z m 0 0" fill="#fefefe"/>
</svg> )SVGICON"},
		{"cogs.svg", R"SVGICON(<svg fill="#fefefe" enable-background="new 924 796 200 200" height="16" viewBox="924 796 200 200" width="16" xmlns="http://www.w3.org/2000/svg"><path d="m1049.078 903.431h-3.447c-3.104 0-5.875-1.963-6.904-4.891-.626-1.793-1.354-3.536-2.176-5.227-1.361-2.806-.799-6.167 1.404-8.369l2.381-2.382c4.029-4.028 4.029-10.556.002-14.583l-1.717-1.717c-4.025-4.024-10.557-4.028-14.58 0l-2.436 2.433c-2.193 2.196-5.538 2.769-8.336 1.425-1.696-.811-3.442-1.532-5.236-2.155-2.948-1.017-4.928-3.795-4.928-6.91v-3.37c0-5.693-4.618-10.31-10.309-10.31h-2.43c-5.695 0-10.312 4.616-10.312 10.31v3.444c0 3.107-1.962 5.877-4.892 6.906-1.792.627-3.534 1.354-5.224 2.176-2.803 1.361-6.166.796-8.371-1.406l-2.377-2.382c-4.03-4.028-10.558-4.028-14.584 0l-1.719 1.717c-4.026 4.028-4.028 10.555 0 14.583l2.434 2.432c2.193 2.197 2.765 5.54 1.421 8.341-.812 1.691-1.532 3.44-2.15 5.234-1.021 2.945-3.798 4.926-6.915 4.926h-3.367c-5.695 0-10.312 4.617-10.312 10.313v2.429c0 5.693 4.617 10.31 10.312 10.31h3.441c3.106 0 5.876 1.963 6.903 4.893.63 1.791 1.358 3.537 2.18 5.227 1.361 2.804.795 6.164-1.408 8.367l-2.379 2.383c-4.029 4.027-4.027 10.555 0 14.582l1.718 1.718c4.025 4.023 10.553 4.026 14.58-.003l2.431-2.432c2.195-2.194 5.54-2.768 8.341-1.424 1.694.813 3.441 1.533 5.236 2.155 2.946 1.018 4.927 3.795 4.927 6.913v3.364c-.004 5.699 4.614 10.313 10.311 10.313h2.427c5.696 0 10.314-4.614 10.311-10.309v-3.445c0-3.104 1.962-5.875 4.892-6.905 1.792-.628 3.537-1.354 5.229-2.175 2.801-1.362 6.165-.798 8.368 1.404l2.379 2.38c4.027 4.029 10.555 4.025 14.583.002l1.717-1.718c4.027-4.026 4.03-10.557 0-14.581l-2.432-2.433c-2.197-2.193-2.768-5.54-1.426-8.337.814-1.696 1.533-3.445 2.154-5.24 1.021-2.947 3.795-4.926 6.914-4.926h3.367c5.695.002 10.31-4.616 10.31-10.312v-2.429c.001-5.691-4.613-10.313-10.306-10.309zm-57.384 36.716c-13.852 0-25.081-11.227-25.081-25.078 0-13.853 11.229-25.08 25.081-25.08 13.85 0 25.079 11.228 25.079 25.08-.001 13.852-11.229 25.078-25.079 25.078z"/><path d="m1117.307 845.487h-1.727c-2.557 0-4.847-1.583-5.752-3.974-.229-.609-.479-1.212-.746-1.804-1.053-2.329-.554-5.07 1.256-6.876l1.219-1.221c2.613-2.611 2.613-6.853 0-9.466l-.473-.473c-2.613-2.612-6.852-2.612-9.465 0l-1.219 1.221c-1.809 1.809-4.547 2.308-6.877 1.258-.593-.268-1.192-.516-1.805-.747-2.389-.903-3.975-3.196-3.975-5.748v-1.729c0-3.697-2.996-6.692-6.689-6.692h-.668c-3.698 0-6.696 2.995-6.696 6.692v1.724c0 2.557-1.581 4.85-3.972 5.753-.609.231-1.215.479-1.805.747-2.328 1.05-5.069.551-6.876-1.256l-1.22-1.221c-2.611-2.614-6.854-2.613-9.467.001l-.472.472c-2.613 2.613-2.613 6.853 0 9.465l1.219 1.22c1.806 1.806 2.31 4.547 1.257 6.876-.268.592-.517 1.194-.748 1.804-.903 2.391-3.193 3.977-5.748 3.977h-1.727c-3.695-.002-6.691 2.997-6.691 6.69v.669c0 3.696 2.996 6.693 6.691 6.693h1.722c2.557-.001 4.85 1.582 5.753 3.973.231.611.48 1.215.747 1.809 1.052 2.326.552 5.065-1.255 6.871l-1.219 1.224c-2.613 2.609-2.613 6.851 0 9.463l.475.473c2.611 2.614 6.852 2.614 9.463 0l1.217-1.219c1.807-1.806 4.549-2.308 6.877-1.255.592.269 1.197.517 1.809.748 2.389.901 3.974 3.193 3.974 5.747v1.724c-.004 3.694 2.995 6.692 6.692 6.692h.669c3.693 0 6.692-2.994 6.692-6.692v-1.721c0-2.556 1.582-4.849 3.971-5.752.612-.23 1.216-.479 1.809-.746 2.326-1.053 5.068-.551 6.873 1.251l1.223 1.222c2.609 2.615 6.85 2.615 9.465 0l.473-.475c2.611-2.611 2.611-6.851 0-9.464l-1.221-1.22c-1.805-1.806-2.307-4.547-1.256-6.875.268-.59.518-1.194.749-1.805.901-2.391 3.191-3.976 5.747-3.976h1.725c3.694.004 6.691-2.995 6.695-6.69v-.669c-.004-3.697-3-6.693-6.693-6.693zm-36.59 21.753c-8.131 0-14.723-6.592-14.723-14.724s6.592-14.724 14.723-14.724c8.133 0 14.725 6.592 14.725 14.724s-6.592 14.724-14.725 14.724z"/></svg> )SVGICON"},
		{"dots.svg", R"SVGICON(<?xml version="1.0" encoding="UTF-8"?>
<svg height="16px" viewBox="0 0 16 16" width="16px" xmlns="http://www.w3.org/2000/svg">
    <path d="m 2 6.007812 c -1.105469 0 -2 0.894532 -2 2 c 0 1.101563 0.894531 2 2 2 s 2 -0.898437 2 -2 c 0 -1.105468 -0.894531 -2 -2 -2 z m 6 0 c -1.105469 0 -2 0.894532 -2 2 c 0 1.101563 0.894531 2 2 2 s 2 -0.898437 2 -2 c 0 -1.105468 -0.894531 -2 -2 -2 z m 6 0 c -1.105469 0 -2 0.894532 -2 2 c 0 1.101563 0.894531 2 2 2 s 2 -0.898437 2 -2 c 0 -1.105468 -0.894531 -2 -2 -2 z m 0 0" fill="#fefefe"/>
</svg> )SVGICON"},
		{"close.svg", R"SVGICON(<svg version="1.2" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16"><style>.a{fill:#fefefe}</style><path class="a" d="m1 1h1.8c0.5 0 0.9 0.2 1.3 0.6l4 4.1 4.1-4.1c0.5-0.5 0.8-0.6 1.2-0.6h1.8v1.8c0 0.5 0 1-0.4 1.4l-4.1 4.2 4 4.1c0.4 0.3 0.5 0.8 0.5 1.3v1.8h-1.8c-0.4 0-0.9-0.1-1.2-0.5l-4.1-4.2-4 4.2c-0.4 0.4-0.8 0.5-1.3 0.5h-1.8v-1.8c0-0.5 0.2-1 0.5-1.3l4.1-4.1-4.1-4.2c-0.4-0.4-0.5-0.9-0.5-1.4z"/></svg> )SVGICON"},
		{"popout.svg", R"SVGICON(<svg class="feather feather-plus" fill="none" stroke="#FFF" stroke-linecap="round" stroke-linejoin="round" stroke-width="2" version="1.1" viewBox="0 0 40 40" xmlns="http://www.w3.org/2000/svg"><rect x="2" y="2" width="24" height="20" /><rect x="2" y="2" width="24" height="4" style="fill:#fff" /><rect x="14" y="18" width="24" height="20" /><rect x="14" y="18" width="24" height="4" style="fill:#fff" /></svg> )SVGICON"},
		{"collapse.svg", R"SVGICON(<?xml version="1.0" encoding="UTF-8"?>
<svg height="16px" viewBox="0 0 16 16" width="16px" xmlns="http://www.w3.org/2000/svg">
    <path d="m 13.996094 5 l -5.996094 6 l -6.003906 -6 z m 0 0" fill="#fefefe"/>
</svg> )SVGICON"},
		{"checkbox_unchecked.svg", R"SVGICON(<svg version="1.1" x="0" y="0" width="16" height="16" viewBox="0 0 128 128">
<g>
	<path fill="none" stroke="#B0AFB3" stroke-width="8" stroke-miterlimit="10" d="M115.579,104.737
		c0,6.221-5.012,11.263-11.192,11.263H22.771c-6.182,0-11.192-5.042-11.192-11.263V23.263c0-6.221,5.01-11.263,11.192-11.263h81.615
		c6.181,0,11.192,5.042,11.192,11.263V104.737z"/>
	<path display="none" fill="#B0AFB3" d="M90,77.91C90,84.587,84.587,90,77.91,90H50.09C43.413,90,38,84.587,38,77.91V50.09
		C38,43.413,43.413,38,50.09,38h27.82C84.587,38,90,43.413,90,50.09V77.91z"/>
</g>
</svg> )SVGICON"},
		{"checkbox_unchecked_focus.svg", R"SVGICON(<svg version="1.1" x="0" y="0" width="16" height="16" viewBox="0 0 128 128">
<g>
	<path fill="none" stroke="#FFFFFF" stroke-width="12" stroke-miterlimit="10" d="
		M115.579,104.737c0,6.221-5.012,11.263-11.192,11.263H22.771c-6.182,0-11.192-5.042-11.192-11.263V23.263
		c0-6.221,5.01-11.263,11.192-11.263h81.615c6.181,0,11.192,5.042,11.192,11.263V104.737z"/>
</g>
</svg> )SVGICON"},
		{"checkbox_unchecked_disabled.svg", R"SVGICON(<svg version="1.1" x="0" y="0" width="16" height="16" viewBox="0 0 128 128">
<g>
	<path fill="none" stroke="#4A4C53" stroke-width="8" stroke-miterlimit="10" d="M115.579,104.737
		c0,6.221-5.012,11.263-11.192,11.263H22.771c-6.182,0-11.192-5.042-11.192-11.263V23.263c0-6.221,5.01-11.263,11.192-11.263h81.615
		c6.181,0,11.192,5.042,11.192,11.263V104.737z"/>
</g>
</svg> )SVGICON"},
		{"checkbox_checked.svg", R"SVGICON(<svg version="1.1" x="0" y="0px" width="16" height="16" viewBox="0 0 128 128">
<g>
	<path fill="none" stroke="#B0AFB3" stroke-width="8" stroke-miterlimit="10" d="M115.579,104.736
		c0,6.222-5.013,11.264-11.192,11.264H22.771c-6.182,0-11.192-5.042-11.192-11.264V23.263c0-6.221,5.01-11.263,11.192-11.263h81.616
		c6.18,0,11.191,5.042,11.191,11.263L115.579,104.736L115.579,104.736z"/>
</g>
<g>
	<line fill="none" stroke="#B0AFB3" stroke-width="12" stroke-miterlimit="10" x1="29.213" y1="66.025" x2="54.362" y2="91.168"/>
	<line fill="none" stroke="#B0AFB3" stroke-width="12" stroke-miterlimit="10" x1="98.213" y1="38.12" x2="45.165" y2="91.168"/>
</g>
</svg> )SVGICON"},
		{"checkbox_checked_focus.svg", R"SVGICON(<svg version="1.1" x="0" y="0" width="16" height="16" viewBox="0 0 128 128">
<g>
	<path fill="none" stroke="#FFFFFF" stroke-width="12" stroke-miterlimit="10" d="
		M115.579,104.737c0,6.221-5.012,11.263-11.192,11.263H22.771c-6.182,0-11.192-5.042-11.192-11.263V23.263
		c0-6.221,5.01-11.263,11.192-11.263h81.615c6.181,0,11.192,5.042,11.192,11.263V104.737z"/>
</g>
<g>
	<line fill="none" stroke="#FFFFFF" stroke-width="20" stroke-miterlimit="10" x1="29.213" y1="66.025" x2="54.362" y2="91.168"/>
	<line fill="none" stroke="#FFFFFF" stroke-width="20" stroke-miterlimit="10" x1="98.213" y1="38.12" x2="45.165" y2="91.168"/>
</g>
</svg> )SVGICON"},
		{"checkbox_checked_disabled.svg", R"SVGICON(<svg version="1.1" x="0" y="0" width="16" height="16" viewBox="0 0 128 128">
<g>
	<path id="Unchecked_1_" fill="none" stroke="#4A4C53" stroke-width="8" stroke-miterlimit="10" d="M115.579,104.737
		c0,6.221-5.012,11.263-11.192,11.263H22.771c-6.182,0-11.192-5.042-11.192-11.263V23.263c0-6.221,5.01-11.263,11.192-11.263h81.615
		c6.181,0,11.192,5.042,11.192,11.263V104.737z"/>
</g>
<g>
	<line fill="none" stroke="#4A4C53" stroke-width="12" stroke-miterlimit="10" x1="29.213" y1="66.025" x2="54.362" y2="91.168"/>
	<line fill="none" stroke="#4A4C53" stroke-width="12" stroke-miterlimit="10" x1="98.213" y1="38.12" x2="45.165" y2="91.168"/>
</g>
</svg> )SVGICON"},
		{"chevron_right.svg", R"SVGICON(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16"><path fill="#fefefe" d="M5.5 2.75L4.25 4 8 8l-3.75 4 1.25 1.25L10.5 8Z"/></svg> )SVGICON"},
		{"chevron_down.svg", R"SVGICON(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16"><path fill="#fefefe" d="M2.75 5.5L4 4.25 8 8l4-3.75L13.25 5.5 8 10.75Z"/></svg> )SVGICON"},
	};
	return items;
}
}  // namespace liedplan_theme_icons
