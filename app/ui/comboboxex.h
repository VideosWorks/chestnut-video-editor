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
#ifndef COMBOBOXEX_H
#define COMBOBOXEX_H

#include <QComboBox>
#include <QDebug>

class ComboBoxEx : public QComboBox {
    Q_OBJECT
  public:
    explicit ComboBoxEx(QWidget* parent = nullptr);
    void setCurrentIndexEx(int i);
    void setCurrentTextEx(const QString &text);
    int getPreviousIndex();
  protected:
    void wheelEvent(QWheelEvent* e) override;
  private slots:
    void index_changed(int);
  private:
    int index;
    int previousIndex;
};

#endif // COMBOBOXEX_H
