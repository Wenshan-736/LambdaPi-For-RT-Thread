
/**
 * @file lv_port_indev_templ.h
 *
 */

 /*Copy this file as "lv_port_indev.h" and set this value to "1" to enable content*/

#ifndef LV_PORT_INDEV_H
#define LV_PORT_INDEV_H

#ifdef BSP_USING_LVGL
#include "lvgl.h"

#define TOUCH_EVENT_UP      (0x01)
#define TOUCH_EVENT_DOWN    (0x02)
#define TOUCH_EVENT_MOVE    (0x03)
#define TOUCH_EVENT_NONE    (0x80)

void lv_port_indev_init(void);
void lv_port_indev_input(rt_int16_t x, rt_int16_t y, lv_indev_state_t state);
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*LV_PORT_INDEV_H*/

