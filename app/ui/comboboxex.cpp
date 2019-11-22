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
#include "comboboxex.h"

#include "project/undo.h"
#include "panels/project.h"
#include "ui/mainwindow.h"

#include <QUndoCommand>
#include <QWheelEvent>


class ComboBoxExCommand : public QUndoCommand {
  public:
    ComboBoxExCommand(ComboBoxEx* obj, int old_index, int new_index)
      : combobox(obj),
        old_val(old_index),
        new_val(new_index),
        done(true),
        old_project_changed(MainWindow::instance().isWindowModified())
    {

    }

    ComboBoxExCommand(const ComboBoxExCommand& ) = delete;
    ComboBoxExCommand& operator=(const ComboBoxExCommand&) = delete;

    void undo() override
    {
      combobox->setCurrentIndex(old_val);
      done = false;
      MainWindow::instance().setWindowModified(old_project_changed);
    }
    void redo() override
    {
      if (!done) {
        combobox->setCurrentIndex(new_val);
      }
      MainWindow::instance().setWindowModified(true);
    }
  private:
    ComboBoxEx* combobox;
    int old_val;
    int new_val;
    bool done;
    bool old_project_changed;
};

ComboBoxEx::ComboBoxEx(QWidget *parent)
  : QComboBox(parent),
    index(0)
{
  connect(this, SIGNAL(activated(int)), this, SLOT(index_changed(int)));
}

void ComboBoxEx::setCurrentIndexEx(const int ix)
{
  index = ix;
  setCurrentIndex(ix);
}

void ComboBoxEx::setCurrentTextEx(QString text)
{
  setCurrentText(std::move(text));
  index = currentIndex();
}

int ComboBoxEx::getPreviousIndex()
{
  return previousIndex;
}

QVariant ComboBoxEx::getValue() const
{
  return index;
}

void ComboBoxEx::setValue(QVariant val)
{
  setCurrentIndexEx(val.toInt());
}

void ComboBoxEx::index_changed(const int ix)
{
  if (index != ix) {
    previousIndex = index;
    index = ix;
  }
}

void ComboBoxEx::wheelEvent(QWheelEvent* e)
{
  Q_ASSERT(e);
  e->ignore();
}
