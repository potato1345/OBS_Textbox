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

#include "liedplan-render.hpp"
#include "liedplan-textfmt.hpp"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QRectF>
#include <QString>

#include <algorithm>
#include <vector>

namespace {

constexpr qreal kCornerRadiusPx = 24.0;
constexpr int kLine12FontPx = 44;
constexpr int kLine3FontPx = 60;
constexpr qreal kLineSpacingFactor = 1.25;

QColor make_qcolor(const std::string &hex, uint32_t fallback_rgb, int opacity)
{
	const uint32_t rgb = parse_hex_color(hex, fallback_rgb);
	const int alpha = std::clamp(opacity, 0, 255);
	return QColor(static_cast<int>((rgb >> 16) & 0xFFu), static_cast<int>((rgb >> 8) & 0xFFu),
		      static_cast<int>(rgb & 0xFFu), alpha);
}

struct LineSpec {
	QString text;
	int pixel_size;
	qreal slot_height;
};

} // namespace

QImage render_card(const LiedplanCard &card, int width, int height, qreal time_seconds)
{
	(void)time_seconds;

	QImage image(width, height, QImage::Format_ARGB32);
	image.fill(Qt::transparent);

	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.setRenderHint(QPainter::TextAntialiasing, true);

	const QColor box_color = make_qcolor(card.box_color, 0xFFFFFFu, card.box_opacity);
	if (box_color.alpha() > 0) {
		painter.setPen(Qt::NoPen);
		painter.setBrush(box_color);
		painter.drawRoundedRect(
			QRectF(0.0, 0.0, static_cast<qreal>(width), static_cast<qreal>(height)),
			kCornerRadiusPx, kCornerRadiusPx);
	}

	const QColor text_color = make_qcolor(card.text_color, 0x000000u, card.text_opacity);
	if (text_color.alpha() <= 0)
		return image;

	struct RawLine {
		const std::string *str;
		int px;
	};
	const RawLine raw_lines[3] = {
		{&card.zeile1, kLine12FontPx},
		{&card.zeile2, kLine12FontPx},
		{&card.zeile3, kLine3FontPx},
	};

	std::vector<LineSpec> lines;
	lines.reserve(3);
	qreal total_height = 0.0;

	for (const RawLine &rl : raw_lines) {
		if (rl.str->empty())
			continue;
		const qreal slot_h = static_cast<qreal>(rl.px) * kLineSpacingFactor;
		lines.push_back(LineSpec{QString::fromUtf8(rl.str->c_str()), rl.px, slot_h});
		total_height += slot_h;
	}

	if (lines.empty())
		return image;

	painter.setBrush(Qt::NoBrush);
	painter.setPen(text_color);

	qreal cur_y = (static_cast<qreal>(height) - total_height) * 0.5;
	for (const LineSpec &line : lines) {
		QFont font(QStringLiteral("Segoe UI"));
		font.setPixelSize(line.pixel_size);
		font.setStyleStrategy(QFont::PreferAntialias);
		painter.setFont(font);

		const QRectF line_rect(0.0, cur_y, static_cast<qreal>(width), line.slot_height);
		painter.drawText(line_rect, Qt::AlignHCenter | Qt::AlignVCenter, line.text);
		cur_y += line.slot_height;
	}

	return image;
}

QImage render_card(const LiedplanCard &card, qreal time_seconds)
{
	return render_card(card, kLiedplanCardWidth, kLiedplanCardHeight, time_seconds);
}
