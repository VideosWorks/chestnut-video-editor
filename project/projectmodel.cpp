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
#include "projectmodel.h"

#include "panels/panels.h"
#include "panels/viewer.h"
#include "ui/viewerwidget.h"
#include "project/media.h"
#include "debug.h"

ProjectModel::ProjectModel(QObject *parent) : QAbstractItemModel(parent), root_item(nullptr) {
    root_item = std::make_shared<Media>(nullptr);
	root_item->root = true;
}

ProjectModel::~ProjectModel() {
	destroy_root();
}

void ProjectModel::destroy_root() {
	if (e_panel_sequence_viewer != nullptr) e_panel_sequence_viewer->viewer_widget->delete_function();
	if (e_panel_footage_viewer != nullptr) e_panel_footage_viewer->viewer_widget->delete_function();
}

void ProjectModel::clear() {
	beginResetModel();
	destroy_root();
    root_item = std::make_shared<Media>(nullptr);
	root_item->root = true;
	endResetModel();
}

MediaPtr ProjectModel::get_root() {
	return root_item;
}

QVariant ProjectModel::data(const QModelIndex &index, int role) const {
	if (!index.isValid())
		return QVariant();

    return static_cast<Media*>(index.internalPointer())->data(index.column(), role);
}

Qt::ItemFlags ProjectModel::flags(const QModelIndex &index) const {
	if (!index.isValid())
		return Qt::ItemIsDropEnabled;

	return QAbstractItemModel::flags(index) | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled | Qt::ItemIsEditable;
}

QVariant ProjectModel::headerData(int section, Qt::Orientation orientation, int role) const {
	if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
		return root_item->data(section, role);

	return QVariant();
}

QModelIndex ProjectModel::index(int row, int column, const QModelIndex &parent) const {
	if (!hasIndex(row, column, parent))
		return QModelIndex();

    MediaPtr parentItem;

    if (!parent.isValid()) {
		parentItem = root_item;
     } else {
//        parentItem = static_cast<Media*>(parent.internalPointer()); //FIXME: ptr cast issue
    }

    MediaPtr childItem = parentItem->child(row);
    if (childItem) {
        return createIndex(row, column, childItem.operator ->()); //FIXME: ptr of shared_ptr?
    } else {
		return QModelIndex();
    }
}

QModelIndex ProjectModel::create_index(int arow, int acolumn, void* aid) {
	return createIndex(arow, acolumn, aid);
}

QModelIndex ProjectModel::parent(const QModelIndex &index) const {
	if (!index.isValid())
		return QModelIndex();

//    MediaPtr childItem = static_cast<Media*>(index.internalPointer()); //FIXME: ptr cast issue
    MediaPtr childItem ;

    if (childItem != nullptr) {
        MediaWPtr parentItem = childItem->parentItem();
        if (!parentItem.expired()) {
            MediaPtr parPtr = parentItem.lock();
            if (parPtr == root_item) {
                return QModelIndex();
            }
            return createIndex(parPtr->row(), 0, parPtr.operator ->()); //FIXME: ptr of shared_ptr?
        }
    }
    return QModelIndex();
}

bool ProjectModel::setData(const QModelIndex &index, const QVariant &value, int role) {
	if (role != Qt::EditRole)
		return false;

//    MediaPtr item = static_cast<Media*>(index.internalPointer());  //FIXME: ptr cast issue
    MediaPtr item;
	bool result = item->setData(index.column(), value);

	if (result)
		emit dataChanged(index, index);

	return result;
}

int ProjectModel::rowCount(const QModelIndex &parent) const {
    MediaPtr parentItem;
	if (parent.column() > 0)
		return 0;

	if (!parent.isValid()) {
		parentItem = root_item;
	} else {
//        parentItem = static_cast<Media*>(parent.internalPointer());  //FIXME: ptr cast issue
	}

    if (parentItem != nullptr) {
        return parentItem->childCount();
    }
    return 0;
}

int ProjectModel::columnCount(const QModelIndex &parent) const {
	if (parent.isValid())
        return static_cast<Media*>(parent.internalPointer())->columnCount();
	else
		return root_item->columnCount();
}

MediaPtr ProjectModel::getItem(const QModelIndex &index) const {
	if (index.isValid()) {
//        MediaPtr item = static_cast<Media*>(index.internalPointer()); //FIXME: ptr cast issue
        MediaPtr item;
        if (item != nullptr) {
			return item;
        }
	}
	return root_item;
}

void ProjectModel::set_icon(MediaPtr m, const QIcon &ico) {
    QModelIndex index = createIndex(m->row(), 0, m.operator ->());//FIXME: ptr of shared_ptr?
	m->set_icon(ico);
	emit dataChanged(index, index);

}

void ProjectModel::appendChild(MediaPtr parent, MediaPtr child) {
    if (parent == nullptr) {
        parent = root_item;
    }
    beginInsertRows(parent == root_item ? QModelIndex() : createIndex(parent->row(), 0, parent.operator ->()), parent->childCount(), parent->childCount());//FIXME: ptr of shared_ptr?
	parent->appendChild(child);
	endInsertRows();
}

void ProjectModel::moveChild(MediaPtr child, MediaPtr to) {
    if (to == nullptr) {
        to = root_item;
    }
    MediaWPtr from = child->parentItem();
    if (!from.expired()) {
        MediaPtr parPtr = from.lock();
        beginMoveRows(
                    parPtr == root_item ? QModelIndex() : createIndex(parPtr->row(), 0, parPtr.operator ->()), //FIXME: ptr of shared_ptr?
                    child->row(),
                    child->row(),
                    to == root_item ? QModelIndex() : createIndex(to->row(), 0, to.operator ->()),//FIXME: ptr of shared_ptr?
                    to->childCount()
                );
        parPtr->removeChild(child->row());
        to->appendChild(child);
        endMoveRows();
    }
}

void ProjectModel::removeChild(MediaPtr parent, MediaPtr m) {
	if (parent == nullptr) parent = root_item;
    beginRemoveRows(parent == root_item ? QModelIndex() : createIndex(parent->row(), 0, parent.operator ->()), m->row(), m->row());//FIXME: ptr of shared_ptr?
	parent->removeChild(m->row());
	endRemoveRows();
}

MediaPtr ProjectModel::child(int i, MediaPtr parent) {
	if (parent == nullptr) parent = root_item;
	return parent->child(i);
}

int ProjectModel::childCount(MediaPtr parent) {
	if (parent == nullptr) parent = root_item;
	return parent->childCount();
}
