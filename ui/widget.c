#include "display/display.h"
#include "ui/ui_common.h"
#include "utils/utils.h"
#include "widget.h"
#include <string.h>

#define MAX_RECT_WIDGETS 16
#define MAX_IMAGE_WIDGETS 16
#define MAX_TEXT_WIDGETS 16
#define MAX_CONTAINER_WIDGETS 16
#define MAX_WIDGETS 64

static ui_widget_t g_widget_pool[MAX_WIDGETS];
static ui_rect_data_t g_rect_pool[MAX_RECT_WIDGETS];
static ui_text_data_t g_text_pool[MAX_TEXT_WIDGETS];
static ui_container_data_t g_container_pool[MAX_CONTAINER_WIDGETS];

static ui_widget_t *alloc_widget()
{
    static uint8_t idx = 0;
    BW_ASSERT(idx < MAX_WIDGETS, "Exhausted the widget pool\n");
    return &g_widget_pool[idx++];
}

static ui_rect_data_t *alloc_rect_data()
{
    static uint8_t idx = 0;
    BW_ASSERT(idx < MAX_RECT_WIDGETS, "Exhausted the rect data pool\n");
    return &g_rect_pool[idx++];
}

static ui_text_data_t *alloc_text_data()
{
    static uint8_t idx = 0;
    BW_ASSERT(idx < MAX_TEXT_WIDGETS, "Exhausted the text data pool\n");
    return &g_text_pool[idx++];
}

static ui_container_data_t *alloc_container_data()
{
    static uint8_t idx = 0;
    BW_ASSERT(idx < MAX_CONTAINER_WIDGETS, "Exhausted the container data pool\n");
    return &g_container_pool[idx++];
}

ui_widget_t *ui_widget_create_rect(uint8_t flex, bool selectable, uint8_t fill)
{
    ui_widget_t *widget = alloc_widget();
    widget->type = UI_WIDGET_TYPE_RECT;
    widget->flex = flex;
    widget->selectable = selectable;
    widget->fit_contents = false;
    widget->node.data = widget;
    widget->rect = alloc_rect_data();
    widget->rect->fill = fill;

    return widget;
}

ui_widget_t *ui_widget_create_image(uint8_t flex, bool selectable, bool fit_contents, const bmp_t *bitmap)
{
    ui_widget_t *widget = alloc_widget();
    widget->type = UI_WIDGET_TYPE_IMAGE;
    widget->flex = flex;
    widget->selectable = selectable;
    widget->fit_contents = fit_contents;
    widget->min_size = bitmap->size;
    widget->node.data = widget;
    widget->image = bitmap;

    return widget;
}

ui_widget_t *ui_widget_create_text(uint8_t flex, bool selectable, bool fit_contents, const char *str, const font_t *font)
{
    ui_widget_t *widget = alloc_widget();
    widget->type = UI_WIDGET_TYPE_TEXT;
    widget->flex = flex;
    widget->selectable = selectable;
    widget->fit_contents = fit_contents;
    widget->node.data = widget;
    widget->text = alloc_text_data();
    widget->text->str = str;
    widget->text->font = font;

    uint8_t idx, min_w = 0, min_h = font->height;
    for (size_t i = 0; i < strlen(widget->text->str); i++)
    {
        idx = widget->text->str[i] - font->first_char;
        min_w += font->glyphs[idx].x_advance;
    }

    widget->min_size = (ui_size_t){min_w, min_h};

    return widget;
}

ui_widget_t *ui_widget_create_row(uint8_t flex, bool selectable, uint8_t fill, uint8_t padding, uint8_t spacing)
{
    ui_widget_t *widget = alloc_widget();
    widget->type = UI_WIDGET_TYPE_CONTAINER;
    widget->flex = flex;
    widget->selectable = selectable;
    widget->fit_contents = false;
    widget->node.data = widget;
    widget->container = alloc_container_data();
    widget->container->layout = UI_LAYOUT_ROW;
    widget->container->fill = fill;
    widget->container->padding = padding;
    widget->container->spacing = spacing;
    widget->container->padding = padding;

    return widget;
}

ui_widget_t *ui_widget_create_col(uint8_t flex, bool selectable, uint8_t fill, uint8_t padding, uint8_t spacing)
{
    ui_widget_t *widget = alloc_widget();
    widget->type = UI_WIDGET_TYPE_CONTAINER;
    widget->flex = flex;
    widget->selectable = selectable;
    widget->fit_contents = false;
    widget->node.data = widget;
    widget->container = alloc_container_data();
    widget->container->layout = UI_LAYOUT_COLUMN;
    widget->container->fill = fill;
    widget->container->padding = padding;
    widget->container->spacing = spacing;
    widget->container->padding = padding;

    return widget;
}

ui_widget_t *ui_widget_create_grid(uint8_t flex, bool selectable, uint8_t fill, uint8_t padding, uint8_t spacing,
                                   uint8_t rows, uint8_t cols)
{
    ui_widget_t *widget = alloc_widget();
    widget->type = UI_WIDGET_TYPE_CONTAINER;
    widget->flex = flex;
    widget->selectable = selectable;
    widget->fit_contents = false;
    widget->node.data = widget;
    widget->container = alloc_container_data();
    widget->container->layout = UI_LAYOUT_GRID;
    widget->container->fill = fill;
    widget->container->padding = padding;
    widget->container->spacing = spacing;
    widget->container->padding = padding;
    widget->container->grid_size = (ui_size_t){cols, rows};

    return widget;
}

void ui_container_add_child(ui_widget_t *widget, ui_widget_t *child)
{
    BW_ASSERT(widget->type == UI_WIDGET_TYPE_CONTAINER, "Expected containter widget in ui_container_add_child");
    widget->container->children[widget->container->child_count++] = child;
}

static void build_row_container(ui_widget_t *widget)
{
    ui_widget_t *child;
    uint8_t w, h;
    size_t total_flex = 0;

    for (size_t i = 0; i < widget->container->child_count; i++)
    {
        total_flex += widget->container->children[i]->flex;
    }

    uint8_t used_space = widget->container->padding;
    uint8_t free_space = widget->bb.w
                         - (2 * widget->container->padding
                            + widget->container->spacing * (widget->container->child_count - 1));

    for (size_t i = 0; i < widget->container->child_count; i++)
    {
        child = widget->container->children[i];
        w = free_space * child->flex / total_flex;
        h = widget->bb.h - 2 * widget->container->padding;

        child->bb.x = widget->bb.x + used_space;
        child->bb.y = widget->bb.y + widget->container->padding;

        if (child->fit_contents)
        {
            child->bb.w = MIN(w, child->min_size.w);
            child->bb.h = MIN(h, child->min_size.h);
            child->bb.x += (w - child->bb.w) / 2;
            child->bb.y += (h - child->bb.h) / 2;
        }
        else
        {
            child->bb.w = w;
            child->bb.h = h;
        }

        used_space += child->bb.w + widget->container->spacing;
        ui_widget_build(child);
    }
}

static void build_col_container(ui_widget_t *widget)
{
    ui_widget_t *child;
    uint8_t w, h;
    size_t total_flex = 0;
    for (size_t i = 0; i < widget->container->child_count; i++)
    {
        total_flex += widget->container->children[i]->flex;
    }

    uint8_t used_space = widget->container->padding;
    uint8_t free_space = widget->bb.h
                         - (2 * widget->container->padding
                            + widget->container->spacing * (widget->container->child_count - 1));

    for (size_t i = 0; i < widget->container->child_count; i++)
    {
        child = widget->container->children[i];
        w = widget->bb.w - 2 * widget->container->padding;
        h = free_space * child->flex / total_flex;

        child->bb.x = widget->bb.x + widget->container->padding;
        child->bb.y = widget->bb.y + used_space;

        if (child->fit_contents)
        {
            child->bb.w = MIN(w, child->min_size.w);
            child->bb.h = MIN(h, child->min_size.h);
            child->bb.x += (w - child->bb.w) / 2;
            child->bb.y += (h - child->bb.h) / 2;
        }
        else
        {
            child->bb.w = w;
            child->bb.h = h;
        }

        used_space += child->bb.h + widget->container->spacing;
        ui_widget_build(child);
    }
}

static void build_grid_container(ui_widget_t *widget)
{
    ui_widget_t *child;
    uint8_t row, col;
    uint8_t w = (widget->bb.w - 2 * widget->container->padding
                 - (widget->container->grid_size.w - 1) * widget->container->spacing)
                / widget->container->grid_size.w;
    uint8_t h = (widget->bb.h - 2 * widget->container->padding
                 - (widget->container->grid_size.h - 1) * widget->container->spacing)
                / widget->container->grid_size.h;

    for (size_t i = 0; i < widget->container->child_count; i++)
    {
        col = i % widget->container->grid_size.w;
        row = i / widget->container->grid_size.w;
        if (row == widget->container->grid_size.h)
        {
            break;
        }

        child = widget->container->children[i];
        child->bb.x = widget->bb.x + widget->container->padding + col * (w + widget->container->spacing);
        child->bb.y = widget->bb.y + widget->container->padding + row * (h + widget->container->spacing);

        if (child->fit_contents)
        {
            child->bb.w = MIN(w, child->min_size.w);
            child->bb.h = MIN(h, child->min_size.h);
            child->bb.x += (w - child->bb.w) / 2;
            child->bb.y += (h - child->bb.h) / 2;
        }
        else
        {
            child->bb.w = w;
            child->bb.h = h;
        }

        ui_widget_build(child);
    }
}

void ui_widget_build(ui_widget_t *widget)
{
    BW_PRINT("(%d, %d, %d, %d)\n", widget->bb.x, widget->bb.y, widget->bb.w, widget->bb.h);

    if (widget->type == UI_WIDGET_TYPE_CONTAINER)
    {
        ui_widget_t *child;
        for (size_t i = 0; i < widget->container->child_count; i++)
        {
            child = widget->container->children[i];
            child->parent = widget;
        }

        switch (widget->container->layout)
        {
        case UI_LAYOUT_ROW:
        {
            build_row_container(widget);
            break;
        }

        case UI_LAYOUT_COLUMN:
        {
            build_col_container(widget);
            break;
        }

        case UI_LAYOUT_GRID:
        {
            build_grid_container(widget);
            break;
        }
        }

        // Find the selection parent
        ui_widget_t *sparent = widget;
        while (!(sparent->parent == NULL || sparent->selectable))
        {
            sparent = sparent->parent;
        }

        // Populate the selection list and the parent
        for (size_t i = 0; i < widget->container->child_count; i++)
        {
            child = widget->container->children[i];
            if (child->selectable)
            {
                clist_push_back(&sparent->container->selection_list, &child->node);
            }
        }
    }
}

void ui_widget_draw(ui_widget_t *widget)
{
    uint8_t x, y;
    ui_rect_t *bb = &widget->bb;

    switch (widget->type)
    {
    case UI_WIDGET_TYPE_RECT:
        display_fill_rect(bb->x, bb->y, bb->w, bb->h, widget->rect->fill);
        break;

    case UI_WIDGET_TYPE_IMAGE:
        display_fill_rect(bb->x, bb->y, bb->w, bb->h, 0x00);
        x = bb->x + (bb->w - widget->image->size.w) / 2;
        y = bb->y + (bb->h - widget->image->size.h) / 2;
        display_draw_bitmap(x, y, widget->image->size.w, widget->image->size.h, widget->image->data);
        break;

    case UI_WIDGET_TYPE_TEXT:
        display_fill_rect(bb->x, bb->y, bb->w, bb->h, 0x00);
        const font_t *font = widget->text->font;
        uint8_t idx, char_x = 0;
        x = bb->x + (bb->w - widget->min_size.w) / 2;
        y = bb->y + (bb->h - widget->min_size.h) / 2;
        for (size_t i = 0; i < strlen(widget->text->str); i++)
        {
            idx = widget->text->str[i] - font->first_char;
            display_draw_bitmap(x + char_x + font->glyphs[idx].x_offset, y, font->glyphs[idx].width,
                                MIN(bb->h, font->height), &font->bitmap[font->glyphs[idx].bitmap_offset]);
            char_x += font->glyphs[idx].x_advance;
            if (char_x >= bb->w)
            {
                break;
            }
        }
        break;

    case UI_WIDGET_TYPE_CONTAINER:
        display_fill_rect(bb->x, bb->y, bb->w, bb->h, widget->container->fill);
        for (int i = 0; i < widget->container->child_count; i++)
        {
            ui_widget_draw(widget->container->children[i]);
        }
        break;

    default:
        return;
    }
}

void ui_widget_invert(ui_widget_t *widget)
{
    display_region_invert(widget->bb.x, widget->bb.y, widget->bb.w, widget->bb.h);
}
