#ifndef UI_WIDGET_H
#define UI_WIDGET_H

#include "assets/bitmaps/bitmaps.h"
#include "assets/fonts/font.h"
#include "lib/utils.h"
#include "ui_common.h"
#include <stdbool.h>
#include <stdint.h>

#define MAX_CONTAINER_CHILDREN 8

struct ui_widget;
struct ui_widget;

enum ui_widget_type {
	UI_WIDGET_TYPE_RECT,
	UI_WIDGET_TYPE_IMAGE,
	UI_WIDGET_TYPE_TEXT,
	UI_WIDGET_TYPE_CONTAINER
};

struct ui_rect_data {
	uint8_t fill;
};

struct ui_text_data {
	const char *str;
	const struct font *font;
};

enum ui_layout { UI_LAYOUT_ROW, UI_LAYOUT_COLUMN, UI_LAYOUT_GRID, UI_LAYOUT_STACK };
enum ui_justify { UI_JUSTIFY_CENTER, UI_JUSTIFY_EVENLY };

struct ui_container_data {
	enum ui_layout layout;
	enum ui_justify justify;
	uint8_t fill;
	uint8_t padding;
	uint8_t spacing;
	union {
		struct ui_size grid_size; // only used when layout is grid layout
		uint8_t active_child; // Only used when layout is stack layout
	};
	struct ui_widget *children[MAX_CONTAINER_CHILDREN];
	uint8_t child_count;
	struct ui_widget *child_sel_head;
	struct ui_widget *child_sel_tail;
};

enum ui_widget_flags {
	UI_WIDGET_FLAG_FIT_CONTENTS = BIT(0),
	UI_WIDGET_FLAG_SELECTABLE = BIT(1),
	UI_WIDGET_FLAG_DIRTY = BIT(2),
	UI_WIDGET_FLAG_SUBTREE_DIRTY = BIT(3),
};

typedef void (*ui_widget_callback)(void *user_data);

struct ui_widget {
	enum ui_widget_type type;
	struct ui_widget *parent;
	struct ui_size min_size;
	struct ui_rect bb;
	uint8_t flags;
	uint8_t flex;
	struct ui_widget *next_sel;
	ui_widget_callback on_select;
	ui_widget_callback on_click;
	void *user_data;

	union {
		struct ui_rect_data *rect;
		const struct bmp *image;
		struct ui_text_data *text;
		struct ui_container_data *container;
	};
};

struct ui_widget *ui_widget_create_rect(uint8_t fill, uint8_t flex, bool selectable);
struct ui_widget *ui_widget_create_image(const struct bmp *bitmap, bool fit_contents, uint8_t flex,
										 bool selectable);
struct ui_widget *ui_widget_create_text(const char *str, const struct font *font, bool fit_contents,
										uint8_t flex, bool selectable);
struct ui_widget *ui_widget_create_row(uint8_t fill, uint8_t padding, uint8_t spacing, uint8_t flex,
									   bool selectable);
struct ui_widget *ui_widget_create_col(uint8_t fill, uint8_t padding, uint8_t spacing, uint8_t flex,
									   bool selectable);
struct ui_widget *ui_widget_create_grid(uint8_t fill, uint8_t padding, uint8_t spacing,
										uint8_t rows, uint8_t cols, uint8_t flex, bool selectable);
struct ui_widget *ui_widget_create_stack(uint8_t fill, uint8_t padding, uint8_t flex,
										 bool selectable);
struct ui_widget *ui_widget_create_button(const char *str, const struct font *font,
										  struct ui_widget **out_text, uint8_t flex);
static inline void ui_widget_set_callbacks(struct ui_widget *widget, ui_widget_callback on_select,
										   ui_widget_callback on_click, void *user_data)
{
	BW_ASSERT(widget->flags & UI_WIDGET_FLAG_SELECTABLE,
			  "Setting callbacks on a non-selectable widget\n");
	widget->on_select = on_select;
	widget->on_click = on_click;
	widget->user_data = user_data;
}

void ui_container_add_child(struct ui_widget *widget, struct ui_widget *child);
void ui_widget_build(struct ui_widget *widget);
void ui_widget_draw(struct ui_widget *widget, bool parent_dirty);
void ui_widget_invert(struct ui_widget *widget);
void ui_widget_update_rect(struct ui_widget *widget, uint8_t fill);
void ui_widget_update_image(struct ui_widget *widget, struct bmp *bmp);
void ui_widget_update_text(struct ui_widget *widget, const char *str);
void ui_widget_set_active_child(struct ui_widget *widget, uint8_t active_child_idx);

#endif
