/*
 * Copyright (c) 2006-2025, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * 2025-09-11     wenshan       修改自artpi的sdcard例程
 */

#include <rtthread.h>

#ifdef BSP_USING_FS

#if DFS_FILESYSTEMS_MAX < 4
#error "Please define DFS_FILESYSTEMS_MAX more than 4"
#endif
#if DFS_FILESYSTEM_TYPES_MAX < 4
#error "Please define DFS_FILESYSTEM_TYPES_MAX more than 4"
#endif

#ifdef BSP_USING_SPI_FLASH_FS
#include "fal.h"
#endif

#include <dfs_fs.h>
#include "dfs_romfs.h"
#include "drv_sdio.h"

#include <rtthread.h>
#include <rtdevice.h>
#include "rtt_board.h"

#define DBG_TAG "app.filesystem"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define BSP_USING_SDCARD_FS

#ifdef BSP_USING_SDCARD_FS

static const struct romfs_dirent _romfs_root[] = {
//    {ROMFS_DIRENT_DIR, "flash", RT_NULL, 0},
    {ROMFS_DIRENT_DIR, "sdcard", RT_NULL, 0}
};

const struct romfs_dirent romfs_root = {
    ROMFS_DIRENT_DIR, "/", (rt_uint8_t *)_romfs_root, sizeof(_romfs_root) / sizeof(_romfs_root[0])};

/* SD Card hot plug detection pin */
#define SD_CHECK_PIN (rt_pin_get(BSP_SDXC1_CDN_PIN))

static void _sdcard_mount(void)
{
    rt_device_t device;

    rt_thread_mdelay(200);
    device = rt_device_find(BOARD_SD_NAME);
    if (device == NULL)
    {
        mmcsd_wait_cd_changed(0);
        hpm_mmcsd_change();
        mmcsd_wait_cd_changed(RT_WAITING_FOREVER);
        device = rt_device_find(BOARD_SD_NAME);
    }
    if (device != RT_NULL)
    {
        if (dfs_mount(BOARD_SD_NAME, "/sdcard", "elm", 0, NULL) == RT_EOK)
        {
            LOG_I("sd card mount to '/sdcard'");
        }
        else
        {
            LOG_W("sd card mount to '/sdcard' failed!");
        }
    }
}

static void _sdcard_unmount(void)
{
    rt_thread_mdelay(200);
    dfs_unmount("/sdcard");
    LOG_I("Unmount \"/sdcard\"");

    mmcsd_wait_cd_changed(0);
    hpm_mmcsd_change();
    mmcsd_wait_cd_changed(RT_WAITING_FOREVER);
}

static void sd_mount(void *parameter)
{
    rt_uint8_t re_sd_check_pin = 1;
    rt_thread_mdelay(200);
    if ((re_sd_check_pin = rt_pin_read(SD_CHECK_PIN)) == 0)
    {
        _sdcard_mount();
    }
    while (1)
    {
        rt_thread_mdelay(200);
        if (!re_sd_check_pin && (re_sd_check_pin = rt_pin_read(SD_CHECK_PIN)) != 0)
        {

            _sdcard_unmount();
        }

        if (re_sd_check_pin && (re_sd_check_pin = rt_pin_read(SD_CHECK_PIN)) == 0)
        {
            _sdcard_mount();
        }
    }
}

#endif /* BSP_USING_SDCARD_FS */

int mount_init(void)
{
    if (dfs_mount(RT_NULL, "/", "rom", 0, &(romfs_root)) != 0)
    {
        LOG_E("rom mount to '/' failed!");
    }

#ifdef BSP_USING_SDCARD_FS
    rt_thread_t tid;

    rt_pin_mode(SD_CHECK_PIN, PIN_MODE_INPUT_PULLUP);

    tid = rt_thread_create("sd_mount", sd_mount, RT_NULL,
                           2048, RT_THREAD_PRIORITY_MAX - 2, 20);
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
    }
    else
    {
        LOG_E("create sd_mount thread err!");
    }
#endif
    return RT_EOK;
}
INIT_APP_EXPORT(mount_init);

#endif /* BSP_USING_FS */
