/*
  Projet : Réveil ESP32-S3 + écran tactile 480x320 (paysage) + LVGL

  Dépendances Arduino IDE :
  - LVGL (bibliothèque "lvgl")
  - TFT_eSPI (à configurer pour votre écran ESP32-S3 dans User_Setup.h)

  Notes matérielles :
  - La dalle est utilisée en paysage : largeur logique = 480 px,
    hauteur logique = 320 px.
  - La lecture tactile dépend du contrôleur utilisé par votre module. La fonction
    read_touchscreen() contient un emplacement prêt à adapter.
*/

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>

// -----------------------------------------------------------------------------
// Configuration écran / LVGL
// -----------------------------------------------------------------------------
static constexpr uint16_t SCREEN_WIDTH = 480;
static constexpr uint16_t SCREEN_HEIGHT = 320;
static constexpr uint8_t TFT_ROTATION_LANDSCAPE = 1;

// Tampon partiel : 40 lignes pour limiter la RAM tout en gardant un bon débit.
static constexpr uint16_t DRAW_BUFFER_LINES = 40;

TFT_eSPI tft = TFT_eSPI();

#if LVGL_VERSION_MAJOR >= 9
static lv_display_t *display_drv = nullptr;
static lv_color_t draw_buffer[SCREEN_WIDTH * DRAW_BUFFER_LINES];
#else
static lv_disp_draw_buf_t draw_buffer;
static lv_color_t buf_1[SCREEN_WIDTH * DRAW_BUFFER_LINES];
static lv_disp_drv_t display_drv;
static lv_indev_drv_t indev_drv;
#endif

static lv_indev_t *touch_indev = nullptr;

// -----------------------------------------------------------------------------
// Objets LVGL principaux
// -----------------------------------------------------------------------------
static lv_obj_t *main_screen = nullptr;
static lv_obj_t *clock_label = nullptr;
static lv_obj_t *date_label = nullptr;
static lv_obj_t *alarm_time_label = nullptr;
static lv_obj_t *animation_zone = nullptr;
static lv_obj_t *alarm_button = nullptr;
static lv_obj_t *menu_button = nullptr;

// -----------------------------------------------------------------------------
// Styles séparés pour garder l'interface lisible et extensible
// -----------------------------------------------------------------------------
static lv_style_t style_screen_bg;
static lv_style_t style_clock_zone;
static lv_style_t style_animation_zone;
static lv_style_t style_right_column;
static lv_style_t style_date_box;
static lv_style_t style_button;
static lv_style_t style_clock_text;
static lv_style_t style_small_text;
static lv_style_t style_button_text;

// -----------------------------------------------------------------------------
// État applicatif : prêt pour les futurs écrans menu / réglage / animation
// -----------------------------------------------------------------------------
enum class AppScreen : uint8_t {
  Main,
  Menu,
  AlarmSettings
};

static AppScreen current_screen = AppScreen::Main;

struct AlarmState {
  uint8_t hour = 7;
  uint8_t minute = 30;
  bool enabled = true;
};

static AlarmState alarm_state;

// Horloge logicielle simple initialisée avec l'heure/date de compilation.
// À remplacer plus tard par NTP ou une RTC matérielle.
struct ClockState {
  uint16_t year = 2026;
  uint8_t month = 1;
  uint8_t day = 1;
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
  uint32_t last_tick_ms = 0;
};

static ClockState clock_state;

// -----------------------------------------------------------------------------
// Prototypes
// -----------------------------------------------------------------------------
void create_main_screen();
void update_clock_display();
static void init_styles();
static void show_menu_screen();
static void show_alarm_settings_screen();
static void alarm_button_event_cb(lv_event_t *event);
static void menu_button_event_cb(lv_event_t *event);
#if LVGL_VERSION_MAJOR >= 9
static void display_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data);
#else
static void display_flush_cb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);
static void touch_read_cb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data);
#endif
static bool read_touchscreen(uint16_t *x, uint16_t *y);
static void init_clock_from_compile_time();
static void tick_clock_one_second();
static uint8_t month_from_compile_string(const char *month_text);

// -----------------------------------------------------------------------------
// Setup Arduino
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(100);

  tft.begin();
  tft.setRotation(TFT_ROTATION_LANDSCAPE);
  tft.fillScreen(TFT_LIGHTGREY);

  lv_init();

#if LVGL_VERSION_MAJOR >= 9
  display_drv = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_display_set_flush_cb(display_drv, display_flush_cb);
  lv_display_set_buffers(display_drv, draw_buffer, nullptr, sizeof(draw_buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);

  touch_indev = lv_indev_create();
  lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(touch_indev, touch_read_cb);
#else
  lv_disp_draw_buf_init(&draw_buffer, buf_1, nullptr, SCREEN_WIDTH * DRAW_BUFFER_LINES);
  lv_disp_drv_init(&display_drv);
  display_drv.hor_res = SCREEN_WIDTH;
  display_drv.ver_res = SCREEN_HEIGHT;
  display_drv.flush_cb = display_flush_cb;
  display_drv.draw_buf = &draw_buffer;
  lv_disp_drv_register(&display_drv);

  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touch_read_cb;
  touch_indev = lv_indev_drv_register(&indev_drv);
#endif

  init_clock_from_compile_time();
  init_styles();
  create_main_screen();
}

// -----------------------------------------------------------------------------
// Boucle principale Arduino
// -----------------------------------------------------------------------------
void loop() {
  lv_timer_handler();
  update_clock_display();
  delay(5);
}

// -----------------------------------------------------------------------------
// Création de l'écran principal du réveil
// -----------------------------------------------------------------------------
void create_main_screen() {
  current_screen = AppScreen::Main;

  main_screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(main_screen);
  lv_obj_add_style(main_screen, &style_screen_bg, 0);
  lv_obj_set_size(main_screen, SCREEN_WIDTH, SCREEN_HEIGHT);

  // Zone horloge verte : x=0, y=0, taille 360x90.
  lv_obj_t *clock_zone = lv_obj_create(main_screen);
  lv_obj_remove_style_all(clock_zone);
  lv_obj_add_style(clock_zone, &style_clock_zone, 0);
  lv_obj_set_pos(clock_zone, 0, 0);
  lv_obj_set_size(clock_zone, 360, 90);

  clock_label = lv_label_create(clock_zone);
  lv_obj_add_style(clock_label, &style_clock_text, 0);
  lv_label_set_text(clock_label, "--:--");
  lv_obj_center(clock_label);

  // Zone animation rouge/marron : x=0, y=90, taille 360x230.
  animation_zone = lv_obj_create(main_screen);
  lv_obj_remove_style_all(animation_zone);
  lv_obj_add_style(animation_zone, &style_animation_zone, 0);
  lv_obj_set_pos(animation_zone, 0, 90);
  lv_obj_set_size(animation_zone, 360, 230);

  lv_obj_t *animation_hint = lv_label_create(animation_zone);
  lv_obj_add_style(animation_hint, &style_small_text, 0);
  lv_label_set_text(animation_hint, "Animation\npixel art");
  lv_obj_center(animation_hint);

  // Colonne droite grise : x=360, y=0, taille 120x320.
  lv_obj_t *right_column = lv_obj_create(main_screen);
  lv_obj_remove_style_all(right_column);
  lv_obj_add_style(right_column, &style_right_column, 0);
  lv_obj_set_pos(right_column, 360, 0);
  lv_obj_set_size(right_column, 120, 320);

  // Date en haut de la colonne droite, dans un rectangle vert.
  lv_obj_t *date_box = lv_obj_create(right_column);
  lv_obj_remove_style_all(date_box);
  lv_obj_add_style(date_box, &style_date_box, 0);
  lv_obj_set_pos(date_box, 8, 8);
  lv_obj_set_size(date_box, 104, 40);

  date_label = lv_label_create(date_box);
  lv_obj_add_style(date_label, &style_small_text, 0);
  lv_label_set_text(date_label, "--/--/----");
  lv_obj_center(date_label);

  // Bouton REVEIL jaune avec bordure noire.
  alarm_button = lv_btn_create(right_column);
  lv_obj_remove_style_all(alarm_button);
  lv_obj_add_style(alarm_button, &style_button, 0);
  lv_obj_set_pos(alarm_button, 8, 64);
  lv_obj_set_size(alarm_button, 104, 52);
  lv_obj_add_event_cb(alarm_button, alarm_button_event_cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *alarm_button_label = lv_label_create(alarm_button);
  lv_obj_add_style(alarm_button_label, &style_button_text, 0);
  lv_label_set_text(alarm_button_label, "REVEIL");
  lv_obj_center(alarm_button_label);

  // Affichage HH:MM de l'heure du réveil sous le bouton REVEIL.
  alarm_time_label = lv_label_create(right_column);
  lv_obj_add_style(alarm_time_label, &style_small_text, 0);
  lv_obj_set_pos(alarm_time_label, 16, 128);
  lv_obj_set_size(alarm_time_label, 88, 32);
  lv_obj_set_style_text_align(alarm_time_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(alarm_time_label, "07:30");

  // Bouton MENU en bas de la colonne droite.
  menu_button = lv_btn_create(right_column);
  lv_obj_remove_style_all(menu_button);
  lv_obj_add_style(menu_button, &style_button, 0);
  lv_obj_set_pos(menu_button, 8, 260);
  lv_obj_set_size(menu_button, 104, 52);
  lv_obj_add_event_cb(menu_button, menu_button_event_cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *menu_button_label = lv_label_create(menu_button);
  lv_obj_add_style(menu_button_label, &style_button_text, 0);
  lv_label_set_text(menu_button_label, "MENU");
  lv_obj_center(menu_button_label);

#if LVGL_VERSION_MAJOR >= 9
  lv_screen_load(main_screen);
#else
  lv_scr_load(main_screen);
#endif
  update_clock_display();
}

// -----------------------------------------------------------------------------
// Mise à jour régulière des labels heure/date/réveil
// -----------------------------------------------------------------------------
void update_clock_display() {
  const uint32_t now_ms = millis();
  while (now_ms - clock_state.last_tick_ms >= 1000) {
    clock_state.last_tick_ms += 1000;
    tick_clock_one_second();
  }

  static int16_t previous_minute = -1;
  static uint8_t previous_day = 0;
  static uint8_t previous_alarm_hour = 255;
  static uint8_t previous_alarm_minute = 255;

  if (clock_label != nullptr && previous_minute != clock_state.minute) {
    char time_text[6];
    snprintf(time_text, sizeof(time_text), "%02u:%02u", clock_state.hour, clock_state.minute);
    lv_label_set_text(clock_label, time_text);
    previous_minute = clock_state.minute;
  }

  if (date_label != nullptr && previous_day != clock_state.day) {
    char date_text[11];
    snprintf(date_text, sizeof(date_text), "%02u/%02u/%04u", clock_state.day, clock_state.month, clock_state.year);
    lv_label_set_text(date_label, date_text);
    previous_day = clock_state.day;
  }

  if (alarm_time_label != nullptr &&
      (previous_alarm_hour != alarm_state.hour || previous_alarm_minute != alarm_state.minute)) {
    char alarm_text[6];
    snprintf(alarm_text, sizeof(alarm_text), "%02u:%02u", alarm_state.hour, alarm_state.minute);
    lv_label_set_text(alarm_time_label, alarm_text);
    previous_alarm_hour = alarm_state.hour;
    previous_alarm_minute = alarm_state.minute;
  }
}

// -----------------------------------------------------------------------------
// Initialisation des styles LVGL
// -----------------------------------------------------------------------------
static void init_styles() {
  lv_style_init(&style_screen_bg);
  lv_style_set_bg_color(&style_screen_bg, lv_color_hex(0xD8D8D8));
  lv_style_set_bg_opa(&style_screen_bg, LV_OPA_COVER);

  lv_style_init(&style_clock_zone);
  lv_style_set_bg_color(&style_clock_zone, lv_color_hex(0x56B85A));
  lv_style_set_bg_opa(&style_clock_zone, LV_OPA_COVER);
  lv_style_set_border_width(&style_clock_zone, 0);
  lv_style_set_radius(&style_clock_zone, 0);

  lv_style_init(&style_animation_zone);
  lv_style_set_bg_color(&style_animation_zone, lv_color_hex(0x8F3A2B));
  lv_style_set_bg_opa(&style_animation_zone, LV_OPA_COVER);
  lv_style_set_border_width(&style_animation_zone, 0);
  lv_style_set_radius(&style_animation_zone, 0);

  lv_style_init(&style_right_column);
  lv_style_set_bg_color(&style_right_column, lv_color_hex(0xBDBDBD));
  lv_style_set_bg_opa(&style_right_column, LV_OPA_COVER);
  lv_style_set_border_width(&style_right_column, 0);
  lv_style_set_radius(&style_right_column, 0);

  lv_style_init(&style_date_box);
  lv_style_set_bg_color(&style_date_box, lv_color_hex(0x56B85A));
  lv_style_set_bg_opa(&style_date_box, LV_OPA_COVER);
  lv_style_set_border_width(&style_date_box, 0);
  lv_style_set_radius(&style_date_box, 4);

  lv_style_init(&style_button);
  lv_style_set_bg_color(&style_button, lv_color_hex(0xFFD84D));
  lv_style_set_bg_opa(&style_button, LV_OPA_COVER);
  lv_style_set_border_color(&style_button, lv_color_black());
  lv_style_set_border_width(&style_button, 2);
  lv_style_set_radius(&style_button, 6);
  lv_style_set_pad_all(&style_button, 0);

  lv_style_init(&style_clock_text);
  lv_style_set_text_color(&style_clock_text, lv_color_black());
#if LV_FONT_MONTSERRAT_48
  lv_style_set_text_font(&style_clock_text, &lv_font_montserrat_48);
#else
  lv_style_set_text_font(&style_clock_text, LV_FONT_DEFAULT);
#endif
  lv_style_set_text_align(&style_clock_text, LV_TEXT_ALIGN_CENTER);

  lv_style_init(&style_small_text);
  lv_style_set_text_color(&style_small_text, lv_color_black());
#if LV_FONT_MONTSERRAT_14
  lv_style_set_text_font(&style_small_text, &lv_font_montserrat_14);
#else
  lv_style_set_text_font(&style_small_text, LV_FONT_DEFAULT);
#endif
  lv_style_set_text_align(&style_small_text, LV_TEXT_ALIGN_CENTER);

  lv_style_init(&style_button_text);
  lv_style_set_text_color(&style_button_text, lv_color_black());
#if LV_FONT_MONTSERRAT_16
  lv_style_set_text_font(&style_button_text, &lv_font_montserrat_16);
#else
  lv_style_set_text_font(&style_button_text, LV_FONT_DEFAULT);
#endif
  lv_style_set_text_align(&style_button_text, LV_TEXT_ALIGN_CENTER);
}

// -----------------------------------------------------------------------------
// Callbacks boutons : emplacements prêts pour les futurs écrans
// -----------------------------------------------------------------------------
static void alarm_button_event_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    show_alarm_settings_screen();
  }
}

static void menu_button_event_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    show_menu_screen();
  }
}

static void show_menu_screen() {
  current_screen = AppScreen::Menu;
  Serial.println(F("Ouverture future de l'ecran MENU"));
  // TODO : créer un écran menu dédié puis appeler lv_screen_load(menu_screen).
}

static void show_alarm_settings_screen() {
  current_screen = AppScreen::AlarmSettings;
  Serial.println(F("Ouverture future du reglage du reveil"));
  // TODO : créer un écran de réglage du réveil puis appeler lv_screen_load(alarm_screen).
}

// -----------------------------------------------------------------------------
// Driver affichage LVGL -> TFT_eSPI
// -----------------------------------------------------------------------------
#if LVGL_VERSION_MAJOR >= 9
static void display_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  const uint32_t width = static_cast<uint32_t>(area->x2 - area->x1 + 1);
  const uint32_t height = static_cast<uint32_t>(area->y2 - area->y1 + 1);

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, width, height);
  tft.pushColors(reinterpret_cast<uint16_t *>(px_map), width * height, true);
  tft.endWrite();

  lv_display_flush_ready(disp);
}
#else
static void display_flush_cb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  const uint32_t width = static_cast<uint32_t>(area->x2 - area->x1 + 1);
  const uint32_t height = static_cast<uint32_t>(area->y2 - area->y1 + 1);

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, width, height);
  tft.pushColors(reinterpret_cast<uint16_t *>(color_p), width * height, true);
  tft.endWrite();

  lv_disp_flush_ready(disp);
}
#endif

// -----------------------------------------------------------------------------
// Driver tactile LVGL
// -----------------------------------------------------------------------------
#if LVGL_VERSION_MAJOR >= 9
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
#else
static void touch_read_cb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
  (void)indev_drv;
#endif

  uint16_t touch_x = 0;
  uint16_t touch_y = 0;

  if (read_touchscreen(&touch_x, &touch_y)) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = touch_x;
    data->point.y = touch_y;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

static bool read_touchscreen(uint16_t *x, uint16_t *y) {
  // TODO : adapter cette fonction au contrôleur tactile de votre écran
  // (FT6236, GT911, CST816, XPT2046, etc.).
  // Exemple si votre configuration TFT_eSPI expose getTouch() :
  // return tft.getTouch(x, y);
  (void)x;
  (void)y;
  return false;
}

// -----------------------------------------------------------------------------
// Horloge logicielle minimale
// -----------------------------------------------------------------------------
static void init_clock_from_compile_time() {
  int compile_hour = 0;
  int compile_minute = 0;
  int compile_second = 0;
  sscanf(__TIME__, "%d:%d:%d", &compile_hour, &compile_minute, &compile_second);

  char compile_month_text[4] = {0};
  int compile_day = 1;
  int compile_year = 2026;
  sscanf(__DATE__, "%3s %d %d", compile_month_text, &compile_day, &compile_year);

  clock_state.year = static_cast<uint16_t>(compile_year);
  clock_state.month = month_from_compile_string(compile_month_text);
  clock_state.day = static_cast<uint8_t>(compile_day);
  clock_state.hour = static_cast<uint8_t>(compile_hour);
  clock_state.minute = static_cast<uint8_t>(compile_minute);
  clock_state.second = static_cast<uint8_t>(compile_second);
  clock_state.last_tick_ms = millis();
}

static void tick_clock_one_second() {
  static constexpr uint8_t days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

  clock_state.second++;
  if (clock_state.second < 60) {
    return;
  }

  clock_state.second = 0;
  clock_state.minute++;
  if (clock_state.minute < 60) {
    return;
  }

  clock_state.minute = 0;
  clock_state.hour++;
  if (clock_state.hour < 24) {
    return;
  }

  clock_state.hour = 0;
  uint8_t month_days = days_in_month[clock_state.month - 1];
  const bool leap_year = (clock_state.year % 4 == 0 && clock_state.year % 100 != 0) || (clock_state.year % 400 == 0);
  if (clock_state.month == 2 && leap_year) {
    month_days = 29;
  }

  clock_state.day++;
  if (clock_state.day <= month_days) {
    return;
  }

  clock_state.day = 1;
  clock_state.month++;
  if (clock_state.month <= 12) {
    return;
  }

  clock_state.month = 1;
  clock_state.year++;
}

static uint8_t month_from_compile_string(const char *month_text) {
  const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  for (uint8_t index = 0; index < 12; index++) {
    if (strncmp(month_text, months[index], 3) == 0) {
      return index + 1;
    }
  }
  return 1;
}
