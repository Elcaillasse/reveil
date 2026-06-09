/*
  Projet : Réveil ESP32-S3 + écran tactile 480x320 (paysage) + LVGL

  Dépendances Arduino IDE :
  - LVGL (bibliothèque "lvgl")
  - TFT_eSPI (à configurer pour votre écran ESP32-S3 dans User_Setup.h)
  - SD et SPI (fournies avec le cœur ESP32)

  Notes matérielles :
  - La dalle est utilisée en paysage : largeur logique = 480 px,
    hauteur logique = 320 px.
  - La lecture tactile dépend du contrôleur utilisé par votre module. La fonction
    read_touchscreen() contient un emplacement prêt à adapter.
*/

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <lvgl.h>

// -----------------------------------------------------------------------------
// Configuration écran / LVGL
// -----------------------------------------------------------------------------
static constexpr uint16_t SCREEN_WIDTH = 480;
static constexpr uint16_t SCREEN_HEIGHT = 320;
static constexpr uint8_t TFT_ROTATION_LANDSCAPE = 1;
static constexpr uint8_t MUSIC_SD_CS = 10;
static constexpr char MUSIC_DIRECTORY[] = "/musiques";

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
static lv_obj_t *alarm_list_screen = nullptr;
static lv_obj_t *alarm_edit_screen = nullptr;
static lv_obj_t *menu_screen = nullptr;
static lv_obj_t *clock_label = nullptr;
static lv_obj_t *date_label = nullptr;
static lv_obj_t *alarm_time_label = nullptr;
static lv_obj_t *alarm_days_label = nullptr;
static lv_obj_t *edit_hour_label = nullptr;
static lv_obj_t *edit_minute_label = nullptr;
static lv_obj_t *alarm_button = nullptr;
static lv_obj_t *menu_button = nullptr;
static lv_obj_t *ringtone_music_list = nullptr;

// -----------------------------------------------------------------------------
// Thème de fond séparé : modifiable/remplaçable plus tard sans déplacer les infos
// -----------------------------------------------------------------------------
struct BackgroundTheme {
  lv_color_t sky_top = lv_color_hex(0x071126);
  lv_color_t sky_bottom = lv_color_hex(0x02040D);
  lv_color_t hill_back = lv_color_hex(0x050B19);
  lv_color_t hill_front = lv_color_hex(0x02040B);
  lv_color_t sun = lv_color_hex(0xFFB21A);
  lv_color_t sun_glow = lv_color_hex(0xD98208);
};

static BackgroundTheme background_theme;

struct StarPoint {
  lv_coord_t x;
  lv_coord_t y;
  uint8_t size;
  lv_opa_t opacity;
};

static constexpr StarPoint STARS[] = {
  {8, 40, 2, LV_OPA_70}, {26, 87, 1, LV_OPA_60}, {46, 25, 1, LV_OPA_50}, {89, 18, 2, LV_OPA_80},
  {127, 36, 2, LV_OPA_50}, {153, 20, 3, LV_OPA_90}, {189, 62, 1, LV_OPA_70}, {230, 31, 2, LV_OPA_50},
  {274, 52, 2, LV_OPA_80}, {344, 16, 1, LV_OPA_60}, {363, 10, 2, LV_OPA_70}, {421, 20, 2, LV_OPA_80},
  {456, 38, 1, LV_OPA_55}, {19, 130, 2, LV_OPA_80}, {52, 165, 1, LV_OPA_40}, {97, 187, 2, LV_OPA_75},
  {134, 158, 1, LV_OPA_60}, {163, 228, 1, LV_OPA_70}, {224, 198, 2, LV_OPA_85}, {278, 220, 1, LV_OPA_65},
  {337, 186, 1, LV_OPA_55}, {397, 228, 2, LV_OPA_85}, {438, 240, 1, LV_OPA_70}, {469, 197, 2, LV_OPA_55}
};

// -----------------------------------------------------------------------------
// Styles séparés pour garder l'interface lisible et extensible
// -----------------------------------------------------------------------------
static lv_style_t style_screen_bg;
static lv_style_t style_background;
static lv_style_t style_hill_back;
static lv_style_t style_hill_front;
static lv_style_t style_sun_glow;
static lv_style_t style_sun;
static lv_style_t style_star;
static lv_style_t style_planet_dot;
static lv_style_t style_alarm_card;
static lv_style_t style_alarm_day_chip;
static lv_style_t style_menu_button;
static lv_style_t style_alarm_button;
static lv_style_t style_clock_text;
static lv_style_t style_date_text;
static lv_style_t style_caption_text;
static lv_style_t style_alarm_text;
static lv_style_t style_button_text;
static lv_style_t style_dark_panel;
static lv_style_t style_alarm_row;
static lv_style_t style_outline_button;
static lv_style_t style_delete_button;
static lv_style_t style_switch_bg;
static lv_style_t style_switch_knob;
static lv_style_t style_day_toggle;
static lv_style_t style_day_toggle_checked;
static lv_style_t style_time_picker_text;

// -----------------------------------------------------------------------------
// État applicatif : prêt pour les futurs écrans menu / réglage / animation
// -----------------------------------------------------------------------------
enum class AppScreen : uint8_t {
  Main,
  Menu,
  Ringtone,
  AlarmSettings
};

static AppScreen current_screen = AppScreen::Main;

static constexpr uint8_t MAX_ALARMS = 5;
static constexpr uint8_t DAY_COUNT = 7;
static constexpr uint8_t MAX_MUSIC_TRACKS = 12;
static constexpr size_t MUSIC_PATH_LENGTH = 96;

static char music_paths[MAX_MUSIC_TRACKS][MUSIC_PATH_LENGTH] = {};
static uint8_t music_count = 0;
static int8_t selected_music_index = -1;
static bool music_sd_ready = false;
static uint64_t last_triggered_alarm_minute = UINT64_MAX;

struct AlarmState {
  uint8_t hour = 8;
  uint8_t minute = 0;
  bool enabled = true;
  bool days[DAY_COUNT] = {false, true, true, true, true, true, false};
};

static AlarmState alarms[MAX_ALARMS] = {
  {7, 0, true, {false, true, true, true, true, true, false}},
  {9, 0, true, {false, true, true, true, true, true, false}}
};
static uint8_t alarm_count = 2;
static AlarmState alarm_state = alarms[0];
static const AlarmState *next_alarm = nullptr;
static AlarmState draft_alarm;
static uint8_t editing_alarm_index = MAX_ALARMS;
static lv_obj_t *edit_day_buttons[DAY_COUNT] = {nullptr};

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
static void create_sky_background(lv_obj_t *parent);
static void create_button_label(lv_obj_t *button, const char *text);
static uint8_t weekday_from_date(uint16_t year, uint8_t month, uint8_t day);
static void show_menu_screen();
static void show_ringtone_screen();
static void show_alarm_settings_screen();
static void scan_music_directory();
static bool is_supported_music_file(const char *name);
static const char *music_display_name(const char *path);
static void play_music_preview(const char *path);
static void trigger_due_alarm();
static void show_alarm_editor_screen();
static void create_alarm_row(lv_obj_t *parent, uint8_t index);
static void alarm_edit_event_cb(lv_event_t *event);
static void refresh_edit_labels();
static void refresh_edit_day_buttons();
static const char *alarm_days_text(const AlarmState &alarm);
static const AlarmState *find_next_active_alarm();
static void refresh_next_alarm();
static void load_screen(lv_obj_t *screen);
static void alarm_button_event_cb(lv_event_t *event);
static void menu_button_event_cb(lv_event_t *event);
static void ringtone_menu_event_cb(lv_event_t *event);
static void back_to_menu_event_cb(lv_event_t *event);
static void music_preview_event_cb(lv_event_t *event);
static void music_select_event_cb(lv_event_t *event);
static void music_scroll_up_event_cb(lv_event_t *event);
static void music_scroll_down_event_cb(lv_event_t *event);
static void back_to_main_event_cb(lv_event_t *event);
static void add_alarm_event_cb(lv_event_t *event);
static void save_alarm_event_cb(lv_event_t *event);
static void cancel_alarm_event_cb(lv_event_t *event);
static void time_adjust_event_cb(lv_event_t *event);
static void day_toggle_event_cb(lv_event_t *event);
static void alarm_toggle_event_cb(lv_event_t *event);
static void alarm_delete_event_cb(lv_event_t *event);
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
  tft.fillScreen(TFT_BLACK);

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
  music_sd_ready = SD.begin(MUSIC_SD_CS);
  if (!music_sd_ready) {
    Serial.println(F("Carte SD indisponible : le menu sonnerie restera vide."));
  }
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

  // Le ciel, le soleil et les collines sont créés en premier : ils restent un
  // arrière-plan indépendant que l'on pourra remplacer par un thème dynamique.
  create_sky_background(main_screen);

  lv_obj_t *hour_caption = lv_label_create(main_screen);
  lv_obj_add_style(hour_caption, &style_caption_text, 0);
  lv_label_set_text(hour_caption, "HEURE");
  lv_obj_set_pos(hour_caption, 22, 15);

  clock_label = lv_label_create(main_screen);
  lv_obj_add_style(clock_label, &style_clock_text, 0);
  lv_label_set_text(clock_label, "--:--");
  lv_obj_set_pos(clock_label, 19, 42);
  lv_obj_set_size(clock_label, 295, 72);

  date_label = lv_label_create(main_screen);
  lv_obj_add_style(date_label, &style_date_text, 0);
  lv_label_set_text(date_label, "--");
  lv_obj_set_pos(date_label, 23, 125);
  lv_obj_set_size(date_label, 180, 20);

  lv_obj_t *alarm_caption = lv_label_create(main_screen);
  lv_obj_add_style(alarm_caption, &style_caption_text, 0);
  lv_label_set_text(alarm_caption, "REVEIL");
  lv_obj_set_pos(alarm_caption, 420, 15);

  lv_obj_t *alarm_card = lv_obj_create(main_screen);
  lv_obj_remove_style_all(alarm_card);
  lv_obj_add_style(alarm_card, &style_alarm_card, 0);
  lv_obj_set_pos(alarm_card, 318, 34);
  lv_obj_set_size(alarm_card, 150, 78);

  lv_obj_t *alarm_dot = lv_obj_create(alarm_card);
  lv_obj_remove_style_all(alarm_dot);
  lv_obj_add_style(alarm_dot, &style_planet_dot, 0);
  lv_obj_set_pos(alarm_dot, 14, 20);
  lv_obj_set_size(alarm_dot, 8, 8);

  alarm_time_label = lv_label_create(alarm_card);
  lv_obj_add_style(alarm_time_label, &style_alarm_text, 0);
  lv_label_set_text(alarm_time_label, "--:--");
  lv_obj_set_pos(alarm_time_label, 31, 10);
  lv_obj_set_size(alarm_time_label, 106, 34);

  alarm_days_label = lv_label_create(alarm_card);
  lv_obj_add_style(alarm_days_label, &style_alarm_day_chip, 0);
  lv_label_set_text(alarm_days_label, "--");
  lv_obj_set_pos(alarm_days_label, 14, 51);
  lv_obj_set_size(alarm_days_label, 84, 16);

  menu_button = lv_btn_create(main_screen);
  lv_obj_remove_style_all(menu_button);
  lv_obj_add_style(menu_button, &style_menu_button, 0);
  lv_obj_set_pos(menu_button, 145, 293);
  lv_obj_set_size(menu_button, 82, 30);
  lv_obj_add_event_cb(menu_button, menu_button_event_cb, LV_EVENT_CLICKED, nullptr);
  create_button_label(menu_button, "MENU");

  alarm_button = lv_btn_create(main_screen);
  lv_obj_remove_style_all(alarm_button);
  lv_obj_add_style(alarm_button, &style_alarm_button, 0);
  lv_obj_set_pos(alarm_button, 242, 293);
  lv_obj_set_size(alarm_button, 102, 30);
  lv_obj_add_event_cb(alarm_button, alarm_button_event_cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *button_dot = lv_obj_create(alarm_button);
  lv_obj_remove_style_all(button_dot);
  lv_obj_add_style(button_dot, &style_sun, 0);
  lv_obj_set_pos(button_dot, 18, 11);
  lv_obj_set_size(button_dot, 8, 8);
  create_button_label(alarm_button, "REVEIL");

  load_screen(main_screen);
  update_clock_display();
}

// -----------------------------------------------------------------------------
// Arrière-plan modifiable séparément des informations affichées au-dessus
// -----------------------------------------------------------------------------
static void create_sky_background(lv_obj_t *parent) {
  lv_obj_t *background = lv_obj_create(parent);
  lv_obj_remove_style_all(background);
  lv_obj_add_style(background, &style_background, 0);
  lv_obj_set_pos(background, 0, 0);
  lv_obj_set_size(background, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_clear_flag(background, LV_OBJ_FLAG_CLICKABLE);

  for (const StarPoint &star : STARS) {
    lv_obj_t *star_dot = lv_obj_create(background);
    lv_obj_remove_style_all(star_dot);
    lv_obj_add_style(star_dot, &style_star, 0);
    lv_obj_set_style_opa(star_dot, star.opacity, 0);
    lv_obj_set_pos(star_dot, star.x, star.y);
    lv_obj_set_size(star_dot, star.size, star.size);
  }

  lv_obj_t *sun_glow = lv_obj_create(background);
  lv_obj_remove_style_all(sun_glow);
  lv_obj_add_style(sun_glow, &style_sun_glow, 0);
  lv_obj_set_pos(sun_glow, 67, 105);
  lv_obj_set_size(sun_glow, 94, 94);

  lv_obj_t *sun = lv_obj_create(background);
  lv_obj_remove_style_all(sun);
  lv_obj_add_style(sun, &style_sun, 0);
  lv_obj_set_pos(sun, 81, 119);
  lv_obj_set_size(sun, 66, 66);

  for (uint8_t index = 0; index < 9; index++) {
    lv_obj_t *dot = lv_obj_create(background);
    lv_obj_remove_style_all(dot);
    lv_obj_add_style(dot, &style_planet_dot, 0);
    lv_obj_set_style_opa(dot, static_cast<lv_opa_t>(LV_OPA_20 + index * 5), 0);
    lv_obj_set_pos(dot, 212 + index * 28, 118 - (index % 3) * 8);
    lv_obj_set_size(dot, 26, 26);
  }

  lv_obj_t *hill_back = lv_obj_create(background);
  lv_obj_remove_style_all(hill_back);
  lv_obj_add_style(hill_back, &style_hill_back, 0);
  lv_obj_set_pos(hill_back, -35, 238);
  lv_obj_set_size(hill_back, 550, 65);

  lv_obj_t *hill_front = lv_obj_create(background);
  lv_obj_remove_style_all(hill_front);
  lv_obj_add_style(hill_front, &style_hill_front, 0);
  lv_obj_set_pos(hill_front, -5, 278);
  lv_obj_set_size(hill_front, 490, 45);
}

static void create_button_label(lv_obj_t *button, const char *text) {
  lv_obj_t *label = lv_label_create(button);
  lv_obj_add_style(label, &style_button_text, 0);
  lv_label_set_text(label, text);
  lv_obj_center(label);
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

  trigger_due_alarm();

  static int16_t previous_minute = -1;
  static uint8_t previous_day = 0;

  if (clock_label != nullptr && previous_minute != clock_state.minute) {
    char time_text[6];
    snprintf(time_text, sizeof(time_text), "%02u:%02u", clock_state.hour, clock_state.minute);
    lv_label_set_text(clock_label, time_text);
    previous_minute = clock_state.minute;
    refresh_next_alarm();
  }

  if (date_label != nullptr && previous_day != clock_state.day) {
    static constexpr char WEEKDAYS_FR[7][9] = {"DIMANCHE", "LUNDI", "MARDI", "MERCREDI", "JEUDI", "VENDREDI", "SAMEDI"};
    static constexpr char MONTHS_FR[12][10] = {"JANVIER", "FEVRIER", "MARS", "AVRIL", "MAI", "JUIN", "JUILLET", "AOUT", "SEPTEMBRE", "OCTOBRE", "NOVEMBRE", "DECEMBRE"};
    char date_text[24];
    const uint8_t weekday = weekday_from_date(clock_state.year, clock_state.month, clock_state.day);
    snprintf(date_text, sizeof(date_text), "%s %02u %s", WEEKDAYS_FR[weekday], clock_state.day, MONTHS_FR[clock_state.month - 1]);
    lv_label_set_text(date_label, date_text);
    previous_day = clock_state.day;
  }

  if (alarm_time_label != nullptr) {
    if (next_alarm != nullptr) {
      char alarm_text[6];
      snprintf(alarm_text, sizeof(alarm_text), "%02u:%02u", next_alarm->hour, next_alarm->minute);
      lv_label_set_text(alarm_time_label, alarm_text);
      if (alarm_days_label != nullptr) {
        lv_label_set_text(alarm_days_label, alarm_days_text(*next_alarm));
      }
    } else {
      lv_label_set_text(alarm_time_label, "--:--");
      if (alarm_days_label != nullptr) {
        lv_label_set_text(alarm_days_label, "AUCUN");
      }
    }
  }
}

// -----------------------------------------------------------------------------
// Initialisation des styles LVGL
// -----------------------------------------------------------------------------
static void init_styles() {
  lv_style_init(&style_screen_bg);
  lv_style_set_bg_color(&style_screen_bg, background_theme.sky_bottom);
  lv_style_set_bg_opa(&style_screen_bg, LV_OPA_COVER);
  lv_style_set_border_width(&style_screen_bg, 0);
  lv_style_set_radius(&style_screen_bg, 0);

  lv_style_init(&style_background);
  lv_style_set_bg_color(&style_background, background_theme.sky_top);
  lv_style_set_bg_grad_color(&style_background, background_theme.sky_bottom);
  lv_style_set_bg_grad_dir(&style_background, LV_GRAD_DIR_VER);
  lv_style_set_bg_opa(&style_background, LV_OPA_COVER);
  lv_style_set_border_width(&style_background, 0);
  lv_style_set_radius(&style_background, 0);

  lv_style_init(&style_hill_back);
  lv_style_set_bg_color(&style_hill_back, background_theme.hill_back);
  lv_style_set_bg_opa(&style_hill_back, LV_OPA_COVER);
  lv_style_set_border_width(&style_hill_back, 0);
  lv_style_set_radius(&style_hill_back, LV_RADIUS_CIRCLE);

  lv_style_init(&style_hill_front);
  lv_style_set_bg_color(&style_hill_front, background_theme.hill_front);
  lv_style_set_bg_opa(&style_hill_front, LV_OPA_COVER);
  lv_style_set_border_width(&style_hill_front, 0);
  lv_style_set_radius(&style_hill_front, 8);

  lv_style_init(&style_sun_glow);
  lv_style_set_bg_color(&style_sun_glow, background_theme.sun_glow);
  lv_style_set_bg_opa(&style_sun_glow, LV_OPA_30);
  lv_style_set_border_width(&style_sun_glow, 0);
  lv_style_set_radius(&style_sun_glow, LV_RADIUS_CIRCLE);

  lv_style_init(&style_sun);
  lv_style_set_bg_color(&style_sun, background_theme.sun);
  lv_style_set_bg_opa(&style_sun, LV_OPA_COVER);
  lv_style_set_border_width(&style_sun, 0);
  lv_style_set_radius(&style_sun, LV_RADIUS_CIRCLE);

  lv_style_init(&style_star);
  lv_style_set_bg_color(&style_star, lv_color_hex(0xDCE8FF));
  lv_style_set_bg_opa(&style_star, LV_OPA_COVER);
  lv_style_set_border_width(&style_star, 0);
  lv_style_set_radius(&style_star, LV_RADIUS_CIRCLE);

  lv_style_init(&style_planet_dot);
  lv_style_set_bg_color(&style_planet_dot, lv_color_hex(0xB7C7FF));
  lv_style_set_bg_opa(&style_planet_dot, LV_OPA_COVER);
  lv_style_set_border_width(&style_planet_dot, 0);
  lv_style_set_radius(&style_planet_dot, LV_RADIUS_CIRCLE);

  lv_style_init(&style_alarm_card);
  lv_style_set_bg_color(&style_alarm_card, lv_color_hex(0x091430));
  lv_style_set_bg_opa(&style_alarm_card, LV_OPA_70);
  lv_style_set_border_color(&style_alarm_card, lv_color_hex(0x4675CD));
  lv_style_set_border_opa(&style_alarm_card, LV_OPA_30);
  lv_style_set_border_width(&style_alarm_card, 1);
  lv_style_set_radius(&style_alarm_card, 8);
  lv_style_set_pad_all(&style_alarm_card, 0);

  lv_style_init(&style_alarm_day_chip);
  lv_style_set_bg_color(&style_alarm_day_chip, lv_color_hex(0x1C396F));
  lv_style_set_bg_opa(&style_alarm_day_chip, LV_OPA_50);
  lv_style_set_radius(&style_alarm_day_chip, 4);
  lv_style_set_pad_hor(&style_alarm_day_chip, 4);
  lv_style_set_pad_ver(&style_alarm_day_chip, 2);
  lv_style_set_text_color(&style_alarm_day_chip, lv_color_hex(0x6FA7FF));
#if LV_FONT_MONTSERRAT_12
  lv_style_set_text_font(&style_alarm_day_chip, &lv_font_montserrat_12);
#else
  lv_style_set_text_font(&style_alarm_day_chip, LV_FONT_DEFAULT);
#endif

  lv_style_init(&style_menu_button);
  lv_style_set_bg_color(&style_menu_button, lv_color_hex(0x0A213B));
  lv_style_set_bg_opa(&style_menu_button, LV_OPA_90);
  lv_style_set_border_color(&style_menu_button, lv_color_hex(0x2D87CD));
  lv_style_set_border_opa(&style_menu_button, LV_OPA_80);
  lv_style_set_border_width(&style_menu_button, 1);
  lv_style_set_radius(&style_menu_button, 5);
  lv_style_set_pad_all(&style_menu_button, 0);

  lv_style_init(&style_alarm_button);
  lv_style_set_bg_color(&style_alarm_button, lv_color_hex(0x2B1D05));
  lv_style_set_bg_opa(&style_alarm_button, LV_OPA_90);
  lv_style_set_border_color(&style_alarm_button, lv_color_hex(0xFFB000));
  lv_style_set_border_opa(&style_alarm_button, LV_OPA_80);
  lv_style_set_border_width(&style_alarm_button, 1);
  lv_style_set_radius(&style_alarm_button, 5);
  lv_style_set_pad_all(&style_alarm_button, 0);

  lv_style_init(&style_clock_text);
  lv_style_set_text_color(&style_clock_text, lv_color_hex(0xEDF2FF));
#if LV_FONT_MONTSERRAT_48
  lv_style_set_text_font(&style_clock_text, &lv_font_montserrat_48);
#else
  lv_style_set_text_font(&style_clock_text, LV_FONT_DEFAULT);
#endif
  lv_style_set_text_align(&style_clock_text, LV_TEXT_ALIGN_LEFT);
  lv_style_set_text_letter_space(&style_clock_text, 2);

  lv_style_init(&style_date_text);
  lv_style_set_text_color(&style_date_text, lv_color_hex(0xA9B9EB));
#if LV_FONT_MONTSERRAT_12
  lv_style_set_text_font(&style_date_text, &lv_font_montserrat_12);
#elif LV_FONT_MONTSERRAT_14
  lv_style_set_text_font(&style_date_text, &lv_font_montserrat_14);
#else
  lv_style_set_text_font(&style_date_text, LV_FONT_DEFAULT);
#endif
  lv_style_set_text_letter_space(&style_date_text, 2);

  lv_style_init(&style_caption_text);
  lv_style_set_text_color(&style_caption_text, lv_color_hex(0x8EA1CE));
#if LV_FONT_MONTSERRAT_12
  lv_style_set_text_font(&style_caption_text, &lv_font_montserrat_12);
#else
  lv_style_set_text_font(&style_caption_text, LV_FONT_DEFAULT);
#endif
  lv_style_set_text_letter_space(&style_caption_text, 4);

  lv_style_init(&style_alarm_text);
  lv_style_set_text_color(&style_alarm_text, lv_color_hex(0xEDF2FF));
#if LV_FONT_MONTSERRAT_32
  lv_style_set_text_font(&style_alarm_text, &lv_font_montserrat_32);
#elif LV_FONT_MONTSERRAT_28
  lv_style_set_text_font(&style_alarm_text, &lv_font_montserrat_28);
#else
  lv_style_set_text_font(&style_alarm_text, LV_FONT_DEFAULT);
#endif
  lv_style_set_text_letter_space(&style_alarm_text, 1);

  lv_style_init(&style_button_text);
  lv_style_set_text_color(&style_button_text, lv_color_hex(0xA9D6FF));
#if LV_FONT_MONTSERRAT_12
  lv_style_set_text_font(&style_button_text, &lv_font_montserrat_12);
#else
  lv_style_set_text_font(&style_button_text, LV_FONT_DEFAULT);
#endif
  lv_style_set_text_align(&style_button_text, LV_TEXT_ALIGN_CENTER);
  lv_style_set_text_letter_space(&style_button_text, 1);

  lv_style_init(&style_dark_panel);
  lv_style_set_bg_color(&style_dark_panel, lv_color_hex(0x05060D));
  lv_style_set_bg_opa(&style_dark_panel, LV_OPA_COVER);
  lv_style_set_border_color(&style_dark_panel, lv_color_hex(0x1C2338));
  lv_style_set_border_width(&style_dark_panel, 1);
  lv_style_set_radius(&style_dark_panel, 0);

  lv_style_init(&style_alarm_row);
  lv_style_set_bg_color(&style_alarm_row, lv_color_hex(0x0C0D22));
  lv_style_set_bg_opa(&style_alarm_row, LV_OPA_COVER);
  lv_style_set_border_color(&style_alarm_row, lv_color_hex(0x20284B));
  lv_style_set_border_width(&style_alarm_row, 1);
  lv_style_set_radius(&style_alarm_row, 8);
  lv_style_set_pad_all(&style_alarm_row, 0);

  lv_style_init(&style_outline_button);
  lv_style_set_bg_color(&style_outline_button, lv_color_hex(0x05060D));
  lv_style_set_bg_opa(&style_outline_button, LV_OPA_COVER);
  lv_style_set_border_color(&style_outline_button, lv_color_hex(0x444B64));
  lv_style_set_border_width(&style_outline_button, 1);
  lv_style_set_radius(&style_outline_button, 7);
  lv_style_set_pad_all(&style_outline_button, 0);

  lv_style_init(&style_delete_button);
  lv_style_set_bg_color(&style_delete_button, lv_color_hex(0x101120));
  lv_style_set_bg_opa(&style_delete_button, LV_OPA_COVER);
  lv_style_set_border_color(&style_delete_button, lv_color_hex(0x42465F));
  lv_style_set_border_width(&style_delete_button, 1);
  lv_style_set_radius(&style_delete_button, 8);
  lv_style_set_pad_all(&style_delete_button, 0);

  lv_style_init(&style_switch_bg);
  lv_style_set_bg_color(&style_switch_bg, lv_color_hex(0x111321));
  lv_style_set_bg_opa(&style_switch_bg, LV_OPA_COVER);
  lv_style_set_border_color(&style_switch_bg, lv_color_hex(0x464B63));
  lv_style_set_border_width(&style_switch_bg, 1);
  lv_style_set_radius(&style_switch_bg, LV_RADIUS_CIRCLE);
  lv_style_set_pad_all(&style_switch_bg, 0);

  lv_style_init(&style_switch_knob);
  lv_style_set_bg_color(&style_switch_knob, lv_color_hex(0x3E9CFF));
  lv_style_set_bg_opa(&style_switch_knob, LV_OPA_COVER);
  lv_style_set_border_width(&style_switch_knob, 0);
  lv_style_set_radius(&style_switch_knob, LV_RADIUS_CIRCLE);

  lv_style_init(&style_day_toggle);
  lv_style_set_bg_color(&style_day_toggle, lv_color_hex(0x05060D));
  lv_style_set_bg_opa(&style_day_toggle, LV_OPA_COVER);
  lv_style_set_border_color(&style_day_toggle, lv_color_hex(0x444B64));
  lv_style_set_border_width(&style_day_toggle, 1);
  lv_style_set_radius(&style_day_toggle, 7);
  lv_style_set_pad_all(&style_day_toggle, 0);

  lv_style_init(&style_day_toggle_checked);
  lv_style_set_bg_color(&style_day_toggle_checked, lv_color_hex(0x172A54));
  lv_style_set_bg_opa(&style_day_toggle_checked, LV_OPA_COVER);
  lv_style_set_border_color(&style_day_toggle_checked, lv_color_hex(0x6FA7FF));
  lv_style_set_border_width(&style_day_toggle_checked, 1);

  lv_style_init(&style_time_picker_text);
  lv_style_set_text_color(&style_time_picker_text, lv_color_hex(0xDDE8FF));
#if LV_FONT_MONTSERRAT_48
  lv_style_set_text_font(&style_time_picker_text, &lv_font_montserrat_48);
#else
  lv_style_set_text_font(&style_time_picker_text, LV_FONT_DEFAULT);
#endif
  lv_style_set_text_letter_space(&style_time_picker_text, 2);

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

static void ringtone_menu_event_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    show_ringtone_screen();
  }
}

static void back_to_menu_event_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    show_menu_screen();
  }
}

static void show_menu_screen() {
  current_screen = AppScreen::Menu;
  scan_music_directory();
  if (selected_music_index >= static_cast<int8_t>(music_count)) selected_music_index = -1;

  menu_screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(menu_screen);
  lv_obj_add_style(menu_screen, &style_dark_panel, 0);
  lv_obj_set_size(menu_screen, SCREEN_WIDTH, SCREEN_HEIGHT);

  lv_obj_t *title = lv_label_create(menu_screen);
  lv_obj_add_style(title, &style_caption_text, 0);
  lv_label_set_text(title, "MENU");
  lv_obj_set_pos(title, 18, 16);

  lv_obj_t *divider = lv_obj_create(menu_screen);
  lv_obj_remove_style_all(divider);
  lv_obj_set_style_bg_color(divider, lv_color_hex(0x1C2338), 0);
  lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
  lv_obj_set_pos(divider, 0, 39);
  lv_obj_set_size(divider, SCREEN_WIDTH, 1);

  lv_obj_t *ringtone_button = lv_btn_create(menu_screen);
  lv_obj_remove_style_all(ringtone_button);
  lv_obj_add_style(ringtone_button, &style_alarm_row, 0);
  lv_obj_set_pos(ringtone_button, 18, 50);
  lv_obj_set_size(ringtone_button, 448, 76);
  lv_obj_add_event_cb(ringtone_button, ringtone_menu_event_cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *ringtone_title = lv_label_create(ringtone_button);
  lv_obj_add_style(ringtone_title, &style_button_text, 0);
  lv_label_set_text(ringtone_title, "SONNERIE");
  lv_obj_set_pos(ringtone_title, 14, 10);

  lv_obj_t *ringtone_current = lv_label_create(ringtone_button);
  lv_obj_add_style(ringtone_current, &style_button_text, 0);
  lv_label_set_text(ringtone_current, selected_music_index >= 0
    ? music_display_name(music_paths[selected_music_index])
    : "Aucune sonnerie selectionnee");
  lv_obj_set_pos(ringtone_current, 14, 31);
  lv_obj_set_width(ringtone_current, 330);
  lv_label_set_long_mode(ringtone_current, LV_LABEL_LONG_DOT);

  lv_obj_t *ringtone_description = lv_label_create(ringtone_button);
  lv_obj_add_style(ringtone_description, &style_caption_text, 0);
  char ringtone_description_text[48];
  snprintf(
    ringtone_description_text,
    sizeof(ringtone_description_text),
    "%u choix disponible%s - appuyer pour choisir",
    music_count,
    music_count > 1 ? "s" : ""
  );
  lv_label_set_text(ringtone_description, ringtone_description_text);
  lv_obj_set_pos(ringtone_description, 14, 54);

  lv_obj_t *ringtone_arrow = lv_label_create(ringtone_button);
  lv_obj_add_style(ringtone_arrow, &style_button_text, 0);
  lv_label_set_text(ringtone_arrow, ">");
  lv_obj_align(ringtone_arrow, LV_ALIGN_RIGHT_MID, -16, 0);

  lv_obj_t *back_button = lv_btn_create(menu_screen);
  lv_obj_remove_style_all(back_button);
  lv_obj_add_style(back_button, &style_outline_button, 0);
  lv_obj_set_pos(back_button, 18, 282);
  lv_obj_set_size(back_button, 106, 30);
  lv_obj_add_event_cb(back_button, back_to_main_event_cb, LV_EVENT_CLICKED, nullptr);
  create_button_label(back_button, "<- RETOUR");

  load_screen(menu_screen);
}

static void show_ringtone_screen() {
  current_screen = AppScreen::Ringtone;
  scan_music_directory();

  menu_screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(menu_screen);
  lv_obj_add_style(menu_screen, &style_dark_panel, 0);
  lv_obj_set_size(menu_screen, SCREEN_WIDTH, SCREEN_HEIGHT);

  lv_obj_t *title = lv_label_create(menu_screen);
  lv_obj_add_style(title, &style_caption_text, 0);
  lv_label_set_text(title, "CHOIX SONNERIE REVEIL");
  lv_obj_set_pos(title, 18, 16);

  lv_obj_t *count_label = lv_label_create(menu_screen);
  lv_obj_add_style(count_label, &style_caption_text, 0);
  char count_text[18];
  snprintf(count_text, sizeof(count_text), "%u musique%s", music_count, music_count > 1 ? "s" : "");
  lv_label_set_text(count_label, count_text);
  lv_obj_align(count_label, LV_ALIGN_TOP_RIGHT, -18, 16);

  lv_obj_t *divider = lv_obj_create(menu_screen);
  lv_obj_remove_style_all(divider);
  lv_obj_set_style_bg_color(divider, lv_color_hex(0x1C2338), 0);
  lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
  lv_obj_set_pos(divider, 0, 39);
  lv_obj_set_size(divider, SCREEN_WIDTH, 1);

  ringtone_music_list = lv_obj_create(menu_screen);
  lv_obj_remove_style_all(ringtone_music_list);
  lv_obj_set_pos(ringtone_music_list, 18, 49);
  lv_obj_set_size(ringtone_music_list, 448, 220);
  lv_obj_add_flag(ringtone_music_list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(ringtone_music_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(ringtone_music_list, LV_SCROLLBAR_MODE_OFF);

  if (!music_sd_ready || music_count == 0) {
    lv_obj_t *empty_label = lv_label_create(ringtone_music_list);
    lv_obj_add_style(empty_label, &style_date_text, 0);
    lv_label_set_text(empty_label, music_sd_ready
      ? "Ajoutez des fichiers audio dans /musiques"
      : "Carte SD indisponible");
    lv_obj_align(empty_label, LV_ALIGN_CENTER, 0, -5);
  }

  for (uint8_t index = 0; index < music_count; index++) {
    lv_obj_t *row = lv_obj_create(ringtone_music_list);
    lv_obj_remove_style_all(row);
    lv_obj_add_style(row, &style_alarm_row, 0);
    if (selected_music_index == static_cast<int8_t>(index)) {
      lv_obj_set_style_border_color(row, lv_color_hex(0x6FA7FF), 0);
    }
    lv_obj_set_pos(row, 0, index * 57);
    lv_obj_set_size(row, 448, 50);

    lv_obj_t *name = lv_label_create(row);
    lv_obj_add_style(name, &style_button_text, 0);
#if LV_FONT_MONTSERRAT_12
    lv_obj_set_style_text_font(name, &lv_font_montserrat_12, 0);
#endif
    lv_label_set_text(name, music_display_name(music_paths[index]));
    lv_obj_set_pos(name, 12, 7);
    lv_obj_set_width(name, 235);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);

    lv_obj_t *status = lv_label_create(row);
    lv_obj_add_style(status, &style_caption_text, 0);
#if LV_FONT_MONTSERRAT_10
    lv_obj_set_style_text_font(status, &lv_font_montserrat_10, 0);
#endif
    lv_obj_set_style_text_letter_space(status, 1, 0);
    lv_label_set_text(status, selected_music_index == static_cast<int8_t>(index) ? "SONNERIE SELECTIONNEE" : "DISPONIBLE");
    lv_obj_set_pos(status, 12, 29);

    lv_obj_t *preview_button = lv_btn_create(row);
    lv_obj_remove_style_all(preview_button);
    lv_obj_add_style(preview_button, &style_outline_button, 0);
    lv_obj_set_pos(preview_button, 264, 10);
    lv_obj_set_size(preview_button, 76, 30);
    lv_obj_add_event_cb(preview_button, music_preview_event_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(index)));
    create_button_label(preview_button, "ECOUTER");

    lv_obj_t *select_button = lv_btn_create(row);
    lv_obj_remove_style_all(select_button);
    lv_obj_add_style(select_button, &style_outline_button, 0);
    lv_obj_set_pos(select_button, 347, 10);
    lv_obj_set_size(select_button, 88, 30);
    lv_obj_add_event_cb(select_button, music_select_event_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(index)));
    create_button_label(select_button, "CHOISIR");
  }

  lv_obj_t *back_button = lv_btn_create(menu_screen);
  lv_obj_remove_style_all(back_button);
  lv_obj_add_style(back_button, &style_outline_button, 0);
  lv_obj_set_pos(back_button, 18, 282);
  lv_obj_set_size(back_button, 106, 30);
  lv_obj_add_event_cb(back_button, back_to_menu_event_cb, LV_EVENT_CLICKED, nullptr);
  create_button_label(back_button, "<- RETOUR");

  lv_obj_t *scroll_up_button = lv_btn_create(menu_screen);
  lv_obj_remove_style_all(scroll_up_button);
  lv_obj_add_style(scroll_up_button, &style_outline_button, 0);
  lv_obj_set_pos(scroll_up_button, 374, 282);
  lv_obj_set_size(scroll_up_button, 42, 30);
  lv_obj_add_event_cb(scroll_up_button, music_scroll_up_event_cb, LV_EVENT_CLICKED, nullptr);
  create_button_label(scroll_up_button, "^");

  lv_obj_t *scroll_down_button = lv_btn_create(menu_screen);
  lv_obj_remove_style_all(scroll_down_button);
  lv_obj_add_style(scroll_down_button, &style_outline_button, 0);
  lv_obj_set_pos(scroll_down_button, 424, 282);
  lv_obj_set_size(scroll_down_button, 42, 30);
  lv_obj_add_event_cb(scroll_down_button, music_scroll_down_event_cb, LV_EVENT_CLICKED, nullptr);
  create_button_label(scroll_down_button, "v");

  load_screen(menu_screen);
}

static void scan_music_directory() {
  music_count = 0;
  if (!music_sd_ready) return;

  File directory = SD.open(MUSIC_DIRECTORY);
  if (!directory || !directory.isDirectory()) {
    Serial.println(F("Dossier /musiques introuvable sur la carte SD."));
    return;
  }

  File entry = directory.openNextFile();
  while (entry && music_count < MAX_MUSIC_TRACKS) {
    if (!entry.isDirectory() && is_supported_music_file(entry.name())) {
      snprintf(music_paths[music_count], MUSIC_PATH_LENGTH, "%s/%s", MUSIC_DIRECTORY, entry.name());
      music_count++;
    }
    entry.close();
    entry = directory.openNextFile();
  }
  directory.close();
}

static bool is_supported_music_file(const char *name) {
  String lower_name(name);
  lower_name.toLowerCase();
  return lower_name.endsWith(".mp3") || lower_name.endsWith(".wav") ||
         lower_name.endsWith(".ogg") || lower_name.endsWith(".m4a") ||
         lower_name.endsWith(".aac") || lower_name.endsWith(".flac");
}

static const char *music_display_name(const char *path) {
  const char *last_separator = strrchr(path, '/');
  return last_separator == nullptr ? path : last_separator + 1;
}

// Point d'intégration à relier au décodeur/I2S de la carte utilisée.
// Cette même fonction est utilisée pour la préécoute et au déclenchement du réveil.
static void play_music_preview(const char *path) {
  Serial.print(F("Lecture sonnerie : "));
  Serial.println(path);
}

static void trigger_due_alarm() {
  const uint64_t minute_key = (((((static_cast<uint64_t>(clock_state.year) * 13) + clock_state.month) * 32 +
                                  clock_state.day) * 24 + clock_state.hour) * 60) + clock_state.minute;
  if (minute_key == last_triggered_alarm_minute) return;

  const uint8_t today = weekday_from_date(clock_state.year, clock_state.month, clock_state.day);
  for (uint8_t index = 0; index < alarm_count; index++) {
    const AlarmState &alarm = alarms[index];
    if (!alarm.enabled || !alarm.days[today] || alarm.hour != clock_state.hour || alarm.minute != clock_state.minute) {
      continue;
    }

    last_triggered_alarm_minute = minute_key;
    if (selected_music_index >= 0 && selected_music_index < static_cast<int8_t>(music_count)) {
      play_music_preview(music_paths[selected_music_index]);
    } else {
      Serial.println(F("Reveil declenche, mais aucune sonnerie n'est selectionnee."));
    }
    return;
  }
}

static void music_preview_event_cb(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  const uintptr_t index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
  if (index < music_count) play_music_preview(music_paths[index]);
}

static void music_select_event_cb(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  const uintptr_t index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
  if (index < music_count) {
    selected_music_index = static_cast<int8_t>(index);
    show_ringtone_screen();
  }
}

static void music_scroll_up_event_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED && ringtone_music_list != nullptr) {
    lv_obj_scroll_by(ringtone_music_list, 0, 57, LV_ANIM_ON);
  }
}

static void music_scroll_down_event_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED && ringtone_music_list != nullptr) {
    lv_obj_scroll_by(ringtone_music_list, 0, -57, LV_ANIM_ON);
  }
}

static void show_alarm_settings_screen() {
  current_screen = AppScreen::AlarmSettings;

  alarm_list_screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(alarm_list_screen);
  lv_obj_add_style(alarm_list_screen, &style_dark_panel, 0);
  lv_obj_set_size(alarm_list_screen, SCREEN_WIDTH, SCREEN_HEIGHT);

  lv_obj_t *title = lv_label_create(alarm_list_screen);
  lv_obj_add_style(title, &style_caption_text, 0);
  lv_label_set_text(title, "REVEILS");
  lv_obj_set_pos(title, 18, 16);

  lv_obj_t *count_label = lv_label_create(alarm_list_screen);
  lv_obj_add_style(count_label, &style_caption_text, 0);
  char count_text[14];
  snprintf(count_text, sizeof(count_text), "%u reveil%s", alarm_count, alarm_count > 1 ? "s" : "");
  lv_label_set_text(count_label, count_text);
  lv_obj_set_pos(count_label, 410, 16);

  lv_obj_t *divider = lv_obj_create(alarm_list_screen);
  lv_obj_remove_style_all(divider);
  lv_obj_set_style_bg_color(divider, lv_color_hex(0x1C2338), 0);
  lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
  lv_obj_set_pos(divider, 0, 39);
  lv_obj_set_size(divider, SCREEN_WIDTH, 1);

  for (uint8_t index = 0; index < alarm_count; index++) {
    create_alarm_row(alarm_list_screen, index);
  }

  lv_obj_t *back_button = lv_btn_create(alarm_list_screen);
  lv_obj_remove_style_all(back_button);
  lv_obj_add_style(back_button, &style_outline_button, 0);
  lv_obj_set_pos(back_button, 18, 284);
  lv_obj_set_size(back_button, 106, 30);
  lv_obj_add_event_cb(back_button, back_to_main_event_cb, LV_EVENT_CLICKED, nullptr);
  create_button_label(back_button, "<- RETOUR");

  lv_obj_t *add_button = lv_btn_create(alarm_list_screen);
  lv_obj_remove_style_all(add_button);
  lv_obj_add_style(add_button, &style_outline_button, 0);
  lv_obj_set_pos(add_button, 350, 284);
  lv_obj_set_size(add_button, 116, 30);
  lv_obj_add_event_cb(add_button, add_alarm_event_cb, LV_EVENT_CLICKED, nullptr);
  create_button_label(add_button, "+ AJOUTER");

  load_screen(alarm_list_screen);
}

static void create_alarm_row(lv_obj_t *parent, uint8_t index) {
  AlarmState &alarm = alarms[index];
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_add_style(row, &style_alarm_row, 0);
  lv_obj_set_pos(row, 18, 46 + index * 72);
  lv_obj_set_size(row, 448, 63);

  lv_obj_t *time_label = lv_label_create(row);
  lv_obj_add_style(time_label, &style_alarm_text, 0);
  char time_text[6];
  snprintf(time_text, sizeof(time_text), "%02u:%02u", alarm.hour, alarm.minute);
  lv_label_set_text(time_label, time_text);
  lv_obj_set_pos(time_label, 12, 9);
  lv_obj_add_flag(time_label, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(time_label, alarm_edit_event_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(index)));

  lv_obj_t *days_label = lv_label_create(row);
  lv_obj_add_style(days_label, &style_caption_text, 0);
  lv_label_set_text(days_label, alarm_days_text(alarm));
  lv_obj_set_pos(days_label, 13, 42);
  lv_obj_add_flag(days_label, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(days_label, alarm_edit_event_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(index)));

  lv_obj_t *switch_bg = lv_btn_create(row);
  lv_obj_remove_style_all(switch_bg);
  lv_obj_add_style(switch_bg, &style_switch_bg, 0);
  lv_obj_set_pos(switch_bg, 338, 21);
  lv_obj_set_size(switch_bg, 36, 20);
  lv_obj_add_event_cb(switch_bg, alarm_toggle_event_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(index)));

  lv_obj_t *knob = lv_obj_create(switch_bg);
  lv_obj_remove_style_all(knob);
  lv_obj_add_style(knob, &style_switch_knob, 0);
  lv_obj_set_pos(knob, alarm.enabled ? 18 : 2, 3);
  lv_obj_set_size(knob, 14, 14);

  lv_obj_t *delete_button = lv_btn_create(row);
  lv_obj_remove_style_all(delete_button);
  lv_obj_add_style(delete_button, &style_delete_button, 0);
  lv_obj_set_pos(delete_button, 384, 13);
  lv_obj_set_size(delete_button, 50, 38);
  lv_obj_add_event_cb(delete_button, alarm_delete_event_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(index)));
  create_button_label(delete_button, "X");
}

static void show_alarm_editor_screen() {
  current_screen = AppScreen::AlarmSettings;

  alarm_edit_screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(alarm_edit_screen);
  lv_obj_add_style(alarm_edit_screen, &style_dark_panel, 0);
  lv_obj_set_size(alarm_edit_screen, SCREEN_WIDTH, SCREEN_HEIGHT);

  lv_obj_t *title = lv_label_create(alarm_edit_screen);
  lv_obj_add_style(title, &style_caption_text, 0);
  lv_label_set_text(title, editing_alarm_index < alarm_count ? "MODIFIER REVEIL" : "NOUVEAU REVEIL");
  lv_obj_set_pos(title, 18, 16);

  lv_obj_t *divider_top = lv_obj_create(alarm_edit_screen);
  lv_obj_remove_style_all(divider_top);
  lv_obj_set_style_bg_color(divider_top, lv_color_hex(0x1C2338), 0);
  lv_obj_set_style_bg_opa(divider_top, LV_OPA_COVER, 0);
  lv_obj_set_pos(divider_top, 0, 39);
  lv_obj_set_size(divider_top, SCREEN_WIDTH, 1);

  const int16_t hour_x = 162;
  const int16_t minute_x = 272;
  const int16_t up_y = 76;
  const int16_t down_y = 173;

  lv_obj_t *hour_up = lv_btn_create(alarm_edit_screen);
  lv_obj_remove_style_all(hour_up);
  lv_obj_add_style(hour_up, &style_outline_button, 0);
  lv_obj_set_pos(hour_up, hour_x + 8, up_y);
  lv_obj_set_size(hour_up, 42, 32);
  lv_obj_add_event_cb(hour_up, time_adjust_event_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(0)));
  create_button_label(hour_up, "^");

  lv_obj_t *minute_up = lv_btn_create(alarm_edit_screen);
  lv_obj_remove_style_all(minute_up);
  lv_obj_add_style(minute_up, &style_outline_button, 0);
  lv_obj_set_pos(minute_up, minute_x + 8, up_y);
  lv_obj_set_size(minute_up, 42, 32);
  lv_obj_add_event_cb(minute_up, time_adjust_event_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(1)));
  create_button_label(minute_up, "^");

  edit_hour_label = lv_label_create(alarm_edit_screen);
  lv_obj_add_style(edit_hour_label, &style_time_picker_text, 0);
  lv_obj_set_pos(edit_hour_label, hour_x, 121);

  lv_obj_t *colon = lv_label_create(alarm_edit_screen);
  lv_obj_add_style(colon, &style_time_picker_text, 0);
  lv_label_set_text(colon, ":");
  lv_obj_set_pos(colon, 236, 121);

  edit_minute_label = lv_label_create(alarm_edit_screen);
  lv_obj_add_style(edit_minute_label, &style_time_picker_text, 0);
  lv_obj_set_pos(edit_minute_label, minute_x, 121);

  lv_obj_t *hour_down = lv_btn_create(alarm_edit_screen);
  lv_obj_remove_style_all(hour_down);
  lv_obj_add_style(hour_down, &style_outline_button, 0);
  lv_obj_set_pos(hour_down, hour_x + 8, down_y);
  lv_obj_set_size(hour_down, 42, 32);
  lv_obj_add_event_cb(hour_down, time_adjust_event_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(2)));
  create_button_label(hour_down, "v");

  lv_obj_t *minute_down = lv_btn_create(alarm_edit_screen);
  lv_obj_remove_style_all(minute_down);
  lv_obj_add_style(minute_down, &style_outline_button, 0);
  lv_obj_set_pos(minute_down, minute_x + 8, down_y);
  lv_obj_set_size(minute_down, 42, 32);
  lv_obj_add_event_cb(minute_down, time_adjust_event_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(3)));
  create_button_label(minute_down, "v");

  static constexpr char DAYS[DAY_COUNT][4] = {"DIM", "LUN", "MAR", "MER", "JEU", "VEN", "SAM"};
  for (uint8_t index = 0; index < DAY_COUNT; index++) {
    edit_day_buttons[index] = lv_btn_create(alarm_edit_screen);
    lv_obj_remove_style_all(edit_day_buttons[index]);
    lv_obj_add_style(edit_day_buttons[index], &style_day_toggle, 0);
    lv_obj_set_pos(edit_day_buttons[index], 105 + index * 41, 216);
    lv_obj_set_size(edit_day_buttons[index], 35, 26);
    lv_obj_add_event_cb(edit_day_buttons[index], day_toggle_event_cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(index)));
    create_button_label(edit_day_buttons[index], DAYS[index]);
  }

  lv_obj_t *divider_bottom = lv_obj_create(alarm_edit_screen);
  lv_obj_remove_style_all(divider_bottom);
  lv_obj_set_style_bg_color(divider_bottom, lv_color_hex(0x1C2338), 0);
  lv_obj_set_style_bg_opa(divider_bottom, LV_OPA_COVER, 0);
  lv_obj_set_pos(divider_bottom, 0, 277);
  lv_obj_set_size(divider_bottom, SCREEN_WIDTH, 1);

  lv_obj_t *cancel_button = lv_btn_create(alarm_edit_screen);
  lv_obj_remove_style_all(cancel_button);
  lv_obj_add_style(cancel_button, &style_outline_button, 0);
  lv_obj_set_pos(cancel_button, 22, 286);
  lv_obj_set_size(cancel_button, 116, 30);
  lv_obj_add_event_cb(cancel_button, cancel_alarm_event_cb, LV_EVENT_CLICKED, nullptr);
  create_button_label(cancel_button, "<- ANNULER");

  lv_obj_t *save_button = lv_btn_create(alarm_edit_screen);
  lv_obj_remove_style_all(save_button);
  lv_obj_add_style(save_button, &style_outline_button, 0);
  lv_obj_set_pos(save_button, 318, 286);
  lv_obj_set_size(save_button, 150, 30);
  lv_obj_add_event_cb(save_button, save_alarm_event_cb, LV_EVENT_CLICKED, nullptr);
  create_button_label(save_button, editing_alarm_index < alarm_count ? "OK MODIFIER" : "OK ENREGISTRER");

  refresh_edit_labels();
  refresh_edit_day_buttons();
  load_screen(alarm_edit_screen);
}

static void refresh_edit_labels() {
  char value[3];
  if (edit_hour_label != nullptr) {
    snprintf(value, sizeof(value), "%02u", draft_alarm.hour);
    lv_label_set_text(edit_hour_label, value);
  }
  if (edit_minute_label != nullptr) {
    snprintf(value, sizeof(value), "%02u", draft_alarm.minute);
    lv_label_set_text(edit_minute_label, value);
  }
}

static void refresh_edit_day_buttons() {
  for (uint8_t index = 0; index < DAY_COUNT; index++) {
    if (edit_day_buttons[index] == nullptr) {
      continue;
    }
    if (draft_alarm.days[index]) {
      lv_obj_add_style(edit_day_buttons[index], &style_day_toggle_checked, 0);
    } else {
      lv_obj_remove_style(edit_day_buttons[index], &style_day_toggle_checked, 0);
    }
  }
}

static const char *alarm_days_text(const AlarmState &alarm) {
  const bool week = alarm.days[1] && alarm.days[2] && alarm.days[3] && alarm.days[4] && alarm.days[5] && !alarm.days[0] && !alarm.days[6];
  const bool weekend = alarm.days[0] && alarm.days[6] && !alarm.days[1] && !alarm.days[2] && !alarm.days[3] && !alarm.days[4] && !alarm.days[5];
  if (week) return "LUN - VEN";
  if (weekend) return "WEEK-END";
  return "PERSO";
}

static const AlarmState *find_next_active_alarm() {
  const uint8_t today = weekday_from_date(clock_state.year, clock_state.month, clock_state.day);
  const uint16_t current_minutes = static_cast<uint16_t>(clock_state.hour) * 60 + clock_state.minute;
  const AlarmState *candidate = nullptr;
  uint16_t candidate_delay_minutes = UINT16_MAX;

  for (uint8_t index = 0; index < alarm_count; index++) {
    const AlarmState &alarm = alarms[index];
    if (!alarm.enabled) {
      continue;
    }

    const uint16_t alarm_minutes = static_cast<uint16_t>(alarm.hour) * 60 + alarm.minute;
    for (uint8_t day_offset = 0; day_offset < DAY_COUNT; day_offset++) {
      const uint8_t checked_day = (today + day_offset) % DAY_COUNT;
      if (!alarm.days[checked_day]) {
        continue;
      }

      if (day_offset == 0 && alarm_minutes < current_minutes) {
        continue;
      }

      const uint16_t delay_minutes = static_cast<uint16_t>(day_offset) * 24 * 60 +
                                     alarm_minutes - (day_offset == 0 ? current_minutes : 0);
      if (delay_minutes < candidate_delay_minutes) {
        candidate_delay_minutes = delay_minutes;
        candidate = &alarm;
      }
      break;
    }
  }

  return candidate;
}

static void refresh_next_alarm() {
  next_alarm = find_next_active_alarm();
  if (next_alarm != nullptr) {
    alarm_state = *next_alarm;
  }
}

static void load_screen(lv_obj_t *screen) {
#if LVGL_VERSION_MAJOR >= 9
  lv_screen_load(screen);
#else
  lv_scr_load(screen);
#endif
}

static void back_to_main_event_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    create_main_screen();
  }
}

static void add_alarm_event_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED && alarm_count < MAX_ALARMS) {
    editing_alarm_index = MAX_ALARMS;
    draft_alarm = AlarmState();
    show_alarm_editor_screen();
  }
}

static void alarm_edit_event_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    const uintptr_t index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    if (index < alarm_count) {
      editing_alarm_index = static_cast<uint8_t>(index);
      draft_alarm = alarms[editing_alarm_index];
      show_alarm_editor_screen();
    }
  }
}

static void save_alarm_event_cb(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  if (editing_alarm_index < alarm_count) {
    alarms[editing_alarm_index] = draft_alarm;
  } else if (alarm_count < MAX_ALARMS) {
    alarms[alarm_count] = draft_alarm;
    alarm_count++;
  } else {
    return;
  }

  editing_alarm_index = MAX_ALARMS;
  refresh_next_alarm();
  show_alarm_settings_screen();
}

static void cancel_alarm_event_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    editing_alarm_index = MAX_ALARMS;
    show_alarm_settings_screen();
  }
}

static void time_adjust_event_cb(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  const uintptr_t action = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
  if (action == 0) draft_alarm.hour = (draft_alarm.hour + 1) % 24;
  if (action == 1) draft_alarm.minute = (draft_alarm.minute + 1) % 60;
  if (action == 2) draft_alarm.hour = (draft_alarm.hour + 23) % 24;
  if (action == 3) draft_alarm.minute = (draft_alarm.minute + 59) % 60;
  refresh_edit_labels();
}

static void day_toggle_event_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    const uintptr_t day = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    if (day < DAY_COUNT) {
      draft_alarm.days[day] = !draft_alarm.days[day];
      refresh_edit_day_buttons();
    }
  }
}

static void alarm_toggle_event_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    const uintptr_t index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    if (index < alarm_count) {
      alarms[index].enabled = !alarms[index].enabled;
      refresh_next_alarm();
      show_alarm_settings_screen();
    }
  }
}

static void alarm_delete_event_cb(lv_event_t *event) {
  if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
    const uintptr_t index = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    if (index < alarm_count) {
      for (uint8_t cursor = index; cursor + 1 < alarm_count; cursor++) {
        alarms[cursor] = alarms[cursor + 1];
      }
      alarm_count--;
      refresh_next_alarm();
      show_alarm_settings_screen();
    }
  }
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

// Zeller : retourne 0=dimanche, 1=lundi, ..., 6=samedi.
static uint8_t weekday_from_date(uint16_t year, uint8_t month, uint8_t day) {
  uint16_t adjusted_year = year;
  uint8_t adjusted_month = month;
  if (adjusted_month < 3) {
    adjusted_month += 12;
    adjusted_year--;
  }

  const uint16_t k = adjusted_year % 100;
  const uint16_t j = adjusted_year / 100;
  const uint8_t h = (day + ((13 * (adjusted_month + 1)) / 5) + k + (k / 4) + (j / 4) + (5 * j)) % 7;
  return (h + 6) % 7;
}

static uint8_t month_from_compile_string(const char *month_text) {
  if (strcmp(month_text, "Jan") == 0) return 1;
  if (strcmp(month_text, "Feb") == 0) return 2;
  if (strcmp(month_text, "Mar") == 0) return 3;
  if (strcmp(month_text, "Apr") == 0) return 4;
  if (strcmp(month_text, "May") == 0) return 5;
  if (strcmp(month_text, "Jun") == 0) return 6;
  if (strcmp(month_text, "Jul") == 0) return 7;
  if (strcmp(month_text, "Aug") == 0) return 8;
  if (strcmp(month_text, "Sep") == 0) return 9;
  if (strcmp(month_text, "Oct") == 0) return 10;
  if (strcmp(month_text, "Nov") == 0) return 11;
  if (strcmp(month_text, "Dec") == 0) return 12;
  return 1;
}
