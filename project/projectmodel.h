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
#ifndef PROJECTMODEL_H
#define PROJECTMODEL_H

#include <QAbstractItemModel>

#include "project/media.h"

class ProjectModel : public QAbstractItemModel
{
	Q_OBJECT
public:
	ProjectModel(QObject* parent = 0);
    virtual ~ProjectModel();

	void destroy_root();
	void clear();
    MediaPtr get_root();
    QVariant data(const QModelIndex &index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
						int role = Qt::DisplayRole) const override;
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex create_index(int arow, int acolumn, const MediaPtr& m_ptr) const;
	QModelIndex create_index(int arow, int acolumn, void *aid);
	QModelIndex parent(const QModelIndex &index) const override;
	bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    MediaPtr getItem(const QModelIndex &index) const;

    void appendChild(MediaPtr parent, MediaPtr child);
    void moveChild(MediaPtr child, MediaPtr to);
    void removeChild(MediaPtr parent, MediaPtr m);
    MediaPtr child(int i, MediaPtr parent = nullptr);
    int childCount(MediaPtr parent = nullptr);
    void set_icon(MediaPtr m, const QIcon &ico);

private:
    MediaPtr root_item;
};

#endif // PROJECTMODEL_H
