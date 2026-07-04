#ifndef UI_WIDGET_H
#define UI_WIDGET_H

#include "assets/bitmaps/bitmaps.h"
#include "assets/fonts/font.h"
#include "ui/ui_common.h"
#include "utils/containers/clist.h"
#include <stdbool.h>
#include <stdint.h>

#define MAX_CONTAINER_CHILDREN 8

struct ui_widget;
typedef struct ui_widget ui_widget_t;

typedef enum
{
    UI_WIDGET_TYPE_RECT,
    UI_WIDGET_TYPE_IMAGE,
    UI_WIDGET_TYPE_TEXT,
    UI_WIDGET_TYPE_CONTAINER
} ui_widget_type_t;

typedef struct
{
    uint8_t fill;
} ui_rect_data_t;

typedef struct
{
    const char *str;
    const font_t *font;
} ui_text_data_t;

typedef enum
{
    UI_LAYOUT_ROW,
    UI_LAYOUT_COLUMN,
    UI_LAYOUT_GRID
} ui_layout_t;

typedef struct
{
    ui_layout_t layout;
    uint8_t fill;
    uint8_t padding;
    uint8_t spacing;
    ui_size_t grid_size; // only used when layout is grid layout
    ui_widget_t *children[MAX_CONTAINER_CHILDREN];
    uint8_t child_count;
    clist_t selection_list;
} ui_container_data_t;

typedef struct ui_widget
{
    ui_widget_type_t type;
    ui_widget_t *parent;
    bool fit_contents;
    bool selectable;
    ui_size_t min_size;
    ui_rect_t bb;
    uint8_t flex;
    clist_node_t node;

    union
    {
        ui_rect_data_t *rect;
        const bmp_t *image;
        ui_text_data_t *text;
        ui_container_data_t *container;
    };
} ui_widget_t;

ui_widget_t *ui_widget_create_rect(uint8_t flex, bool selectable, uint8_t fill);
ui_widget_t *ui_widget_create_image(uint8_t flex, bool selectable, bool fit_contents, const bmp_t *bitmap);
ui_widget_t *ui_widget_create_text(uint8_t flex, bool selectable, bool fit_contents, const char *str, const font_t *font);
ui_widget_t *ui_widget_create_row(uint8_t flex, bool selectable, uint8_t fill, uint8_t padding, uint8_t spacing);
ui_widget_t *ui_widget_create_col(uint8_t flex, bool selectable, uint8_t fill, uint8_t padding, uint8_t spacing);
ui_widget_t *ui_widget_create_grid(uint8_t flex, bool selectable, uint8_t fill, uint8_t padding, uint8_t spacing,
                                   uint8_t rows, uint8_t cols);
void ui_container_add_child(ui_widget_t *widget, ui_widget_t *child);
void ui_widget_build(ui_widget_t *widget);
void ui_widget_draw(ui_widget_t *widget);
void ui_widget_invert(ui_widget_t *widget);

#endif
