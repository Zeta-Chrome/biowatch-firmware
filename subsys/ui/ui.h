#ifndef UI_H
#define UI_H

#include "widget.h"

#define MAX_UI_PAGES 16

void ui_init();
void ui_build(struct ui_widget *widget);
void ui_change_page(uint8_t page);
void ui_draw();
void ui_debug_print(struct ui_widget *widget, uint8_t level);
struct ui_widget* ui_get_selected();
void ui_select(struct ui_widget *widget);
void ui_unselect();
void ui_select_next();
void ui_select_child();
void ui_click_selection();

#endif
