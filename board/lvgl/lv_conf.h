/**
 * @file lv_conf.h
 * Configuration file for v7.11.0
 */

#ifndef LV_CONF_H
#define LV_CONF_H
/* clang-format off */

#include <rtconfig.h>

#define LV_COLOR_16_SWAP    0
#define LV_USE_PERF_MONITOR 1

#define LV_COLOR_DEPTH              16
#define LV_HOR_RES_MAX              800
#define LV_VER_RES_MAX              480


#define LV_USE_DEMO_RTT_MUSIC       0
#define LV_DEMO_RTT_MUSIC_AUTO_PLAY 0
//#define LV_DEMO_RTT_MUSIC_AUTO_PLAY_FOREVER 1

/* 文件系统相关的东西 */
#ifdef BSP_LVGL_FS_SUPPORT
//#define LV_USE_FS_POSIX                 1           /* 使用posix接口 */
//#define CONFIG_LV_FS_POSIX_LETTER       'S'         /* 用来设置盘符的 */
//#define LV_FS_DEFAULT_DRIVE_LETTER      'S'         /* 用来设置默认盘符的，8.3.11版本没有这个参数 */
#define CONFIG_LV_FS_POSIX_PATH         ""          /* 用来设置默认路径的 */

#define LV_USE_FS_FATFS                 1           /* 使用posix接口 */
#define CONFIG_LV_FS_FATFS_LETTER       'S'         /* 用来设置盘符的 */

#endif /* BSP_LVGL_FS_SUPPORT */

#define LV_FONT_MONTSERRAT_12       1
#define LV_FONT_MONTSERRAT_16       1

#endif /*End of "Content enable"*/
