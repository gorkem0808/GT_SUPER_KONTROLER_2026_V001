#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/regs/addressmap.h"
#include "bsp/board_api.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "tusb.h"
#include "gt_super_protocol.h"

#define FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - (2 * FLASH_SECTOR_SIZE))
#define SLOT0_OFFSET FLASH_TARGET_OFFSET
#define SLOT1_OFFSET (FLASH_TARGET_OFFSET + FLASH_SECTOR_SIZE)
#define MAX_LINE 160

static gts_controller_config_t cfg;
static uint32_t credits = 0;
static bool p1_active = false;
static bool p2_active = false;
static uint32_t p1_last_ms = 0;
static uint32_t p2_last_ms = 0;
static uint32_t last_status_ms = 0;
static char rx_line[MAX_LINE];
static uint32_t rx_len = 0;
static bool last_coin = false, last_p1_start = false, last_p2_start = false;
static bool last_p1_trig = false, last_p2_trig = false, last_p1_reload = false, last_p2_reload = false;
static bool last_p1_act = false, last_p2_act = false;
static uint32_t p1_vib_next_ms = 0, p2_vib_next_ms = 0;
static bool p1_vib_state = false, p2_vib_state = false;
static bool p1_relay_state = false, p2_relay_state = false;
static bool last_calibration_btn = false;
static bool last_keypad[10] = { false };
static bool phys_p1_trig = false;
static bool phys_p2_trig = false;


typedef struct {
    bool initialized;
    bool raw;
    bool stable;
    uint32_t changed_ms;
} debounce_t;

#define BTN_DEBOUNCE_MS 45u
#define GORKEM_CONTROLLER_MARKER 0xD5u /* V002M: eski bozuk controller flash kaydini gecersiz say */

static debounce_t db_coin;
static debounce_t db_p1_start, db_p2_start;
static debounce_t db_p1_trig, db_p2_trig;
static debounce_t db_p1_reload, db_p2_reload;
static debounce_t db_cal_btn;
static debounce_t db_keypad[9];

static bool debounce_update(debounce_t *d, bool raw, uint32_t now_ms) {
    if (!d->initialized) {
        d->initialized = true;
        d->raw = raw;
        d->stable = raw;
        d->changed_ms = now_ms;
        return d->stable;
    }
    if (raw != d->raw) {
        d->raw = raw;
        d->changed_ms = now_ms;
    }
    if ((uint32_t)(now_ms - d->changed_ms) >= BTN_DEBOUNCE_MS) {
        d->stable = d->raw;
    }
    return d->stable;
}


static void gorkem_lock_fixed_mapping(void);
static void cfg_save(void);

static uint32_t crc32_calc(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) crc = (crc >> 1) ^ (0xEDB88320u & (-(int32_t)(crc & 1u)));
    }
    return ~crc;
}

static void cfg_defaults(void) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.magic = GTS_MAGIC;
    cfg.version = GTS_CFG_VERSION;
    cfg.size = sizeof(cfg);
    cfg.seq = 1;
    cfg.common_coin_gp = 2;   /* V002: kredi girişi GP2 */
    /* V002M: kredi tercihi kullanıcıdan gelir; varsayılan LOW/GND = 0 */
    cfg.p1_start_gp = 3;   /* Gorkem final: GP3 = P1 Start + klavye 2 */
    cfg.p2_start_gp = 6;   /* Gorkem final: GP6 = P2 Start + klavye 5 */
    cfg.p1_trigger_gp = 4;  /* Gorkem final: GP4 = P1 Tetik + klavye 3 */
    cfg.p2_trigger_gp = 7;  /* Gorkem final: GP7 = P2 Tetik + klavye 6 */
    cfg.p1_reload_gp = 5;   /* Gorkem final: GP5 = P1 Bomba/Reload + klavye 4 */
    cfg.p2_reload_gp = 8;   /* Gorkem final: GP8 = P2 Bomba/Reload + klavye 7 */
    cfg.p1_relay_gp = 27;  /* Fiziksel pin 32 = GP27 = RELAY 1 */
    cfg.p2_relay_gp = 26;  /* Fiziksel pin 31 = GP26 = RELAY 2 */
    cfg.p1_vibration_gp = GTS_GP_DISABLED; /* V002: titreşim motoru röleye bağlı */
    cfg.p2_vibration_gp = GTS_GP_DISABLED; /* V002: titreşim motoru röleye bağlı */
    cfg.relay_active_high = cfg.coin_active_high; /* V002M: röle yönü kredi tercihini takip eder */
    cfg.vibration_active_high = 1;
    cfg.relay_mode = 0; /* V002M: kredi+start ile hazır, aktif oyuncu tetik basınca röle çeker */
    cfg.p1_activity_in_gp = GTS_GP_DISABLED; /* V002M: P1/P2 Controller'a fiziksel bağlı değil; hareket girişi kaldırıldı */
    cfg.p2_activity_in_gp = GTS_GP_DISABLED; /* V002M: P1/P2 Controller'a fiziksel bağlı değil; hareket girişi kaldırıldı */
    cfg.calibration_gp = GTS_GP_DISABLED; /* V002: kalibrasyon PC programında tetik/normal mouse ile onaylanır */
    cfg.system_enable_gp = GTS_GP_DISABLED; /* P1/P2 GP20 DIP switch potans aktif/pasif içindir; controller default daima aktif */
    cfg.menu_up_gp = GTS_GP_DISABLED;
    cfg.menu_down_gp = GTS_GP_DISABLED;
    cfg.menu_left_gp = GTS_GP_DISABLED;
    cfg.menu_right_gp = GTS_GP_DISABLED;
    cfg.menu_select_gp = GTS_GP_DISABLED;
    cfg.buzzer_gp = GTS_GP_DISABLED;
    cfg.idle_off_min = 5;
    cfg.vibration_mode = 0; /* kullanılmaz */
    cfg.vib_pulse_on_ms = 60;
    cfg.vib_pulse_off_ms = 140;
    cfg.key_coin = HID_KEY_1;
    cfg.key_p1_start = HID_KEY_2;
    cfg.key_p2_start = HID_KEY_5;
    cfg.key_p1_trigger = HID_KEY_3;
    cfg.key_p1_reload = HID_KEY_4;
    cfg.key_p2_trigger = HID_KEY_6;
    cfg.key_p2_reload = HID_KEY_7;
    gorkem_lock_fixed_mapping();
    cfg.crc32 = 0;
    cfg.crc32 = crc32_calc((uint8_t*)&cfg, sizeof(cfg));
}


static void gorkem_lock_fixed_mapping(void) {
    /* V002M NO ACTION: Bu bağlantı sabittir. PC programı veya eski kayıt yanlış değer gönderse bile bozulmaz. */
    cfg.common_coin_gp = 2;
    /* Kredi tercihi sabitlenmez; kullanıcı LOW/HIGH seçebilir. */
    cfg.relay_active_high = cfg.coin_active_high;
    cfg.p1_start_gp = 3;
    cfg.p2_start_gp = 6;
    cfg.p1_trigger_gp = 4;
    cfg.p2_trigger_gp = 7;
    cfg.p1_reload_gp = 5;
    cfg.p2_reload_gp = 8;
    cfg.p1_relay_gp = 27;
    cfg.p2_relay_gp = 26;
    cfg.p1_vibration_gp = GTS_GP_DISABLED;
    cfg.p2_vibration_gp = GTS_GP_DISABLED;
    cfg.relay_active_high = cfg.coin_active_high;
    cfg.relay_mode = 0;
    cfg.p1_activity_in_gp = GTS_GP_DISABLED;
    cfg.p2_activity_in_gp = GTS_GP_DISABLED;
    cfg.calibration_gp = GTS_GP_DISABLED;
    cfg.system_enable_gp = GTS_GP_DISABLED;
    cfg.key_coin = HID_KEY_1;
    cfg.key_p1_start = HID_KEY_2;
    cfg.key_p2_start = HID_KEY_5;
    cfg.key_p1_trigger = HID_KEY_3;
    cfg.key_p1_reload = HID_KEY_4;
    cfg.key_p2_trigger = HID_KEY_6;
    cfg.key_p2_reload = HID_KEY_7;
    cfg.reserved[0] = GORKEM_CONTROLLER_MARKER;
}

static bool cfg_valid(const gts_controller_config_t *c) {
    if (c->magic != GTS_MAGIC || c->version != GTS_CFG_VERSION || c->size != sizeof(*c)) return false;
    if (c->reserved[0] != GORKEM_CONTROLLER_MARKER) return false; /* eski V002/V002B kaydi okunmasin */
    gts_controller_config_t tmp = *c;
    uint32_t old = tmp.crc32;
    tmp.crc32 = 0;
    return old == crc32_calc((uint8_t*)&tmp, sizeof(tmp));
}

static const gts_controller_config_t *slot_ptr(uint32_t offset) {
    return (const gts_controller_config_t *)(XIP_BASE + offset);
}

static void cfg_load(void) {
    const gts_controller_config_t *a = slot_ptr(SLOT0_OFFSET);
    const gts_controller_config_t *b = slot_ptr(SLOT1_OFFSET);
    bool av = cfg_valid(a), bv = cfg_valid(b);
    bool had_valid = true;
    if (av && bv) cfg = (a->seq >= b->seq) ? *a : *b;
    else if (av) cfg = *a;
    else if (bv) cfg = *b;
    else { cfg_defaults(); had_valid = false; }
    gorkem_lock_fixed_mapping();
    if (!had_valid) cfg_save();
}

static void cfg_save(void) {
    gorkem_lock_fixed_mapping();
    cfg.magic = GTS_MAGIC;
    cfg.version = GTS_CFG_VERSION;
    cfg.size = sizeof(cfg);
    cfg.seq++;
    cfg.crc32 = 0;
    cfg.crc32 = crc32_calc((uint8_t*)&cfg, sizeof(cfg));
    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    memcpy(page, &cfg, sizeof(cfg));
    uint32_t target = (cfg.seq & 1) ? SLOT0_OFFSET : SLOT1_OFFSET;
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(target, FLASH_SECTOR_SIZE);
    flash_range_program(target, page, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
}

static bool valid_gp(uint8_t gp) { return gp <= 28; }

static void pin_input_pull(uint8_t gp) {
    if (!valid_gp(gp)) return;
    gpio_init(gp);
    gpio_set_dir(gp, GPIO_IN);
    gpio_pull_up(gp);
}

static void pin_output(uint8_t gp, bool active_high) {
    if (!valid_gp(gp)) return;
    gpio_init(gp);
    gpio_set_dir(gp, GPIO_OUT);
    gpio_put(gp, active_high ? 0 : 1);
}

static bool read_pin_active(uint8_t gp, bool active_high) {
    if (!valid_gp(gp)) return false;
    bool v = gpio_get(gp);
    return active_high ? v : !v;
}

static void put_active(uint8_t gp, bool active_high, bool on) {
    if (!valid_gp(gp)) return;
    gpio_put(gp, active_high ? on : !on);
}

static void apply_pins(void) {
    pin_input_pull(cfg.common_coin_gp);
    pin_input_pull(cfg.p1_start_gp); pin_input_pull(cfg.p2_start_gp);
    pin_input_pull(cfg.p1_trigger_gp); pin_input_pull(cfg.p2_trigger_gp);
    pin_input_pull(cfg.p1_reload_gp); pin_input_pull(cfg.p2_reload_gp);
    /* V002M: P1_ACTIN/P2_ACTIN tamamen kaldırıldı. Controller ile silah Pico arasında hareket kablosu yok. */
    pin_input_pull(cfg.system_enable_gp); pin_input_pull(cfg.calibration_gp);
    pin_input_pull(cfg.menu_up_gp); pin_input_pull(cfg.menu_down_gp);
    pin_input_pull(cfg.menu_left_gp); pin_input_pull(cfg.menu_right_gp); pin_input_pull(cfg.menu_select_gp);
    for (uint8_t gp = 3; gp <= 11; gp++) pin_input_pull(gp); /* V002: GP2 coin+kredi+klavye 1, GP3-GP11 -> klavye 2-0 */
    pin_output(cfg.p1_relay_gp, cfg.relay_active_high);
    pin_output(cfg.p2_relay_gp, cfg.relay_active_high);
    pin_output(cfg.p1_vibration_gp, cfg.vibration_active_high);
    pin_output(cfg.p2_vibration_gp, cfg.vibration_active_high);
    pin_output(cfg.buzzer_gp, 1);
}

static void cdc_send(const char *s) {
    if (tud_cdc_connected()) {
        tud_cdc_write_str(s);
        tud_cdc_write_flush();
    }
}

static void send_key_once(uint8_t key) {
    if (!tud_hid_ready() || key == HID_KEY_NONE) return;
    uint8_t keys[6] = { key, 0,0,0,0,0 };
    tud_hid_keyboard_report(0, 0, keys);
    sleep_ms(12);
    uint8_t empty[6] = {0};
    tud_hid_keyboard_report(0, 0, empty);
}


static bool gp_is_number_keypad(uint8_t gp) {
    /* GP2 coin butonu kendi içinde klavye 1 gönderir. GP3-GP11 sayı tuşlarıdır. */
    return (gp == cfg.common_coin_gp) || (gp >= 3 && gp <= 11);
}

static uint8_t last_keyboard_report_keys[6] = {0};

static void send_numeric_keyboard_held(bool k1, bool k2, bool k3, bool k4, bool k5, bool k6, bool k7) {
    /* V002M: Butonlar gerçek klavye gibi çalışır.
       Tek basış = 1 karakter; basılı tutma = Windows klavye tekrar hızıyla sürekli karakter.
       GP2=1, GP3=2, GP4=3, GP5=4, GP6=5, GP7=6, GP8=7. */
    if (!tud_hid_ready()) return;
    uint8_t keys[6] = {0};
    uint8_t n = 0;
    if (k1 && n < 6) keys[n++] = HID_KEY_1;
    if (k2 && n < 6) keys[n++] = HID_KEY_2;
    if (k3 && n < 6) keys[n++] = HID_KEY_3;
    if (k4 && n < 6) keys[n++] = HID_KEY_4;
    if (k5 && n < 6) keys[n++] = HID_KEY_5;
    if (k6 && n < 6) keys[n++] = HID_KEY_6;
    if (k7 && n < 6) keys[n++] = HID_KEY_7;
    if (memcmp(keys, last_keyboard_report_keys, sizeof(keys)) != 0) {
        tud_hid_keyboard_report(0, 0, keys);
        memcpy(last_keyboard_report_keys, keys, sizeof(keys));
    }
}

static void release_numeric_keyboard(void) {
    uint8_t empty[6] = {0};
    if (memcmp(empty, last_keyboard_report_keys, sizeof(empty)) != 0 && tud_hid_ready()) {
        tud_hid_keyboard_report(0, 0, empty);
        memcpy(last_keyboard_report_keys, empty, sizeof(empty));
    }
}

static void set_relay_states(bool p1_on, bool p2_on) {
    p1_relay_state = p1_on;
    p2_relay_state = p2_on;
    put_active(cfg.p1_relay_gp, cfg.relay_active_high, p1_on);
    put_active(cfg.p2_relay_gp, cfg.relay_active_high, p2_on);
}

static void deactivate_p1(void) {
    p1_active = false;
    p1_relay_state = false;
    put_active(cfg.p1_relay_gp, cfg.relay_active_high, false);
    put_active(cfg.p1_vibration_gp, cfg.vibration_active_high, false);
    p1_vib_state = false;
}
static void deactivate_p2(void) {
    p2_active = false;
    p2_relay_state = false;
    put_active(cfg.p2_relay_gp, cfg.relay_active_high, false);
    put_active(cfg.p2_vibration_gp, cfg.vibration_active_high, false);
    p2_vib_state = false;
}

static void buzzer_beep(uint16_t ms) {
    put_active(cfg.buzzer_gp, 1, true);
    sleep_ms(ms);
    put_active(cfg.buzzer_gp, 1, false);
}

static bool system_enabled(void) {
    if (!valid_gp(cfg.system_enable_gp)) return true;
    return read_pin_active(cfg.system_enable_gp, false); // opsiyonel controller enable: GND = aktif
}


static void process_keypad_gp3_gp11(uint32_t now) {
    /*
       V002 TUŞ TAKIMI:
       GP2  -> tuş takımındaki COIN/KREDİ butonu + klavye 1 gönderir
       GP3  -> klavye 2
       GP4  -> klavye 3
       GP5  -> klavye 4
       GP6  -> klavye 5
       GP7  -> klavye 6
       GP8  -> klavye 7
       GP9  -> klavye 8
       GP10 -> klavye 9
       GP11 -> klavye 0 (10. tuş)
       Aktif seviye: GND'ye çekilince basılı kabul edilir.
    */
    const uint8_t gps[9]  = {3,4,5,6,7,8,9,10,11};
    const uint8_t keys[9] = {HID_KEY_2,HID_KEY_3,HID_KEY_4,HID_KEY_5,HID_KEY_6,HID_KEY_7,HID_KEY_8,HID_KEY_9,HID_KEY_0};
    for (uint8_t i = 0; i < 9; i++) {
        bool pressed = debounce_update(&db_keypad[i], read_pin_active(gps[i], false), now);
        if (pressed && !last_keypad[i]) {
            send_key_once(keys[i]);
            char ev[32];
            uint8_t num = (i == 8) ? 10 : (uint8_t)(i + 2);
            snprintf(ev, sizeof(ev), "EVENT,KEYPAD,GP%u,%u\n", gps[i], num);
            cdc_send(ev);
        }
        last_keypad[i] = pressed;
    }
}

static void process_logic(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    bool enabled = system_enabled();
    if (!enabled) {
        deactivate_p1(); deactivate_p2();
        release_numeric_keyboard();
        return;
    }

    /* V002M: GP2-GP8 artık HID klavye gibi basılı tutulur; eski tek-pulse keypad fonksiyonu kullanılmaz. */

    bool coin = debounce_update(&db_coin, read_pin_active(cfg.common_coin_gp, cfg.coin_active_high != 0), now);
    bool p1_start = debounce_update(&db_p1_start, read_pin_active(cfg.p1_start_gp, false), now);
    bool p2_start = debounce_update(&db_p2_start, read_pin_active(cfg.p2_start_gp, false), now);
    bool p1_trig = debounce_update(&db_p1_trig, read_pin_active(cfg.p1_trigger_gp, false), now);
    bool p2_trig = debounce_update(&db_p2_trig, read_pin_active(cfg.p2_trigger_gp, false), now);
    phys_p1_trig = p1_trig;
    phys_p2_trig = p2_trig;
    bool p1_reload = debounce_update(&db_p1_reload, read_pin_active(cfg.p1_reload_gp, false), now);
    bool p2_reload = debounce_update(&db_p2_reload, read_pin_active(cfg.p2_reload_gp, false), now);
    bool p1_act = false; /* V002M: ACTIN kaldırıldı */
    bool p2_act = false; /* V002M: ACTIN kaldırıldı */
    bool cal_btn = debounce_update(&db_cal_btn, read_pin_active(cfg.calibration_gp, false), now);
    if (cal_btn && !last_calibration_btn) { cdc_send("EVENT,CAL_TRIGGER\n"); buzzer_beep(35); }
    last_calibration_btn = cal_btn;

    if (coin && !last_coin) {
        credits++;
        buzzer_beep(45);
    }
    last_coin = coin;

    if (p1_start && !last_p1_start) {
        /* V002M: P1 Start klavye karakteri GP3 basılı kaldığı sürece HID raporunda tutulur. */
        if (!p1_active && credits > 0) {
            credits--;
            p1_active = true;
            p1_last_ms = now;
            /* Oyuncu aktif oldu; röle tetik basılana kadar çekmez. */
            buzzer_beep(80);
        }
    }
    if (p2_start && !last_p2_start) {
        /* V002M: P2 Start klavye karakteri GP6 basılı kaldığı sürece HID raporunda tutulur. */
        if (!p2_active && credits > 0) {
            credits--;
            p2_active = true;
            p2_last_ms = now;
            /* Oyuncu aktif oldu; röle tetik basılana kadar çekmez. */
            buzzer_beep(80);
        }
    }
    last_p1_start = p1_start;
    last_p2_start = p2_start;

    if (p1_active && (p1_trig || p1_reload || (p1_act && !last_p1_act))) p1_last_ms = now;
    if (p2_active && (p2_trig || p2_reload || (p2_act && !last_p2_act))) p2_last_ms = now;
    last_p1_act = p1_act; last_p2_act = p2_act;

    uint32_t idle_ms = (uint32_t)cfg.idle_off_min * 60000u;
    if (p1_active && idle_ms && (now - p1_last_ms > idle_ms)) deactivate_p1();
    if (p2_active && idle_ms && (now - p2_last_ms > idle_ms)) deactivate_p2();

    /* V002M SABİT RÖLE MANTIĞI:
       Kredi atılınca ortak kredi artar.
       Start basılınca ilgili oyuncu aktif/hazır olur.
       Oyuncu aktif değilse tetik röle çekmez.
       Oyuncu aktifken tetik basılıysa röle çeker, bırakınca bırakır.
       Boşta kalma süresi dolunca oyuncu pasif olur ve röle kapanır. */
    bool p1_fire = p1_trig;
    bool p2_fire = p2_trig;
    bool p1_rel = p1_reload;
    bool p2_rel = p2_reload;

    bool p1_relay_on = p1_active && p1_trig;
    bool p2_relay_on = p2_active && p2_trig;
    set_relay_states(p1_relay_on, p2_relay_on);

    /* V002M NO ACTION:
       GP2-GP8 artık gerçek klavye gibi tutulur.
       Tek basış 1 karakter üretir; basılı tutunca Windows kendi tekrar hızıyla sürekli yazar. */
    send_numeric_keyboard_held(coin, p1_start, p1_fire, p1_rel, p2_start, p2_fire, p2_rel);
    last_p1_trig = p1_fire; last_p1_reload = p1_rel; last_p2_trig = p2_fire; last_p2_reload = p2_rel;

    /* V002: titreşim motoru ayrı GP çıkışıyla sürülmez.
       Motor röleye bağlıdır; tetik basınca röle çektiği için motor da röle üzerinden çalışır. */
    p1_vib_state = p1_relay_on;
    p2_vib_state = p2_relay_on;
}

static void send_cfg(void) {
    char b[512];
    snprintf(b, sizeof(b),
        "CFG,CONTROLLER,V002M_SADE_GUNCEL,SEQ,%lu,CREDIT_GP,%u,COIN_AH,%u,P1_START,%u,P2_START,%u,P1_TRIG,%u,P2_TRIG,%u,P1_RELOAD,%u,P2_RELOAD,%u,P1_RELAY,%u,P2_RELAY,%u,P1_VIB,%u,P2_VIB,%u,P1_ACTIN,%u,P2_ACTIN,%u,CAL_GP,%u,CTRL_ENABLE_GP,%u,IDLE_MIN,%u,REL_AH,%u,VIB_AH,%u,RELAY_MODE,%u,VIB_MODE,%u,KEY_COIN,%u,KEY_P1_START,%u,KEY_P2_START,%u,KEY_P1_TRIG,%u,KEY_P1_RELOAD,%u,KEY_P2_TRIG,%u,KEY_P2_RELOAD,%u\n",
        cfg.seq, cfg.common_coin_gp, cfg.coin_active_high, cfg.p1_start_gp, cfg.p2_start_gp,
        cfg.p1_trigger_gp, cfg.p2_trigger_gp, cfg.p1_reload_gp, cfg.p2_reload_gp,
        cfg.p1_relay_gp, cfg.p2_relay_gp, cfg.p1_vibration_gp, cfg.p2_vibration_gp,
        cfg.p1_activity_in_gp, cfg.p2_activity_in_gp, cfg.calibration_gp, cfg.system_enable_gp, cfg.idle_off_min,
        cfg.relay_active_high, cfg.vibration_active_high, cfg.relay_mode, cfg.vibration_mode,
        cfg.key_coin, cfg.key_p1_start, cfg.key_p2_start, cfg.key_p1_trigger, cfg.key_p1_reload, cfg.key_p2_trigger, cfg.key_p2_reload);
    cdc_send(b);
}

static void send_status(void) {
    char b[256];
    snprintf(b, sizeof(b), "STATUS,CONTROLLER,V002M,CREDITS,%lu,P1,%u,P2,%u,R1,%u,R2,%u,T1,%u,T2,%u,EN,%u,CAL,%u\n",
             credits, p1_active, p2_active, p1_relay_state, p2_relay_state, phys_p1_trig, phys_p2_trig, system_enabled(), read_pin_active(cfg.calibration_gp, false));
    cdc_send(b);
}

static void handle_set(char *key, char *val) {
    int v = atoi(val);
    if (!strcmp(key,"CREDIT_GP")) cfg.common_coin_gp = v;
    else if (!strcmp(key,"COIN_AH")) { cfg.coin_active_high = v ? 1 : 0; cfg.relay_active_high = cfg.coin_active_high; }
    else if (!strcmp(key,"P1_START")) cfg.p1_start_gp = v;
    else if (!strcmp(key,"P2_START")) cfg.p2_start_gp = v;
    else if (!strcmp(key,"P1_TRIG")) cfg.p1_trigger_gp = v;
    else if (!strcmp(key,"P2_TRIG")) cfg.p2_trigger_gp = v;
    else if (!strcmp(key,"P1_RELOAD")) cfg.p1_reload_gp = v;
    else if (!strcmp(key,"P2_RELOAD")) cfg.p2_reload_gp = v;
    else if (!strcmp(key,"P1_RELAY")) cfg.p1_relay_gp = v;
    else if (!strcmp(key,"P2_RELAY")) cfg.p2_relay_gp = v;
    else if (!strcmp(key,"P1_VIB")) cfg.p1_vibration_gp = v;
    else if (!strcmp(key,"P2_VIB")) cfg.p2_vibration_gp = v;
    else if (!strcmp(key,"P1_ACTIN")) cfg.p1_activity_in_gp = GTS_GP_DISABLED; /* V002M: kaldırıldı */
    else if (!strcmp(key,"P2_ACTIN")) cfg.p2_activity_in_gp = GTS_GP_DISABLED; /* V002M: kaldırıldı */
    else if (!strcmp(key,"CAL_GP")) cfg.calibration_gp = v;
    else if (!strcmp(key,"CTRL_ENABLE_GP")) cfg.system_enable_gp = v;
    else if (!strcmp(key,"RELAY_MODE")) cfg.relay_mode = 0; /* V002M: röle modu sabit */
    else if (!strcmp(key,"IDLE_MIN")) cfg.idle_off_min = (v < 1) ? 1 : (v > 30 ? 30 : v);
    else if (!strcmp(key,"REL_AH")) cfg.relay_active_high = cfg.coin_active_high; /* V002M: ayrı röle yönü yok */
    else if (!strcmp(key,"VIB_AH")) cfg.vibration_active_high = v;
    else if (!strcmp(key,"VIB_MODE")) cfg.vibration_mode = v;
    else if (!strcmp(key,"KEY_COIN")) cfg.key_coin = v;
    else if (!strcmp(key,"KEY_P1_START")) cfg.key_p1_start = v;
    else if (!strcmp(key,"KEY_P2_START")) cfg.key_p2_start = v;
    else if (!strcmp(key,"KEY_P1_TRIG")) cfg.key_p1_trigger = v;
    else if (!strcmp(key,"KEY_P1_RELOAD")) cfg.key_p1_reload = v;
    else if (!strcmp(key,"KEY_P2_TRIG")) cfg.key_p2_trigger = v;
    else if (!strcmp(key,"KEY_P2_RELOAD")) cfg.key_p2_reload = v;
    gorkem_lock_fixed_mapping();
    apply_pins();
}

static void handle_line(char *line) {
    if (!strcmp(line, "HELLO") || !strcmp(line,"GETCFG")) { cdc_send("HELLO,CONTROLLER,V002M_SADE_GUNCEL\n"); send_cfg(); return; }
    if (!strcmp(line, "STATUS")) { send_status(); return; }
    if (!strcmp(line, "SAVE")) { cfg_save(); cdc_send("OK,SAVED\n"); return; }
    if (!strcmp(line, "FACTORY")) { cfg_defaults(); cfg_save(); apply_pins(); cdc_send("OK,FACTORY\n"); return; }
    if (!strcmp(line, "P1OFF")) { deactivate_p1(); cdc_send("OK,P1OFF\n"); return; }
    if (!strcmp(line, "P2OFF")) { deactivate_p2(); cdc_send("OK,P2OFF\n"); return; }
    if (!strncmp(line, "SET,", 4)) {
        char *k = line + 4;
        char *v = strchr(k, ',');
        if (v) { *v++ = 0; handle_set(k, v); cdc_send("OK,SET,LOCKED\n"); }
        else cdc_send("ERR,SET\n");
        return;
    }
    if (!strcmp(line,"TEST,P1_RELAY")) { put_active(cfg.p1_relay_gp,cfg.relay_active_high,true); sleep_ms(250); put_active(cfg.p1_relay_gp,cfg.relay_active_high,false); cdc_send("OK,TEST,P1_RELAY\n"); return; }
    if (!strcmp(line,"TEST,P2_RELAY")) { put_active(cfg.p2_relay_gp,cfg.relay_active_high,true); sleep_ms(250); put_active(cfg.p2_relay_gp,cfg.relay_active_high,false); cdc_send("OK,TEST,P2_RELAY\n"); return; }
    if (!strcmp(line,"TEST,P1_VIB") || !strcmp(line,"TEST,P2_VIB")) { cdc_send("OK,V002,VIBRATION_CONNECTED_TO_RELAY\n"); return; }
    cdc_send("ERR,UNKNOWN\n");
}

static void poll_cdc(void) {
    while (tud_cdc_available()) {
        char ch;
        tud_cdc_read(&ch, 1);
        if (ch == '\r') continue;
        if (ch == '\n') {
            rx_line[rx_len] = 0;
            if (rx_len) handle_line(rx_line);
            rx_len = 0;
        } else if (rx_len < MAX_LINE - 1) {
            rx_line[rx_len++] = ch;
        }
    }
}

int main(void) {
    board_init();
    tusb_init();
    cfg_load();
    apply_pins();
    uint32_t now = to_ms_since_boot(get_absolute_time());
    p1_last_ms = p2_last_ms = now;

    while (true) {
        tud_task();
        poll_cdc();
        process_logic();
        now = to_ms_since_boot(get_absolute_time());
        if (now - last_status_ms > 1000) {
            last_status_ms = now;
            send_status();
        }
        sleep_ms(2);
    }
}
