/*Copy this file as "lv_port_indev.c" and set this value to "1" to enable content*/

#include <rtthread.h>
#include "board.h"
#include "lv_port_indev.h"

#if defined(BSP_USING_TOUCH)
static void touchpad_init(void);
static void touchpad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);
static bool touchpad_is_pressed(void);
static void touchpad_get_xy(lv_coord_t *x, lv_coord_t *y);
#endif

lv_indev_t * touch_indev;
static rt_int16_t last_x = 0;
static rt_int16_t last_y = 0;
static lv_indev_state_t last_state = LV_INDEV_STATE_REL;
//static struct rt_touch_data *read_data;

void lv_port_indev_input(rt_int16_t x, rt_int16_t y, lv_indev_state_t state)
{
    last_state = state;
    last_x = x;
    last_y = y;
}

#if defined(BSP_USING_TOUCH)

static void touchpad_get_xy(lv_coord_t *x, lv_coord_t *y)
{
#ifdef BSP_USING_TOUCH_GT9147
    *x = last_x;
    *y = last_y;
#endif
}

/*Return true is the touchpad is pressed*/
static bool touchpad_is_pressed(void)
{
#ifdef BSP_USING_ILI9488
    if (RT_EOK == rt_device_read(ts, 0, read_data, 1)) /* return RT_EOK is a bug (ft6236) */
    {
        if (read_data->event == RT_TOUCH_EVENT_DOWN)
        {
            /* swap x and y */
            rt_int16_t tmp_x = read_data->y_coordinate;
            rt_int16_t tmp_y = read_data->x_coordinate;

            /* invert y */
            tmp_y = 320 - tmp_y;

            /* restore data */
            last_x = tmp_x;
            last_y = tmp_y;

            rt_kprintf("touch: x = %d, y = %d\n", last_x, last_y);
            return true;
        }
    }
#endif
    return false;
}

/* Will be called by the library to read the touchpad */
/* 这里lvgl要用的这三个数据是gt9147-config.c里面函数修改的 */
static void touchpad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
#ifdef BSP_USING_TOUCH_GT9147
    data->point.x = last_x;
    data->point.y = last_y;
    data->state = last_state;
#else
    /*`touchpad_is_pressed` and `touchpad_get_xy` needs to be implemented by you*/
        if (touchpad_is_pressed())
        {
            data->state = LV_INDEV_STATE_PRESSED;
            touchpad_get_xy(&data->point.x, &data->point.y);
        }
        else
        {
            data->state = LV_INDEV_STATE_RELEASED;
        }
#endif
}

void lv_port_indev_init(void)
{
    /* Here you will find example implementation of input devices supported by LittelvGL:
     *  - Touchpad
     *  - Mouse (with cursor support)
     *  - Keypad (supports GUI usage only with key)
     *  - Encoder (supports GUI usage only with: left, right, push)
     *  - Button (external buttons to press points on the screen)
     *
     *  The `..._read()` function are only examples.
     *  You should shape them according to your hardware
     */

#if defined(BSP_USING_TOUCH)
    static lv_indev_drv_t indev_drv; /* Descriptor of a input device driver */
    lv_indev_drv_init(&indev_drv); /* Basic initialization */
    indev_drv.type = LV_INDEV_TYPE_POINTER; /* Touch pad is a pointer-like device */
    indev_drv.read_cb = touchpad_read; /* Set your driver function */

    touch_indev = lv_indev_drv_register(&indev_drv);
#endif

}

#endif
