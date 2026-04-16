#include <pebble.h>

static Window *s_main_window;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static GFont s_time_font;
static GFont s_date_font;

static Layer *s_battery_layer;
static int s_battery_level;

static BitmapLayer *s_bt_icon_layer;
static GBitmap *s_bt_icon_bitmap;

// -------------------------------------------------------
// Time / date
// -------------------------------------------------------
static void update_time(void) {
    time_t temp = time(NULL);
    struct tm *tick_time = localtime(&temp);

    static char s_time_buffer[8];

    if(clock_is_24h_style()) {
        // 24-hour format
        strftime(s_time_buffer, sizeof(s_time_buffer), "%H:%M", tick_time);
    } else {
        // 12-hour format
        strftime(s_time_buffer, sizeof(s_time_buffer), "%I:%M", tick_time);

        // Remove leading zero (e.g., "09:15" -> "9:15")
        if(s_time_buffer[0] == '0') {
            memmove(s_time_buffer, &s_time_buffer[1], sizeof(s_time_buffer) - 1);
        }
    }

    text_layer_set_text(s_time_layer, s_time_buffer);

    static char s_date_buffer[16];
    strftime(s_date_buffer, sizeof(s_date_buffer), "%a %b %d", tick_time);
    text_layer_set_text(s_date_layer, s_date_buffer);
}
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
    (void)tick_time;
    (void)units_changed;
    update_time();
}

// -------------------------------------------------------
// Battery
// -------------------------------------------------------
static void battery_callback(BatteryChargeState state) {
    s_battery_level = state.charge_percent;
    if (s_battery_layer) {
        layer_mark_dirty(s_battery_layer);
    }
}

static GColor worm_color(void) {
    if (s_battery_level <= 20) {
        return PBL_IF_COLOR_ELSE(GColorRed, GColorWhite);
    } else if (s_battery_level <= 40) {
        return PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite);
    }
    return PBL_IF_COLOR_ELSE(GColorBrass, GColorWhite);
}

// -------------------------------------------------------
// Horizontal sandworm battery meter
// -------------------------------------------------------
#define WORM_SEGMENTS 10
#define WORM_HEAD_W   14
#define WORM_HEAD_H   14
#define WORM_BODY_H    8
#define WORM_GAP       2

static void battery_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);

    graphics_context_set_stroke_color(ctx, GColorWhite);

    // Sand line / dune marks across the top of the worm layer.
    int sand_y = bounds.origin.y + bounds.size.h - 2;
    for (int dx = 0; dx < bounds.size.w; dx += 5) {
        graphics_draw_line(ctx,
                           GPoint(bounds.origin.x + dx, sand_y),
                           GPoint(bounds.origin.x + dx + 2, sand_y));
    }

    int filled = s_battery_level / 10;
    if (filled <= 0) {
        return;
    }
    if (filled > WORM_SEGMENTS) {
        filled = WORM_SEGMENTS;
    }

    GColor color = worm_color();
    graphics_context_set_stroke_color(ctx, color);

    int head_x = bounds.origin.x + 2;
    int head_y = bounds.origin.y + (bounds.size.h - WORM_HEAD_H) / 2;
    int body_x = head_x + WORM_HEAD_W + 3;

    int available_w = bounds.size.w - (body_x - bounds.origin.x) - 2;
    int seg_w = (available_w - ((WORM_SEGMENTS - 1) * WORM_GAP)) / WORM_SEGMENTS;
    if (seg_w < 4) {
        seg_w = 4;
    }

    int body_y = bounds.origin.y + (bounds.size.h - WORM_BODY_H) / 2;

    // Body segments, left to right.
    for (int i = 0; i < filled; i++) {
        int x = body_x + i * (seg_w + WORM_GAP);
        graphics_draw_round_rect(ctx, GRect(x, body_y, seg_w, WORM_BODY_H), 2);

        // Inner detail line to suggest the worm's segmented body.
        graphics_draw_line(ctx,
                           GPoint(x + 2, body_y + WORM_BODY_H / 2),
                           GPoint(x + seg_w - 3, body_y + WORM_BODY_H / 2));
    }

    // Head.
    graphics_draw_round_rect(ctx, GRect(head_x, head_y, WORM_HEAD_W, WORM_HEAD_H), 4);

    // Maw / eyes detail.
    int cx = head_x + WORM_HEAD_W / 2;
    int cy = head_y + WORM_HEAD_H / 2;
    graphics_draw_circle(ctx, GPoint(cx, cy), 3);
    graphics_draw_circle(ctx, GPoint(cx, cy), 2);
    graphics_draw_pixel(ctx, GPoint(cx, cy));
}

// -------------------------------------------------------
// Bluetooth
// -------------------------------------------------------
static void bluetooth_callback(bool connected) {
    if (s_bt_icon_layer) {
        layer_set_hidden(bitmap_layer_get_layer(s_bt_icon_layer), connected);
    }
    if (!connected) {
        vibes_double_pulse();
    }
}

// -------------------------------------------------------
// Layout
// -------------------------------------------------------
static void main_window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    // Load custom fonts
    s_time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_DUNE_FONT_56));
    s_date_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_DUNE_FONT_24));

    int margin_x = (bounds.size.w >= 180) ? 16 : 10;
    int worm_layer_h = 20;
    int bt_icon_h = 30;

    // Center the whole stack vertically so it scales across square and round Pebbles.
    int content_h = 56 + 24 + worm_layer_h + bt_icon_h + 8;
    int top_y = (bounds.size.h - content_h) / 2;
    if (top_y < 8) {
        top_y = 8;
    }

    int time_y = top_y;
    int date_y = time_y + 56;
    int worm_y = date_y + 24 + 4;
    int bt_y = worm_y + worm_layer_h + 4;

    // Time layer
    s_time_layer = text_layer_create(GRect(0, time_y, bounds.size.w, 60));
    text_layer_set_background_color(s_time_layer, GColorClear);
    text_layer_set_text_color(s_time_layer, GColorWhite);
    text_layer_set_font(s_time_layer, s_time_font);
    text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
    text_layer_set_overflow_mode(s_time_layer, GTextOverflowModeTrailingEllipsis);

    // Date layer
    s_date_layer = text_layer_create(GRect(0, date_y, bounds.size.w, 30));
    text_layer_set_background_color(s_date_layer, GColorClear);
    text_layer_set_text_color(s_date_layer, GColorWhite);
    text_layer_set_font(s_date_layer, s_date_font);
    text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
    text_layer_set_overflow_mode(s_date_layer, GTextOverflowModeTrailingEllipsis);

    // Horizontal sandworm battery layer
    s_battery_layer = layer_create(GRect(margin_x, worm_y,
                                         bounds.size.w - (margin_x * 2),
                                         worm_layer_h));
    layer_set_update_proc(s_battery_layer, battery_update_proc);

    // Bluetooth icon below the worm
    s_bt_icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BT_ICON);
    s_bt_icon_layer = bitmap_layer_create(
        GRect((bounds.size.w - 30) / 2, bt_y, 30, bt_icon_h));
    bitmap_layer_set_bitmap(s_bt_icon_layer, s_bt_icon_bitmap);
    bitmap_layer_set_compositing_mode(s_bt_icon_layer, GCompOpSet);

    // Add layers
    layer_add_child(window_layer, text_layer_get_layer(s_time_layer));
    layer_add_child(window_layer, text_layer_get_layer(s_date_layer));
    layer_add_child(window_layer, s_battery_layer);
    layer_add_child(window_layer, bitmap_layer_get_layer(s_bt_icon_layer));

    bluetooth_callback(connection_service_peek_pebble_app_connection());
}

static void main_window_unload(Window *window) {
    (void)window;
    text_layer_destroy(s_time_layer);
    text_layer_destroy(s_date_layer);
    fonts_unload_custom_font(s_time_font);
    fonts_unload_custom_font(s_date_font);
    layer_destroy(s_battery_layer);
    gbitmap_destroy(s_bt_icon_bitmap);
    bitmap_layer_destroy(s_bt_icon_layer);
}

static void init(void) {
    s_main_window = window_create();
    window_set_background_color(s_main_window, GColorBlack);
    window_set_window_handlers(s_main_window, (WindowHandlers) {
        .load = main_window_load,
        .unload = main_window_unload
    });
    window_stack_push(s_main_window, true);

    update_time();
    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    battery_state_service_subscribe(battery_callback);
    battery_callback(battery_state_service_peek());
    connection_service_subscribe((ConnectionHandlers) {
        .pebble_app_connection_handler = bluetooth_callback
    });
}

static void deinit(void) {
    window_destroy(s_main_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}
