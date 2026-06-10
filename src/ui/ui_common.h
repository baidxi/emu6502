/*
 * NES Emulator - UI Common Definitions
 */

#ifndef UI_COMMON_H
#define UI_COMMON_H

#include <zephyr/kernel.h>

/* 应用状态机 */
enum app_state {
    STATE_MENU_INIT,      /* 初始化菜单 */
    STATE_FILE_BROWSER,   /* 文件浏览器 */
    STATE_LOADING,        /* 加载 ROM */
    STATE_RUNNING,        /* 游戏运行中 */
    STATE_PAUSED,         /* 游戏暂停 */
};

/* 应用设置 */
struct app_settings {
    bool show_debug_bar;      /* 显示底部调试信息栏 */
    bool show_fps;            /* 显示帧率 */
    bool show_cpu_info;       /* 显示CPU/PPU状态 */
    bool show_mapper_info;    /* 显示Mapper信息 */
};

/* 模拟线程命令 */
enum emu_cmd {
    EMU_CMD_NONE,
    EMU_CMD_LOAD_ROM,         /* 加载 ROM, data 指向路径字符串 */
    EMU_CMD_PAUSE,
    EMU_CMD_RESUME,
    EMU_CMD_STOP,
};

/* 模拟线程命令消息 */
struct emu_msg {
    enum emu_cmd cmd;
    void *data;               /* 命令数据 (如 ROM 路径) */
};

#endif /* UI_COMMON_H */
