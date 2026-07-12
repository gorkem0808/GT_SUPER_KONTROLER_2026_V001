#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/regs/addressmap.h"
#include "bsp/board_api.h"
#include "hardware/adc.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "tusb.h"
#include "gt_super_protocol.h"

#ifndef PLAYER_NUM
#define PLAYER_NUM 1
#endif
#define FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - (2 * FLASH_SECTOR_SIZE))
#define SLOT0_OFFSET FLASH_TARGET_OFFSET
#define SLOT1_OFFSET (FLASH_TARGET_OFFSET + FLASH_SECTOR_SIZE)
#define MAX_LINE 128
#define GORKEM_MOUSE_MARKER 0xE7u /* V001D: GP19 kilit + otomatik X/Y kalibrasyon */
#define GORKEM_MOUSE_FW_TAG "V001D_XY_AUTO"

typedef struct __attribute__((packed)) {
    uint8_t buttons;
    int16_t x;
    int16_t y;
} abs_mouse_report_t;

static gts_mouse_config_t cfg;
static uint16_t fx = 0, fy = 0;
static uint16_t last_raw_x = 0, last_raw_y = 0;
static uint32_t last_status_ms = 0, last_report_ms = 0, pulse_until_ms = 0;
static char rx_line[MAX_LINE];
static uint32_t rx_len = 0;

static void cfg_save(void);
static void gp19_lock_fixed_mapping(void);

static uint32_t crc32_calc(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) crc = (crc >> 1) ^ (0xEDB88320u & (-(int32_t)(crc & 1u)));
    }
    return ~crc;
}


static void gp19_lock_fixed_mapping(void) {
    /* V001C KESIN KILIT:
       GP19 her zaman DIP aktif/pasif pinidir.
       Eski kayit veya PC programi ENABLE_GP=255 gonderse bile mouse daima GP19'a bakar.
       GP19 GND = mouse aktif, GP19 acikta = mouse pasif.
    */
    cfg.enable_gp = 19;
    cfg.calibration_gp = GTS_GP_DISABLED;
    cfg.activity_out_gp = GTS_GP_DISABLED;
    /* Eski/bozuk kayıtta aynı veya geçersiz ADC kanalı varsa güvenli varsayılana dön. */
    if (cfg.x_adc > 1 || cfg.y_adc > 1 || cfg.x_adc == cfg.y_adc) {
        cfg.x_adc = 0;
        cfg.y_adc = 1;
    }
    cfg.reserved[0] = GORKEM_MOUSE_MARKER;
}

static void cfg_defaults(void) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.magic = GTS_MAGIC;
    cfg.version = GTS_CFG_VERSION;
    cfg.size = sizeof(cfg);
    cfg.seq = 1;
    cfg.player_num = PLAYER_NUM;
    cfg.x_adc = 0;
    cfg.y_adc = 1;
    cfg.enable_gp = 19;
    cfg.calibration_gp = GTS_GP_DISABLED;
    cfg.activity_out_gp = GTS_GP_DISABLED; /* V002M: Controller ile hareket kablosu yok, ACTOUT kaldırıldı */
    cfg.invert_x = 0;
    cfg.invert_y = 0;
    cfg.filter_shift = 4;   /* V002M: titreşim azaltma varsayılan güçlü */
    cfg.min_move_threshold = 12; /* V002M: küçük potans gürültüsünü yok say */
    cfg.x_min = 200;
    cfg.x_max = 3900;
    cfg.y_min = 200;
    cfg.y_max = 3900;
    cfg.edge_left = 0; cfg.edge_right = 0; cfg.edge_top = 0; cfg.edge_bottom = 0;
    gp19_lock_fixed_mapping();
    cfg.crc32 = 0;
    cfg.crc32 = crc32_calc((uint8_t*)&cfg, sizeof(cfg));
}

static bool cfg_valid(const gts_mouse_config_t *c) {
    if (c->magic != GTS_MAGIC || c->version != GTS_CFG_VERSION || c->size != sizeof(*c)) return false;
    if (c->reserved[0] != GORKEM_MOUSE_MARKER) return false;
    gts_mouse_config_t tmp = *c;
    uint32_t old = tmp.crc32;
    tmp.crc32 = 0;
    return old == crc32_calc((uint8_t*)&tmp, sizeof(tmp));
}

static const gts_mouse_config_t *slot_ptr(uint32_t offset) {
    return (const gts_mouse_config_t *)(XIP_BASE + offset);
}

static void cfg_load(void) {
    const gts_mouse_config_t *a = slot_ptr(SLOT0_OFFSET);
    const gts_mouse_config_t *b = slot_ptr(SLOT1_OFFSET);
    bool av = cfg_valid(a), bv = cfg_valid(b);
    bool had_valid = true;
    if (av && bv) cfg = (a->seq >= b->seq) ? *a : *b;
    else if (av) cfg = *a;
    else if (bv) cfg = *b;
    else { cfg_defaults(); had_valid = false; }
    cfg.player_num = PLAYER_NUM;
    gp19_lock_fixed_mapping();
    if (!had_valid) cfg_save();
}

static void cfg_save(void) {
    gp19_lock_fixed_mapping();
    cfg.magic = GTS_MAGIC;
    cfg.version = GTS_CFG_VERSION;
    cfg.size = sizeof(cfg);
    cfg.player_num = PLAYER_NUM;
    cfg.reserved[0] = GORKEM_MOUSE_MARKER;
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

static void cdc_send(const char *s) {
    if (tud_cdc_connected()) {
        tud_cdc_write_str(s);
        tud_cdc_write_flush();
    }
}

static uint16_t adc_read_ch(uint8_t ch) {
    adc_select_input(ch);
    sleep_us(5);
    return adc_read();
}

static void reset_axis_filters(void) {
    /* X/Y ADC eşlemesi değiştiğinde eski kanalın filtre değerleri kalmasın. */
    uint16_t x = adc_read_ch(cfg.x_adc);
    uint16_t y = adc_read_ch(cfg.y_adc);
    fx = x;
    fy = y;
    last_raw_x = x;
    last_raw_y = y;
}

static bool valid_gp(uint8_t gp) { return gp <= 28; }

static bool enabled(void) {
    /* V001C: ENABLE_GP ayarina bakma; sadece fiziksel GP19'a bak.
       GP19 acikta/pull-up = pasif, GP19 GND = aktif. */
    return !gpio_get(19);
}

static int16_t map_axis(uint16_t raw, uint16_t mn, uint16_t mx, uint8_t inv, int16_t edge_min, int16_t edge_max) {
    int32_t minv = (int32_t)mn - edge_min;
    int32_t maxv = (int32_t)mx + edge_max;
    if (maxv <= minv) { minv = 0; maxv = 4095; }
    int32_t v = raw;
    if (v < minv) v = minv;
    if (v > maxv) v = maxv;
    int32_t out = (v - minv) * 32767 / (maxv - minv);
    if (inv) out = 32767 - out;
    if (out < 0) out = 0;
    if (out > 32767) out = 32767;
    return (int16_t)out;
}

static void apply_pins(void) {
    gp19_lock_fixed_mapping();
    gpio_init(19); gpio_set_dir(19, GPIO_IN); gpio_pull_up(19);
    if (valid_gp(cfg.calibration_gp)) { gpio_init(cfg.calibration_gp); gpio_set_dir(cfg.calibration_gp, GPIO_IN); gpio_pull_up(cfg.calibration_gp); }
    if (valid_gp(cfg.activity_out_gp)) { gpio_init(cfg.activity_out_gp); gpio_set_dir(cfg.activity_out_gp, GPIO_OUT); gpio_put(cfg.activity_out_gp, 0); }
}

static void send_cfg(void) {
    char b[420];
    snprintf(b, sizeof(b), "CFG,MOUSE,P%u,%s,SEQ,%lu,XADC,%u,YADC,%u,XMIN,%u,XMAX,%u,YMIN,%u,YMAX,%u,INVX,%u,INVY,%u,FILTER,%u,THR,%u,ENABLE_GP,%u,CAL_GP,%u,ACTOUT_GP,%u,EDGE_L,%d,EDGE_R,%d,EDGE_T,%d,EDGE_B,%d\n",
             PLAYER_NUM, GORKEM_MOUSE_FW_TAG, (unsigned long)cfg.seq, cfg.x_adc, cfg.y_adc,
             cfg.x_min, cfg.x_max, cfg.y_min, cfg.y_max, cfg.invert_x, cfg.invert_y, cfg.filter_shift,
             cfg.min_move_threshold, cfg.enable_gp, cfg.calibration_gp, cfg.activity_out_gp,
             cfg.edge_left, cfg.edge_right, cfg.edge_top, cfg.edge_bottom);
    cdc_send(b);
}

static void send_status(uint16_t rx, uint16_t ry, int16_t hx, int16_t hy) {
    char b[280];
    snprintf(b, sizeof(b), "STATUS,MOUSE,P%u,%s,RAW,%u,%u,HID,%d,%d,XADC,%u,YADC,%u,ACTIVE,%u,DIP19,%u,CALBTN,%u,CAL,%u,%u,%u,%u,FILTER,%u\n",
             PLAYER_NUM, GORKEM_MOUSE_FW_TAG, rx, ry, hx, hy, cfg.x_adc, cfg.y_adc,
             enabled(), !gpio_get(19), valid_gp(cfg.calibration_gp) ? !gpio_get(cfg.calibration_gp) : 0,
             cfg.x_min, cfg.x_max, cfg.y_min, cfg.y_max, cfg.filter_shift);
    cdc_send(b);
}

static void handle_set(char *key, char *val) {
    int v = atoi(val);
    bool axis_changed = false;
    if (!strcmp(key,"XADC")) { cfg.x_adc = (v == 1) ? 1 : 0; axis_changed = true; }
    else if (!strcmp(key,"YADC")) { cfg.y_adc = (v == 1) ? 1 : 0; axis_changed = true; }
    else if (!strcmp(key,"XMIN")) cfg.x_min = v;
    else if (!strcmp(key,"XMAX")) cfg.x_max = v;
    else if (!strcmp(key,"YMIN")) cfg.y_min = v;
    else if (!strcmp(key,"YMAX")) cfg.y_max = v;
    else if (!strcmp(key,"INVX")) cfg.invert_x = v ? 1 : 0;
    else if (!strcmp(key,"INVY")) cfg.invert_y = v ? 1 : 0;
    else if (!strcmp(key,"FILTER")) cfg.filter_shift = (v > 7) ? 7 : v;
    else if (!strcmp(key,"THR")) cfg.min_move_threshold = v;
    else if (!strcmp(key,"ENABLE_GP")) cfg.enable_gp = 19; /* GP19 sabit kilit */
    else if (!strcmp(key,"CAL_GP")) cfg.calibration_gp = v;
    else if (!strcmp(key,"ACTOUT_GP")) cfg.activity_out_gp = GTS_GP_DISABLED;
    else if (!strcmp(key,"EDGE_L")) cfg.edge_left = v;
    else if (!strcmp(key,"EDGE_R")) cfg.edge_right = v;
    else if (!strcmp(key,"EDGE_T")) cfg.edge_top = v;
    else if (!strcmp(key,"EDGE_B")) cfg.edge_bottom = v;
    gp19_lock_fixed_mapping();
    apply_pins();
    if (axis_changed) reset_axis_filters();
}

static void handle_line(char *line) {
    if (!strcmp(line,"HELLO") || !strcmp(line,"GETCFG")) {
        char h[72];
        snprintf(h, sizeof(h), "HELLO,MOUSE,P%u,%s\n", PLAYER_NUM, GORKEM_MOUSE_FW_TAG);
        cdc_send(h);
        send_cfg();
        return;
    }
    if (!strcmp(line,"SAVE")) { cfg_save(); cdc_send("OK,SAVED\n"); return; }
    if (!strcmp(line,"FACTORY")) { cfg_defaults(); cfg_save(); apply_pins(); reset_axis_filters(); cdc_send("OK,FACTORY\n"); return; }
    if (!strncmp(line,"SET,",4)) {
        char *k = line + 4;
        char *v = strchr(k, ',');
        if (v) { *v++ = 0; handle_set(k,v); cdc_send("OK,SET\n"); }
        else cdc_send("ERR,SET\n");
        return;
    }
    /*
       Yeni atomik kalibrasyon komutu:
       CALXY,xadc,yadc,xmin,xmax,ymin,ymax,invx,invy
       Böylece X/Y kanalı, yönler ve sınırlar tek seferde kaydedilir.
    */
    if (!strncmp(line,"CALXY,",6)) {
        int xadc,yadc,xmin,xmax,ymin,ymax,invx,invy;
        int n = sscanf(line+6, "%d,%d,%d,%d,%d,%d,%d,%d",
                       &xadc,&yadc,&xmin,&xmax,&ymin,&ymax,&invx,&invy);
        bool valid = (n == 8) &&
                     (xadc >= 0 && xadc <= 1) &&
                     (yadc >= 0 && yadc <= 1) &&
                     (xadc != yadc) &&
                     (xmin >= 0 && xmax <= 4095 && xmax > xmin) &&
                     (ymin >= 0 && ymax <= 4095 && ymax > ymin) &&
                     (invx == 0 || invx == 1) &&
                     (invy == 0 || invy == 1);
        if (valid) {
            cfg.x_adc = (uint8_t)xadc;
            cfg.y_adc = (uint8_t)yadc;
            cfg.x_min = (uint16_t)xmin;
            cfg.x_max = (uint16_t)xmax;
            cfg.y_min = (uint16_t)ymin;
            cfg.y_max = (uint16_t)ymax;
            cfg.invert_x = (uint8_t)invx;
            cfg.invert_y = (uint8_t)invy;
            reset_axis_filters();
            cfg_save();
            cdc_send("OK,CALXY,SAVED\n");
        } else {
            cdc_send("ERR,CALXY\n");
        }
        return;
    }
    /* Eski CAL komutu geriye uyumluluk için korunur. */
    if (!strncmp(line,"CAL,",4)) {
        int xmin,xmax,ymin,ymax;
        if (sscanf(line+4, "%d,%d,%d,%d", &xmin,&xmax,&ymin,&ymax) == 4) {
            cfg.x_min=xmin; cfg.x_max=xmax; cfg.y_min=ymin; cfg.y_max=ymax;
            reset_axis_filters();
            cfg_save();
            cdc_send("OK,CAL,SAVED\n");
        } else cdc_send("ERR,CAL\n");
        return;
    }
    cdc_send("ERR,UNKNOWN\n");
}

static void poll_cdc(void) {
    while (tud_cdc_available()) {
        char ch; tud_cdc_read(&ch, 1);
        if (ch == '\r') continue;
        if (ch == '\n') {
            rx_line[rx_len] = 0;
            if (rx_len) handle_line(rx_line);
            rx_len = 0;
        } else if (rx_len < MAX_LINE - 1) rx_line[rx_len++] = ch;
    }
}

int main(void) {
    board_init();
    adc_init();
    adc_gpio_init(26);
    adc_gpio_init(27);
    tusb_init();
    cfg_load();
    apply_pins();

    reset_axis_filters();

    while (true) {
        tud_task();
        poll_cdc();
        uint32_t now = to_ms_since_boot(get_absolute_time());
        uint16_t rx_now = adc_read_ch(cfg.x_adc);
        uint16_t ry_now = adc_read_ch(cfg.y_adc);

        /* V002M TITRESIM AZALT:
           Potans sabit dururken ADC küçük küçük oynar. Eski kod bu küçük oynama ile
           HID değerini de oynattığı için nişangah titriyordu. Burada küçük ADC
           farklarını yok sayıyoruz; gerçek hareket eşik değerini geçince filtreye alıyoruz. */
        uint16_t dx = (rx_now > last_raw_x) ? (rx_now - last_raw_x) : (last_raw_x - rx_now);
        uint16_t dy = (ry_now > last_raw_y) ? (ry_now - last_raw_y) : (last_raw_y - ry_now);
        bool real_move = (dx > cfg.min_move_threshold) || (dy > cfg.min_move_threshold);
        if (real_move) {
            last_raw_x = rx_now;
            last_raw_y = ry_now;
            pulse_until_ms = now + 30;
        }

        uint16_t rx = last_raw_x;
        uint16_t ry = last_raw_y;
        fx = fx + ((int32_t)rx - fx) / (1 << cfg.filter_shift);
        fy = fy + ((int32_t)ry - fy) / (1 << cfg.filter_shift);
        int16_t hx = map_axis(fx, cfg.x_min, cfg.x_max, cfg.invert_x, cfg.edge_left, cfg.edge_right);
        int16_t hy = map_axis(fy, cfg.y_min, cfg.y_max, cfg.invert_y, cfg.edge_top, cfg.edge_bottom);

        if (valid_gp(cfg.activity_out_gp)) gpio_put(cfg.activity_out_gp, now < pulse_until_ms);

        if (enabled() && tud_hid_ready() && now - last_report_ms >= 5) {
            last_report_ms = now;
            abs_mouse_report_t rpt = {0, hx, hy};
            tud_hid_report(0, &rpt, sizeof(rpt));
        }
        if (now - last_status_ms > 1000) {
            last_status_ms = now;
            send_status(rx, ry, hx, hy);
        }
        sleep_ms(2);
    }
}
