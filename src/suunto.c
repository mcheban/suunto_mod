#include <pebble.h>

//#define DEBUG
#define TIME_INIT_STR "00:00"
#define DATE_INIT_STR "Mon 01.01"
#define WEATHER_INIT_STR "No Data"
#define WEATHER_TIME_INIT_STR "01.01 00:00"
#define WEATHER_LOADING_STR "Loading..."
#define WEATHER_FAILED_STR "No Data"
#define RATE_FAILED_STR "No Rate Data"
#define RATE_LOADING_STR "Loading..."
#define RATE_INIT_STR "No Rate Data"
#define BAT_INFO_INIT_STR ""
#define TEMPERATURE_INIT_STR ""

#define TIME_HEIGHT 71
#define DATE_HEIGHT 33
#define WEATHER_HEIGHT 80
#define WEATHER_TIME_HEIGHT 30
#define BAT_INFO_HEIGHT 21
#define RATE_HEIGHT 64
#define RATE_SMALL_HEIGHT 33
#define TEMPERATURE_HEIGHT 21
#define SUM_HEIGHT (TIME_HEIGHT + DATE_HEIGHT)
#define KEY_TEMPERATURE 0
#define KEY_SUMMARY 1
#define KEY_CITY 2
#define KEY_LOADING_WEATHER 3
#define KEY_FAILED_WEATHER 4
#define KEY_LAST_UPDATED 5
#define KEY_RATE 6
#define KEY_LOADING_RATE 7
#define KEY_FAILED_RATE 8
#define KEY_FAILED_LOCATION 9

#define WEATHER_CACHE_AGE_SEC 30*60*1000
#define RATE_CACHE_AGE_SEC 30*60*1000
#define STORAGE_WEATHER_TIMESTAMP 1
#define STORAGE_WEATHER 2
#define STORAGE_TEMPERATURE 5
#define STORAGE_RATE_TIMESTAMP 3
#define STORAGE_RATE 4

static const int TIME_FONT_ID = RESOURCE_ID_FONT_SUUNTO_NUMBERS_70;
static const int DATE_FONT_ID = RESOURCE_ID_FONT_SUUNTO_32;
static const int WEATHER_FONT_ID = RESOURCE_ID_FONT_SUUNTO_32;
static const int RATE_FONT_ID = RESOURCE_ID_FONT_SUUNTO_32;
static const int SMALL_FONT_ID = RESOURCE_ID_FONT_SUUNTO_21;

static Window *s_main_window;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static Layer *s_extra_layer;
static TextLayer *s_fill_layer;
static TextLayer *s_weather_data_layer;
static TextLayer *s_weather_time_layer;
static TextLayer *s_battery_layer;
static TextLayer *s_rate_layer;
static TextLayer *s_rate_small_layer;
static TextLayer *s_temperature_layer;
static GFont s_time_font;
static GFont s_date_font;
static GFont s_small_font;
static GFont s_weather_font;
static GFont s_rate_font;
static AppTimer* s_extra_layer_timer = NULL;
static int tap_counter = 0;

static void logm(uint8_t level, const char * str) {
#ifdef DEBUG
  APP_LOG(level, str);
#endif
}

static void update_time(struct tm *tick_time) {
  static char time_buffer[] = TIME_INIT_STR;
  strftime(time_buffer, sizeof(time_buffer), clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);
  
  static char date_buffer[] = DATE_INIT_STR;
  strftime(date_buffer, sizeof(date_buffer), "%a %d.%m", tick_time);

  text_layer_set_text(s_time_layer, time_buffer);
  text_layer_set_text(s_date_layer, date_buffer);
}

static void update_weather_info(char* weather_data, char* temperature, time_t last_updated) {
  text_layer_set_text(s_weather_data_layer, weather_data);
  text_layer_set_text(s_temperature_layer, temperature);
  
  struct tm *last_updated_time = localtime(&last_updated);
  static char weather_time_buffer[] = WEATHER_TIME_INIT_STR;
  strftime(weather_time_buffer, 
           sizeof(weather_time_buffer), 
           clock_is_24h_style() ? "%d.%m %H:%M" : "%d.%m %I:%M", last_updated_time);
  text_layer_set_text(s_weather_time_layer, weather_time_buffer);
}

static void update_rate_info(char* rate) {
  static char rate_buffer[32];
  snprintf(rate_buffer, sizeof(rate_buffer), "USD/RUB: %s", rate);
  
  text_layer_set_text(s_rate_layer, rate_buffer);
  text_layer_set_text(s_rate_small_layer, rate);
}

static bool load_info_from_cache() {
  bool weather_cached = false;
  bool rate_cached = false;
  time_t now = time(NULL);
  if(persist_exists(STORAGE_WEATHER_TIMESTAMP)) {
    int32_t timestamp = persist_read_int(STORAGE_WEATHER_TIMESTAMP);
    if(((now - timestamp) < WEATHER_CACHE_AGE_SEC) && persist_exists(STORAGE_WEATHER)) {
      logm(APP_LOG_LEVEL_INFO, "Using weather from cache");
      static char weather_buffer[38];
      static char temperature_buffer[38];
      persist_read_string(STORAGE_WEATHER, weather_buffer, sizeof(weather_buffer));
      persist_read_string(STORAGE_TEMPERATURE, temperature_buffer, sizeof(temperature_buffer));
      update_weather_info(weather_buffer, temperature_buffer, timestamp);
      weather_cached = true;
    }
  }
  if(persist_exists(STORAGE_RATE_TIMESTAMP)) {
    int32_t timestamp = persist_read_int(STORAGE_RATE_TIMESTAMP);
    if(((now - timestamp) < RATE_CACHE_AGE_SEC) && persist_exists(STORAGE_RATE)) {
      logm(APP_LOG_LEVEL_INFO, "Using rate from cache");
      static char rate[38];
      persist_read_string(STORAGE_RATE, rate, sizeof(rate));
      update_rate_info(rate);
      rate_cached = true;
    }
  }
  return weather_cached && rate_cached;
}

static void request_update_online_info() {
  logm(APP_LOG_LEVEL_INFO, "Updating online info...");
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  dict_write_uint8(iter, 0, 0);
  app_message_outbox_send();
}

static void battery_handler(BatteryChargeState bat_state) {
  static char s_bat_buffer[10];
  if (bat_state.is_charging) {
    snprintf(s_bat_buffer, sizeof(s_bat_buffer), "CHARGING");
  } else {
    snprintf(s_bat_buffer, sizeof(s_bat_buffer), "%d %%", bat_state.charge_percent);
  }
  text_layer_set_text(s_battery_layer, s_bat_buffer);
}

static void init_text_layer(TextLayer *text_layer, Layer *parent_layer, 
                            GFont font, char* init_str, GTextAlignment align) {
  text_layer_set_background_color(text_layer, GColorBlack);
  text_layer_set_text_color(text_layer, GColorClear);
  text_layer_set_font(text_layer, font);
  text_layer_set_text_alignment(text_layer, align);
  text_layer_set_text(text_layer, init_str);
  layer_add_child(parent_layer, text_layer_get_layer(text_layer));
}

static void main_window_load(Window *window) {
  s_time_font = fonts_load_custom_font(resource_get_handle(TIME_FONT_ID));
  s_date_font = fonts_load_custom_font(resource_get_handle(DATE_FONT_ID));
  s_weather_font = fonts_load_custom_font(resource_get_handle(WEATHER_FONT_ID));
  s_small_font = fonts_load_custom_font(resource_get_handle(SMALL_FONT_ID));
  s_rate_font = fonts_load_custom_font(resource_get_handle(RATE_FONT_ID));
  
  Layer *window_layer = window_get_root_layer(window);
  GRect window_bounds = layer_get_bounds(window_layer);
  int win_h = window_bounds.size.h;
  int win_w = window_bounds.size.w;
  int time_y = (win_h - SUM_HEIGHT) / 2 - 10;
  int date_y = time_y + TIME_HEIGHT;
  
  s_time_layer = text_layer_create(GRect(4, time_y, win_w, TIME_HEIGHT));
  init_text_layer(s_time_layer, window_layer, s_time_font, TIME_INIT_STR, GTextAlignmentCenter);
  
  s_date_layer = text_layer_create(GRect(4, date_y, win_w, DATE_HEIGHT));
  init_text_layer(s_date_layer, window_layer, s_date_font, DATE_INIT_STR, GTextAlignmentCenter);

  s_extra_layer = layer_create(GRect(0, 0, win_w, win_h));
  
  s_fill_layer = text_layer_create(GRect(0, 0, win_w, win_h));
  text_layer_set_background_color(s_fill_layer, GColorBlack);
  layer_add_child(s_extra_layer, text_layer_get_layer(s_fill_layer));
  
  int rate_y = BAT_INFO_HEIGHT - 5;//date_y + DATE_HEIGHT;
  //int weather_y = (win_h - WEATHER_HEIGHT) / 2;//date_y + DATE_HEIGHT;
  int weather_y = rate_y + RATE_HEIGHT;//date_y + DATE_HEIGHT;
  
  s_weather_data_layer = text_layer_create(GRect(1, weather_y, win_w, WEATHER_HEIGHT));
  init_text_layer(s_weather_data_layer, s_extra_layer, s_weather_font, WEATHER_INIT_STR, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_weather_data_layer, GTextOverflowModeWordWrap);
  
  s_weather_time_layer = text_layer_create(GRect(1, win_h - WEATHER_TIME_HEIGHT, win_w, WEATHER_TIME_HEIGHT));
  init_text_layer(s_weather_time_layer, s_extra_layer, s_small_font, WEATHER_TIME_INIT_STR, GTextAlignmentCenter);
  
  s_battery_layer = text_layer_create(GRect(1, -6, win_w / 2, BAT_INFO_HEIGHT));
  init_text_layer(s_battery_layer, s_extra_layer, s_small_font, BAT_INFO_INIT_STR, GTextAlignmentLeft);
  
  s_temperature_layer = text_layer_create(GRect(win_w / 2, -6, win_w / 2, TEMPERATURE_HEIGHT));
  init_text_layer(s_temperature_layer, s_extra_layer, s_small_font, TEMPERATURE_INIT_STR, GTextAlignmentRight);
  
  s_rate_layer = text_layer_create(GRect(0, rate_y, win_w, RATE_HEIGHT));
  init_text_layer(s_rate_layer, s_extra_layer, s_rate_font, RATE_INIT_STR, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_rate_layer, GTextOverflowModeWordWrap);
  
  s_rate_small_layer = text_layer_create(GRect(0, win_h - RATE_SMALL_HEIGHT, win_w, RATE_SMALL_HEIGHT));
  init_text_layer(s_rate_small_layer, window_layer, s_rate_font, RATE_INIT_STR, GTextAlignmentCenter);
  layer_set_hidden((Layer*)s_rate_small_layer, true);
  
  layer_set_hidden(s_extra_layer, true);
  layer_add_child(window_layer, s_extra_layer);

  time_t now = time(NULL);
  update_time(localtime(&now));
  load_info_from_cache();
  battery_handler(battery_state_service_peek());
}

static void main_window_unload(Window *window) {
  fonts_unload_custom_font(s_time_font);
  fonts_unload_custom_font(s_date_font);
  fonts_unload_custom_font(s_weather_font);
  fonts_unload_custom_font(s_small_font);
  fonts_unload_custom_font(s_rate_font);
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_weather_data_layer);
  text_layer_destroy(s_weather_time_layer);
  text_layer_destroy(s_battery_layer);
  text_layer_destroy(s_rate_layer);
  text_layer_destroy(s_rate_small_layer);
  text_layer_destroy(s_temperature_layer);
  text_layer_destroy(s_fill_layer); 
  layer_destroy(s_extra_layer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time(tick_time);

  if(tick_time->tm_min % 30 == 0) {
    request_update_online_info();
  }
}

void hide_extra_layer(void *data) {
  s_extra_layer_timer = 0;
  tap_counter = 0;
  layer_set_hidden(s_extra_layer, true);
}

void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  tap_counter++;
  if(tap_counter == 2) {
    request_update_online_info();
  }
  layer_set_hidden(s_extra_layer, false);
  if(s_extra_layer_timer) {
    app_timer_cancel(s_extra_layer_timer);
    s_extra_layer_timer = 0;
  }
  s_extra_layer_timer = app_timer_register(2500, hide_extra_layer, NULL);
}

static void inbox_received_callback(DictionaryIterator *dict, void *context) {
  logm(APP_LOG_LEVEL_INFO, "Received message");
  if(dict_find(dict, KEY_LOADING_WEATHER)) {
    logm(APP_LOG_LEVEL_INFO, "Received message: loading weather");
    text_layer_set_text(s_weather_data_layer, WEATHER_LOADING_STR);
    return;
  }
  if(dict_find(dict, KEY_LOADING_RATE)) {
    logm(APP_LOG_LEVEL_INFO, "Received message: loading rate");
    text_layer_set_text(s_rate_layer, RATE_LOADING_STR);
    return;
  }
  if(dict_find(dict, KEY_FAILED_WEATHER)) {
    logm(APP_LOG_LEVEL_INFO, "Received message: failed weather");
    text_layer_set_text(s_weather_data_layer, WEATHER_FAILED_STR);
    return;
  }
  if(dict_find(dict, KEY_FAILED_RATE)) {
    logm(APP_LOG_LEVEL_INFO, "Received message: failed rate");
    text_layer_set_text(s_rate_layer, RATE_FAILED_STR);
    return;
  }
  if(dict_find(dict, KEY_FAILED_LOCATION)) {
    logm(APP_LOG_LEVEL_INFO, "Received message: failed location");
    text_layer_set_text(s_weather_data_layer, WEATHER_FAILED_STR);
    return;
  }
  
  Tuple *temp = dict_find(dict, KEY_TEMPERATURE);
  if(temp) {
    static char temperature_buffer[8];
    static char summary_buffer[32];
//    static char city_buffer[30];
//    static char weather_buffer[38];
    int32_t last_updated = time(NULL);
    
    snprintf(temperature_buffer, sizeof(temperature_buffer), "%ld\xC2\xA0\xC2\xB0""C", temp->value->int32);
    snprintf(summary_buffer, sizeof(summary_buffer), "%s", dict_find(dict, KEY_SUMMARY)->value->cstring);
//    snprintf(city_buffer, sizeof(city_buffer), "%s", dict_find(dict, KEY_CITY)->value->cstring);
   
//    snprintf(weather_buffer, sizeof(weather_buffer), 
//             "%s\n%s", summary_buffer, temperature_buffer);
    update_weather_info(summary_buffer, temperature_buffer, last_updated);
    
    persist_write_string(STORAGE_WEATHER, summary_buffer);
    persist_write_string(STORAGE_TEMPERATURE, temperature_buffer);
    persist_write_int(STORAGE_WEATHER_TIMESTAMP, last_updated);
  }
  
  Tuple *rate = dict_find(dict, KEY_RATE);
  if(rate) {
    int32_t last_updated = time(NULL);
    update_rate_info(rate->value->cstring);
    persist_write_string(STORAGE_RATE, rate->value->cstring);
    persist_write_int(STORAGE_RATE_TIMESTAMP, last_updated);
  }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  logm(APP_LOG_LEVEL_ERROR, "Message dropped!");
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
  logm(APP_LOG_LEVEL_ERROR, "Outbox send failed!");
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {
  logm(APP_LOG_LEVEL_INFO, "Outbox send success!");
}

static void init() {
  s_main_window = window_create();

  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });

  window_stack_push(s_main_window, true);
  window_set_background_color(s_main_window, GColorBlack);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_register_outbox_sent(outbox_sent_callback);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());

  accel_tap_service_subscribe(accel_tap_handler);
  battery_state_service_subscribe(battery_handler);
}

static void deinit() {
  window_destroy(s_main_window);
  tick_timer_service_unsubscribe();
  app_message_deregister_callbacks();
  accel_tap_service_unsubscribe();
  battery_state_service_unsubscribe();
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
