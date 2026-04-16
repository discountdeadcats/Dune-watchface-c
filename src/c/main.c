#include <pebble.h>

static Window *s_main_window;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;

// Load fonts
static GFont s_time_font;
static GFont s_date_font;

static Layer *s_battery_layer;
static int s_battery_level;

// Bluetooth
static BitmapLayer *s_bt_icon_layer;
static GBitmap *s_bt_icon_bitmap;


static void update_time() {
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);

  static char s_time_buffer[8];
  strftime(s_time_buffer, sizeof(s_time_buffer), clock_is_24h_style() ?
                                                    "%H:%M" : "%I:%M", tick_time);
  text_layer_set_text(s_time_layer, s_time_buffer);

  static char s_date_buffer[16];
  strftime(s_date_buffer, sizeof(s_date_buffer), "%a %b %d", tick_time);
  text_layer_set_text(s_date_layer, s_date_buffer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();
}

static void battery_callback(BatteryChargeState state) {
  s_battery_level = state.charge_percent;
  layer_mark_dirty(s_battery_layer);
}

// -------------------------------------------------------
// Sandworm battery meter
// -------------------------------------------------------
#define WORM_SEGMENTS   10
#define SEGMENT_HEIGHT  5
#define SEGMENT_GAP     2
#define WORM_WIDTH      14
#define HEAD_HEIGHT     8

static void battery_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  int filled = s_battery_level / 10;  // 0–10 segments

  int x = bounds.origin.x + (bounds.size.w - WORM_WIDTH) / 2;
  int base_y = bounds.origin.y + bounds.size.h;

  // Sand surface — dashed line at the base
  graphics_context_set_stroke_color(ctx, GColorWhite);
  for (int dx = 0; dx < bounds.size.w; dx += 4) {
    graphics_draw_line(ctx,
      GPoint(bounds.origin.x + dx,     base_y - 1),
      GPoint(bounds.origin.x + dx + 2, base_y - 1)
    );
  }

  if (filled == 0) return;  // nothing above the sand

  // Body segments — drawn bottom up, above the head
  for (int i = 0; i < filled; i++) {
    int seg_y = base_y - HEAD_HEIGHT
                - (i + 1) * (SEGMENT_HEIGHT + SEGMENT_GAP)
                + SEGMENT_GAP;

    GColor seg_color;
    if (s_battery_level <= 20) {
      seg_color = PBL_IF_COLOR_ELSE(GColorRed, GColorWhite);
    } else if (s_battery_level <= 40) {
      seg_color = PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite);
    } else {
      seg_color = PBL_IF_COLOR_ELSE(GColorBrass, GColorWhite);
    }

    graphics_context_set_stroke_color(ctx, seg_color);
    graphics_draw_round_rect(ctx, GRect(x, seg_y, WORM_WIDTH, SEGMENT_HEIGHT), 2);

    // Inner ring detail line
    graphics_draw_line(ctx,
      GPoint(x + 3, seg_y + 2),
      GPoint(x + WORM_WIDTH - 3, seg_y + 2)
    );
  }

  // Head / maw — sits just above the sand line
  int head_y = base_y - HEAD_HEIGHT;

  GColor head_color;
  if (s_battery_level <= 20) {
    head_color = PBL_IF_COLOR_ELSE(GColorRed, GColorWhite);
  } else if (s_battery_level <= 40) {
    head_color = PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite);
  } else {
    head_color = PBL_IF_COLOR_ELSE(GColorBrass, GColorWhite);
  }

  graphics_context_set_stroke_color(ctx, head_color);
  graphics_draw_round_rect(ctx, GRect(x - 1, head_y, WORM_WIDTH + 2, HEAD_HEIGHT), 3);

  // Concentric maw rings
  int cx = x + WORM_WIDTH / 2;
  int cy = head_y + HEAD_HEIGHT / 2;
  graphics_draw_circle(ctx, GPoint(cx, cy), 3);
  graphics_draw_circle(ctx, GPoint(cx, cy), 2);
  graphics_draw_pixel(ctx, GPoint(cx, cy));
}

// -------------------------------------------------------

static void bluetooth_callback(bool connected) {
  layer_set_hidden(bitmap_layer_get_layer(s_bt_icon_layer), connected);
  if (!connected) {
    vibes_double_pulse();
  }
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Load custom fonts
  s_time_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_DUNE_FONT_56));
  s_date_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_DUNE_FONT_24));

  // Center the time + date block vertically
  int date_height = 30;
  int block_height = 56 + date_height;
  int time_y = (bounds.size.h / 2) - (block_height / 2) - 10;
  int date_y = time_y + 56;

  // Time layer
  s_time_layer = text_layer_create(GRect(0, time_y, bounds.size.w, 60));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorWhite);
  text_layer_set_font(s_time_layer, s_time_font);
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);

  // Date layer
  s_date_layer = text_layer_create(GRect(0, date_y, bounds.size.w, 30));
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, GColorWhite);
  text_layer_set_font(s_date_layer, s_date_font);
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);

  // Sandworm battery layer — tall enough for 10 segments + head + sand line
  // 10 segments * (5px + 2px gap) + 8px head + 2px sand = 80px total
  int worm_layer_height = (WORM_SEGMENTS * (SEGMENT_HEIGHT + SEGMENT_GAP))
                          + HEAD_HEIGHT + 2;
  int worm_x = (bounds.size.w / 2) - 20;  // slight left-of-center, 40px wide
  int worm_y = PBL_IF_ROUND_ELSE(bounds.size.h / 8, bounds.size.h / 28);
  s_battery_layer = layer_create(
      GRect(worm_x, worm_y, 40, worm_layer_height));
  layer_set_update_proc(s_battery_layer, battery_update_proc);

  // Bluetooth icon — below the worm layer
  s_bt_icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BT_ICON);
  int bt_y = worm_y + worm_layer_height + 4;
  s_bt_icon_layer = bitmap_layer_create(
      GRect((bounds.size.w - 30) / 2, bt_y, 30, 30));
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
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_date_layer);
  fonts_unload_custom_font(s_time_font);
  fonts_unload_custom_font(s_date_font);
  layer_destroy(s_battery_layer);
  gbitmap_destroy(s_bt_icon_bitmap);
  bitmap_layer_destroy(s_bt_icon_layer);
}

static void init() {
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

static void deinit() {
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}