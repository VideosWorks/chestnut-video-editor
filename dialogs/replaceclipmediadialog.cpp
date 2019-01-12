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
#include "replaceclipmediadialog.h"

#include "ui/sourcetable.h"
#include "panels/panels.h"
#include "panels/timeline.h"
#include "panels/project.h"
#include "project/sequence.h"
#include "project/clip.h"
#include "playback/playback.h"
#include "playback/cacher.h"
#include "project/footage.h"
#include "project/undo.h"
#include "project/media.h"

#include <QVBoxLayout>
#include <QTreeView>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QCheckBox>

ReplaceClipMediaDialog::ReplaceClipMediaDialog(QWidget *parent, Media *old_media) :
	QDialog(parent),
	media(old_media)
{
	setWindowTitle("Replace clips using \"" + old_media->get_name() + "\"");

	resize(300, 400);

	QVBoxLayout* layout = new QVBoxLayout();

	layout->addWidget(new QLabel("Select which media you want to replace this media's clips with:"));

	tree = new QTreeView();

	layout->addWidget(tree);

	use_same_media_in_points = new QCheckBox("Keep the same media in-points");
	use_same_media_in_points->setChecked(true);
	layout->addWidget(use_same_media_in_points);

	QHBoxLayout* buttons = new QHBoxLayout();

	buttons->addStretch();

	QPushButton* replace_button = new QPushButton("Replace");
	connect(replace_button, SIGNAL(clicked(bool)), this, SLOT(replace()));
	buttons->addWidget(replace_button);

	QPushButton* cancel_button = new QPushButton("Cancel");
	connect(cancel_button, SIGNAL(clicked(bool)), this, SLOT(close()));
	buttons->addWidget(cancel_button);

	buttons->addStretch();

	layout->addLayout(buttons);

	setLayout(layout);

	tree->setModel(&project_model);
}

void ReplaceClipMediaDialog::replace() {
	QModelIndexList selected_items = tree->selectionModel()->selectedRows();
	if (selected_items.size() != 1) {
		QMessageBox::critical(this, "No media selected", "Please select a media to replace with or click 'Cancel'.", QMessageBox::Ok);
	} else {
		Media* new_item = static_cast<Media*>(selected_items.at(0).internalPointer());
		if (media == new_item) {
			QMessageBox::critical(this, "Same media selected", "You selected the same media that you're replacing. Please select a different one or click 'Cancel'.", QMessageBox::Ok);
		} else if (new_item->get_type() == MEDIA_TYPE_FOLDER) {
			QMessageBox::critical(this, "Folder selected", "You cannot replace footage with a folder.", QMessageBox::Ok);
		} else {
			if (new_item->get_type() == MEDIA_TYPE_SEQUENCE && e_sequence == new_item->to_sequence()) {
				QMessageBox::critical(this, "Active sequence selected", "You cannot insert a sequence into itself.", QMessageBox::Ok);
			} else {
				ReplaceClipMediaCommand* rcmc = new ReplaceClipMediaCommand(
							media,
							new_item,
							use_same_media_in_points->isChecked()
						);

				for (int i=0;i<e_sequence->clips.size();i++) {
                    ClipPtr c = e_sequence->clips.at(i);
					if (c != NULL && c->media == media) {
						rcmc->clips.append(c);
					}
				}

				e_undo_stack.push(rcmc);

				close();
			}

		}
	}
}
