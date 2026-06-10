/*
 * NES Emulator - LVGL File Browser UI
 */

#ifndef UI_FILEBROWSER_H
#define UI_FILEBROWSER_H

#include <lvgl.h>

/**
 * @brief 创建文件浏览器界面
 * @param parent 父对象 (通常为 lv_scr_act())
 * @return 文件浏览器容器对象
 */
lv_obj_t *ui_filebrowser_create(lv_obj_t *parent);

/**
 * @brief 设置 ROM 选择回调
 * @param rom_path 选中的 ROM 完整路径 (静态字符串, 需立即复制)
 */
typedef void (*ui_filebrowser_cb_t)(const char *rom_path);
void ui_filebrowser_set_callback(ui_filebrowser_cb_t cb);

/**
 * @brief 刷新文件列表
 */
void ui_filebrowser_refresh(void);

/**
 * @brief 销毁文件浏览器界面
 */
void ui_filebrowser_destroy(void);

/**
 * @brief 获取当前浏览路径
 */
const char *ui_filebrowser_get_path(void);

#endif /* UI_FILEBROWSER_H */
