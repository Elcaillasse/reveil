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
static lv_obj_t *alarm_button = nullptr;
static lv_obj_t *menu_button = nullptr;

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
  uint8_t hour = 8;
  uint8_t minute = 0;
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
static void create_sky_background(lv_obj_t *parent);
static void create_button_label(lv_obj_t *button, const char *text);
static uint8_t weekday_from_date(uint16_t year, uint8_t month, uint8_t day);
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
  lv_label_set_text(alarm_time_label, "08:00");
  lv_obj_set_pos(alarm_time_label, 31, 10);
  lv_obj_set_size(alarm_time_label, 106, 34);

  lv_obj_t *alarm_days = lv_label_create(alarm_card);
  lv_obj_add_style(alarm_days, &style_alarm_day_chip, 0);
  lv_label_set_text(alarm_days, "LUN - VEN");
  lv_obj_set_pos(alarm_days, 14, 51);
  lv_obj_set_size(alarm_days, 62, 16);

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

#if LVGL_VERSION_MAJOR >= 9
  lv_screen_load(main_screen);
#else
  lv_scr_load(main_screen);
#endif
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
    static constexpr char WEEKDAYS_FR[7][9] = {"DIMANCHE", "LUNDI", "MARDI", "MERCREDI", "JEUDI", "VENDREDI", "SAMEDI"};
    static constexpr char MONTHS_FR[12][10] = {"JANVIER", "FEVRIER", "MARS", "AVRIL", "MAI", "JUIN", "JUILLET", "AOUT", "SEPTEMBRE", "OCTOBRE", "NOVEMBRE", "DECEMBRE"};
    char date_text[24];
    const uint8_t weekday = weekday_from_date(clock_state.year, clock_state.month, clock_state.day);
    snprintf(date_text, sizeof(date_text), "%s %02u %s", WEEKDAYS_FR[weekday], clock_state.day, MONTHS_FR[clock_state.month - 1]);
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
