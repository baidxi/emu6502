/*
 * NES Emulator - LVGL File Browser UI
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "ui_filebrowser.h"
#include "../platform/sd_card.h"

LOG_MODULE_REGISTER(ui_fb, CONFIG_LOG_DEFAULT_LEVEL);

/* 当前浏览路径 */
static char current_path[256] = "/SD:";
static lv_obj_t *list = NULL;
static lv_obj_t *container = NULL;
static lv_obj_t *path_label = NULL;
static lv_obj_t *status_label = NULL;
static ui_filebrowser_cb_t rom_select_cb = NULL;

/* 保存按钮对象和对应的文件路径 */
#define MAX_ENTRIES 512
static lv_obj_t *entry_btns[MAX_ENTRIES];
static char entry_paths[MAX_ENTRIES][320];
static int entry_count = 0;

static void build_path(char *out, size_t size, const char *dir, const char *name)
{
    if (strcmp(dir, "/SD:") == 0) {
        snprintk(out, size, "/SD:/%s", name);
    } else {
        snprintk(out, size, "%s/%s", dir, name);
    }
}

static void on_item_clicked(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target_obj(e);
    if (!btn) return;

    /* 查找按钮对应的路径 */
    const char *full_path = NULL;
    for (int i = 0; i < entry_count; i++) {
        if (entry_btns[i] == btn) {
            full_path = entry_paths[i];
            break;
        }
    }
    if (!full_path) return;

    LOG_INF("Clicked: %s", full_path);

    /* 判断是否是目录 */
    if (full_path[0] == '/') {
        /* 以 / 开头的是目录或 ROM 文件的完整路径 */
        /* 检查后缀 */
        size_t len = strlen(full_path);
        if (len > 4 && (strncmp(&full_path[len - 4], ".nes", 4) == 0 ||
                        strncmp(&full_path[len - 4], ".NES", 4) == 0)) {
            /* ROM 文件 */
            LOG_INF("ROM selected: %s", full_path);
            if (rom_select_cb) {
                rom_select_cb(full_path);
            }
        } else {
            /* 目录 */
            LOG_INF("Navigate to: %s", full_path);
            strncpy(current_path, full_path, sizeof(current_path) - 1);
            current_path[sizeof(current_path) - 1] = '\0';
            ui_filebrowser_refresh();
        }
    }
}

lv_obj_t *ui_filebrowser_create(lv_obj_t *parent)
{
    container = lv_obj_create(parent);
    lv_obj_set_size(container, 800, 600);
    lv_obj_set_style_bg_color(container, lv_color_black(), 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_border_width(container, 0, 0);

    /* 标题 */
    path_label = lv_label_create(container);
    lv_label_set_text(path_label, "NES Emulator - 选择游戏");
    lv_obj_set_style_text_color(path_label, lv_color_white(), 0);
    lv_obj_align(path_label, LV_ALIGN_TOP_MID, 0, 10);

    /* 文件列表 */
    list = lv_list_create(container);
    lv_obj_set_size(list, 780, 500);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x1A1A2E), 0);
    lv_obj_set_style_text_color(list, lv_color_white(), 0);

    /* 触摸优化: 降低滚动惯性, 让短按更容易被识别为点击 */
    lv_obj_set_scroll_snap_y(list, LV_SCROLL_SNAP_NONE);

    /* 滚动条样式 */
    static lv_style_t style_scrollbar;
    lv_style_init(&style_scrollbar);
    lv_style_set_width(&style_scrollbar, 6);
    lv_style_set_pad_right(&style_scrollbar, 2);
    lv_style_set_bg_color(&style_scrollbar, lv_palette_main(LV_PALETTE_GREY));
    lv_style_set_bg_opa(&style_scrollbar, LV_OPA_50);
    lv_style_set_radius(&style_scrollbar, 3);
    lv_obj_add_style(list, &style_scrollbar, LV_PART_SCROLLBAR);

    /* 状态标签 */
    status_label = lv_label_create(container);
    lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x666666), 0);

    /* 刷新列表 */
    ui_filebrowser_refresh();

    return container;
}

void ui_filebrowser_refresh(void)
{
    if (!list) return;

    /* 清空列表 */
    lv_obj_clean(list);
    memset(entry_btns, 0, sizeof(entry_btns));
    memset(entry_paths, 0, sizeof(entry_paths));
    entry_count = 0;

    /* 使用 static 避免在栈上分配 128KB (MAX_ENTRIES*256) 导致栈溢出 */
    static char entries[MAX_ENTRIES][256];
    int dir_count = 0;
    int rom_count = 0;

    /* 添加返回上级 */
    if (strcmp(current_path, "/SD:") != 0) {
        lv_obj_t *btn = lv_list_add_btn(list, NULL, "..  [返回上级目录]");
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        /* 获取上级目录路径 */
        char parent_path[256];
        strncpy(parent_path, current_path, sizeof(parent_path) - 1);
        parent_path[sizeof(parent_path) - 1] = '\0';
        char *slash = strrchr(parent_path, '/');
        if (slash && slash != parent_path + 3) {
            *slash = '\0';
        } else if (slash) {
            strcpy(parent_path, "/SD:");
        }
        entry_btns[entry_count] = btn;
        strncpy(entry_paths[entry_count], parent_path, sizeof(entry_paths[0]) - 1);
        lv_obj_add_event_cb(btn, on_item_clicked, LV_EVENT_CLICKED, NULL);
        entry_count++;
    }

    /* 添加子目录 */
    dir_count = sd_card_list_dirs(current_path, entries, MAX_ENTRIES);
    for (int i = 0; i < dir_count && entry_count < MAX_ENTRIES; i++) {
        char label[280];
        snprintk(label, sizeof(label), "[DIR]  %s", entries[i]);
        lv_obj_t *btn = lv_list_add_btn(list, NULL, label);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);

        char full_path[320];
        build_path(full_path, sizeof(full_path), current_path, entries[i]);

        entry_btns[entry_count] = btn;
        strncpy(entry_paths[entry_count], full_path, sizeof(entry_paths[0]) - 1);
        lv_obj_add_event_cb(btn, on_item_clicked, LV_EVENT_CLICKED, NULL);
        entry_count++;
    }

    /* 添加 .nes 文件 */
    rom_count = sd_card_list_nes(current_path, entries, MAX_ENTRIES);
    for (int i = 0; i < rom_count && entry_count < MAX_ENTRIES; i++) {
        char label[280];
        snprintk(label, sizeof(label), "[ROM] %s", entries[i]);
        lv_obj_t *btn = lv_list_add_btn(list, NULL, label);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);

        char full_path[320];
        build_path(full_path, sizeof(full_path), current_path, entries[i]);

        entry_btns[entry_count] = btn;
        strncpy(entry_paths[entry_count], full_path, sizeof(entry_paths[0]) - 1);
        lv_obj_add_event_cb(btn, on_item_clicked, LV_EVENT_CLICKED, NULL);
        entry_count++;
    }

    /* 更新路径显示 */
    lv_label_set_text(path_label, current_path);

    if (status_label) {
        char buf[64];
        snprintk(buf, sizeof(buf), "%d 个目录, %d 个游戏", dir_count, rom_count);
        lv_label_set_text(status_label, buf);
    }
}

void ui_filebrowser_set_callback(ui_filebrowser_cb_t cb)
{
    rom_select_cb = cb;
}

void ui_filebrowser_destroy(void)
{
    if (container) {
        lv_obj_del(container);
        container = NULL;
        list = NULL;
        path_label = NULL;
        status_label = NULL;
    }
    entry_count = 0;
}

const char *ui_filebrowser_get_path(void)
{
    return current_path;
}
