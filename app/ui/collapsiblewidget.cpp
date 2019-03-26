/*
 * Olive. Olive is a free non-linear video editor for Windows, macOS, and Linux.
 * Copyright (C) 2018  {{ organization }}
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 *along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include "collapsiblewidget.h"

#include "ui/checkboxex.h"

#include <QDebug>
#include <QLabel>
#include <QLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QMouseEvent>
#include <QWidget>
#include <QPainter>

#include "debug.h"

CollapsibleWidget::CollapsibleWidget(QWidget* parent) : QWidget(parent) {
  selected = false;

  layout = new QVBoxLayout(this);
  layout->setMargin(0);
  layout->setSpacing(0);

  title_bar = new CollapsibleWidgetHeader();
  title_bar->setFocusPolicy(Qt::ClickFocus);
  title_bar->setAutoFillBackground(true);
  title_bar_layout = new QHBoxLayout();
  title_bar_layout->setMargin(5);
  title_bar->setLayout(title_bar_layout);
  enabled_check = new CheckboxEx();
  enabled_check->setChecked(true);
  header = new QLabel();
  collapse_button = new QPushButton();
  collapse_button->setIconSize(QSize(8, 8));
  collapse_button->setStyleSheet("QPushButton { border: none; }");
  setText(tr("<untitled>"));

  reset_button_ = new QPushButton();
  reset_button_->setToolTip(tr("Reset parameters to default"));
  reset_button_->setFlat(true);
  reset_button_->setText("R");
  reset_button_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);

  title_bar_layout->addWidget(collapse_button);
  title_bar_layout->addWidget(enabled_check);
  title_bar_layout->addWidget(reset_button_);
  title_bar_layout->addWidget(header);
  title_bar_layout->addStretch();

  layout->addWidget(title_bar);

  connect(title_bar, SIGNAL(select(bool, bool)), this, SLOT(header_click(bool, bool)));

  set_button_icon(true);

  contents = nullptr;
}

CollapsibleWidget::~CollapsibleWidget()
{

}

void CollapsibleWidget::header_click(bool s, bool deselect) {
  selected = s;
  title_bar->selected = s;
  if (s) {
    QPalette p = title_bar->palette();
    p.setColor(QPalette::Background, QColor(255, 255, 255, 64));
    title_bar->setPalette(p);
  } else {
    title_bar->setPalette(palette());
  }
  if (deselect) emit deselect_others(this);
}

bool CollapsibleWidget::is_focused() {
  if (hasFocus()) return true;
  return title_bar->hasFocus();
}

bool CollapsibleWidget::is_expanded() {
  return contents->isVisible();
}

void CollapsibleWidget::set_button_icon(bool open) {
  collapse_button->setIcon(open ? QIcon(":/icons/tri-down.png") : QIcon(":/icons/tri-right.png"));
}

void CollapsibleWidget::setContents(QWidget* c) {
  bool existing = (contents != nullptr);
  contents = c;
  if (!existing) {
    layout->addWidget(contents);
    connect(enabled_check, SIGNAL(toggled(bool)), this, SLOT(on_enabled_change(bool)));
    connect(collapse_button, SIGNAL(clicked()), this, SLOT(on_visible_change()));
  }
}

void CollapsibleWidget::setText(const QString &s) {
  header->setText(s);
}

void CollapsibleWidget::on_enabled_change(bool b) {
  contents->setEnabled(b);
}

void CollapsibleWidget::on_visible_change() {
  contents->setVisible(!contents->isVisible());
  set_button_icon(contents->isVisible());
  emit visibleChanged();
}

CollapsibleWidgetHeader::CollapsibleWidgetHeader(QWidget* parent) : QWidget(parent), selected(false) {
  setContextMenuPolicy(Qt::CustomContextMenu);
}

void CollapsibleWidgetHeader::mousePressEvent(QMouseEvent* event) {
  if (selected) {
    if ((event->modifiers() & Qt::ShiftModifier)) {
      selected = false;
      emit select(selected, false);
    }
  } else {
    selected = true;
    emit select(selected, !(event->modifiers() & Qt::ShiftModifier));
  }
}

void CollapsibleWidgetHeader::paintEvent(QPaintEvent *event) {
  QWidget::paintEvent(event);
  QPainter p(this);
  p.setPen(Qt::white);
  int line_x = width() / 100;
  int line_y = height() - 1;
  p.drawLine(line_x, line_y, width() - line_x - line_x, line_y);
}
