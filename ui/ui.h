#ifndef UI_H
#define UI_H

#include "widget.h"

#define MAX_UI_PAGES 16

void ui_init();
void ui_set_root_widget(ui_widget_t *widget);
void ui_build(ui_widget_t *widget);
void ui_draw();
void ui_select_next();
void ui_select_parent();
void ui_unselect();

#endif
