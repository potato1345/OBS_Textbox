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

#pragma once

#include "liedplan-import.hpp"
#include "liedplan-state.hpp"

#include <QWidget>
#include <string>
#include <vector>

class QVBoxLayout;

class LiedplanDock : public QWidget {
	Q_OBJECT

public:
	explicit LiedplanDock(const std::string &state_path, QWidget *parent = nullptr);
	~LiedplanDock() override = default;

private:
	LiedplanCard *find_card_mut(const std::string &id);
	void create_row_widget(const LiedplanCard &card);
	void rebuild_rows();
	void on_add_card();
	void on_clear_all();
	void on_open_import_dialog();
	void apply_import_result(const liedplan_import::LiedplanImportResult &res);
	void save_state();

	std::string state_path_;
	LiedplanState state_;
	QVBoxLayout *cards_layout_ = nullptr;
	std::vector<QWidget *> row_widgets_;
};
