// Rich text of the message dialog and the tooltips: text with '@' codes (paragraph, new line, image, link to a
// message) wrapped to a box width, drawn or only measured, with scrolling and the link boxes around linked words.
//
// Measuring and drawing differ in two places:
// - a code inside a word measures the character after the '@', and a link counts the '@' and its digits;
// - an image met after the first line is drawn one line later.

#include "graphics/rich_text.h"
#include "core/text.h"
#include "graphics/color.h"
#include "graphics/graphics_data.h"
#include "graphics/graphics_display.h"
#include "graphics/image_collection.h"
#include "graphics/image_draw.h"
#include "graphics/text_draw.h"
#include "input/input_data.h"
#include "widget/widget_data.h"
#include "window/window_data.h"
#include "window/window_image_group.h"
#include <stdint.h>
#include <string.h>

int32_t rich_num_links;

int32_t rich_link_write;

rich_link rich_links[50];

enum rich_text_code {
    RICH_TEXT_CODE = '@',
    RICH_TEXT_CODE_PARAGRAPH = 'P',
    RICH_TEXT_CODE_LINE = 'L',
    RICH_TEXT_CODE_IMAGE = 'G',
};

enum rich_text_metric {
    RICH_TEXT_SPACE = 4,
    RICH_TEXT_UNKNOWN_ADVANCE = 6,
    RICH_TEXT_SMALL_FONT_SPACING = 2,
    RICH_TEXT_LINE_HEIGHT = 11,
    RICH_TEXT_TALL_LINE_HEIGHT = 16,
    RICH_TEXT_LINE_PIXELS = 16,
    RICH_TEXT_PARAGRAPH_INDENT = 50,
    RICH_TEXT_IMAGE_TOP = 8,
    RICH_TEXT_IMAGE_INSET = 4,
    RICH_TEXT_DIALOG_BLOCK = 16,
    RICH_TEXT_LINK_MARGIN_X = 2,
    RICH_TEXT_LINK_TOP = -1,
    RICH_TEXT_LINK_BOTTOM = 13,
    RICH_TEXT_IMAGE_EXTRA_LINES = 2,
};

enum rich_text_line_limit {
    RICH_TEXT_MAX_LINES = 999,
    RICH_TEXT_WORD_MAX_CHARS = 2000,
};

enum rich_text_letter {
    RICH_TEXT_LETTER_TALL_FIRST = 0x60,
    RICH_TEXT_LETTER_COMMA = 12,
    RICH_TEXT_LETTER_PERIOD = 14,
};

typedef struct rich_text_layout {
    const uint8_t *p;
    int32_t box_width;
    int32_t measure_only;
    int32_t has_more;
    int32_t image_lines;
    int32_t pending_image;
    int32_t lines_before_image;
    int32_t paragraph;
    int32_t line;
} rich_text_layout;

typedef struct rich_text_line {
    int32_t x_indent;
    int32_t width;
    int32_t length;
    int32_t skip_space;
} rich_text_line;

static int32_t is_digit(uint8_t c)
{
    return c >= '0' && c <= '9';
}

static int32_t image_line_count(int32_t image_id)
{
    return (image_header_get(image_id)->height / RICH_TEXT_LINE_PIXELS) + RICH_TEXT_IMAGE_EXTRA_LINES;
}

void rich_text_clear_links(void)
{
    for (int32_t i = 0; i < RICH_TEXT_MAX_LINKS; i++) {
        rich_link *link = &rich_links[i];
        link->x_start = 0;
        link->x_end = 0;
        link->y_top = 0;
        link->y_bottom = 0;
        link->message_id = 0;
    }
    rich_link_write = 0;
    rich_num_links = 0;
}

void rich_text_add_link(int32_t message_id, int32_t x_start, int32_t x_end, int32_t y)
{
    if (rich_num_links >= RICH_TEXT_MAX_LINKS) {
        return;
    }
    rich_link *link = &rich_links[rich_link_write];
    link->x_start = x_start - RICH_TEXT_LINK_MARGIN_X;
    link->x_end = x_end + RICH_TEXT_LINK_MARGIN_X;
    link->y_top = y + RICH_TEXT_LINK_TOP;
    link->y_bottom = y + RICH_TEXT_LINK_BOTTOM;
    link->message_id = message_id;
    rich_num_links++;
    rich_link_write++;
}

int32_t rich_text_get_clicked_link(void)
{
    if (!mouse_left_went_down) {
        return -1;
    }
    int32_t count = rich_num_links;
    for (int32_t i = 0; i < count; i++) {
        const rich_link *link = &rich_links[i];
        if (link->x_start <= mouse_x && mouse_x <= link->x_end && link->y_top <= mouse_y && mouse_y <= link->y_bottom) {
            return link->message_id;
        }
    }
    return -1;
}

int32_t rich_text_get_word_width(const uint8_t *str)
{
    int32_t width = 0;
    g_word_chars = 0;
    int32_t word_seen = 0;
    for (int32_t chars = 1; chars < RICH_TEXT_WORD_MAX_CHARS; chars++) {
        uint8_t c = *str;
        const uint8_t *next = str + 1;
        if (c == 0) {
            return width;
        }
        if (c == RICH_TEXT_CODE) {
            c = *next;
            next = str + 2;
            if (!word_seen) {
                if (c == RICH_TEXT_CODE_PARAGRAPH || c == RICH_TEXT_CODE_LINE) {
                    g_word_chars += 2;
                    return 0;
                }
                if (c == RICH_TEXT_CODE_IMAGE) {
                    g_word_chars += 2;
                    c = str[2];
                    next = str + 3;
                    while (is_digit(c)) {
                        g_word_chars++;
                        c = *next++;
                    }
                    return 0;
                }
                g_word_chars++;
                while (is_digit(c)) {
                    c = *next++;
                    g_word_chars++;
                }
            }
        }
        if (c == ' ') {
            if (word_seen) {
                return width;
            }
            width += RICH_TEXT_SPACE;
        } else if (c >= ' ') {
            width += text_get_letter_width(c, rich_text_normal_font);
            word_seen = 1;
        }
        g_word_chars++;
        str = next;
    }
    return width;
}

int32_t rich_text_draw_character(int32_t font, uint8_t letter_index, int32_t x, int32_t y, int32_t measure_only, int32_t large_line)
{
    g_last_image_id = text_letter_image_id(letter_index);
    if (g_last_image_id == 0) {
        return 0;
    }
    int32_t image_id = (g_last_image_id - 1) + font;
    int32_t line_height = (large_line == 0) ? RICH_TEXT_LINE_HEIGHT : RICH_TEXT_TALL_LINE_HEIGHT;
    int32_t raise = image_header_get(image_id)->height - line_height;

    if (raise < 0) {
        raise = 0;
    }

    if (letter_index < RICH_TEXT_LETTER_TALL_FIRST &&
        letter_index != RICH_TEXT_LETTER_COMMA &&
        letter_index != RICH_TEXT_LETTER_PERIOD) {
        raise = 0;
    }
    if (measure_only == 0) {
        image_draw(image_id, x, y - raise);
    }
    return image_header_get(image_id)->width;
}

void rich_text_draw_line(const uint8_t *str, int32_t x, int32_t y, int32_t measure_only, int32_t large_line)
{
    int32_t link_chars_left = 0;
    uint8_t c = *str;
    while (c != 0) {
        const uint8_t *next = str + 1;
        if (c == RICH_TEXT_CODE) {
            int32_t message_id = string_to_int((const char *) next);
            c = *next;
            next = str + 2;
            while (is_digit(c)) {
                c = *next++;
            }
            int32_t word_width = rich_text_get_word_width(next - 1);
            link_chars_left = g_word_chars + 1;
            rich_text_add_link(message_id, x, x + word_width, y);
        }
        if (c >= ' ') {
            int32_t font = rich_text_normal_font;
            if (link_chars_left > 0) {
                link_chars_left--;
                font = rich_text_link_font;
            }
            uint8_t letter_index = (uint8_t) (c - ' ');
            int32_t advance;
            if (text_letter_image_id(letter_index) == 0) {
                advance = RICH_TEXT_UNKNOWN_ADVANCE;
            } else {
                advance = rich_text_draw_character(font, letter_index, x, y, measure_only, large_line);
                if (font == image_group(IMAGE_GROUP_FONT_SMALL)) {
                    advance += RICH_TEXT_SMALL_FONT_SPACING;
                }
            }
            x += advance;
        }
        str = next;
        c = *str;
    }
}

static void start_image(rich_text_layout *layout, const uint8_t *number, rich_text_line *line)
{
    if (line->length != 0 && layout->measure_only == 1) {
        rich_line_count++;
    }
    int32_t image_number = string_to_int((const char *) number);
    uint8_t d = number[0];
    layout->p = number + 1;
    while (is_digit(d)) {
        d = *layout->p++;
    }
    layout->pending_image = image_group(IMAGE_GROUP_MESSAGE_IMAGES) - 1 + image_number;
    layout->image_lines = image_line_count(layout->pending_image);
    line->width = layout->box_width;
    if (layout->line > 0) {
        layout->lines_before_image = 1;
    }
}

static void copy_word(rich_text_layout *layout, rich_text_line *line, int32_t word_chars)
{
    const uint8_t *q = layout->p;
    for (int32_t copied = 0; copied < word_chars; copied++) {
        uint8_t c = *q;
        if (c == RICH_TEXT_CODE) {
            uint8_t code = q[1];
            if (code == RICH_TEXT_CODE_PARAGRAPH || code == RICH_TEXT_CODE_LINE) {
                if (code == RICH_TEXT_CODE_PARAGRAPH) {
                    layout->paragraph = 1;
                }
                layout->p = q + 2;
                line->width = layout->box_width;
                return;
            }
            if (code == RICH_TEXT_CODE_IMAGE) {
                start_image(layout, q + 2, line);
                return;
            }
        }
        if (!line->skip_space || c != ' ') {
            line->skip_space = 0;
            tmp_line_buffer[line->length++] = (char) c;
        }
        q++;
        layout->p = q;
    }
}

static void fill_line(rich_text_layout *layout, rich_text_line *line)
{
    while ((layout->has_more || layout->image_lines != 0) && line->width < layout->box_width) {
        if (layout->image_lines != 0) {
            layout->image_lines--;
            return;
        }
        int32_t word_width = rich_text_get_word_width(layout->p);
        int32_t word_chars = g_word_chars;
        line->width += word_width;
        if (line->width >= layout->box_width) {
            if (line->width == 0) {
                layout->has_more = 0;
            }
            return;
        }
        copy_word(layout, line, word_chars);
        if (*layout->p == 0) {
            layout->has_more = 0;
        }
    }
}

static void draw_line_with_highlight(int32_t x, int32_t y, int32_t measure_only, int32_t large_line)
{
    rich_text_draw_line((const uint8_t *) tmp_line_buffer, x, y, measure_only, large_line);
    uint16_t saved_color = g_draw_color;
    if (rich_text_normal_font == image_group(IMAGE_GROUP_FONT_SMALL)) {
        g_draw_color = (display_mode == DISPLAY_MODE_RGB565) ? COLOR_LIGHT_YELLOW : COLOR_LIGHT_YELLOW_RGB555;
        rich_text_draw_line((const uint8_t *) tmp_line_buffer, x - 1, y - 1, measure_only, large_line);
    }
    g_draw_color = saved_color;
}

static void place_pending_image(rich_text_layout *layout, int32_t x_offset, int32_t y_offset)
{
    if (layout->pending_image == 0) {
        return;
    }
    if (layout->lines_before_image != 0) {
        layout->lines_before_image--;
        return;
    }
    int32_t image_id = layout->pending_image;
    layout->image_lines = image_line_count(image_id);
    if (layout->line < rich_scroll_pos + rich_viewport_lines) {
        int32_t image_y = y_offset + RICH_TEXT_IMAGE_TOP;
        if (layout->line < rich_scroll_pos) {
            image_y = ((layout->line - rich_scroll_pos) * RICH_TEXT_LINE_PIXELS) + RICH_TEXT_IMAGE_TOP + y_offset;
        }
        int32_t free_width = (dialog_text_width_blocks * RICH_TEXT_DIALOG_BLOCK) - image_header_get(image_id)->width - RICH_TEXT_IMAGE_INSET;
        image_draw(image_id, (free_width / 2) - RICH_TEXT_IMAGE_INSET + x_offset, image_y);
    }
    layout->pending_image = 0;
}

void rich_text_draw(const uint8_t *text, int32_t x_offset, int32_t y_offset, int32_t box_width, int32_t measure_only, int32_t large_line)
{
    rich_text_layout layout = { text, box_width, measure_only, 1, 0, 0, 0, 0, 0 };
    if (measure_only == 1) {
        rich_line_count = 0;
    }
    for (int32_t lines = 1; lines <= RICH_TEXT_MAX_LINES && (layout.has_more || layout.image_lines != 0); lines++) {
        memset(tmp_line_buffer, 0, TEXT_LINE_BUFFER_BYTES);
        rich_text_line line = { layout.paragraph ? RICH_TEXT_PARAGRAPH_INDENT : 0, 0, 0, 1 };
        line.width = line.x_indent;
        layout.paragraph = 0;
        fill_line(&layout, &line);
        g_hash_mode = TEXT_HASH_SUBSTITUTE;
        g_text_width = 0;
        g_multiline_flag = 1;
        int32_t outside = 0;
        if (measure_only != 0) {
            draw_line_with_highlight(line.x_indent + x_offset, y_offset, measure_only, large_line);
        } else {
            outside = (layout.line < rich_scroll_pos) || (layout.line >= rich_scroll_pos + rich_viewport_lines);
            if (!outside) {
                draw_line_with_highlight(line.x_indent + x_offset, y_offset, measure_only, large_line);
            }
            place_pending_image(&layout, x_offset, y_offset);
        }
        if (g_max_line_width < g_text_width) {
            g_max_line_width = g_text_width;
        }
        layout.line++;
        if (measure_only == 1) {
            rich_line_count++;
        }
        if (!outside) {
            y_offset += RICH_TEXT_LINE_PIXELS;
            g_draw_y_accum += RICH_TEXT_LINE_PIXELS;
        }
    }
}
