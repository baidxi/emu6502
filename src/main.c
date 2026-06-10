/*
 * NES Emulator - Minimal: force NMI only
 */
#include <lvgl.h>
#include <lvgl_zephyr.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include "nes/nes_system.h"
#include "platform/sd_card.h"
#include "ui/ui_game.h"
LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);
static nes_system_t nes_sys;
static struct k_thread emu_td;
static K_THREAD_STACK_DEFINE(emu_stk, 32768);
static void emu_thread(void *a, void *b, void *c){
    while(1){if(!nes_sys.running){k_msleep(16);continue;}nes_system_run_frame(&nes_sys);}}
static void lvgl_cb(lv_timer_t *t){
    ui_game_blit_frame(nes_system_get_framebuffer(&nes_sys));
    static uint32_t c=0;
    if(++c%120==0){
        uint8_t *v=nes_sys.ppu.vram;
        LOG_INF("f=%u PC=%04X VRAM:0=%02X 400=%02X 800=%02X C00=%02X",
                nes_sys.frame_count,nes_sys.cpu.pc,
                v[0],v[0x400],v[0x800],v[0xC00]);}}
static int load(void){
    char f[1][256];if(sd_card_list_nes("/SD:",f,1)<=0)return-1;
    char p[320];snprintk(p,sizeof(p),"/SD:/%s",f[0]);
    uint8_t *d;size_t s;if(sd_card_load_rom(p,&d,&s)<0)return-1;
    int r=nes_system_load_rom(&nes_sys,d,s,p);k_free(d);return r;}
int main(void){
    LOG_ERR("=== NES ===");sd_card_init();nes_system_init(&nes_sys);
    lvgl_lock();ui_game_create(lv_scr_act());lv_timer_create(lvgl_cb,16,NULL);lvgl_unlock();
    if(load()==0){nes_sys.running=true;}
    k_thread_create(&emu_td,emu_stk,32768,emu_thread,NULL,NULL,NULL,50,0,K_NO_WAIT);
    while(1){lvgl_lock();lv_timer_handler();lvgl_unlock();k_msleep(5);}return 0;}
