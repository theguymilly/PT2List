#include <pebble.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Limits (kept modest so the app fits comfortably in watch RAM)
// ---------------------------------------------------------------------------
#define MAX_LISTS 10
#define MAX_ITEMS 20
#define NAME_LEN  24
#define TEXT_LEN  64     // 63 characters per item; long items wrap onto more lines

// AppMessage commands (must match src/pkjs/index.js)
enum {
  CMD_RESET = 1,   // phone -> watch: clear everything, carries UPPER + FONT
  CMD_LIST  = 2,   // phone -> watch: LIST, TEXT(name)
  CMD_ITEM  = 3,   // phone -> watch: LIST, ITEM, TEXT   (only items that are NOT lined through)
  CMD_END   = 4,   // phone -> watch: transfer finished, save + redraw
  CMD_CROSS = 10   // watch -> phone: LIST, ITEM, TEXT  (I crossed this item out)
};

// Persistent storage keys
#define P_META 1
#define P_NAME 10    // + list
#define P_ITEM 100   // + list * MAX_ITEMS + item

typedef struct {
  uint8_t nlists;
  uint8_t count[MAX_LISTS];
  uint8_t upper;
  uint8_t font;
} Meta;

static char    s_names[MAX_LISTS][NAME_LEN];
static char    s_text[MAX_LISTS][MAX_ITEMS][TEXT_LEN];
static uint8_t s_count[MAX_LISTS];
static uint8_t s_nlists = 0;
static bool    s_upper = true;
static uint8_t s_font = 1;          // 0 small, 1 medium, 2 large
static int16_t s_w = 144;           // screen width, set when a window loads

static Window    *s_list_window;
static Window    *s_item_window;
static MenuLayer *s_list_menu;
static MenuLayer *s_item_menu;
static TextLayer *s_title;
static char       s_title_buf[NAME_LEN + 1];
static int        s_cur_list = 0;

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------
static void save_meta(void) {
  Meta m;
  memset(&m, 0, sizeof(m));
  m.nlists = s_nlists;
  memcpy(m.count, s_count, sizeof(m.count));
  m.upper = s_upper ? 1 : 0;
  m.font = s_font;
  persist_write_data(P_META, &m, sizeof(m));
}

static void save_all(void) {
  save_meta();
  for (int l = 0; l < s_nlists; l++) {
    persist_write_data(P_NAME + l, s_names[l], NAME_LEN);
    for (int i = 0; i < s_count[l]; i++) {
      persist_write_data(P_ITEM + l * MAX_ITEMS + i, s_text[l][i], TEXT_LEN);
    }
  }
}

static void load_all(void) {
  Meta m;
  if (persist_read_data(P_META, &m, sizeof(m)) != (int)sizeof(m)) return;
  s_nlists = m.nlists > MAX_LISTS ? MAX_LISTS : m.nlists;
  s_upper = m.upper != 0;
  s_font = m.font > 2 ? 1 : m.font;
  for (int l = 0; l < s_nlists; l++) {
    s_count[l] = m.count[l] > MAX_ITEMS ? MAX_ITEMS : m.count[l];
    persist_read_data(P_NAME + l, s_names[l], NAME_LEN);
    s_names[l][NAME_LEN - 1] = 0;
    for (int i = 0; i < s_count[l]; i++) {
      persist_read_data(P_ITEM + l * MAX_ITEMS + i, s_text[l][i], TEXT_LEN);
      s_text[l][i][TEXT_LEN - 1] = 0;
    }
  }
}


// ---------------------------------------------------------------------------
// Outgoing (watch -> phone): "I crossed this item out"
// Small queue with retries so quick presses are not lost.
// ---------------------------------------------------------------------------
typedef struct { uint8_t list, item; char text[TEXT_LEN]; } Out;
#define OUTQ 8
static Out       s_outq[OUTQ];
static uint8_t   s_out_head = 0, s_out_len = 0, s_out_tries = 0;
static bool      s_out_busy = false;
static AppTimer *s_out_timer = NULL;

static void out_pump(void);

static void out_pop(void) {
  s_out_head = (s_out_head + 1) % OUTQ;
  s_out_len--;
  s_out_tries = 0;
}

static void out_retry(void *data) {
  s_out_timer = NULL;
  out_pump();
}

static void out_fail(void) {
  s_out_busy = false;
  if (++s_out_tries >= 3) {          // phone unreachable: give up on this one and warn
    out_pop();
    vibes_double_pulse();
    out_pump();
  } else {
    s_out_timer = app_timer_register(1500, out_retry, NULL);
  }
}

static void out_pump(void) {
  if (s_out_busy || s_out_len == 0 || s_out_timer) return;
  Out *o = &s_outq[s_out_head];
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) { out_fail(); return; }
  dict_write_uint8(it, MESSAGE_KEY_CMD, CMD_CROSS);
  dict_write_uint8(it, MESSAGE_KEY_LIST, o->list);
  dict_write_uint8(it, MESSAGE_KEY_ITEM, o->item);
  dict_write_cstring(it, MESSAGE_KEY_TEXT, o->text);
  if (app_message_outbox_send() == APP_MSG_OK) s_out_busy = true;
  else out_fail();
}

static void out_push(uint8_t list, uint8_t item, const char *text) {
  if (s_out_len >= OUTQ) return;
  Out *o = &s_outq[(s_out_head + s_out_len) % OUTQ];
  o->list = list;
  o->item = item;
  strncpy(o->text, text, TEXT_LEN - 1);
  o->text[TEXT_LEN - 1] = 0;
  s_out_len++;
  out_pump();
}

static void outbox_sent(DictionaryIterator *it, void *ctx) {
  s_out_busy = false;
  out_pop();
  out_pump();
}
static void outbox_failed(DictionaryIterator *it, AppMessageResult reason, void *ctx) {
  out_fail();
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------
#define PAD_X 8
#define PAD_Y 6

static GFont main_font(void) {
  static const char *keys[] = { FONT_KEY_GOTHIC_18_BOLD, FONT_KEY_GOTHIC_24_BOLD, FONT_KEY_GOTHIC_28_BOLD };
  return fonts_get_system_font(keys[s_font]);
}
static int16_t row_height(void) {
  static const int16_t h[] = { 34, 40, 46 };
  return h[s_font];
}
static int16_t font_px(void) {
  static const int16_t h[] = { 20, 26, 30 };
  return h[s_font];
}

static void make_text(char *dst, size_t n, const char *src) {
  size_t i = 0;
  for (; i + 1 < n && src[i]; i++) {
    char c = src[i];
    dst[i] = (s_upper && c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
  }
  dst[i] = 0;
}

static int16_t wrapped_height(const char *text, int16_t width) {
  GSize sz = graphics_text_layout_get_content_size(
      text, main_font(), GRect(0, 0, width, 1000),
      GTextOverflowModeWordWrap, GTextAlignmentLeft);
  return sz.h;
}

static void fill_row(GContext *ctx, const Layer *cell, GRect b) {
  bool hl = menu_cell_layer_is_highlighted(cell);
  GColor fg = hl ? GColorWhite : GColorBlack;
  GColor bg = hl ? GColorBlack : GColorWhite;
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  graphics_context_set_text_color(ctx, fg);
}

// One line, cut off with "..." if too long, with an optional count on the right.
static void draw_list_row(GContext *ctx, const Layer *cell, const char *text, const char *right) {
  GRect b = layer_get_bounds(cell);
  fill_row(ctx, cell, b);
  int16_t right_w = right ? 44 : 0;
  int16_t ty = (b.size.h - font_px()) / 2 - 5;
  GRect tr = GRect(PAD_X, ty, b.size.w - PAD_X - 6 - right_w, font_px() + 8);
  graphics_draw_text(ctx, text, main_font(), tr, GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  if (right) {
    GRect rr = GRect(b.size.w - right_w - 6, (b.size.h - 22) / 2 - 3, right_w, 24);
    graphics_draw_text(ctx, right, fonts_get_system_font(FONT_KEY_GOTHIC_18),
                       rr, GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);
  }
}

// Wraps onto as many lines as it needs.
static void draw_item_row(GContext *ctx, const Layer *cell, const char *text) {
  GRect b = layer_get_bounds(cell);
  fill_row(ctx, cell, b);
  int16_t w = b.size.w - PAD_X - 6;
  int16_t th = wrapped_height(text, w);
  int16_t y = (b.size.h - th) / 2 - 4;
  graphics_draw_text(ctx, text, main_font(), GRect(PAD_X, y, w, th + 8),
                     GTextOverflowModeWordWrap, GTextAlignmentLeft, NULL);
}

// ---------------------------------------------------------------------------
// Lists menu
// ---------------------------------------------------------------------------
static uint16_t list_rows(MenuLayer *m, uint16_t s, void *c) {
  return s_nlists ? s_nlists : 1;   // the lists, or one hint row when empty
}
static int16_t list_height(MenuLayer *m, MenuIndex *i, void *c) { return row_height(); }

static void list_draw(GContext *ctx, const Layer *cell, MenuIndex *idx, void *c) {
  char buf[NAME_LEN + 4];
  if (s_nlists == 0) {
    draw_list_row(ctx, cell, "No lists yet", NULL);
  } else {
    int l = idx->row;
    char right[8];
    snprintf(right, sizeof(right), "%d", s_count[l]);   // items still to do
    make_text(buf, sizeof(buf), s_names[l]);
    draw_list_row(ctx, cell, buf, right);
  }
}

static void item_window_push(void);

static void list_select(MenuLayer *m, MenuIndex *idx, void *c) {
  if (s_nlists > 0 && idx->row < s_nlists) {
    s_cur_list = idx->row;
    item_window_push();
  }
}

static void list_window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_w = layer_get_bounds(root).size.w;
  s_list_menu = menu_layer_create(layer_get_bounds(root));
  menu_layer_set_callbacks(s_list_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows = list_rows,
    .get_cell_height = list_height,
    .draw_row = list_draw,
    .select_click = list_select,
  });
  menu_layer_set_click_config_onto_window(s_list_menu, w);
  layer_add_child(root, menu_layer_get_layer(s_list_menu));
}
static void list_window_unload(Window *w) {
  menu_layer_destroy(s_list_menu);
  s_list_menu = NULL;
}

// ---------------------------------------------------------------------------
// Items menu. Select crosses an item out: it disappears here and is lined
// through on the phone. (Items already lined through on the phone are not sent.)
// ---------------------------------------------------------------------------
static uint16_t item_rows(MenuLayer *m, uint16_t s, void *c) {
  if (s_cur_list >= s_nlists) return 1;
  return s_count[s_cur_list] ? s_count[s_cur_list] : 1;
}

static int16_t item_height(MenuLayer *m, MenuIndex *idx, void *c) {
  if (s_cur_list >= s_nlists || s_count[s_cur_list] == 0) return row_height();
  char buf[TEXT_LEN];
  make_text(buf, sizeof(buf), s_text[s_cur_list][idx->row]);
  int16_t h = wrapped_height(buf, s_w - PAD_X - 6) + 2 * PAD_Y;
  return h > row_height() ? h : row_height();
}

static void item_draw(GContext *ctx, const Layer *cell, MenuIndex *idx, void *c) {
  if (s_cur_list >= s_nlists || s_count[s_cur_list] == 0) {
    draw_item_row(ctx, cell, "Nothing to do");
    return;
  }
  char buf[TEXT_LEN];
  make_text(buf, sizeof(buf), s_text[s_cur_list][idx->row]);
  draw_item_row(ctx, cell, buf);
}

static void item_select(MenuLayer *m, MenuIndex *idx, void *c) {
  if (s_cur_list >= s_nlists) return;
  int l = s_cur_list, i = idx->row;
  if (i >= s_count[l]) return;

  out_push(l, i, s_text[l][i]);                       // tell the phone

  // Completing "TOW M4D" on a list named "3030 S3A11" moves the aircraft:
  // the list becomes "3030 M4D". (The phone applies the same rule.)
  {
    const char *t = s_text[l][i];
    const char *n = s_names[l];
    bool tow = (t[0] == 'T' || t[0] == 't') && (t[1] == 'O' || t[1] == 'o') &&
               (t[2] == 'W' || t[2] == 'w') && t[3] == ' ' && t[4] != 0;
    bool ac = n[0] == '3' && n[1] == '0' && n[2] >= '0' && n[2] <= '9' &&
              n[3] >= '0' && n[3] <= '9' && n[4] == ' ' && n[5] != 0;
    if (tow && ac) {
      char nn[NAME_LEN];
      memset(nn, 0, sizeof(nn));
      memcpy(nn, n, 5);                              // "30dd "
      strncpy(nn + 5, t + 4, NAME_LEN - 6);          // the new location
      memcpy(s_names[l], nn, NAME_LEN);
      s_names[l][NAME_LEN - 1] = 0;
      persist_write_data(P_NAME + l, s_names[l], NAME_LEN);
      if (s_title) {
        make_text(s_title_buf, sizeof(s_title_buf), s_names[l]);
        text_layer_set_text(s_title, s_title_buf);
        layer_mark_dirty(text_layer_get_layer(s_title));
      }
    }
  }

  for (int k = i; k + 1 < s_count[l]; k++) {          // remove it here
    memcpy(s_text[l][k], s_text[l][k + 1], TEXT_LEN);
  }
  s_count[l]--;
  memset(s_text[l][s_count[l]], 0, TEXT_LEN);
  save_meta();
  for (int k = i; k < s_count[l]; k++) {
    persist_write_data(P_ITEM + l * MAX_ITEMS + k, s_text[l][k], TEXT_LEN);
  }
  persist_delete(P_ITEM + l * MAX_ITEMS + s_count[l]);

  vibes_short_pulse();
  menu_layer_reload_data(m);
  if (s_count[l] > 0 && i >= s_count[l]) {
    menu_layer_set_selected_index(m, (MenuIndex){ .section = 0, .row = (uint16_t)(s_count[l] - 1) },
                                  MenuRowAlignCenter, false);
  }
  if (s_list_menu) menu_layer_reload_data(s_list_menu);
}

// Up / Down move through the list; a single Select does nothing, so an item
// can't be removed by accident. DOUBLE-click Select to delete (cross out).
static int16_t title_h(void) { return font_px() + 6; }   // list-name bar: same size as the items

static void item_up(ClickRecognizerRef r, void *c) {
  menu_layer_set_selected_next(s_item_menu, true, MenuRowAlignCenter, true);
}
static void item_down(ClickRecognizerRef r, void *c) {
  menu_layer_set_selected_next(s_item_menu, false, MenuRowAlignCenter, true);
}
static void item_double(ClickRecognizerRef r, void *c) {
  if (!s_item_menu) return;
  MenuIndex idx = menu_layer_get_selected_index(s_item_menu);
  item_select(s_item_menu, &idx, NULL);
}
static void item_click_config(void *c) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, item_up);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, item_down);
  window_multi_click_subscribe(BUTTON_ID_SELECT, 2, 2, 250, true, item_double);
}

static void item_window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  GRect b = layer_get_bounds(root);
  s_w = b.size.w;

  // Name of the list, shown above the items
  make_text(s_title_buf, sizeof(s_title_buf),
            s_cur_list < s_nlists ? s_names[s_cur_list] : "");
  s_title = text_layer_create(GRect(0, 0, b.size.w, title_h()));
  text_layer_set_text(s_title, s_title_buf);
  text_layer_set_font(s_title, main_font());
  text_layer_set_text_alignment(s_title, GTextAlignmentCenter);
  text_layer_set_overflow_mode(s_title, GTextOverflowModeTrailingEllipsis);
  text_layer_set_background_color(s_title, GColorBlack);
  text_layer_set_text_color(s_title, GColorWhite);
  layer_add_child(root, text_layer_get_layer(s_title));

  s_item_menu = menu_layer_create(GRect(0, title_h(), b.size.w, b.size.h - title_h()));
  menu_layer_set_callbacks(s_item_menu, NULL, (MenuLayerCallbacks){
    .get_num_rows = item_rows,
    .get_cell_height = item_height,
    .draw_row = item_draw,
  });
  window_set_click_config_provider(w, item_click_config);
  layer_add_child(root, menu_layer_get_layer(s_item_menu));
}
static void item_window_unload(Window *w) {
  text_layer_destroy(s_title);
  s_title = NULL;
  menu_layer_destroy(s_item_menu);
  s_item_menu = NULL;
}

static void item_window_push(void) {
  if (!s_item_window) {
    s_item_window = window_create();
    window_set_window_handlers(s_item_window, (WindowHandlers){
      .load = item_window_load, .unload = item_window_unload });
  }
  window_stack_push(s_item_window, true);
}

// ---------------------------------------------------------------------------
// Incoming (phone -> watch)
// ---------------------------------------------------------------------------
static int32_t num(DictionaryIterator *it, uint32_t key, int32_t dflt) {
  Tuple *t = dict_find(it, key);
  return t ? t->value->int32 : dflt;
}

static void inbox_received(DictionaryIterator *it, void *ctx) {
  Tuple *c = dict_find(it, MESSAGE_KEY_CMD);
  if (!c) return;
  int cmd = c->value->int32;

  if (cmd == CMD_RESET) {
    memset(s_names, 0, sizeof(s_names));
    memset(s_text, 0, sizeof(s_text));
    memset(s_count, 0, sizeof(s_count));
    s_nlists = 0;
    s_upper = num(it, MESSAGE_KEY_UPPER, 1) != 0;
    int f = num(it, MESSAGE_KEY_FONT, 1);
    s_font = (f < 0 || f > 2) ? 1 : f;
  } else if (cmd == CMD_LIST) {
    int l = num(it, MESSAGE_KEY_LIST, -1);
    Tuple *t = dict_find(it, MESSAGE_KEY_TEXT);
    if (l >= 0 && l < MAX_LISTS && t) {
      strncpy(s_names[l], t->value->cstring, NAME_LEN - 1);
      s_names[l][NAME_LEN - 1] = 0;
      s_count[l] = 0;
      if (l + 1 > s_nlists) s_nlists = l + 1;
    }
  } else if (cmd == CMD_ITEM) {
    int l = num(it, MESSAGE_KEY_LIST, -1);
    int i = num(it, MESSAGE_KEY_ITEM, -1);
    Tuple *t = dict_find(it, MESSAGE_KEY_TEXT);
    if (l >= 0 && l < MAX_LISTS && i >= 0 && i < MAX_ITEMS && t) {
      strncpy(s_text[l][i], t->value->cstring, TEXT_LEN - 1);
      s_text[l][i][TEXT_LEN - 1] = 0;
      if (i + 1 > s_count[l]) s_count[l] = i + 1;
    }
  } else if (cmd == CMD_END) {
    save_all();
    if (s_list_menu) menu_layer_reload_data(s_list_menu);
    if (s_item_menu) menu_layer_reload_data(s_item_menu);
  }
}

// ---------------------------------------------------------------------------
int main(void) {
  load_all();

  app_message_register_inbox_received(inbox_received);
  app_message_register_outbox_sent(outbox_sent);
  app_message_register_outbox_failed(outbox_failed);
  app_message_open(256, 128);

  s_list_window = window_create();
  window_set_window_handlers(s_list_window, (WindowHandlers){
    .load = list_window_load, .unload = list_window_unload });
  window_stack_push(s_list_window, true);

  app_event_loop();

  if (s_item_window) window_destroy(s_item_window);
  window_destroy(s_list_window);
  return 0;
}
