#include "drivers/display/display.h"
#include "lib/utils.h"
#include "ui_common.h"
#include "widget.h"
#include <string.h>

#define MAX_RECT_WIDGETS 4
#define MAX_IMAGE_WIDGETS 16
#define MAX_TEXT_WIDGETS 44
#define MAX_CONTAINER_WIDGETS 32
#define MAX_WIDGETS 96

static struct ui_widget g_widget_pool[MAX_WIDGETS];
static struct ui_rect_data g_rect_pool[MAX_RECT_WIDGETS];
static struct ui_text_data g_text_pool[MAX_TEXT_WIDGETS];
static struct ui_container_data g_container_pool[MAX_CONTAINER_WIDGETS];

static struct ui_widget *alloc_widget()
{
	static uint8_t idx = 0;
	BW_ASSERT(idx < MAX_WIDGETS, "Exhausted the widget pool\n");
	return &g_widget_pool[idx++];
}

static struct ui_rect_data *alloc_rect_data()
{
	static uint8_t idx = 0;
	BW_ASSERT(idx < MAX_RECT_WIDGETS, "Exhausted the rect data pool\n");
	return &g_rect_pool[idx++];
}

static struct ui_text_data *alloc_text_data()
{
	static uint8_t idx = 0;
	BW_ASSERT(idx < MAX_TEXT_WIDGETS, "Exhausted the text data pool\n");
	return &g_text_pool[idx++];
}

static struct ui_container_data *alloc_container_data()
{
	static uint8_t idx = 0;
	BW_ASSERT(idx < MAX_CONTAINER_WIDGETS, "Exhausted the container data pool\n");
	return &g_container_pool[idx++];
}

static struct ui_widget *widget_create(enum ui_widget_type type, bool fit_contents, uint8_t flex,
									   bool selectable)
{
	struct ui_widget *widget = alloc_widget();
	widget->type = type;
	widget->flex = flex;
	widget->flags = UI_WIDGET_FLAG_DIRTY | UI_WIDGET_FLAG_SUBTREE_DIRTY |
					(fit_contents ? UI_WIDGET_FLAG_FIT_CONTENTS : 0) |
					(selectable ? UI_WIDGET_FLAG_SELECTABLE : 0);

	return widget;
}

struct ui_widget *ui_widget_create_rect(uint8_t fill, uint8_t flex, bool selectable)
{
	struct ui_widget *widget = widget_create(UI_WIDGET_TYPE_RECT, false, flex, selectable);
	widget->rect = alloc_rect_data();
	widget->rect->fill = fill;

	return widget;
}

struct ui_widget *ui_widget_create_image(const struct bmp *bitmap, bool fit_contents, uint8_t flex,
										 bool selectable)
{
	struct ui_widget *widget = widget_create(UI_WIDGET_TYPE_IMAGE, fit_contents, flex, selectable);
	widget->min_size = bitmap->size;
	widget->image = bitmap;

	return widget;
}

struct ui_widget *ui_widget_create_text(const char *str, const struct font *font, bool fit_contents,
										uint8_t flex, bool selectable)
{
	struct ui_widget *widget = widget_create(UI_WIDGET_TYPE_TEXT, fit_contents, flex, selectable);
	widget->text = alloc_text_data();
	widget->text->str = str;
	widget->text->font = font;

	uint8_t idx, min_w = 0, min_h = font->height;
	for (size_t i = 0; i < strlen(widget->text->str); i++) {
		idx = widget->text->str[i] - font->first_char;
		min_w += font->glyphs[idx].x_advance;
	}

	widget->min_size = (struct ui_size){ min_w, min_h };

	return widget;
}

struct ui_widget *ui_widget_create_row(uint8_t fill, uint8_t padding, uint8_t spacing, uint8_t flex,
									   bool selectable)
{
	struct ui_widget *widget = widget_create(UI_WIDGET_TYPE_CONTAINER, false, flex, selectable);
	widget->container = alloc_container_data();
	widget->container->layout = UI_LAYOUT_ROW;
	widget->container->fill = fill;
	widget->container->padding = padding;
	widget->container->spacing = spacing;

	return widget;
}

struct ui_widget *ui_widget_create_col(uint8_t fill, uint8_t padding, uint8_t spacing, uint8_t flex,
									   bool selectable)
{
	struct ui_widget *widget = widget_create(UI_WIDGET_TYPE_CONTAINER, false, flex, selectable);
	widget->container = alloc_container_data();
	widget->container->layout = UI_LAYOUT_COLUMN;
	widget->container->fill = fill;
	widget->container->padding = padding;
	widget->container->spacing = spacing;

	return widget;
}

struct ui_widget *ui_widget_create_grid(uint8_t fill, uint8_t padding, uint8_t spacing,
										uint8_t rows, uint8_t cols, uint8_t flex, bool selectable)
{
	struct ui_widget *widget = widget_create(UI_WIDGET_TYPE_CONTAINER, false, flex, selectable);
	widget->container = alloc_container_data();
	widget->container->layout = UI_LAYOUT_GRID;
	widget->container->fill = fill;
	widget->container->padding = padding;
	widget->container->spacing = spacing;
	widget->container->grid_size = (struct ui_size){ cols, rows };

	return widget;
}

struct ui_widget *ui_widget_create_stack(uint8_t fill, uint8_t padding, uint8_t flex,
										 bool selectable)
{
	struct ui_widget *widget = widget_create(UI_WIDGET_TYPE_CONTAINER, false, flex, selectable);
	widget->container = alloc_container_data();
	widget->container->layout = UI_LAYOUT_STACK;
	widget->container->fill = fill;
	widget->container->padding = padding;
	widget->container->active_child = 0;

	return widget;
}

struct ui_widget *ui_widget_create_button(const char *str, const struct font *font,
										  struct ui_widget **out_text, uint8_t flex)
{
	struct ui_widget *btn = ui_widget_create_col(0xFF, 1, 0, flex, true);
	struct ui_widget *text = ui_widget_create_text(str, font, false, 1, false);
	ui_container_add_child(btn, text);
	if (out_text)
		*out_text = text;
	return btn;
}

void ui_container_add_child(struct ui_widget *widget, struct ui_widget *child)
{
	BW_ASSERT(widget->type == UI_WIDGET_TYPE_CONTAINER,
			  "Expected containter widget in ui_container_add_child");
	widget->container->children[widget->container->child_count++] = child;
}

static void build_row_container(struct ui_widget *widget)
{
	struct ui_widget *child;
	uint8_t w, h;
	size_t total_flex = 0;

	for (size_t i = 0; i < widget->container->child_count; i++)
		total_flex += widget->container->children[i]->flex;

	uint8_t used_space = widget->container->padding;
	uint8_t free_space =
		widget->bb.w - (2 * widget->container->padding +
						widget->container->spacing * (widget->container->child_count - 1));

	for (size_t i = 0; i < widget->container->child_count; i++) {
		child = widget->container->children[i];
		w = free_space * child->flex / total_flex;
		h = widget->bb.h - 2 * widget->container->padding;

		child->bb.x = widget->bb.x + used_space;
		child->bb.y = widget->bb.y + widget->container->padding;

		if (child->flags & UI_WIDGET_FLAG_FIT_CONTENTS) {
			child->bb.w = MIN(w, child->min_size.w);
			child->bb.h = MIN(h, child->min_size.h);
			child->bb.x += (w - child->bb.w) / 2;
			child->bb.y += (h - child->bb.h) / 2;
		} else {
			child->bb.w = w;
			child->bb.h = h;
		}

		used_space += w + widget->container->spacing;
		ui_widget_build(child);
	}
}

static void build_col_container(struct ui_widget *widget)
{
	struct ui_widget *child;
	uint8_t w, h;
	size_t total_flex = 0;
	for (size_t i = 0; i < widget->container->child_count; i++)
		total_flex += widget->container->children[i]->flex;

	uint8_t used_space = widget->container->padding;
	uint8_t free_space =
		widget->bb.h - (2 * widget->container->padding +
						widget->container->spacing * (widget->container->child_count - 1));

	for (size_t i = 0; i < widget->container->child_count; i++) {
		child = widget->container->children[i];
		w = widget->bb.w - 2 * widget->container->padding;
		h = free_space * child->flex / total_flex;

		child->bb.x = widget->bb.x + widget->container->padding;
		child->bb.y = widget->bb.y + used_space;

		if (child->flags & UI_WIDGET_FLAG_FIT_CONTENTS) {
			child->bb.w = MIN(w, child->min_size.w);
			child->bb.h = MIN(h, child->min_size.h);
			child->bb.x += (w - child->bb.w) / 2;
			child->bb.y += (h - child->bb.h) / 2;
		} else {
			child->bb.w = w;
			child->bb.h = h;
		}

		used_space += h + widget->container->spacing;
		ui_widget_build(child);
	}
}

static void build_grid_container(struct ui_widget *widget)
{
	struct ui_widget *child;
	uint8_t row, col;
	uint8_t w = (widget->bb.w - 2 * widget->container->padding -
				 (widget->container->grid_size.w - 1) * widget->container->spacing) /
				widget->container->grid_size.w;
	uint8_t h = (widget->bb.h - 2 * widget->container->padding -
				 (widget->container->grid_size.h - 1) * widget->container->spacing) /
				widget->container->grid_size.h;

	for (size_t i = 0; i < widget->container->child_count; i++) {
		col = i % widget->container->grid_size.w;
		row = i / widget->container->grid_size.w;
		if (row == widget->container->grid_size.h)
			break;

		child = widget->container->children[i];
		child->bb.x =
			widget->bb.x + widget->container->padding + col * (w + widget->container->spacing);
		child->bb.y =
			widget->bb.y + widget->container->padding + row * (h + widget->container->spacing);

		if (child->flags & UI_WIDGET_FLAG_FIT_CONTENTS) {
			child->bb.w = MIN(w, child->min_size.w);
			child->bb.h = MIN(h, child->min_size.h);
			child->bb.x += (w - child->bb.w) / 2;
			child->bb.y += (h - child->bb.h) / 2;
		} else {
			child->bb.w = w;
			child->bb.h = h;
		}

		ui_widget_build(child);
	}
}

static void build_stack_container(struct ui_widget *widget)
{
	struct ui_rect bb;
	struct ui_widget *child;
	bb.x = widget->bb.x + widget->container->padding;
	bb.y = widget->bb.y + widget->container->padding;
	bb.w = widget->bb.w - 2 * widget->container->padding;
	bb.h = widget->bb.h - 2 * widget->container->padding;

	for (size_t i = 0; i < widget->container->child_count; i++) {
		child = widget->container->children[i];
		child->bb = bb;

		ui_widget_build(child);
	}
}

void ui_widget_build(struct ui_widget *widget)
{
	if (widget->type == UI_WIDGET_TYPE_CONTAINER) {
		struct ui_widget *child;
		for (size_t i = 0; i < widget->container->child_count; i++) {
			child = widget->container->children[i];
			child->parent = widget;
		}

		switch (widget->container->layout) {
		case UI_LAYOUT_ROW:
			build_row_container(widget);
			break;
		case UI_LAYOUT_COLUMN:
			build_col_container(widget);
			break;
		case UI_LAYOUT_GRID:
			build_grid_container(widget);
			break;
		case UI_LAYOUT_STACK:
			build_stack_container(widget);
			break;
		}
	}

	if (!widget->parent || widget->parent->container->layout == UI_LAYOUT_STACK) {
		if (widget->type == UI_WIDGET_TYPE_CONTAINER && widget->container->child_sel_tail)
			widget->container->child_sel_tail->next_sel = widget->container->child_sel_head;

		return;
	}

	struct ui_container_data *parent_container = widget->parent->container;
	struct ui_widget *head, *tail;

	if (widget->flags & UI_WIDGET_FLAG_SELECTABLE) {
		head = tail = widget;
	} else if (widget->type == UI_WIDGET_TYPE_CONTAINER) {
		head = widget->container->child_sel_head;
		tail = widget->container->child_sel_tail;
		if (!head)
			return;
	} else {
		return;
	}

	if (parent_container->child_sel_tail)
		parent_container->child_sel_tail->next_sel = head;
	else
		parent_container->child_sel_head = head;

	parent_container->child_sel_tail = tail;
	tail->next_sel = head;
}

void ui_widget_draw(struct ui_widget *widget, bool parent_dirty)
{
	if (!(parent_dirty || widget->flags & (UI_WIDGET_FLAG_DIRTY | UI_WIDGET_FLAG_SUBTREE_DIRTY)))
		return;

	uint8_t x, y;
	struct ui_rect *bb = &widget->bb;

	if (widget->flags & UI_WIDGET_FLAG_DIRTY || parent_dirty) {
		switch (widget->type) {
		case UI_WIDGET_TYPE_RECT:
			display_fill_rect(bb->x, bb->y, bb->w, bb->h, widget->rect->fill);
			break;

		case UI_WIDGET_TYPE_IMAGE:
			display_fill_rect(bb->x, bb->y, bb->w, bb->h, 0x00);
			x = bb->x + (bb->w - widget->image->size.w) / 2;
			y = bb->y + (bb->h - widget->image->size.h) / 2;
			display_draw_bitmap(x, y, widget->image->size.w, widget->image->size.h,
								widget->image->data);
			break;

		case UI_WIDGET_TYPE_TEXT:
			display_fill_rect(bb->x, bb->y, bb->w, bb->h, 0x00);
			const struct font *font = widget->text->font;
			uint8_t idx, char_x = 0;
			x = bb->x + SSUB(bb->w, widget->min_size.w) / 2;
			y = bb->y + SSUB(bb->h, widget->min_size.h) / 2;
			for (size_t i = 0; i < strlen(widget->text->str); i++) {
				idx = widget->text->str[i] - font->first_char;
				display_draw_bitmap(x + char_x + font->glyphs[idx].x_offset, y,
									font->glyphs[idx].width, MIN(bb->h, font->height),
									&font->bitmap[font->glyphs[idx].bitmap_offset]);
				char_x += font->glyphs[idx].x_advance;
				if (char_x >= bb->w)
					break;
			}
			break;

		case UI_WIDGET_TYPE_CONTAINER:
			display_fill_rect(bb->x, bb->y, bb->w, bb->h, widget->container->fill);
			break;

		default:
			break;
		}
	}

	if (widget->type == UI_WIDGET_TYPE_CONTAINER) {
		if (widget->container->layout == UI_LAYOUT_STACK) {
			ui_widget_draw(widget->container->children[widget->container->active_child],
						   (widget->flags & UI_WIDGET_FLAG_DIRTY) || parent_dirty);
		} else {
			for (int i = 0; i < widget->container->child_count; i++)
				ui_widget_draw(widget->container->children[i],
							   (widget->flags & UI_WIDGET_FLAG_DIRTY) || parent_dirty);
		}

		widget->flags &= ~UI_WIDGET_FLAG_SUBTREE_DIRTY;
	}

	widget->flags &= ~UI_WIDGET_FLAG_DIRTY;
}

void ui_widget_invert(struct ui_widget *widget)
{
	display_region_invert(widget->bb.x, widget->bb.y, widget->bb.w, widget->bb.h);
}

static void set_widget_dirty(struct ui_widget *widget)
{
	widget->flags |= UI_WIDGET_FLAG_DIRTY;

	while (widget->parent) {
		widget = widget->parent;
		if (widget->flags & UI_WIDGET_FLAG_SUBTREE_DIRTY)
			break;
		widget->flags |= UI_WIDGET_FLAG_SUBTREE_DIRTY;
	}
}

void ui_widget_update_rect(struct ui_widget *widget, uint8_t fill)
{
	BW_ASSERT(widget->type == UI_WIDGET_TYPE_RECT,
			  "Expected rect type widget ui_widget_update_rect");

	widget->rect->fill = fill;
	set_widget_dirty(widget);
}

void ui_widget_update_image(struct ui_widget *widget, struct bmp *bmp)
{
	BW_ASSERT(widget->type == UI_WIDGET_TYPE_IMAGE,
			  "Expected rect type widget in ui_widget_update_image");

	widget->image = bmp;
	set_widget_dirty(widget);
}

void ui_widget_update_text(struct ui_widget *widget, const char *str)
{
	BW_ASSERT(widget->type == UI_WIDGET_TYPE_TEXT,
			  "Expected rect type widget in ui_widget_update_text");

	widget->text->str = str;
	const struct font *font = widget->text->font;

	uint8_t idx, min_w = 0, min_h = font->height;
	for (size_t i = 0; i < strlen(widget->text->str); i++) {
		idx = widget->text->str[i] - font->first_char;
		min_w += font->glyphs[idx].x_advance;
	}

	widget->min_size = (struct ui_size){ min_w, min_h };

	set_widget_dirty(widget);
}

void ui_widget_set_active_child(struct ui_widget *widget, uint8_t active_child_idx)
{
	BW_ASSERT(widget->type == UI_WIDGET_TYPE_CONTAINER &&
				  widget->container->layout == UI_LAYOUT_STACK,
			  "Expected stack container widget in ui_widget_set_active_child");

	widget->container->active_child = active_child_idx;
	struct ui_widget *active_child = widget->container->children[active_child_idx];
	if (active_child->type == UI_WIDGET_TYPE_CONTAINER) {
		widget->container->child_sel_head = active_child->container->child_sel_head;
		widget->container->child_sel_tail = active_child->container->child_sel_tail;
	}
	set_widget_dirty(widget);
}
