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
#include "keyframe.h"

#include <QVector>

#include "effectfield.h"
#include "undo.h"
#include "panels/panelmanager.h"

void delete_keyframes(QVector<EffectField*>& selected_key_fields, QVector<int> &selected_keys)
{
  QVector<EffectField*> fields;
  QVector<int> key_indices;

  for (int i=0;i<selected_keys.size();i++) {
    bool added = false;
    for (int j=0;j<key_indices.size();j++) {
      if (key_indices.at(j) < selected_keys.at(i)) {
        key_indices.insert(j, selected_keys.at(i));
        fields.insert(j, selected_key_fields.at(i));
        added = true;
        break;
      }
    }
    if (!added) {
      key_indices.append(selected_keys.at(i));
      fields.append(selected_key_fields.at(i));
    }
  }

  if (!fields.empty()) {
    auto ca = new ComboAction();
    for (int i=0;i<key_indices.size();i++) {
      ca->append(new KeyframeDelete(fields.at(i), key_indices.at(i)));
    }
    e_undo_stack.push(ca);
    selected_keys.clear();
    selected_key_fields.clear();
    panels::PanelManager::refreshPanels(false);
  }
}

EffectKeyframe::EffectKeyframe(const EffectField* parent) : parent_(parent)
{
  Q_ASSERT(parent != nullptr);
}


bool EffectKeyframe::load(QXmlStreamReader& stream)
{
  for (auto attr : stream.attributes()) {
    auto name = attr.name().toString().toLower();
    if (name == "type") {
      type = static_cast<KeyframeType>(attr.value().toInt());
    } else {
      qWarning() << "Unhandled attribute" << name;
    }
  }

  while (stream.readNextStartElement()) {
    auto name = stream.name().toString().toLower();
    if (name == "value") {
      auto val = stream.readElementText();
      data.setValue(val);
    } else if (name == "frame") {
      time = stream.readElementText().toInt();
    } else if (name == "prex") {
      pre_handle_x = stream.readElementText().toInt();
    } else if (name == "prey") {
      pre_handle_y = stream.readElementText().toInt();
    } else if (name == "postx") {
      post_handle_x = stream.readElementText().toInt();
    } else if (name == "posty") {
      post_handle_y = stream.readElementText().toInt();
    } else {
      qWarning() << "Unhandled Element" << name;
      stream.skipCurrentElement();
    }
  }
  return true;
}

bool EffectKeyframe::save(QXmlStreamWriter& stream) const
{
  Q_ASSERT(parent_ != nullptr);
  stream.writeStartElement("keyframe");

  stream.writeAttribute("type", QString::number(static_cast<int>(type)));

  stream.writeTextElement("value", fieldTypeValueToString(parent_->type, data));
  stream.writeTextElement("frame", QString::number(time));
  stream.writeTextElement("prex", QString::number(pre_handle_x));
  stream.writeTextElement("prey", QString::number(pre_handle_y));
  stream.writeTextElement("postx", QString::number(post_handle_x));
  stream.writeTextElement("posty", QString::number(post_handle_y));
  stream.writeEndElement();
  return true;
}
