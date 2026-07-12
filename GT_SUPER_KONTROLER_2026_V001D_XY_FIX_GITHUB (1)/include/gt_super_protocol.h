#ifndef GT_SUPER_PROTOCOL_H
#define GT_SUPER_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

#define GTS_MAGIC 0x47545343u /* GTSC */
#define GTS_CFG_VERSION 2u
#define GTS_FLASH_SLOT_SIZE 4096u
#define GTS_GP_DISABLED 255u

#define HID_KEY_NONE 0x00
#define HID_KEY_1 0x1e
#define HID_KEY_2 0x1f
#define HID_KEY_3 0x20
#define HID_KEY_4 0x21
#define HID_KEY_5 0x22
#define HID_KEY_6 0x23
#define HID_KEY_7 0x24
#define HID_KEY_8 0x25
#define HID_KEY_9 0x26
#define HID_KEY_0 0x27
#define HID_KEY_ENTER 0x28
#define HID_KEY_SPACE 0x2c
#define HID_KEY_A 0x04
#define HID_KEY_B 0x05
#define HID_KEY_C 0x06
#define HID_KEY_D 0x07
#define HID_KEY_E 0x08
#define HID_KEY_F 0x09
#define HID_KEY_G 0x0a
#define HID_KEY_H 0x0b
#define HID_KEY_I 0x0c
#define HID_KEY_J 0x0d
#define HID_KEY_K 0x0e
#define HID_KEY_L 0x0f
#define HID_KEY_M 0x10
#define HID_KEY_N 0x11
#define HID_KEY_O 0x12
#define HID_KEY_P 0x13
#define HID_KEY_Q 0x14
#define HID_KEY_R 0x15
#define HID_KEY_S 0x16
#define HID_KEY_T 0x17
#define HID_KEY_U 0x18
#define HID_KEY_V 0x19
#define HID_KEY_W 0x1a
#define HID_KEY_X 0x1b
#define HID_KEY_Y 0x1c
#define HID_KEY_Z 0x1d

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t seq;
    uint32_t crc32;

    uint8_t common_coin_gp;       /* V002: Ortak kredi / coin GP, default GP2 */
    uint8_t coin_active_high;

    uint8_t p1_start_gp;
    uint8_t p2_start_gp;
    uint8_t p1_trigger_gp;
    uint8_t p2_trigger_gp;
    uint8_t p1_reload_gp;
    uint8_t p2_reload_gp;

    uint8_t p1_relay_gp;
    uint8_t p2_relay_gp;
    uint8_t p1_vibration_gp;       /* V002: kullanılmaz, motor röleye bağlı */
    uint8_t p2_vibration_gp;       /* V002: kullanılmaz, motor röleye bağlı */
    uint8_t relay_active_high;
    uint8_t vibration_active_high; /* V002: geriye uyumluluk, kullanılmaz */
    uint8_t relay_mode;           /* V002 default 0: aktif oyuncu + tetik GP basılı = röle çeker */

    uint8_t p1_activity_in_gp;    /* P1 mouse Pico hareket pulse girişi */
    uint8_t p2_activity_in_gp;    /* P2 mouse Pico hareket pulse girişi */
    uint8_t calibration_gp;       /* GP19 servis/kalibrasyon */
    uint8_t system_enable_gp;     /* 255=kapalı/daima aktif. P1/P2 DIP ile karışmaz. */
    uint8_t menu_up_gp;
    uint8_t menu_down_gp;
    uint8_t menu_left_gp;
    uint8_t menu_right_gp;
    uint8_t menu_select_gp;
    uint8_t buzzer_gp;

    uint8_t idle_off_min;
    uint8_t vibration_mode;       /* 0=trigger follow, 1=pulse */
    uint8_t vib_pulse_on_ms;
    uint8_t vib_pulse_off_ms;

    uint8_t key_coin;
    uint8_t key_p1_start;
    uint8_t key_p2_start;
    uint8_t key_p1_trigger;
    uint8_t key_p1_reload;
    uint8_t key_p2_trigger;
    uint8_t key_p2_reload;

    uint8_t reserved[195];
} gts_controller_config_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t seq;
    uint32_t crc32;

    uint8_t player_num;
    uint8_t x_adc;                /* ADC channel, default 0 = GP26 */
    uint8_t y_adc;                /* ADC channel, default 1 = GP27 */
    uint8_t enable_gp;            /* P1/P2 DIP switch potans aktif/pasif: default GP19 */
    uint8_t calibration_gp;       /* 255=kapalı; controller GP19 servis için ayrı Pico üzerindedir */
    uint8_t activity_out_gp;      /* hareket pulse çıkışı */
    uint8_t invert_x;
    uint8_t invert_y;
    uint8_t filter_shift;
    uint8_t min_move_threshold;

    uint16_t x_min;
    uint16_t x_max;
    uint16_t y_min;
    uint16_t y_max;
    int16_t edge_left;
    int16_t edge_right;
    int16_t edge_top;
    int16_t edge_bottom;

    uint8_t reserved[220];
} gts_mouse_config_t;

#endif
