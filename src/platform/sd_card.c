/*
 * NES Emulator - SD Card Platform Layer
 * 使用 Zephyr SDHCI API + FatFS
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/fs/fs.h>
#include <zephyr/drivers/sdhc.h>
#include <zephyr/drivers/disk.h>
#include <zephyr/sd/sd.h>
#include <zephyr/sd/sdmmc.h>
#include <zephyr/logging/log.h>
#include <ff.h>
#include <string.h>
#include <ctype.h>

#include "sd_card.h"

LOG_MODULE_REGISTER(sd_card, CONFIG_LOG_DEFAULT_LEVEL);

#define SD_MOUNT_POINT  "/SD:"
#define MAX_FILES        256
#define MAX_DIRS         64

static struct sd_card s_card;

int sd_card_init(void)
{
    struct fs_statvfs stat;
    int ret;

    /* 步骤 1: sd_init 使能 SDHCI 时钟并检测卡 */
    const struct device *sdhc_dev = DEVICE_DT_GET(DT_NODELABEL(mmc0));
    if (!device_is_ready(sdhc_dev)) {
        LOG_ERR("SDHC device not ready");
        return -ENODEV;
    }

    memset(&s_card, 0, sizeof(s_card));
    ret = sd_init(sdhc_dev, &s_card);
    if (ret < 0) {
        LOG_ERR("SD card init failed (err %d)", ret);
        return ret;
    }

    uint32_t block_count = 0;
    sdmmc_ioctl(&s_card, DISK_IOCTL_GET_SECTOR_COUNT, &block_count);
    LOG_INF("SD card detected: %u MB",
            (uint32_t)(((uint64_t)block_count * s_card.block_size) / (1024 * 1024)));

    /* 步骤 2: 等待硬件稳定后挂载 (SDHCI 在启动后需要几秒稳定) */
    k_msleep(5000);

    static FATFS fat_fs;
    static struct fs_mount_t sd_mount = {
        .type = FS_FATFS,
        .mnt_point = SD_MOUNT_POINT,
        .fs_data = &fat_fs,
    };

    ret = fs_mount(&sd_mount);
    if (ret < 0) {
        LOG_ERR("FatFS mount failed (err %d)", ret);
        return ret;
    }

    ret = fs_statvfs(SD_MOUNT_POINT, &stat);
    if (ret < 0) {
        LOG_ERR("SD stat failed (err %d)", ret);
        return ret;
    }

    LOG_INF("SD mounted: %lu KB total, %lu KB free",
            (unsigned long)(stat.f_bsize * stat.f_blocks / 1024),
            (unsigned long)(stat.f_bsize * stat.f_bfree / 1024));
    return 0;
}

int sd_card_list_nes(const char *path, char files[][256], int max_files)
{
    struct fs_dir_t dir;
    struct fs_dirent entry;
    int count = 0;
    const char *search_path = path ? path : SD_MOUNT_POINT;

    fs_dir_t_init(&dir);
    int ret = fs_opendir(&dir, search_path);
    if (ret < 0) {
        LOG_ERR("Failed to open dir %s (err %d)", search_path, ret);
        return ret;
    }

    while (count < max_files) {
        ret = fs_readdir(&dir, &entry);
        if (ret < 0) break;
        if (entry.name[0] == '\0') break;

        if (entry.type == FS_DIR_ENTRY_FILE) {
            size_t len = strlen(entry.name);
            if (len > 4 &&
                (strncmp(&entry.name[len - 4], ".nes", 4) == 0 ||
                 strncmp(&entry.name[len - 4], ".NES", 4) == 0)) {
                strncpy(files[count], entry.name, 255);
                files[count][255] = '\0';
                count++;
            }
        }
    }

    fs_closedir(&dir);
    return count;
}

int sd_card_list_dirs(const char *path, char dirs[][256], int max_dirs)
{
    struct fs_dir_t dir;
    struct fs_dirent entry;
    int count = 0;
    const char *search_path = path ? path : SD_MOUNT_POINT;

    fs_dir_t_init(&dir);
    int ret = fs_opendir(&dir, search_path);
    if (ret < 0) return ret;

    while (count < max_dirs) {
        ret = fs_readdir(&dir, &entry);
        if (ret < 0) break;
        if (entry.name[0] == '\0') break;

        if (entry.type == FS_DIR_ENTRY_DIR &&
            strcmp(entry.name, ".") != 0 &&
            strcmp(entry.name, "..") != 0) {
            strncpy(dirs[count], entry.name, 255);
            dirs[count][255] = '\0';
            count++;
        }
    }

    fs_closedir(&dir);
    return count;
}

int sd_card_load_rom(const char *path, uint8_t **data, size_t *size)
{
    struct fs_file_t file;
    struct fs_dirent stat_entry;
    ssize_t read_bytes;
    off_t file_size;

    int ret = fs_stat(path, &stat_entry);
    if (ret < 0) {
        LOG_ERR("Failed to stat ROM %s (err %d)", path, ret);
        return ret;
    }
    if (stat_entry.type != FS_DIR_ENTRY_FILE) {
        LOG_ERR("Not a regular file: %s", path);
        return -EINVAL;
    }
    file_size = (off_t)stat_entry.size;

    fs_file_t_init(&file);
    ret = fs_open(&file, path, FS_O_READ);
    if (ret < 0) {
        LOG_ERR("Failed to open ROM %s (err %d)", path, ret);
        return ret;
    }

    *data = k_malloc(file_size);
    if (!*data) {
        LOG_ERR("Failed to allocate %ld bytes for ROM", (long)file_size);
        fs_close(&file);
        return -ENOMEM;
    }

    read_bytes = fs_read(&file, *data, file_size);
    fs_close(&file);

    if (read_bytes != file_size) {
        LOG_ERR("Short read: got %zd of %ld bytes", read_bytes, (long)file_size);
        k_free(*data); *data = NULL;
        return -EIO;
    }

    *size = file_size;
    LOG_INF("Loaded ROM %s: %zu bytes", path, *size);
    return 0;
}

int sd_card_save_file(const char *path, const uint8_t *data, size_t size)
{
    struct fs_file_t file;
    fs_file_t_init(&file);
    int ret = fs_open(&file, path, FS_O_CREATE | FS_O_WRITE);
    if (ret < 0) return ret;
    ssize_t written = fs_write(&file, data, size);
    fs_close(&file);
    return (written != (ssize_t)size) ? -EIO : 0;
}

bool sd_card_file_exists(const char *path)
{
    struct fs_dirent entry;
    return fs_stat(path, &entry) == 0;
}
