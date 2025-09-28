/*
 * Copyright (c) 2021 HPMicro
 *
 * Change Logs:
 * Date         Author          Notes
 * 2021-08-13   Fan YANG        first version
 *
 */

#include <rtthread.h>
#include <rtdevice.h>
#include "rtt_board.h"

#if defined(BSP_USING_LVGL) && (!defined(PKG_USING_LVGL_SQUARELINE))
void lv_user_gui_init(void)
{
#if defined(PKG_USING_LV_MUSIC_DEMO)
    lv_demo_music();
#endif
}
#endif

void led_sw(void *arg);

int main(void)
{

    app_init_led_pins();

    static uint32_t led_thread_arg = 0;
    rt_thread_t led_thread = rt_thread_create("led_th", led_sw, &led_thread_arg, 1024, 18, 10);
    rt_thread_startup(led_thread);

    return 0;
}


void led_sw(void *arg)
{
    while(1){
#ifdef APP_LED0
        app_led_write(APP_LED0, APP_LED_ON);
        rt_thread_mdelay(500);
        app_led_write(APP_LED0, APP_LED_OFF);
        rt_thread_mdelay(500);
#endif
#ifdef APP_LED1
        app_led_write(APP_LED1, APP_LED_ON);
        rt_thread_mdelay(500);
        app_led_write(APP_LED1, APP_LED_OFF);
        rt_thread_mdelay(500);
#endif
#ifdef APP_LED2
        app_led_write(APP_LED2, APP_LED_ON);
        rt_thread_mdelay(500);
        app_led_write(APP_LED2, APP_LED_OFF);
        rt_thread_mdelay(500);
#endif
    }
}

static int ceshi(int argc, char *argv[])
{

    return 0;
}
MSH_CMD_EXPORT(ceshi, ceshi example);


