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
#ifndef SOURCEICONVIEW_H
#define SOURCEICONVIEW_H

#include <QListView>

#include "ui/sourceview.h"

class Project;

class SourceIconView : public QListView, public chestnut::ui::SourceView {
    Q_OBJECT
public:
    SourceIconView(Project& projParent, QWidget* parent = nullptr);

    SourceIconView() = delete;
    SourceIconView(const SourceIconView&) = delete;
    SourceIconView(const SourceIconView&&) = delete;
    SourceIconView& operator=(const SourceIconView&) = delete;
    SourceIconView& operator=(const SourceIconView&&) = delete;

  protected:
    void mousePressEvent(QMouseEvent* event) final;
    void mouseDoubleClickEvent(QMouseEvent *event) final;
    void dragEnterEvent(QDragEnterEvent *event) final;
    void dragMoveEvent(QDragMoveEvent *event) final;
    void dropEvent(QDropEvent* event) final;

    QModelIndex viewIndex(const QPoint& pos) const override;
    QModelIndexList selectedItems() const override;
signals:
    void changed_root();
private slots:
    void show_context_menu();
    void item_click(const QModelIndex& index);
private:
};

#endif // SOURCEICONVIEW_H
