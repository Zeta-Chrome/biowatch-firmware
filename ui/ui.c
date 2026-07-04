#include "display/display.h"
#include "ui.h"
#include "ui/widget.h"
#include "utils/assert.h"
#include "utils/containers/clist.h"
#include <stddef.h>

static ui_widget_t *g_root_widget;
static bool g_redraw_page = true;
static ui_widget_t *g_selected_parent = NULL;
static clist_node_t *g_selected_node = NULL;

void ui_init()
{
    display_init(true, true);
}

void ui_build(ui_widget_t *widget)
{
    widget->bb = (ui_rect_t){0, 0, 128, 64};
    ui_widget_build(widget);
}

void ui_set_root_widget(ui_widget_t *widget)
{
    BW_ASSERT(widget->parent == NULL, "Expected root widget to have no parent\n");

    g_root_widget = widget;
    g_selected_parent = widget;
    g_redraw_page = true;
}

void ui_draw()
{
    if (g_redraw_page)
    {
        ui_widget_draw(g_root_widget);
        g_redraw_page = false;
    }
    display_flush();
}

void ui_select_next()
{
    if (g_selected_node)
    {
        ui_widget_invert((ui_widget_t *)g_selected_node->data);
        g_selected_node = g_selected_node->next;
    }
    else
    {
        g_selected_node = g_selected_parent->container->selection_list.head;
    }

    if (g_selected_node)
    {
        ui_widget_invert((ui_widget_t *)g_selected_node->data);
    }
}

void ui_select_parent()
{
    g_selected_parent = g_selected_parent->parent;
    g_selected_node = NULL;
    ui_select_next();
}

void ui_unselect()
{
    if (g_selected_node)
    {
        ui_widget_invert((ui_widget_t *)g_selected_node->data);
        g_selected_node = NULL;
    }
}
