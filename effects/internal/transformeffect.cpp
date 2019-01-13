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
#include "transformeffect.h"

#include <QWidget>
#include <QLabel>
#include <QGridLayout>
#include <QSpinBox>
#include <QCheckBox>
#include <QOpenGLFunctions>
#include <QComboBox>
#include <QMouseEvent>

#include "ui/collapsiblewidget.h"
#include "project/clip.h"
#include "project/sequence.h"
#include "project/footage.h"
#include "io/math.h"
#include "ui/labelslider.h"
#include "ui/comboboxex.h"
#include "panels/project.h"
#include "debug.h"

#include "panels/panels.h"
#include "panels/viewer.h"
#include "ui/viewerwidget.h"

#define BLEND_MODE_NORMAL 0
#define BLEND_MODE_SCREEN 1
#define BLEND_MODE_MULTIPLY 2
#define BLEND_MODE_OVERLAY 3

TransformEffect::TransformEffect(ClipPtr c, const EffectMeta* em) : Effect(c, em) {
    enable_coords = true;
    EffectRowPtr position_row = add_row("Position");
    position_x = position_row->add_field(EFFECT_FIELD_DOUBLE, "posx"); // position X
    position_y = position_row->add_field(EFFECT_FIELD_DOUBLE, "posy"); // position Y

    EffectRowPtr scale_row = add_row("Scale");
    scale_x = scale_row->add_field(EFFECT_FIELD_DOUBLE, "scalex"); // scale X (and Y is uniform scale is selected)
    scale_x->set_double_minimum_value(0);
    scale_x->set_double_maximum_value(3000);
    scale_y = scale_row->add_field(EFFECT_FIELD_DOUBLE, "scaley"); // scale Y (disabled if uniform scale is selected)
    scale_y->set_double_minimum_value(0);
    scale_y->set_double_maximum_value(3000);

    EffectRowPtr uniform_scale_row = add_row("Uniform Scale");
    uniform_scale_field = uniform_scale_row->add_field(EFFECT_FIELD_BOOL, "uniformscale"); // uniform scale option

    EffectRowPtr rotation_row = add_row("Rotation");
    rotation = rotation_row->add_field(EFFECT_FIELD_DOUBLE, "rotation");

    EffectRowPtr anchor_point_row = add_row("Anchor Point");
    anchor_x_box = anchor_point_row->add_field(EFFECT_FIELD_DOUBLE, "anchorx"); // anchor point X
    anchor_y_box = anchor_point_row->add_field(EFFECT_FIELD_DOUBLE, "anchory"); // anchor point Y

    EffectRowPtr opacity_row = add_row("Opacity");
    opacity = opacity_row->add_field(EFFECT_FIELD_DOUBLE, "opacity"); // opacity
    opacity->set_double_minimum_value(0);
    opacity->set_double_maximum_value(100);

    EffectRowPtr blend_mode_row = add_row("Blend Mode");
    blend_mode_box = blend_mode_row->add_field(EFFECT_FIELD_COMBO, "blendmode"); // blend mode
    blend_mode_box->add_combo_item("Normal", BLEND_MODE_NORMAL);
    blend_mode_box->add_combo_item("Overlay", BLEND_MODE_OVERLAY);
    blend_mode_box->add_combo_item("Screen", BLEND_MODE_SCREEN);
    blend_mode_box->add_combo_item("Multiply", BLEND_MODE_MULTIPLY);

    // set up gizmos
    top_left_gizmo = add_gizmo(GIZMO_TYPE_DOT);
    top_left_gizmo->set_cursor(Qt::SizeFDiagCursor);
    top_left_gizmo->x_field1 = scale_x;

    top_center_gizmo = add_gizmo(GIZMO_TYPE_DOT);
    top_center_gizmo->set_cursor(Qt::SizeVerCursor);
    top_center_gizmo->y_field1 = scale_x;

    top_right_gizmo = add_gizmo(GIZMO_TYPE_DOT);
    top_right_gizmo->set_cursor(Qt::SizeBDiagCursor);
    top_right_gizmo->x_field1 = scale_x;

    bottom_left_gizmo = add_gizmo(GIZMO_TYPE_DOT);
    bottom_left_gizmo->set_cursor(Qt::SizeBDiagCursor);
    bottom_left_gizmo->x_field1 = scale_x;

    bottom_center_gizmo = add_gizmo(GIZMO_TYPE_DOT);
    bottom_center_gizmo->set_cursor(Qt::SizeVerCursor);
    bottom_center_gizmo->y_field1 = scale_x;

    bottom_right_gizmo = add_gizmo(GIZMO_TYPE_DOT);
    bottom_right_gizmo->set_cursor(Qt::SizeFDiagCursor);
    bottom_right_gizmo->x_field1 = scale_x;

    left_center_gizmo = add_gizmo(GIZMO_TYPE_DOT);
    left_center_gizmo->set_cursor(Qt::SizeHorCursor);
    left_center_gizmo->x_field1 = scale_x;

    right_center_gizmo = add_gizmo(GIZMO_TYPE_DOT);
    right_center_gizmo->set_cursor(Qt::SizeHorCursor);
    right_center_gizmo->x_field1 = scale_x;

    anchor_gizmo = add_gizmo(GIZMO_TYPE_TARGET);
    anchor_gizmo->set_cursor(Qt::SizeAllCursor);
    anchor_gizmo->x_field1 = anchor_x_box;
    anchor_gizmo->y_field1 = anchor_y_box;
    anchor_gizmo->x_field2 = position_x;
    anchor_gizmo->y_field2 = position_y;

    rotate_gizmo = add_gizmo(GIZMO_TYPE_DOT);
    rotate_gizmo->color = Qt::green;
    rotate_gizmo->set_cursor(Qt::SizeAllCursor);
    rotate_gizmo->x_field1 = rotation;

    rect_gizmo = add_gizmo(GIZMO_TYPE_POLY);
    rect_gizmo->x_field1 = position_x;
    rect_gizmo->y_field1 = position_y;

    connect(uniform_scale_field, SIGNAL(toggled(bool)), this, SLOT(toggle_uniform_scale(bool)));

    // set defaults
    uniform_scale_field->set_bool_value(true);
    blend_mode_box->set_combo_index(0);
    set = false;
    refresh();
}

TransformEffect::~TransformEffect() {
    delete position_x;
    delete position_y;
    delete scale_x;
    delete scale_y;
    delete uniform_scale_field;
    delete rotation;
    delete anchor_x_box;
    delete anchor_y_box;
    delete opacity;
    delete blend_mode_box;
}

void adjust_field(EffectField* field, double old_offset, double new_offset) {
    if (field->keyframes.size() > 0) {
        for (int i=0;i<field->keyframes.size();i++) {
            field->keyframes[i].data = field->keyframes.at(i).data.toDouble() - old_offset + new_offset;
        }
    } else {
        field->set_current_data(field->get_current_data().toDouble() - old_offset + new_offset);
    }
}

void TransformEffect::refresh() {
    if (parent_clip != NULL && parent_clip->sequence != NULL) {
        double new_default_pos_x = parent_clip->sequence->getWidth()/2;
        double new_default_pos_y = parent_clip->sequence->getHeight()/2;

        /*if (set) {
            adjust_field(position_x, default_pos_x, new_default_pos_x);
            adjust_field(position_y, default_pos_y, new_default_pos_y);
        }*/

        double default_pos_x = new_default_pos_x;
        double default_pos_y = new_default_pos_y;

        position_x->set_double_default_value(default_pos_x);
        position_y->set_double_default_value(default_pos_y);
        scale_x->set_double_default_value(100);
        scale_y->set_double_default_value(100);

        anchor_x_box->set_double_default_value(0);
        anchor_y_box->set_double_default_value(0);
        opacity->set_double_default_value(100);

        double x_percent_multipler = 200.0 / parent_clip->sequence->getWidth();
        double y_percent_multipler = 200.0 / parent_clip->sequence->getHeight();
        top_left_gizmo->x_field_multi1 = -x_percent_multipler;
        top_left_gizmo->y_field_multi1 = -y_percent_multipler;
        top_center_gizmo->y_field_multi1 = -y_percent_multipler;
        top_right_gizmo->x_field_multi1 = x_percent_multipler;
        top_right_gizmo->y_field_multi1 = -y_percent_multipler;
        bottom_left_gizmo->x_field_multi1 = -x_percent_multipler;
        bottom_left_gizmo->y_field_multi1 = y_percent_multipler;
        bottom_center_gizmo->y_field_multi1 = y_percent_multipler;
        bottom_right_gizmo->x_field_multi1 = x_percent_multipler;
        bottom_right_gizmo->y_field_multi1 = y_percent_multipler;
        left_center_gizmo->x_field_multi1 = -x_percent_multipler;
        right_center_gizmo->x_field_multi1 = x_percent_multipler;
        rotate_gizmo->x_field_multi1 = x_percent_multipler;

        set = true;
    }
}

void TransformEffect::toggle_uniform_scale(bool enabled) {
    scale_y->set_enabled(!enabled);

    top_center_gizmo->y_field1 = enabled ? scale_x : scale_y;
    bottom_center_gizmo->y_field1 = enabled ? scale_x : scale_y;
    top_left_gizmo->y_field1 = enabled ? NULL : scale_y;
    top_right_gizmo->y_field1 = enabled ? NULL : scale_y;
    bottom_left_gizmo->y_field1 = enabled ? NULL : scale_y;
    bottom_right_gizmo->y_field1 = enabled ? NULL : scale_y;
}

void TransformEffect::process_coords(double timecode, GLTextureCoords& coords, int data) {
    // position
    glTranslatef(position_x->get_double_value(timecode)-(parent_clip->sequence->getWidth()/2),
                 position_y->get_double_value(timecode)-(parent_clip->sequence->getHeight()/2), 0);

    // anchor point
    int anchor_x_offset = (anchor_x_box->get_double_value(timecode));
    int anchor_y_offset = (anchor_y_box->get_double_value(timecode));
    coords.vertexTopLeftX -= anchor_x_offset;
    coords.vertexTopRightX -= anchor_x_offset;
    coords.vertexBottomLeftX -= anchor_x_offset;
    coords.vertexBottomRightX -= anchor_x_offset;
    coords.vertexTopLeftY -= anchor_y_offset;
    coords.vertexTopRightY -= anchor_y_offset;
    coords.vertexBottomLeftY -= anchor_y_offset;
    coords.vertexBottomRightY -= anchor_y_offset;

    // rotation
    glRotatef(rotation->get_double_value(timecode), 0, 0, 1);

    // scale
    float sx = scale_x->get_double_value(timecode)*0.01;
    float sy = (uniform_scale_field->get_bool_value(timecode)) ? sx : scale_y->get_double_value(timecode)*0.01;
    glScalef(sx, sy, 1);

    // blend mode
    switch (blend_mode_box->get_combo_data(timecode).toInt()) {
    case BLEND_MODE_NORMAL:
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        break;
    case BLEND_MODE_OVERLAY:
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        break;
    case BLEND_MODE_SCREEN:
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
        break;
    case BLEND_MODE_MULTIPLY:
        glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
        break;
    default:
        dout << "[ERROR] Invalid blend mode. This is a bug - please contact developers";
    }

    // opacity
    float color[4];
    glGetFloatv(GL_CURRENT_COLOR, color);
    glColor4f(1.0, 1.0, 1.0, color[3]*(opacity->get_double_value(timecode)*0.01));
}

void TransformEffect::gizmo_draw(double timecode, GLTextureCoords& coords) {
    top_left_gizmo->world_pos[0] = QPoint(coords.vertexTopLeftX, coords.vertexTopLeftY);
    top_center_gizmo->world_pos[0] = QPoint(lerp(coords.vertexTopLeftX, coords.vertexTopRightX, 0.5), lerp(coords.vertexTopLeftY, coords.vertexTopRightY, 0.5));
    top_right_gizmo->world_pos[0] = QPoint(coords.vertexTopRightX, coords.vertexTopRightY);
    right_center_gizmo->world_pos[0] = QPoint(lerp(coords.vertexTopRightX, coords.vertexBottomRightX, 0.5), lerp(coords.vertexTopRightY, coords.vertexBottomRightY, 0.5));
    bottom_right_gizmo->world_pos[0] = QPoint(coords.vertexBottomRightX, coords.vertexBottomRightY);
    bottom_center_gizmo->world_pos[0] = QPoint(lerp(coords.vertexBottomRightX, coords.vertexBottomLeftX, 0.5), lerp(coords.vertexBottomRightY, coords.vertexBottomLeftY, 0.5));
    bottom_left_gizmo->world_pos[0] = QPoint(coords.vertexBottomLeftX, coords.vertexBottomLeftY);
    left_center_gizmo->world_pos[0] = QPoint(lerp(coords.vertexBottomLeftX, coords.vertexTopLeftX, 0.5), lerp(coords.vertexBottomLeftY, coords.vertexTopLeftY, 0.5));

    rotate_gizmo->world_pos[0] = QPoint(lerp(top_center_gizmo->world_pos[0].x(), bottom_center_gizmo->world_pos[0].x(), -0.1), lerp(top_center_gizmo->world_pos[0].y(), bottom_center_gizmo->world_pos[0].y(), -0.1));

    rect_gizmo->world_pos[0] = QPoint(coords.vertexTopLeftX, coords.vertexTopLeftY);
    rect_gizmo->world_pos[1] = QPoint(coords.vertexTopRightX, coords.vertexTopRightY);
    rect_gizmo->world_pos[2] = QPoint(coords.vertexBottomRightX, coords.vertexBottomRightY);
    rect_gizmo->world_pos[3] = QPoint(coords.vertexBottomLeftX, coords.vertexBottomLeftY);
}
