// The message dialog: its background (a manual page or city message in a panel, or a message with a video or an
// image), foreground, input, scroll bar and close. A city message shows its date and its subject by kind, and an
// advisor's request is answered by up to three phrase buttons.
//
// Every draw of the manual's first page writes its image position into its message record.

#include "window/message_dialog.h"
#include "city/city_player.h"
#include "city/message.h"
#include "city/resource_type.h"
#include "core/calc.h"
#include "empire/empire.h"
#include "graphics/arrow_buttons.h"
#include "graphics/button_border.h"
#include "graphics/color.h"
#include "graphics/generic_buttons.h"
#include "graphics/graphics_data.h"
#include "graphics/graphics_primitives.h"
#include "graphics/image_buttons.h"
#include "graphics/image_collection.h"
#include "graphics/image_draw.h"
#include "graphics/panel_draw.h"
#include "graphics/rich_text.h"
#include "graphics/text_draw.h"
#include "input/input_data.h"
#include "map/grid.h"
#include "scenario/event_helpers.h"
#include "scenario/scenario_data.h"
#include "scenario/scenario_internal.h"
#include "text/event_text.h"
#include "text/lang.h"
#include "text/lang_message.h"
#include "text/text_data.h"
#include "video/smacker.h"
#include "widget/city_draw_view.h"
#include "widget/sidebar.h"
#include "window/window_id.h"
#include "window/window_image_group.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int32_t graphics_mode_setting;

int32_t dialog_header_offset;

int32_t dialog_subtitle_width;

int32_t dialog_y;

enum message_dialog_text {
    MESSAGE_DIALOG_TEXT_GROUP = 12,
    MESSAGE_DIALOG_TEXT_BACK = 0,
    MESSAGE_DIALOG_TEXT_DISASTER_SUBJECT = 1,
    MESSAGE_DIALOG_TEXT_DUE_IN = 2,
    MESSAGE_DIALOG_TEXT_LOW_MOOD_CAUSE_FIRST = 2,
    MESSAGE_LIST_TEXT_GROUP = 63,
    MESSAGE_LIST_TEXT_FROM = 5,
    MONTH_TEXT_GROUP = 25,
    DISASTER_TEXT_GROUP = 41,
    AMOUNT_TEXT_GROUP = 8,
    AMOUNT_TEXT_PLAIN = 0,
    AMOUNT_TEXT_MONTHS = 4,
    PHRASE_TEXT_FIRST = 207,
    RESOURCE_BLOCKS_TEXT_OFFSET = 54,
    MESSAGE_DISASTER_WITH_AMOUNT = 251,
    LOW_MOOD_CAUSES = 5,
};

enum message_dialog_tooltip {
    MESSAGE_DIALOG_TOOLTIP_BACK = 10,
    MESSAGE_DIALOG_TOOLTIP_CLOSE = 11,
    MESSAGE_DIALOG_TOOLTIP_SCROLL_UP = 12,
    MESSAGE_DIALOG_TOOLTIP_SCROLL_DOWN = 13,
    MESSAGE_DIALOG_TOOLTIP_GO_TO = 15,
};

enum message_dialog_phrase {
    PHRASE_CONTENT = 0,
    PHRASE_TITLE = 1,
    PHRASE_SUBTITLE = 2,
    PHRASE_REQUEST_AMOUNT = 4,
    PHRASE_REQUEST_RESOURCE = 5,
    PHRASE_REQUEST_MONTHS = 6,
    PHRASE_BUTTONS = 3,
    PHRASE_FLAG_FIRST = 1,
    PHRASE_FLAG_SECOND = 2,
    PHRASE_FLAG_THIRD = 4,
};

enum message_dialog_layout {
    MESSAGE_CONTENT_LENGTH = 1000,
    MESSAGE_TITLE_LENGTH = 200,
    MESSAGE_BLOCK = 16,
    MESSAGE_IMAGES_HIGH = 1000,
    MESSAGE_VIDEO_WIDTH = 466,
    MESSAGE_VIDEO_HEIGHT = 239,
    MESSAGE_VIDEO_MARKER = '@',
    MESSAGE_SCROLL_THUMB = 39,
    MESSAGE_HISTORY_MAX = 199,
    MANUAL_FIRST_PAGE = 0,
    MANUAL_FIRST_PAGE_IMAGE_OFFSET = 16,
    GRAPHICS_MODE_LOW = 2,
    DEBEN_ICON = RESOURCE_GOLD,
};

static const city_message *shown_message(void)
{
    return &city_messages[city_message_shown_index];
}

static const scenario_event *scenario_event_at(int32_t index)
{
    return &scenario_events.events[index];
}

static void *advisor_button(void)
{
    switch (city_message_category) {
        case MESSAGE_ADVISOR_LABOR:
            return message_labor_advisor.buttons;
        case MESSAGE_ADVISOR_TRADE:
            return message_trade_advisor.buttons;
        case MESSAGE_ADVISOR_POPULATION:
            return message_population_advisor.buttons;
        case MESSAGE_ADVISOR_IMPERIAL:
            return message_imperial_advisor[0].buttons;
        case MESSAGE_ADVISOR_MILITARY:
            return message_military_advisor[0].buttons;
        case MESSAGE_ADVISOR_HEALTH:
            return message_health_advisor[0].buttons;
        case MESSAGE_ADVISOR_RELIGION:
            return message_religion_advisor[0].buttons;
        default:
            return message_help.buttons;
    }
}

static int32_t resource_counted_in_blocks(int32_t resource)
{
    return resource == RESOURCE_STONE || resource == RESOURCE_LIMESTONE || resource == RESOURCE_GRANITE || resource == RESOURCE_SANDSTONE ||
        resource == RESOURCE_MARBLE;
}

static int32_t resource_amount_scale(int32_t resource, int32_t amount)
{
    if (resource_counted_in_blocks(resource) || resource == RESOURCE_CHARIOTS || resource == RESOURCE_WEAPONS || resource == RESOURCE_DEBEN) {
        return amount;
    }
    return amount * 100;
}

static int32_t resource_name_index_offset(int32_t resource, int32_t amount)
{
    if (resource_counted_in_blocks(resource)) {
        return RESOURCE_BLOCKS_TEXT_OFFSET;
    }
    if ((resource == RESOURCE_CHARIOTS || resource == RESOURCE_WEAPONS) && amount > 1) {
        return RESOURCE_BLOCKS_TEXT_OFFSET;
    }
    return 0;
}

static int32_t resource_icon(int32_t resource)
{
    return image_group(IMAGE_GROUP_RESOURCE_ICONS) + (resource == RESOURCE_DEBEN ? DEBEN_ICON : resource);
}

static int32_t phrase_buttons_active(void)
{
    return city_message_category == MESSAGE_ADVISOR_PHRASE_BUTTONS && dialog_phrase_flags != 0;
}

void message_dialog_draw_phrases(void)
{
    static const struct {
        uint16_t flag;
        int32_t text;
        int32_t button;
        int32_t focus;
    } phrases[PHRASE_BUTTONS] = { { PHRASE_FLAG_THIRD, PHRASE_TEXT_FIRST, 2, 3 }, { PHRASE_FLAG_FIRST, PHRASE_TEXT_FIRST + 1, 0, 1 },
        { PHRASE_FLAG_SECOND, PHRASE_TEXT_FIRST + 2, 1, 2 } };
    const lang_message *message = lang_message_get(dialog_text_id);
    for (int32_t i = 0; i < PHRASE_BUTTONS; i++) {
        if (dialog_phrase_flags & phrases[i].flag) {
            const int16_t *rect = &phrase_buttons.buttons[phrases[i].button].x;
            lang_text_draw_centered_rect(LANG_GROUP_EDITOR, phrases[i].text, rect, image_group(IMAGE_GROUP_FONT_BLACK), COLOR_BLACK, message->x + 4, message->y + 4);
            button_border_draw_rect(rect, focus_button_id == phrases[i].focus, message->x, message->y);
        }
    }
}

static void draw_player_name_at(int32_t x, int32_t y, int32_t font)
{
    text_draw((const uint8_t *) message_token_player_name(), x, y, font, COLOR_BLACK);
}

static void draw_request_months(int32_t months, int32_t x, int32_t y)
{
    g_text_width = 0;
    lang_text_draw_amount(AMOUNT_TEXT_GROUP, AMOUNT_TEXT_MONTHS, months, x, y, image_group(IMAGE_GROUP_FONT_WHITE), COLOR_BLACK);
    lang_text_draw(MESSAGE_DIALOG_TEXT_GROUP, MESSAGE_DIALOG_TEXT_DUE_IN, g_text_width + x, y, image_group(IMAGE_GROUP_FONT_WHITE), COLOR_BLACK);
}

static void draw_imperial_request_normal(void)
{
    const city_message *shown = shown_message();
    int32_t amount = shown->phrases[PHRASE_REQUEST_AMOUNT];
    int32_t resource = shown->phrases[PHRASE_REQUEST_RESOURCE];
    int32_t months = shown->phrases[PHRASE_REQUEST_MONTHS];
    if (lang_message_get(dialog_text_id)->message_type != LANG_MESSAGE_TYPE_IMPERIAL) {
        return;
    }
    if (dialog_param1 < 0 || dialog_param1 >= scenario_events.count) {
        return;
    }
    const scenario_event *event = scenario_event_at(dialog_param1);
    if (!event || resource == RESOURCE_TROOPS) {
        return;
    }
    int32_t scaled = resource_amount_scale(resource, amount);
    int32_t y = g_draw_y_accum + 86 + dialog_y_text;
    g_text_width = 0;
    image_draw(resource_icon(resource), dialog_x_text + 8, g_draw_y_accum + 81 + dialog_y_text);
    text_draw_number(scaled, NUMBER_PREFIX_NONE, postfix_none, dialog_x_text + 20, y, image_group(IMAGE_GROUP_FONT_WHITE), COLOR_BLACK);
    int32_t offset = resource_name_index_offset(resource, scaled);
    lang_text_draw(LANG_GROUP_RESOURCE_NAMES, resource + offset, g_text_width + 20 + dialog_x_text, y, image_group(IMAGE_GROUP_FONT_WHITE), COLOR_BLACK);
    if (event->type == EVENT_TYPE_REQUEST && event->state < REQUEST_STATE_DISPATCHED) {
        draw_request_months(months, dialog_x_text + 220, y);
    }
}

static void draw_imperial_request_video(void)
{
    const city_message *shown = shown_message();
    int32_t amount = shown->phrases[PHRASE_REQUEST_AMOUNT];
    int32_t resource = shown->phrases[PHRASE_REQUEST_RESOURCE];
    int32_t months = shown->phrases[PHRASE_REQUEST_MONTHS];
    const lang_message *message = lang_message_get(dialog_text_id);
    const scenario_event *event = 0;
    if (message->type != LANG_MESSAGE_DIALOG_CITY_MESSAGE || message->message_type != LANG_MESSAGE_TYPE_IMPERIAL) {
        return;
    }
    if (dialog_param1 >= 0 && dialog_param1 < scenario_events.count) {
        event = scenario_event_at(dialog_param1);
    }
    int32_t scaled = resource_amount_scale(resource, amount);
    text_draw_number(scaled, NUMBER_PREFIX_NONE, postfix_none, dialog_x + 8, dialog_y + 384, image_group(IMAGE_GROUP_FONT_WHITE), COLOR_BLACK);
    image_draw(resource_icon(resource), dialog_x + 70, dialog_y + 379);
    int32_t offset = resource_name_index_offset(resource, scaled);
    lang_text_draw(LANG_GROUP_RESOURCE_NAMES, resource + offset, dialog_x + 100, dialog_y + 384, image_group(IMAGE_GROUP_FONT_WHITE), COLOR_BLACK);
    if (event && event->state < REQUEST_STATE_DISPATCHED) {
        draw_request_months(months, dialog_x + 250, dialog_y + 384);
    }
}

static void draw_content(const uint8_t *content, int32_t y_offset)
{
    rich_text_draw(content, dialog_x_text + 8, dialog_y_text + y_offset, (dialog_text_width_blocks * MESSAGE_BLOCK) - 16, 0, 0);
}

static void draw_message_date(int32_t type, int32_t message_type)
{
    g_text_width = 0;
    lang_text_draw(MONTH_TEXT_GROUP, dialog_month, dialog_x_text + 10, dialog_y_text + 6, image_group(IMAGE_GROUP_FONT_WHITE), COLOR_BLACK);
    lang_text_draw_year(0, dialog_year, g_text_width + 12 + dialog_x_text, dialog_y_text + 6, image_group(IMAGE_GROUP_FONT_WHITE), COLOR_BLACK);
    if (message_type == LANG_MESSAGE_TYPE_DISASTER && dialog_param1 != 0) {
        if (type == MESSAGE_DISASTER_WITH_AMOUNT) {
            lang_text_draw_amount(AMOUNT_TEXT_GROUP, AMOUNT_TEXT_PLAIN, dialog_param1, dialog_x + 240, dialog_y_text + 6, image_group(IMAGE_GROUP_FONT_WHITE), COLOR_BLACK);
        } else {
            lang_text_draw(DISASTER_TEXT_GROUP, dialog_param1, dialog_x + 240, dialog_y_text + 6, image_group(IMAGE_GROUP_FONT_WHITE), COLOR_BLACK);
        }
    } else {
        lang_text_draw(MESSAGE_LIST_TEXT_GROUP, MESSAGE_LIST_TEXT_FROM, g_text_width + 80 + dialog_x_text, dialog_y_text + 6, image_group(IMAGE_GROUP_FONT_WHITE), COLOR_BLACK);
        draw_player_name_at(g_text_width + 80 + dialog_x_text, dialog_y_text + 6, image_group(IMAGE_GROUP_FONT_WHITE));
    }
}

static void draw_trade_city(void)
{
    image_draw(image_group(IMAGE_GROUP_RESOURCE_ICONS) + dialog_param2, dialog_x + 64, dialog_y_text + 40);
    int32_t group = settings_data.classical_names == 0 ? LANG_GROUP_CITY_NAMES : LANG_GROUP_CITY_NAMES_CLASSICAL;
    lang_text_draw(group, empire_city_get(dialog_param1)->name_id, dialog_x + 100, dialog_y_text + 44, image_group(IMAGE_GROUP_FONT_WHITE), COLOR_BLACK);
}

static void draw_city_message_body(const uint8_t *content)
{
    int32_t type = dialog_text_id;
    int32_t message_type = lang_message_get(type)->message_type;
    if (message_type != LANG_MESSAGE_TYPE_TUTORIAL) {
        draw_message_date(type, message_type);
    }
    g_draw_y_accum = 0;
    if (message_type == LANG_MESSAGE_TYPE_DISASTER || message_type == LANG_MESSAGE_TYPE_INVASION) {
        lang_text_draw(MESSAGE_DIALOG_TEXT_GROUP, MESSAGE_DIALOG_TEXT_DISASTER_SUBJECT, dialog_x + 100, dialog_y_text + 44, image_group(IMAGE_GROUP_FONT_WHITE), COLOR_BLACK);
        draw_content(content, 86);
    } else if (message_type == LANG_MESSAGE_TYPE_EMIGRATION) {
        int32_t cause = city_player_current()->sentiment_low_mood_cause;
        if (cause >= 1 && cause <= LOW_MOOD_CAUSES) {
            lang_text_draw(
                MESSAGE_DIALOG_TEXT_GROUP, cause + MESSAGE_DIALOG_TEXT_LOW_MOOD_CAUSE_FIRST, dialog_x + 64, dialog_y_text + 44, image_group(IMAGE_GROUP_FONT_WHITE), COLOR_BLACK);
        }
        draw_content(content, 86);
    } else if (message_type == LANG_MESSAGE_TYPE_TUTORIAL) {
        draw_content(content, 6);
    } else if (message_type == LANG_MESSAGE_TYPE_TRADE_CHANGE) {
        draw_trade_city();
        draw_content(content, 86);
    } else if (message_type == LANG_MESSAGE_TYPE_PRICE_CHANGE) {
        image_draw(image_group(IMAGE_GROUP_RESOURCE_ICONS) + dialog_param2, dialog_x + 64, dialog_y_text + 40);
        text_draw_number(dialog_param1, NUMBER_PREFIX_NONE, money_format, dialog_x + 100, dialog_y_text + 44, image_group(IMAGE_GROUP_FONT_WHITE), COLOR_BLACK);
        draw_content(content, 86);
    } else {
        draw_content(content, 56);
    }
    draw_imperial_request_normal();
    message_dialog_draw_phrases();
}

static void message_texts_from_phrases(
    const uint8_t **content, const uint8_t **title, const uint8_t **subtitle, char *content_buffer, char *title_buffer, char *subtitle_buffer)
{
    const city_message *shown = shown_message();
    if (shown->phrases[PHRASE_CONTENT] >= 0) {
        event_text_format(shown->phrases[PHRASE_CONTENT], content_buffer, MESSAGE_CONTENT_LENGTH);
        *content = (const uint8_t *) content_buffer;
    }
    if (shown->phrases[PHRASE_TITLE] >= 0) {
        event_text_format(shown->phrases[PHRASE_TITLE], title_buffer, MESSAGE_TITLE_LENGTH);
        *title = (const uint8_t *) title_buffer;
    }
    if (shown->phrases[PHRASE_SUBTITLE] >= 0) {
        event_text_format(shown->phrases[PHRASE_SUBTITLE], subtitle_buffer, MESSAGE_TITLE_LENGTH);
        *subtitle = (const uint8_t *) subtitle_buffer;
    }
}

static void draw_city_behind(void)
{
    if (dialog_background == 0) {
        widget_city_draw_current_view();
        if (window_redraw_request == 1) {
            widget_sidebar_draw_background();
        }
    }
}

static void raise_text_top(int32_t bottom)
{
    if (dialog_y_text < bottom) {
        dialog_y_text = bottom;
    }
}

static void draw_title(lang_message *message, const uint8_t *title)
{
    int32_t width = message->width_blocks * MESSAGE_BLOCK;
    if (message->title_x == 0) {
        if (message->message_type == LANG_MESSAGE_TYPE_TUTORIAL) {
            text_draw_centered(title, dialog_x, message->title_y + dialog_y, width, image_group(IMAGE_GROUP_FONT_LARGE), COLOR_BLACK);
        } else {
            text_draw_centered(title, dialog_x, dialog_y + 14, width, image_group(IMAGE_GROUP_FONT_LARGE), COLOR_BLACK);
        }
        dialog_y_text = dialog_y + 48;
    } else {
        text_draw(title, message->title_x + dialog_x, message->title_y + dialog_y, image_group(IMAGE_GROUP_FONT_LARGE), COLOR_BLACK);
        dialog_y_text = dialog_y + 32;
    }
}

static void draw_first_image(int32_t type, lang_message *message)
{
    if (type == MANUAL_FIRST_PAGE) {
        message->image_x = MANUAL_FIRST_PAGE_IMAGE_OFFSET;
        message->image_y = MANUAL_FIRST_PAGE_IMAGE_OFFSET;
        if (graphics_mode_setting != GRAPHICS_MODE_LOW) {
            image_draw(image_group(IMAGE_GROUP_BIG_PEOPLE), dialog_x + 16, dialog_y + 16);
        } else {
            image_draw(image_group(IMAGE_GROUP_BULLET), dialog_x + 16, dialog_y + 16);
            panel_draw_unbordered(message->image_x + 30 + dialog_x, message->image_y + 40 + dialog_y, 2, 1);
        }
    } else {
        int32_t image = message->image_id;
        int32_t image_id = image < MESSAGE_IMAGES_HIGH ? (image - 1 + image_group(IMAGE_GROUP_MESSAGE_IMAGES)) : (image - (MESSAGE_IMAGES_HIGH + 1) + image_group(IMAGE_GROUP_MESSAGE_IMAGES_HI));
        image_draw(image_id, message->image_x + dialog_x, message->image_y + dialog_y);
    }
    raise_text_top(g_img_height + 8 + dialog_y + message->image_y);
}

static void measure_content(lang_message *message, const uint8_t *content)
{
    int32_t used = (dialog_y_text - dialog_y) + dialog_header_offset;
    message_scroll_up.button.state_word.flags = 1;
    int32_t height_blocks = message->height_blocks - (used / MESSAGE_BLOCK);
    message_scroll_down.button.state_word.flags = 1;
    dialog_text_height_blocks = height_blocks - 1;
    rich_viewport_lines = height_blocks - 2;
    dialog_text_width_blocks = message->width_blocks - 4;
    rich_text_draw(content, dialog_x_text + 8, dialog_y_text + 6, (dialog_text_width_blocks * MESSAGE_BLOCK) - 16, 1, 0);
    if (rich_viewport_lines < rich_line_count) {
        dialog_scroll_max = rich_line_count - rich_viewport_lines;
    } else {
        dialog_scroll_max = 0;
        message_scroll_up.button.state_word.flags = 0;
        message_scroll_down.button.state_word.flags = 0;
        dialog_text_width_blocks = message->width_blocks - 2;
    }
    window_redraw_request = 1;
}

void message_dialog_draw_background_normal(void)
{
    int32_t type = dialog_text_id;
    char content_buffer[MESSAGE_CONTENT_LENGTH];
    char title_buffer[MESSAGE_TITLE_LENGTH];
    char subtitle_buffer[MESSAGE_TITLE_LENGTH];
    lang_message *message = lang_message_get(type);
    const uint8_t *content = lang_message_data + message->content_text;
    const uint8_t *title = lang_message_data + message->title_text;
    const uint8_t *subtitle = lang_message_data + message->subtitle_text;
    if (dialog_has_phrases) {
        message_texts_from_phrases(&content, &title, &subtitle, content_buffer, title_buffer, subtitle_buffer);
    }
    rich_text_link_font = image_group(IMAGE_GROUP_COLOR_NORMAL_BLACK);
    rich_text_normal_font = image_group(IMAGE_GROUP_FONT_WHITE);
    dialog_x = message->x + ui_origin_x;
    dialog_y = message->y + ui_origin_y;
    draw_city_behind();
    dialog_header_offset = message->type != LANG_MESSAGE_DIALOG_MANUAL ? 32 : 48;
    dialog_x_text = dialog_x + 16;
    outer_panel_draw(dialog_x, dialog_y, message->width_blocks, message->height_blocks);
    draw_title(message, title);
    if (message->image_id != 0) {
        draw_first_image(type, message);
    }
    if (message->second_image_id != 0) {
        image_draw(message->second_image_id - 1 + image_group(IMAGE_GROUP_MESSAGE_IMAGES), message->second_image_x + dialog_x, message->second_image_y + dialog_y);
        raise_text_top(g_img_height + 8 + dialog_y + message->second_image_y);
    }
    if (message->subtitle_x != 0) {
        dialog_subtitle_width = (message->width_blocks * MESSAGE_BLOCK) - message->subtitle_x - 16;
        g_draw_y_accum = 0;
        text_draw_multiline(
            (const char *) subtitle, message->subtitle_x + dialog_x, message->subtitle_y + dialog_y, dialog_subtitle_width, image_group(IMAGE_GROUP_FONT_BLACK), COLOR_BLACK);
        int32_t bottom = message->subtitle_y + g_draw_y_accum + dialog_y;
        if (dialog_y_text < bottom) {
            dialog_y_text = bottom + 4;
        }
    }
    if (rich_line_count == 0) {
        measure_content(message, content);
    }
    if (rich_viewport_lines < rich_line_count) {
        inner_panel_draw((message->width_blocks * MESSAGE_BLOCK) - 46 + dialog_x, dialog_y_text + 26, 2, dialog_text_height_blocks - 2);
    }
    if (dialog_show_video != 0) {
        graphics_fill_rect(message->video_x + dialog_x, message->video_y + dialog_y, 200, 200, COLOR_RED);
    }
    inner_panel_draw(dialog_x_text, dialog_y_text, dialog_text_width_blocks, dialog_text_height_blocks);
    g_clip_x0 = dialog_x_text + 3;
    g_clip_x1 = (dialog_text_width_blocks * MESSAGE_BLOCK) - 3 + dialog_x_text;
    g_clip_y0 = dialog_y_text + 3;
    g_clip_y1 = (dialog_text_height_blocks * MESSAGE_BLOCK) - 3 + dialog_y_text;
    rich_text_clear_links();
    if (message->type != LANG_MESSAGE_DIALOG_CITY_MESSAGE) {
        draw_content(content, 19);
    } else {
        draw_city_message_body(content);
    }
    g_clip_x1 = g_screen_width;
    g_clip_x0 = 0;
    g_clip_y0 = 0;
    g_clip_y1 = g_screen_height;
    message_dialog_draw_scrollbar_dot();
}

static void draw_video_or_image(const uint8_t *video_path)
{
    if (video_path[0] != MESSAGE_VIDEO_MARKER) {
        video_start((const char *) video_path, dialog_x + 8, dialog_y + 8, window_current_id);
        return;
    }
    int32_t image_id = atoi((const char *) (video_path + 1)) - 1 + image_group(IMAGE_GROUP_VIDEO);
    int32_t width = 0;
    int32_t height = 0;
    if (image_get_size(image_id, &width, &height)) {
        image_draw(image_id, ((MESSAGE_VIDEO_WIDTH - width) / 2) + 8 + dialog_x, ((MESSAGE_VIDEO_HEIGHT - height) / 2) + 8 + dialog_y);
    }
}

void message_dialog_draw_background_video(void)
{
    int32_t type = dialog_text_id;
    char content_buffer[MESSAGE_CONTENT_LENGTH];
    char title_buffer[MESSAGE_TITLE_LENGTH];
    char subtitle_buffer[MESSAGE_TITLE_LENGTH];
    lang_message *message = lang_message_get(type);
    const uint8_t *content = lang_message_data + message->content_text;
    const uint8_t *title = lang_message_data + message->title_text;
    const uint8_t *subtitle = 0;
    const uint8_t *video_path = lang_message_data + message->video_text;
    if (dialog_has_phrases) {
        message_texts_from_phrases(&content, &title, &subtitle, content_buffer, title_buffer, subtitle_buffer);
    }
    rich_text_normal_font = image_group(IMAGE_GROUP_FONT_WHITE);
    rich_text_link_font = image_group(IMAGE_GROUP_FONT_RED);
    dialog_y = ui_origin_y + 28;
    dialog_x = ui_origin_x;
    draw_city_behind();
    outer_panel_draw(dialog_x, dialog_y, 30, 28);
    graphics_draw_rect(dialog_x + 7, dialog_y + 9, MESSAGE_VIDEO_WIDTH, MESSAGE_VIDEO_HEIGHT, COLOR_BLACK);
    rich_text_clear_links();
    draw_video_or_image(video_path);
    inner_panel_draw(dialog_x + 8, dialog_y + 245, 29, 10);
    text_draw_centered(title, dialog_x + 8, dialog_y + 414, 464, image_group(IMAGE_GROUP_FONT_BLACK), COLOR_BLACK);
    g_text_width = 0;
    lang_text_draw(MONTH_TEXT_GROUP, dialog_month, dialog_x + 16, dialog_y + 257, image_group(IMAGE_GROUP_FONT_WHITE), COLOR_BLACK);
    lang_text_draw_year(0, dialog_year, g_text_width + 18 + dialog_x, dialog_y + 257, image_group(IMAGE_GROUP_FONT_WHITE), COLOR_BLACK);
    if (message->type == LANG_MESSAGE_DIALOG_CITY_MESSAGE && message->message_type == LANG_MESSAGE_TYPE_DISASTER && type == MESSAGE_DISASTER_WITH_AMOUNT) {
        lang_text_draw_amount(AMOUNT_TEXT_GROUP, AMOUNT_TEXT_PLAIN, dialog_param1, g_text_width + 90 + dialog_x, dialog_y + 257, image_group(IMAGE_GROUP_FONT_WHITE), COLOR_BLACK);
    } else {
        lang_text_draw(MESSAGE_LIST_TEXT_GROUP, MESSAGE_LIST_TEXT_FROM, g_text_width + 90 + dialog_x, dialog_y + 257, image_group(IMAGE_GROUP_FONT_WHITE), COLOR_BLACK);
        draw_player_name_at(g_text_width + 90 + dialog_x, dialog_y + 257, image_group(IMAGE_GROUP_FONT_WHITE));
    }
    int32_t used = (dialog_header_offset - dialog_y) + dialog_y_text;
    g_draw_y_accum = 0;
    int32_t height_blocks = message->height_blocks - (used / MESSAGE_BLOCK);
    dialog_text_height_blocks = height_blocks - 1;
    rich_viewport_lines = height_blocks - 2;
    dialog_text_width_blocks = message->width_blocks - 4;
    rich_text_draw(content, dialog_x + 16, dialog_y + 277, 480, 0, 0);
    draw_imperial_request_video();
    window_message_dialog_draw_foreground();
}

void window_message_dialog_draw_background(void)
{
    if (dialog_show_video != 0) {
        message_dialog_draw_background_video();
        return;
    }
    message_dialog_draw_background_normal();
}

void message_dialog_draw_scrollbar_dot(void)
{
    if (dialog_scroll_max <= 0) {
        return;
    }
    int32_t percent;
    if (rich_scroll_pos < 1) {
        percent = 0;
    } else if (rich_scroll_pos < dialog_scroll_max) {
        percent = calc_percentage(rich_scroll_pos, dialog_scroll_max);
    } else {
        percent = 100;
    }
    int32_t offset = calc_adjust_with_percentage((dialog_text_height_blocks * MESSAGE_BLOCK) - 77, percent);
    if (dialog_scroll_dragging != 0) {
        offset = dialog_scroll_drag_y;
    }
    image_draw(image_group(IMAGE_GROUP_PANEL_BUTTON) + MESSAGE_SCROLL_THUMB, (dialog_text_width_blocks * MESSAGE_BLOCK) + 6 + dialog_x_text, dialog_y_text + 26 + offset);
}

int32_t message_dialog_handle_scrollbar(void)
{
    if (dialog_scroll_max < 1 || mouse_left_down == 0) {
        return 0;
    }
    int32_t track_x = (dialog_text_width_blocks * MESSAGE_BLOCK) + dialog_x_text;
    int32_t track_length = (dialog_text_height_blocks * MESSAGE_BLOCK) - 52;
    if (mouse_x < (track_x + 1) || mouse_x > (track_x + 41)) {
        return 0;
    }
    if (mouse_y < (dialog_y_text + 26) || mouse_y > (dialog_y_text + 26 + track_length)) {
        return 0;
    }
    int32_t position = mouse_y - dialog_y_text - 11;
    if (position > track_length) {
        position = track_length;
    }
    rich_scroll_pos = calc_adjust_with_percentage(dialog_scroll_max, calc_percentage(position, track_length));
    dialog_scroll_drag_y = position - 25;
    dialog_scroll_dragging = 1;
    rich_text_clear_links();
    if (dialog_scroll_drag_y < 0) {
        dialog_scroll_drag_y = 0;
    }
    window_redraw_request = 2;
    return 1;
}

static int32_t scroll_buttons_x(void)
{
    return (dialog_text_width_blocks * MESSAGE_BLOCK) - 1 + dialog_x_text;
}

static int32_t scroll_down_y(void)
{
    return (dialog_text_height_blocks * MESSAGE_BLOCK) - 26 + dialog_y_text;
}

static int32_t message_has_go_to(int32_t message_type)
{
    return message_type == LANG_MESSAGE_TYPE_DISASTER || message_type == LANG_MESSAGE_TYPE_INVASION;
}

void window_message_dialog_draw_foreground(void)
{
    int32_t phrases = phrase_buttons_active();
    const lang_message *message = lang_message_get(dialog_text_id);
    int32_t bottom = (message->height_blocks * MESSAGE_BLOCK) + dialog_y;
    window_ui_redraw = 1;
    if (dialog_show_video != 0) {
        if (phrases) {
            message_dialog_draw_phrases();
        } else {
            image_buttons_draw(dialog_x + 16, dialog_y + 408, advisor_button(), 1);
            image_buttons_draw(dialog_x + 436, dialog_y + 410, message_close.buttons, 1);
        }
        g_force_refresh = 0;
        return;
    }
    if (message->type == LANG_MESSAGE_DIALOG_MANUAL && dialog_history_count > 0) {
        image_buttons_draw(dialog_x + 16, bottom - 36, message_back.buttons, 1);
        lang_text_draw(MESSAGE_DIALOG_TEXT_GROUP, MESSAGE_DIALOG_TEXT_BACK, dialog_x + 52, bottom - 31, image_group(IMAGE_GROUP_FONT_BLACK), COLOR_BLACK);
    }
    if (message->type == LANG_MESSAGE_DIALOG_CITY_MESSAGE) {
        if (phrases) {
            message_dialog_draw_phrases();
        } else {
            image_buttons_draw(dialog_x + 16, bottom - 40, advisor_button(), 1);
            if (message_has_go_to(message->message_type)) {
                image_buttons_draw(dialog_x + 64, dialog_y_text + 36, message_go_to_problem.buttons, 1);
            }
        }
    }
    image_buttons_draw((message->width_blocks * MESSAGE_BLOCK) - 38 + dialog_x, bottom - 36, message_close.buttons, 1);
    if (dialog_scroll_max != 0) {
        image_buttons_draw(scroll_buttons_x(), dialog_y_text, message_scroll_up.button.buttons, 1);
        image_buttons_draw(scroll_buttons_x(), scroll_down_y(), message_scroll_down.button.buttons, 1);
    }
}

static int32_t image_button_handled(int32_t x, int32_t y, void *buttons, int32_t tooltip)
{
    image_buttons_set_tooltip(x, y, buttons, 1, tooltip);
    return image_buttons_handle_mouse(x, y, buttons, 1) != 0;
}

static int32_t phrase_buttons_handled(void)
{
    focus_button_id = generic_buttons_get_focus(dialog_x, dialog_y, phrase_buttons.buttons, PHRASE_BUTTONS);
    return generic_buttons_handle_mouse(dialog_x, dialog_y, phrase_buttons.buttons, PHRASE_BUTTONS) != 0;
}

static int32_t video_dialog_handled(int32_t phrases)
{
    if (phrases) {
        g_force_refresh = 0;
        phrase_buttons_handled();
        return 1;
    }
    if (image_buttons_handle_mouse(dialog_x + 16, dialog_y + 408, advisor_button(), 1) != 0) {
        return 1;
    }
    if (image_buttons_handle_mouse(dialog_x + 436, dialog_y + 410, message_close.buttons, 1) != 0) {
        return 1;
    }
    g_force_refresh = 0;
    return 1;
}

static void follow_link(int32_t link)
{
    if (dialog_history_count < MESSAGE_HISTORY_MAX) {
        dialog_history_id[dialog_history_count] = dialog_text_id;
        dialog_history_scroll[dialog_history_count] = rich_scroll_pos;
        dialog_history_count++;
    }
    rich_scroll_pos = 0;
    rich_line_count = 0;
    dialog_scroll_dragging = 0;
    dialog_text_id = link;
    rich_text_clear_links();
    window_redraw_request = 1;
}

void window_message_dialog_handle_input(void)
{
    int32_t phrases = phrase_buttons_active();
    const lang_message *message = lang_message_get(dialog_text_id);
    int32_t bottom = (message->height_blocks * MESSAGE_BLOCK) + dialog_y;
    if (mouse_right_went_up != 0 && !phrases) {
        window_message_dialog_close();
        return;
    }
    if (dialog_show_video != 0) {
        video_dialog_handled(phrases);
        return;
    }
    if (message->type == LANG_MESSAGE_DIALOG_MANUAL && image_button_handled(dialog_x + 16, bottom - 36, message_back.buttons, MESSAGE_DIALOG_TOOLTIP_BACK)) {
        return;
    }
    if (message->type == LANG_MESSAGE_DIALOG_CITY_MESSAGE) {
        if (phrases) {
            if (phrase_buttons_handled()) {
                return;
            }
        } else {
            if (image_buttons_handle_mouse(dialog_x + 16, bottom - 36, advisor_button(), 1) != 0) {
                return;
            }
            if (message_has_go_to(message->message_type) &&
                image_button_handled(dialog_x + 64, dialog_y_text + 36, message_go_to_problem.buttons, MESSAGE_DIALOG_TOOLTIP_GO_TO)) {
                return;
            }
        }
    }
    if (!phrases && image_button_handled((message->width_blocks * MESSAGE_BLOCK) - 38 + dialog_x, bottom - 36, message_close.buttons, MESSAGE_DIALOG_TOOLTIP_CLOSE)) {
        return;
    }
    if (image_button_handled(scroll_buttons_x(), dialog_y_text, message_scroll_up.button.buttons, MESSAGE_DIALOG_TOOLTIP_SCROLL_UP)) {
        return;
    }
    if (image_button_handled(scroll_buttons_x(), scroll_down_y(), message_scroll_down.button.buttons, MESSAGE_DIALOG_TOOLTIP_SCROLL_DOWN)) {
        return;
    }
    if (message_dialog_handle_scrollbar() != 0 || phrases) {
        return;
    }
    int32_t link = rich_text_get_clicked_link();
    if (link >= 0) {
        follow_link(link);
    }
}

void window_message_dialog_show(int32_t type, int32_t arg, bool has_phrases)
{
    dialog_has_phrases = has_phrases;
    if (!has_phrases) {
        dialog_phrase_flags = 0;
    } else {
        dialog_phrase_flags = city_messages[city_message_shown_index].phrases[EVENT_PHRASE_BUTTONS];
    }
    for (int32_t i = 0; i < DIALOG_HISTORY_ENTRIES; i++) {
        dialog_history_id[i] = 0;
        dialog_history_scroll[i] = 0;
    }
    dialog_text_id = type;
    dialog_background = arg;
    dialog_history_count = 0;
    rich_scroll_pos = 0;
    rich_line_count = 0;
    dialog_scroll_dragging = 0;
    dialog_show_video = lang_message_get(type)->video_text != 0;
    if (city_message_use_popup != 1) {
        dialog_show_video = 0;
    }
    rich_text_clear_links();
    window_redraw_request = 1;
    dialog_prev_window = window_current_id;
    window_current_id = WINDOW_MESSAGE_DIALOG;
}

void window_message_dialog_close(void)
{
    if (dialog_show_video != 0) {
        video_stop();
    }
    uint8_t previous = (uint8_t) dialog_prev_window;
    dialog_show_video = 0;
    window_current_id = previous;
    city_message_category = 0;
    window_redraw_request = 1;
}
