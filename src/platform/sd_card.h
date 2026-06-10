/*
 * NES Emulator - SD Card Platform Layer
 */

#ifndef SD_CARD_H
#define SD_CARD_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief 初始化并挂载 SD 卡文件系统
 * @return 0 成功, 负值错误码
 */
int sd_card_init(void);

/**
 * @brief 列出指定目录中的 .nes 文件
 * @param path      目录路径 (如 "/SD:/nes_roms/")
 * @param files     输出文件名数组 (不含路径前缀)
 * @param max_files 最大文件数
 * @return 实际找到的 .nes 文件数量, 负值错误码
 */
int sd_card_list_nes(const char *path, char files[][256], int max_files);

/**
 * @brief 读取 .nes 文件到动态分配的内存缓冲区
 * @param path 文件完整路径
 * @param data 输出数据指针 (由调用者 k_free 释放)
 * @param size 输出数据大小
 * @return 0 成功, 负值错误码
 */
int sd_card_load_rom(const char *path, uint8_t **data, size_t *size);

/**
 * @brief 列出目录中的子目录
 * @param path      目录路径
 * @param dirs      输出目录名数组
 * @param max_dirs  最大目录数
 * @return 实际找到的子目录数量, 负值错误码
 */
int sd_card_list_dirs(const char *path, char dirs[][256], int max_dirs);

/**
 * @brief 保存数据到文件
 * @param path 文件路径
 * @param data 数据指针
 * @param size 数据大小
 * @return 0 成功, 负值错误码
 */
int sd_card_save_file(const char *path, const uint8_t *data, size_t size);

/**
 * @brief 检查文件是否存在
 * @param path 文件路径
 * @return true 存在, false 不存在
 */
bool sd_card_file_exists(const char *path);

#endif /* SD_CARD_H */
