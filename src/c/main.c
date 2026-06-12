#include <pebble.h>

#define MAX_TASKS 10
#define NAME_LEN 32

#define SEND_RETRY_MS 2500
#define MAX_AUTO_RETRIES 3

#define PERSIST_KEY_AUTO_RETURN 1

// Menu geometry. While tracking, the header collapses and row 0 is a combined
// status+Stop row (task + big worked time + end estimate; select = Stop), so the
// status doesn't pay for a separate Stop row. Task rows are two lines: name on
// top, a smaller time/percent stats line below.
#define HEADER_H      46
#define STATUS_ROW_H  44   // connecting/error/no-tasks row (title + subtitle)
#define STOP_ROW_H    48   // combined status+Stop row while tracking
#define TASK_ROW_H    42   // name + small stats line

static Window *s_window;
static MenuLayer *s_menu;

// Connection/data state. The menu always renders at least one row (a status
// row) so the app never shows a blank white screen, even when the phone or
// trackworktime is not responding.
typedef enum { CONN_LOADING, CONN_READY, CONN_ERROR } ConnState;
static ConnState s_conn = CONN_LOADING;

static bool s_tracking = false;
static char s_status_name[NAME_LEN + 1] = "";
static int s_worked_min = 0;

// End-of-workday estimate: minutes remaining today, from the phone
// (TimerManager.getMinutesRemaining(); auto-pause/flexi aware). May be
// negative = overtime. REMAIN_NONE = no estimate (non-work day / old app).
// end-of-day = s_remain_recv + s_remain_min*60: constant while tracking, an
// "if you resume now" estimate while stopped.
#define REMAIN_NONE INT32_MIN
static int32_t s_remain_min = REMAIN_NONE;
static time_t s_remain_recv = 0;

static int s_task_count = 0;
static int32_t s_task_ids[MAX_TASKS];
static char s_task_names[MAX_TASKS][NAME_LEN + 1];
static int s_task_worked_min[MAX_TASKS];
static int s_task_percent[MAX_TASKS];   // % of budget; -1 = no budget
static int s_task_day_pct[MAX_TASKS];   // % of today's total; -1 = unknown/none

// Last command, kept so an auto-retry can re-send the same request.
static uint8_t s_last_cmd = 1;
static int32_t s_last_task = 0;
static int s_retries = 0;
static AppTimer *s_retry_timer = NULL;

// After a task-select (cmd 2) or stop (cmd 3) is confirmed by the phone, exit
// back to the watchface automatically. A plain refresh (cmd 1) keeps the menu.
static bool s_exit_on_response = false;

// User setting (Clay config). When false, never auto-exit. Default on; persisted.
static bool s_auto_return = true;

static bool has_stop_row();
static void send_cmd(uint8_t cmd, int32_t taskId);

// Number of "real" content rows (Stop + tasks), excluding the status row.
static int content_rows() { return (has_stop_row() ? 1 : 0) + s_task_count; }
static bool showing_status_row() { return content_rows() == 0; }

// Issue a fresh request: reset the auto-retry budget, then send.
static void request(uint8_t cmd, int32_t taskId) {
  s_exit_on_response = (cmd == 2 || cmd == 3);
  s_retries = 0;
  send_cmd(cmd, taskId);
}

static void send_cmd(uint8_t cmd, int32_t taskId) {
  s_last_cmd = cmd;
  s_last_task = taskId;
  s_conn = CONN_LOADING;
  if (s_menu) menu_layer_reload_data(s_menu);

  DictionaryIterator *iter;
  AppMessageResult r = app_message_outbox_begin(&iter);
  if (r != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "outbox_begin failed: %d", (int)r);
    return;
  }
  // CMD is int32 (not uint8) so the PebbleKit Android 2 side reads it as a plain Integer.
  dict_write_int32(iter, MESSAGE_KEY_CMD, cmd);
  if (cmd == 2) {
    dict_write_int32(iter, MESSAGE_KEY_CMD_TASK_ID, taskId);
  }
  app_message_outbox_send();
}

static void retry_timer_cb(void *data) {
  s_retry_timer = NULL;
  send_cmd(s_last_cmd, s_last_task);  // keeps current retry budget
}

static void parse_task_list(const char *s) {
  s_task_count = 0;
  if (!s || s[0] == '\0') return;
  // 48 slack/row = tab+id(10)+tab+min(10)+tab+pct(10)+tab+dayPct(10)+nl, covering the
  // 5-field row format. (The AppMessage inbox is capped at 512 B anyway, so the input
  // never reaches this size.)
  static char buf[(NAME_LEN + 48) * MAX_TASKS];
  strncpy(buf, s, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char *line = buf;
  while (line && *line && s_task_count < MAX_TASKS) {
    char *nl = strchr(line, '\n');
    if (nl) *nl = '\0';
    char *tab = strchr(line, '\t');
    if (tab) {
      *tab = '\0';
      s_task_ids[s_task_count] = atoi(line);
      char *name = tab + 1;
      int worked = 0, pct = -1, day_pct = -1;
      char *tab2 = strchr(name, '\t');
      if (tab2) {
        *tab2 = '\0';
        char *minutes = tab2 + 1;
        char *tab3 = strchr(minutes, '\t');
        if (tab3) {
          *tab3 = '\0';
          pct = atoi(tab3 + 1);
          char *tab4 = strchr(tab3 + 1, '\t');
          if (tab4) day_pct = atoi(tab4 + 1);  // absent on an old phone app -> -1
        }
        worked = atoi(minutes);
      }
      s_task_worked_min[s_task_count] = worked;
      s_task_percent[s_task_count] = pct;
      s_task_day_pct[s_task_count] = day_pct;
      strncpy(s_task_names[s_task_count], name, NAME_LEN);
      s_task_names[s_task_count][NAME_LEN] = '\0';
      s_task_count++;
    }
    line = nl ? nl + 1 : NULL;
  }
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t;
  if ((t = dict_find(iter, MESSAGE_KEY_ST_TRACKING))) s_tracking = (t->value->uint8 != 0);
  if ((t = dict_find(iter, MESSAGE_KEY_ST_TASK_NAME))) {
    strncpy(s_status_name, t->value->cstring, NAME_LEN);
    s_status_name[NAME_LEN] = '\0';
  }
  if ((t = dict_find(iter, MESSAGE_KEY_ST_WORKED_MIN))) s_worked_min = (int)t->value->int32;
  if ((t = dict_find(iter, MESSAGE_KEY_ST_REMAIN_MIN))) {
    s_remain_min = t->value->int32;
    s_remain_recv = time(NULL);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_TASK_LIST))) parse_task_list(t->value->cstring);
  if ((t = dict_find(iter, MESSAGE_KEY_CFG_AUTO_RETURN))) {
    s_auto_return = (t->value->int32 != 0);
    persist_write_bool(PERSIST_KEY_AUTO_RETURN, s_auto_return);
    APP_LOG(APP_LOG_LEVEL_INFO, "CFG_AUTO_RETURN = %d", (int)s_auto_return);
  }

  // Got a response: connection is healthy, cancel any pending retry.
  s_conn = CONN_READY;
  s_retries = 0;
  if (s_retry_timer) { app_timer_cancel(s_retry_timer); s_retry_timer = NULL; }

  if (s_menu) menu_layer_reload_data(s_menu);

  // A task-select / stop succeeded: drop back to the watchface (if enabled).
  if (s_exit_on_response) {
    s_exit_on_response = false;
    if (s_auto_return) window_stack_pop_all(true);
  }
}

static void inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "inbox_dropped reason=%d", (int)reason);
}

static void outbox_failed(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "outbox_failed reason=%d (retry %d/%d)",
          (int)reason, s_retries, MAX_AUTO_RETRIES);
  if (s_retries < MAX_AUTO_RETRIES) {
    s_retries++;
    if (s_retry_timer) app_timer_cancel(s_retry_timer);
    s_retry_timer = app_timer_register(SEND_RETRY_MS, retry_timer_cb, NULL);
    // stay in CONN_LOADING while retrying
  } else {
    s_conn = CONN_ERROR;
    if (s_menu) menu_layer_reload_data(s_menu);
  }
}

static void outbox_sent(DictionaryIterator *iter, void *context) {
}

static bool has_stop_row() { return s_tracking; }
static int task_index_for_row(int row) { return has_stop_row() ? row - 1 : row; }

static uint16_t menu_num_rows(MenuLayer *m, uint16_t section, void *ctx) {
  int c = content_rows();
  return c == 0 ? 1 : c;  // never zero -> never a blank screen
}

static int16_t menu_header_height(MenuLayer *m, uint16_t section, void *ctx) {
  // Header visibility tracks DATA, not connection state, so neither the initial
  // connect nor an in-flight command (which both flip s_conn to CONN_LOADING)
  // ever flashes a branding header over what's on screen:
  //  - no content yet (connecting / error / no tasks): the status row carries
  //    the message -> no header.
  //  - while tracking: status lives in the combined row 0 -> no header.
  //  - idle with tasks: the header shows "Stopped" + worked time.
  if (showing_status_row() || has_stop_row()) return 0;
  return HEADER_H;
}

static int16_t menu_get_cell_height(MenuLayer *m, MenuIndex *idx, void *ctx) {
  if (showing_status_row()) return STATUS_ROW_H;
  if (has_stop_row() && idx->row == 0) return STOP_ROW_H;
  return TASK_ROW_H;
}

// "→16:33" (estimated end of workday) or "+0:43" (overtime past the target).
// Returns false when there is no estimate.
static bool format_end_estimate(char *buf, size_t len) {
  if (s_remain_min == REMAIN_NONE) return false;
  if (s_remain_min > 0) {
    time_t end = s_remain_recv + (time_t)s_remain_min * 60;
    struct tm *lt = localtime(&end);
    int h = lt->tm_hour;
    if (!clock_is_24h_style()) { h %= 12; if (h == 0) h = 12; }
    snprintf(buf, len, "\xE2\x86\x92%d:%02d", h, lt->tm_min);
  } else {
    int over = (int)-s_remain_min;
    snprintf(buf, len, "+%d:%02d", over / 60, over % 60);
  }
  return true;
}

static void menu_draw_header(GContext *ctx, const Layer *cell, uint16_t section, void *c) {
  GRect b = layer_get_bounds(cell);
  graphics_context_set_text_color(ctx, GColorBlack);

  // The header is only laid out in the idle-with-tasks state (see
  // menu_header_height), so there's no loading/error branding to draw here.
  // Small label (task or "Stopped") on top, big worked-today time below.
  const char *label = s_tracking ? s_status_name : "Stopped";
  graphics_draw_text(ctx, label,
      fonts_get_system_font(FONT_KEY_GOTHIC_18),
      GRect(4, -2, b.size.w - 8, 20),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

  // Right side of the big-time row: estimated end of workday ("→16:33"),
  // or overtime past the daily target ("+0:43").
  const int est_w = 70;
  static char endbuf[12];
  if (format_end_estimate(endbuf, sizeof(endbuf))) {
    graphics_draw_text(ctx, endbuf,
        fonts_get_system_font(FONT_KEY_GOTHIC_18),
        GRect(b.size.w - est_w - 4, 20, est_w, 22),
        GTextOverflowModeFill, GTextAlignmentRight, NULL);
  }

  static char timebuf[12];
  snprintf(timebuf, sizeof(timebuf), "%d:%02d", s_worked_min / 60, s_worked_min % 60);
  graphics_draw_text(ctx, timebuf,
      fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
      GRect(4, 14, b.size.w - 8 - est_w, 32),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

static void menu_draw_row(GContext *ctx, const Layer *cell, MenuIndex *idx, void *c) {
  if (showing_status_row()) {
    const char *title, *subtitle;
    switch (s_conn) {
      case CONN_ERROR:
        title = "No phone connection"; subtitle = "Tap to retry"; break;
      case CONN_READY:
        title = "No tasks"; subtitle = "Tap to refresh"; break;
      case CONN_LOADING:
      default:
        title = "Connecting..."; subtitle = "Waiting for phone"; break;
    }
    menu_cell_basic_draw(ctx, cell, title, subtitle, NULL);
    return;
  }

  GRect b = layer_get_bounds(cell);
  GColor fg = menu_cell_layer_is_highlighted(cell) ? GColorWhite : GColorBlack;
  graphics_context_set_text_color(ctx, fg);

  if (has_stop_row() && idx->row == 0) {
    // Combined status+Stop row: current task + "■ Stop" hint on top, big
    // worked-today time + end estimate below. Selecting it stops tracking.
    const int sq = 10;
    const int stop_w = 52;  // square + "Stop", top right
    graphics_context_set_fill_color(ctx, fg);
    graphics_fill_rect(ctx, GRect(b.size.w - stop_w - 4, 5, sq, sq), 1, GCornersAll);
    graphics_draw_text(ctx, "Stop",
        fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
        GRect(b.size.w - stop_w - 4 + sq + 3, -2, stop_w - sq - 3 + 4, 20),
        GTextOverflowModeFill, GTextAlignmentLeft, NULL);
    graphics_draw_text(ctx, s_status_name,
        fonts_get_system_font(FONT_KEY_GOTHIC_18),
        GRect(4, -2, b.size.w - stop_w - 12, 20),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    const int est_w = 70;
    static char endbuf[12];
    if (format_end_estimate(endbuf, sizeof(endbuf))) {
      graphics_draw_text(ctx, endbuf,
          fonts_get_system_font(FONT_KEY_GOTHIC_18),
          GRect(b.size.w - est_w - 4, 22, est_w, 22),
          GTextOverflowModeFill, GTextAlignmentRight, NULL);
    }
    static char timebuf[12];
    snprintf(timebuf, sizeof(timebuf), "%d:%02d", s_worked_min / 60, s_worked_min % 60);
    graphics_draw_text(ctx, timebuf,
        fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD),
        GRect(4, 14, b.size.w - 8 - est_w, 32),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    return;
  }

  int ti = task_index_for_row(idx->row);
  if (ti >= 0 && ti < s_task_count) {
    // Two lines: name on top, smaller stats line below. Fixed "d"/"b" slots keep
    // the day and budget percents column-comparable; "---" = not available.
    graphics_draw_text(ctx, s_task_names[ti],
        fonts_get_system_font(FONT_KEY_GOTHIC_24),
        GRect(8, -4, b.size.w - 12, 28),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    char dbuf[8] = "---", bbuf[8] = "---";
    if (s_task_day_pct[ti] >= 0) {
      snprintf(dbuf, sizeof(dbuf), "%d%%", s_task_day_pct[ti] > 999 ? 999 : s_task_day_pct[ti]);
    }
    if (s_task_percent[ti] >= 0) {
      snprintf(bbuf, sizeof(bbuf), "%d%%", s_task_percent[ti] > 999 ? 999 : s_task_percent[ti]);
    }
    int wm = s_task_worked_min[ti];
    static char sb[40];
    snprintf(sb, sizeof(sb), "%d:%02d \xC2\xB7 d %s \xC2\xB7 b %s",
             wm / 60, wm % 60, dbuf, bbuf);
    graphics_draw_text(ctx, sb,
        fonts_get_system_font(FONT_KEY_GOTHIC_14),
        GRect(8, 21, b.size.w - 12, 17),
        GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  }
}

static void menu_select(MenuLayer *m, MenuIndex *idx, void *c) {
  if (showing_status_row()) {
    request(1, 0);  // manual refresh / retry
    return;
  }
  if (has_stop_row() && idx->row == 0) {
    request(3, 0);
    return;
  }
  int ti = task_index_for_row(idx->row);
  if (ti >= 0 && ti < s_task_count) {
    request(2, s_task_ids[ti]);
  }
}

static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  s_menu = menu_layer_create(b);
  menu_layer_set_callbacks(s_menu, NULL, (MenuLayerCallbacks) {
    .get_num_rows = menu_num_rows,
    .get_header_height = menu_header_height,
    .get_cell_height = menu_get_cell_height,
    .draw_header = menu_draw_header,
    .draw_row = menu_draw_row,
    .select_click = menu_select,
  });
  menu_layer_set_click_config_onto_window(s_menu, w);
  layer_add_child(root, menu_layer_get_layer(s_menu));
}

static void window_unload(Window *w) {
  menu_layer_destroy(s_menu);
  s_menu = NULL;
}

static void init(void) {
  app_message_register_inbox_received(inbox_received);
  app_message_register_inbox_dropped(inbox_dropped);
  app_message_register_outbox_failed(outbox_failed);
  app_message_register_outbox_sent(outbox_sent);
  app_message_open(512, 64);

  if (persist_exists(PERSIST_KEY_AUTO_RETURN)) {
    s_auto_return = persist_read_bool(PERSIST_KEY_AUTO_RETURN);
  }

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

#ifdef SCREENSHOT_FIXTURES
  // Demo state for appstore screenshots (no phone): tracking active with a task list.
  s_conn = CONN_READY;
  s_tracking = true;
  strncpy(s_status_name, "Client work", NAME_LEN); s_status_name[NAME_LEN] = '\0';
  s_worked_min = 372;          // 6:12 worked today
  s_remain_min = 78;           // end-of-day estimate (~1:18 left)
  s_remain_recv = time(NULL);
  s_task_count = 3;
  s_task_ids[0] = 1; strncpy(s_task_names[0], "Client work", NAME_LEN); s_task_names[0][NAME_LEN] = '\0';
  s_task_worked_min[0] = 145; s_task_percent[0] = 60; s_task_day_pct[0] = 39;
  s_task_ids[1] = 2; strncpy(s_task_names[1], "Email", NAME_LEN); s_task_names[1][NAME_LEN] = '\0';
  s_task_worked_min[1] = 47;  s_task_percent[1] = -1; s_task_day_pct[1] = 13;
  s_task_ids[2] = 3; strncpy(s_task_names[2], "Meetings", NAME_LEN); s_task_names[2][NAME_LEN] = '\0';
  s_task_worked_min[2] = 92;  s_task_percent[2] = 80; s_task_day_pct[2] = 25;
  if (s_menu) menu_layer_reload_data(s_menu);
#else
  request(1, 0);
#endif
}

static void deinit(void) {
  if (s_retry_timer) { app_timer_cancel(s_retry_timer); s_retry_timer = NULL; }
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
