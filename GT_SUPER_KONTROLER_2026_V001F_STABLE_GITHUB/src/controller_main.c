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
#define MAX_LINE 192
#define GORKEM_CONTROLLER_MARKER 0xD6u
#define CONTROLLER_FW_TAG "V001F_STABLE_KEYS_TRIGGER"
#define STATUS_INTERVAL_MS 100u
#define NORMAL_DEBOUNCE_MS 18u
#define TRIGGER_DEBOUNCE_MS 8u

static const uint8_t KEYPAD_GPS[10] = {2,3,4,5,6,7,8,9,10,11};
static const uint8_t KEYPAD_HID[10] = {
    HID_KEY_1,HID_KEY_2,HID_KEY_3,HID_KEY_4,HID_KEY_5,
    HID_KEY_6,HID_KEY_7,HID_KEY_8,HID_KEY_9,HID_KEY_0
};
static const uint8_t KEYPAD_NUM[10] = {1,2,3,4,5,6,7,8,9,0};

typedef struct {
    bool initialized;
    bool raw;
    bool stable;
    uint32_t changed_ms;
} debounce_t;

static gts_controller_config_t cfg;
static uint32_t credits = 0;
static bool p1_active = false, p2_active = false;
static uint32_t p1_last_ms = 0, p2_last_ms = 0, last_status_ms = 0;
static char rx_line[MAX_LINE];
static uint32_t rx_len = 0;
static debounce_t key_db[10];
static bool key_state[10] = {false};
static bool last_key_state[10] = {false};
static uint8_t last_keyboard_report_keys[6] = {0};
static bool p1_relay_state = false, p2_relay_state = false;

static void cfg_save(void);
static void lock_fixed_mapping(void);

static uint32_t crc32_calc(const uint8_t *data, size_t len) {
    uint32_t crc=0xFFFFFFFFu;
    for (size_t i=0;i<len;i++) {
        crc^=data[i];
        for (int j=0;j<8;j++) crc=(crc>>1)^(0xEDB88320u&(-(int32_t)(crc&1u)));
    }
    return ~crc;
}

static bool debounce_update(debounce_t *d, bool raw, uint32_t now_ms, uint32_t wait_ms) {
    if (!d->initialized) {
        d->initialized=true; d->raw=raw; d->stable=raw; d->changed_ms=now_ms; return d->stable;
    }
    if (raw!=d->raw) { d->raw=raw; d->changed_ms=now_ms; }
    if ((uint32_t)(now_ms-d->changed_ms)>=wait_ms) d->stable=d->raw;
    return d->stable;
}

static void lock_fixed_mapping(void) {
    cfg.common_coin_gp=2;
    cfg.p1_start_gp=3; cfg.p1_trigger_gp=4; cfg.p1_reload_gp=5;
    cfg.p2_start_gp=6; cfg.p2_trigger_gp=7; cfg.p2_reload_gp=8;
    cfg.p1_relay_gp=27; cfg.p2_relay_gp=26;
    cfg.p1_vibration_gp=GTS_GP_DISABLED; cfg.p2_vibration_gp=GTS_GP_DISABLED;
    cfg.relay_active_high=cfg.coin_active_high;
    cfg.relay_mode=0;
    cfg.p1_activity_in_gp=GTS_GP_DISABLED; cfg.p2_activity_in_gp=GTS_GP_DISABLED;
    cfg.calibration_gp=GTS_GP_DISABLED; cfg.system_enable_gp=GTS_GP_DISABLED;
    cfg.key_coin=HID_KEY_1;
    cfg.key_p1_start=HID_KEY_2; cfg.key_p1_trigger=HID_KEY_3; cfg.key_p1_reload=HID_KEY_4;
    cfg.key_p2_start=HID_KEY_5; cfg.key_p2_trigger=HID_KEY_6; cfg.key_p2_reload=HID_KEY_7;
    cfg.reserved[0]=GORKEM_CONTROLLER_MARKER;
}

static void cfg_defaults(void) {
    memset(&cfg,0,sizeof(cfg));
    cfg.magic=GTS_MAGIC; cfg.version=GTS_CFG_VERSION; cfg.size=sizeof(cfg); cfg.seq=1;
    cfg.coin_active_high=0; cfg.vibration_active_high=1; cfg.idle_off_min=5;
    cfg.menu_up_gp=cfg.menu_down_gp=cfg.menu_left_gp=cfg.menu_right_gp=cfg.menu_select_gp=GTS_GP_DISABLED;
    cfg.buzzer_gp=GTS_GP_DISABLED;
    lock_fixed_mapping();
    cfg.crc32=0; cfg.crc32=crc32_calc((uint8_t*)&cfg,sizeof(cfg));
}

static bool cfg_valid(const gts_controller_config_t *c) {
    if (c->magic!=GTS_MAGIC || c->version!=GTS_CFG_VERSION || c->size!=sizeof(*c)) return false;
    if (c->reserved[0]!=GORKEM_CONTROLLER_MARKER) return false;
    gts_controller_config_t tmp=*c; uint32_t old=tmp.crc32; tmp.crc32=0;
    return old==crc32_calc((uint8_t*)&tmp,sizeof(tmp));
}

static const gts_controller_config_t *slot_ptr(uint32_t offset) {
    return (const gts_controller_config_t*)(XIP_BASE+offset);
}

static void cfg_load(void) {
    const gts_controller_config_t *a=slot_ptr(SLOT0_OFFSET), *b=slot_ptr(SLOT1_OFFSET);
    bool av=cfg_valid(a), bv=cfg_valid(b), had=true;
    if (av&&bv) cfg=(a->seq>=b->seq)?*a:*b;
    else if (av) cfg=*a;
    else if (bv) cfg=*b;
    else { cfg_defaults(); had=false; }
    lock_fixed_mapping();
    if (!had) cfg_save();
}

static void cfg_save(void) {
    lock_fixed_mapping();
    cfg.magic=GTS_MAGIC; cfg.version=GTS_CFG_VERSION; cfg.size=sizeof(cfg); cfg.seq++;
    cfg.crc32=0; cfg.crc32=crc32_calc((uint8_t*)&cfg,sizeof(cfg));
    uint8_t page[FLASH_PAGE_SIZE]; memset(page,0xFF,sizeof(page)); memcpy(page,&cfg,sizeof(cfg));
    uint32_t target=(cfg.seq&1u)?SLOT0_OFFSET:SLOT1_OFFSET;
    uint32_t ints=save_and_disable_interrupts();
    flash_range_erase(target,FLASH_SECTOR_SIZE); flash_range_program(target,page,FLASH_PAGE_SIZE);
    restore_interrupts(ints);
}

static bool valid_gp(uint8_t gp) { return gp<=28; }
static void pin_input_pullup(uint8_t gp) { if(valid_gp(gp)){gpio_init(gp);gpio_set_dir(gp,GPIO_IN);gpio_pull_up(gp);} }
static void pin_output(uint8_t gp,bool ah){if(valid_gp(gp)){gpio_init(gp);gpio_set_dir(gp,GPIO_OUT);gpio_put(gp,ah?0:1);}}
static bool read_active(uint8_t gp,bool ah){if(!valid_gp(gp))return false;bool v=gpio_get(gp);return ah?v:!v;}
static void put_active(uint8_t gp,bool ah,bool on){if(valid_gp(gp))gpio_put(gp,ah?on:!on);}

static void apply_pins(void) {
    for (size_t i=0;i<10;i++) pin_input_pullup(KEYPAD_GPS[i]);
    pin_output(cfg.p1_relay_gp,cfg.relay_active_high); pin_output(cfg.p2_relay_gp,cfg.relay_active_high);
}

static void cdc_send(const char *s){if(tud_cdc_connected()){tud_cdc_write_str(s);tud_cdc_write_flush();}}
static void buzzer_beep(uint16_t ms){if(!valid_gp(cfg.buzzer_gp))return;put_active(cfg.buzzer_gp,true,true);sleep_ms(ms);put_active(cfg.buzzer_gp,true,false);}

static void send_keyboard_state(void) {
    if(!tud_hid_ready())return;
    uint8_t keys[6]={0};uint8_t n=0;
    for(size_t i=0;i<10&&n<6;i++)if(key_state[i])keys[n++]=KEYPAD_HID[i];
    if(memcmp(keys,last_keyboard_report_keys,sizeof(keys))!=0){tud_hid_keyboard_report(0,0,keys);memcpy(last_keyboard_report_keys,keys,sizeof(keys));}
}

static void emit_key_event(size_t i,bool pressed) {
    char b[96];
    snprintf(b,sizeof(b),"EVENT,KEY,GP%u,%u,%u\n",KEYPAD_GPS[i],KEYPAD_NUM[i],pressed?1u:0u);cdc_send(b);
    if(i==2){snprintf(b,sizeof(b),"EVENT,TRIGGER,P1,%u\n",pressed?1u:0u);cdc_send(b);}
    else if(i==5){snprintf(b,sizeof(b),"EVENT,TRIGGER,P2,%u\n",pressed?1u:0u);cdc_send(b);}
}

static void set_relays(bool p1,bool p2){p1_relay_state=p1;p2_relay_state=p2;put_active(cfg.p1_relay_gp,cfg.relay_active_high,p1);put_active(cfg.p2_relay_gp,cfg.relay_active_high,p2);}
static void deactivate_p1(void){p1_active=false;p1_relay_state=false;put_active(cfg.p1_relay_gp,cfg.relay_active_high,false);}
static void deactivate_p2(void){p2_active=false;p2_relay_state=false;put_active(cfg.p2_relay_gp,cfg.relay_active_high,false);}

static void process_logic(void) {
    uint32_t now=to_ms_since_boot(get_absolute_time());
    for(size_t i=0;i<10;i++){
        bool raw=(i==0)?read_active(KEYPAD_GPS[i],cfg.coin_active_high!=0):read_active(KEYPAD_GPS[i],false);
        uint32_t ms=(i==2||i==5)?TRIGGER_DEBOUNCE_MS:NORMAL_DEBOUNCE_MS;
        key_state[i]=debounce_update(&key_db[i],raw,now,ms);
        if(key_state[i]!=last_key_state[i]){emit_key_event(i,key_state[i]);last_key_state[i]=key_state[i];}
    }
    bool coin=key_state[0],p1s=key_state[1],p1t=key_state[2],p1r=key_state[3];
    bool p2s=key_state[4],p2t=key_state[5],p2r=key_state[6];
    static bool oc=false,os1=false,os2=false;
    if(coin&&!oc){credits++;buzzer_beep(25);}
    if(p1s&&!os1&&!p1_active&&credits>0){credits--;p1_active=true;p1_last_ms=now;buzzer_beep(40);}
    if(p2s&&!os2&&!p2_active&&credits>0){credits--;p2_active=true;p2_last_ms=now;buzzer_beep(40);}
    oc=coin;os1=p1s;os2=p2s;
    if(p1_active&&(p1t||p1r))p1_last_ms=now;
    if(p2_active&&(p2t||p2r))p2_last_ms=now;
    uint32_t idle=(uint32_t)cfg.idle_off_min*60000u;
    if(p1_active&&idle&&(uint32_t)(now-p1_last_ms)>idle)deactivate_p1();
    if(p2_active&&idle&&(uint32_t)(now-p2_last_ms)>idle)deactivate_p2();
    set_relays(p1_active&&p1t,p2_active&&p2t);
    send_keyboard_state();
}

static void send_cfg(void) {
    char b[600];
    snprintf(b,sizeof(b),"CFG,CONTROLLER,%s,SEQ,%lu,CREDIT_GP,%u,COIN_AH,%u,P1_START,%u,P2_START,%u,P1_TRIG,%u,P2_TRIG,%u,P1_RELOAD,%u,P2_RELOAD,%u,P1_RELAY,%u,P2_RELAY,%u,IDLE_MIN,%u,REL_AH,%u,KEY_COIN,%u,KEY_P1_START,%u,KEY_P2_START,%u,KEY_P1_TRIG,%u,KEY_P1_RELOAD,%u,KEY_P2_TRIG,%u,KEY_P2_RELOAD,%u\n",
      CONTROLLER_FW_TAG,(unsigned long)cfg.seq,cfg.common_coin_gp,cfg.coin_active_high,cfg.p1_start_gp,cfg.p2_start_gp,cfg.p1_trigger_gp,cfg.p2_trigger_gp,cfg.p1_reload_gp,cfg.p2_reload_gp,cfg.p1_relay_gp,cfg.p2_relay_gp,cfg.idle_off_min,cfg.relay_active_high,cfg.key_coin,cfg.key_p1_start,cfg.key_p2_start,cfg.key_p1_trigger,cfg.key_p1_reload,cfg.key_p2_trigger,cfg.key_p2_reload);
    cdc_send(b);
}

static void send_status(void) {
    char b[340];
    snprintf(b,sizeof(b),"STATUS,CONTROLLER,%s,CREDITS,%lu,P1,%u,P2,%u,R1,%u,R2,%u,T1,%u,T2,%u,K1,%u,K2,%u,K3,%u,K4,%u,K5,%u,K6,%u,K7,%u,K8,%u,K9,%u,K0,%u,EN,1\n",
      CONTROLLER_FW_TAG,(unsigned long)credits,p1_active?1u:0u,p2_active?1u:0u,p1_relay_state?1u:0u,p2_relay_state?1u:0u,key_state[2]?1u:0u,key_state[5]?1u:0u,key_state[0]?1u:0u,key_state[1]?1u:0u,key_state[2]?1u:0u,key_state[3]?1u:0u,key_state[4]?1u:0u,key_state[5]?1u:0u,key_state[6]?1u:0u,key_state[7]?1u:0u,key_state[8]?1u:0u,key_state[9]?1u:0u);
    cdc_send(b);
}

static void handle_set(char *key,char *val){int v=atoi(val);if(!strcmp(key,"COIN_AH")){cfg.coin_active_high=v?1:0;cfg.relay_active_high=cfg.coin_active_high;}else if(!strcmp(key,"IDLE_MIN"))cfg.idle_off_min=(uint8_t)((v<1)?1:(v>30?30:v));lock_fixed_mapping();apply_pins();}

static void handle_line(char *line) {
    if(!strcmp(line,"HELLO")||!strcmp(line,"GETCFG")){char h[96];snprintf(h,sizeof(h),"HELLO,CONTROLLER,%s\n",CONTROLLER_FW_TAG);cdc_send(h);send_cfg();return;}
    if(!strcmp(line,"STATUS")){send_status();return;}
    if(!strcmp(line,"SAVE")){cfg_save();cdc_send("OK,SAVED\n");return;}
    if(!strcmp(line,"FACTORY")){cfg_defaults();cfg_save();apply_pins();cdc_send("OK,FACTORY\n");return;}
    if(!strcmp(line,"P1OFF")){deactivate_p1();cdc_send("OK,P1OFF\n");return;}
    if(!strcmp(line,"P2OFF")){deactivate_p2();cdc_send("OK,P2OFF\n");return;}
    if(!strncmp(line,"SET,",4)){char*k=line+4;char*v=strchr(k,',');if(v){*v++=0;handle_set(k,v);cdc_send("OK,SET,LOCKED\n");}else cdc_send("ERR,SET\n");return;}
    if(!strcmp(line,"TEST,P1_RELAY")){put_active(cfg.p1_relay_gp,cfg.relay_active_high,true);sleep_ms(250);put_active(cfg.p1_relay_gp,cfg.relay_active_high,false);cdc_send("OK,TEST,P1_RELAY\n");return;}
    if(!strcmp(line,"TEST,P2_RELAY")){put_active(cfg.p2_relay_gp,cfg.relay_active_high,true);sleep_ms(250);put_active(cfg.p2_relay_gp,cfg.relay_active_high,false);cdc_send("OK,TEST,P2_RELAY\n");return;}
    cdc_send("ERR,UNKNOWN\n");
}

static void poll_cdc(void){while(tud_cdc_available()){char ch;tud_cdc_read(&ch,1);if(ch=='\r')continue;if(ch=='\n'){rx_line[rx_len]=0;if(rx_len)handle_line(rx_line);rx_len=0;}else if(rx_len<MAX_LINE-1)rx_line[rx_len++]=ch;}}

int main(void){board_init();tusb_init();cfg_load();apply_pins();uint32_t now=to_ms_since_boot(get_absolute_time());p1_last_ms=p2_last_ms=now;while(true){tud_task();poll_cdc();process_logic();now=to_ms_since_boot(get_absolute_time());if((uint32_t)(now-last_status_ms)>=STATUS_INTERVAL_MS){last_status_ms=now;send_status();}sleep_ms(2);}}
