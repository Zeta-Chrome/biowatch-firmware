#include "drivers/display/display.h"
#include "lib/assert.h"
#include "ui.h"
#include "widget.h"
#include <stddef.h>

static struct ui_widget *g_root_widget = NULL;
static struct ui_widget *g_next_sel_widget = NULL;
static struct ui_widget *g_prev_sel_widget = NULL;

void ui_init()
{
	display_init(true, true);
}

void ui_change_page(uint8_t page)
{
	ui_widget_set_active_child(g_root_widget, page);
	ui_unselect();
}

void ui_build(struct ui_widget *widget)
{
	BW_ASSERT(widget->parent == NULL, "Expected root widget to have no parent\n");

	g_root_widget = widget;
	g_root_widget->flags |= UI_WIDGET_FLAG_DIRTY | UI_WIDGET_FLAG_SUBTREE_DIRTY;

	widget->bb = (struct ui_rect){ 0, 0, 128, 64 };
	ui_widget_build(widget);
}

void ui_draw()
{
	if (!g_root_widget) {
		BW_LOG("Root widget not set\n");
		return;
	}

	// Unselect before recusive drawing to prevent overwrites to child
	if (g_prev_sel_widget &&
		(g_next_sel_widget != g_prev_sel_widget ||
		 g_root_widget->flags & (UI_WIDGET_FLAG_DIRTY | UI_WIDGET_FLAG_SUBTREE_DIRTY))) {
		ui_widget_invert(g_prev_sel_widget);
		g_prev_sel_widget = NULL;
	}

	ui_widget_draw(g_root_widget, g_root_widget->flags & UI_WIDGET_FLAG_DIRTY);

	// Select after recursive drawing
	if (g_next_sel_widget && g_next_sel_widget != g_prev_sel_widget) {
		ui_widget_invert(g_next_sel_widget);
		g_prev_sel_widget = g_next_sel_widget;
	}

	display_flush();
}

void ui_debug_print(struct ui_widget *widget, uint8_t level)
{
	static const char *wtypes[4] = { "RECT", "IMAGE", "TEXT", "CONTAINER" };
	static const char *layouts[4] = { "ROW", "COLUMN", "GRID", "STACK" };

	for (int i = 0; i < level; i++)
		BW_PRINT("  ");

	BW_PRINT("%d. %p - %s WIDGET, (x: %d, y: %d, w: %d, h: %d), SEL: %d, NEXT: %p\n", level, widget,
			 wtypes[widget->type], widget->bb.x, widget->bb.y, widget->bb.w, widget->bb.h,
			 widget->flags & UI_WIDGET_FLAG_SELECTABLE, widget->next_sel);

	if (widget->type == UI_WIDGET_TYPE_CONTAINER) {
		for (int i = 0; i < level; i++)
			BW_PRINT("  ");

		BW_PRINT("%s CONTAINER, HEAD: %p, TAIL: %p\n", layouts[widget->container->layout],
				 widget->container->child_sel_head, widget->container->child_sel_tail);
		for (int i = 0; i < widget->container->child_count; i++)
			ui_debug_print(widget->container->children[i], level + 1);
	}
}

struct ui_widget *ui_get_selected()
{
	return g_next_sel_widget;
}

void ui_select(struct ui_widget *widget)
{
	g_next_sel_widget = widget;

	if (g_next_sel_widget && g_next_sel_widget->on_select)
		g_next_sel_widget->on_select(g_next_sel_widget->user_data);
}

void ui_unselect()
{
	if (g_next_sel_widget) {
		ui_widget_invert(g_next_sel_widget);
		g_prev_sel_widget = NULL;
		g_next_sel_widget = NULL;
	}
}

void ui_select_next()
{
	if (!g_root_widget) {
		BW_LOG("Root widget not set\n");
		return;
	}

	if (g_next_sel_widget == NULL) {
		g_next_sel_widget = g_root_widget->container->child_sel_head;
		return;
	}

	if (g_next_sel_widget->next_sel == NULL) {
		g_next_sel_widget = NULL;
		return;
	}

	ui_select(g_next_sel_widget->next_sel);
}

void ui_select_child()
{
	if (!g_root_widget) {
		BW_LOG("Root widget not set\n");
		return;
	}

	if (g_next_sel_widget->type != UI_WIDGET_TYPE_CONTAINER) {
		return;
	}

	if (g_next_sel_widget->container->child_sel_head == NULL)
		return;

	ui_select(g_next_sel_widget->container->child_sel_head);
}

void ui_click_selection()
{
	if (g_next_sel_widget && g_next_sel_widget->on_click)
		g_next_sel_widget->on_click(g_next_sel_widget->user_data);
}
