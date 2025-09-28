/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-09-13     wenshan      参考artpi->drv/touch
 */

#include <rtthread.h>
#include <rtdevice.h>

#ifdef BSP_USING_TOUCH_GT9147

#ifdef BSP_USING_LVGL
#include <lv_port_indev.h>
#endif

#include <gt9147.h>

#define DBG_ENABLE
#define DBG_SECTION_NAME  "drv/touch"
#define DBG_LEVEL         DBG_LOG
#define DBG_COLOR
#include <rtdbg.h>

#define THREAD_PRIORITY     25
#define THREAD_STACK_SIZE   1024
#define THREAD_TIMESLICE    10

static rt_sem_t gt9147_sem = RT_NULL;
static rt_device_t dev = RT_NULL;
static struct rt_touch_data *read_data;
static struct rt_touch_info info;

#ifdef BSP_USING_LVGL
static rt_bool_t touch_down = RT_FALSE;

rt_err_t gt9147_get_ready(void);

static void post_down_event(rt_uint16_t x, rt_uint16_t y)
{
    touch_down = RT_TRUE;
    lv_port_indev_input(x, y, (touch_down == RT_TRUE) ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL);
}

static void post_motion_event(rt_uint16_t x, rt_uint16_t y)
{
    lv_port_indev_input(x, y, (touch_down == RT_TRUE) ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL);
}

static void post_up_event(rt_uint16_t x, rt_uint16_t y)
{
    touch_down = RT_FALSE;
    lv_port_indev_input(x, y, (touch_down == RT_TRUE) ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL);
}
#endif

static void touch_thread_entry(void *parameter)
{
    rt_uint16_t point_x, point_y;

    rt_device_control(dev, RT_TOUCH_CTRL_GET_INFO, &info);

    read_data = (struct rt_touch_data *) rt_malloc(sizeof(struct rt_touch_data) * info.point_num);

    while (1)
    {
        rt_sem_take(gt9147_sem, RT_WAITING_FOREVER);

        if (rt_device_read(dev, 0, read_data, info.point_num) == info.point_num)
        {
            point_x = read_data[0].x_coordinate;
            point_y = read_data[0].y_coordinate;
#ifdef BSP_USING_LVGL
            switch (read_data[0].event)
            {
                case TOUCH_EVENT_UP:
                    post_up_event(point_x, point_y);
                    break;
                case TOUCH_EVENT_DOWN:
                    post_down_event(point_x, point_y);
                    break;
                case TOUCH_EVENT_MOVE:
                    post_motion_event(point_x, point_y);
                    break;
                default:
                    break;
            }
#endif

#ifdef BSP_USING_TOUCH_DEBUG
            if (read_data[0].event == RT_TOUCH_EVENT_DOWN || read_data[0].event == RT_TOUCH_EVENT_MOVE)
            {
                rt_kprintf("point0\nid:%d\nx:%d\ny:%d\ntimestamp:%d\nwidth:%d\n\n", read_data[0].track_id, point_x,
                        point_y, read_data[0].timestamp, read_data[0].width);
            }
#endif
        }
        rt_device_control(dev, RT_TOUCH_CTRL_ENABLE_INT, RT_NULL);
    }
}

static rt_err_t rx_callback(rt_device_t dev, rt_size_t size)
{
    rt_device_control(dev, RT_TOUCH_CTRL_DISABLE_INT, RT_NULL);
    rt_sem_release(gt9147_sem);
    return 0;
}

static int touch_bg_init(const char *name, rt_uint16_t x, rt_uint16_t y, uint8_t point_num)
{
    void *id;
    rt_err_t ret = RT_EOK;
    rt_thread_t tid = RT_NULL;

    /* 查找 Touch 设备 */
    dev = rt_device_find(name);
    if (dev == RT_NULL)
    {
        rt_kprintf("can't find device:%s\n", name);
        return -1;
    }

    /* 以中断的方式打开设备 */
    if (rt_device_open(dev, RT_DEVICE_FLAG_INT_RX) != RT_EOK)
    {
        rt_kprintf("open device failed!");
        return -1;
    }

    /* 创建信号量 */
    gt9147_sem = rt_sem_create("dsem", 0, RT_IPC_FLAG_FIFO);
    if (gt9147_sem == RT_NULL)
    {
        rt_kprintf("create dynamic semaphore failed.\n");
        return -1;
    }


    id = rt_malloc(sizeof(rt_uint8_t) * 8);

#ifdef BSP_USING_TOUCH_DEBUG
    rt_device_control(dev, RT_TOUCH_CTRL_GET_ID, id);
    rt_uint8_t * read_id = (rt_uint8_t *) id;
    rt_kprintf("gt9147.id = %d %d %d %d \n", read_id[0] - '0', read_id[1] - '0', read_id[2] - '0', read_id[3] - '0');
#endif

    rt_device_control(dev, RT_TOUCH_CTRL_GET_INFO, id);
    rt_int32_t rangex = (*(struct rt_touch_info*) id).range_x;
    rt_int32_t rangey = (*(struct rt_touch_info*) id).range_y;
    rt_int32_t _point_num = (*(struct rt_touch_info*) id).point_num;
#ifdef BSP_USING_TOUCH_DEBUG
    rt_kprintf("range_x = %d \n", rangex);
    rt_kprintf("range_y = %d \n", rangey);
    rt_kprintf("point_num = %d \n", _point_num);
#endif
    if((rangex != x) || (rangey != y) || (_point_num != point_num))
    {
#ifdef BSP_USING_TOUCH_DEBUG
        rt_kprintf("gtxxx read parameter error, try to config\n");
#endif
        /* 使用下面函数要硬件重启一下 */
        rt_device_control(dev, RT_TOUCH_CTRL_SET_X_RANGE, &x); /* if possible you can set your x y coordinate*/
        rt_device_control(dev, RT_TOUCH_CTRL_SET_Y_RANGE, &y);
        /* 这个函数可能会导致1158不能正常工作，也有可能没影响 */
        ret = gt9147_get_ready();
        if(ret != RT_EOK)
        {
            rt_kprintf("gtxxx try to hw reset fail\n");
        }
    }

    rt_free(id);

    /* 设置接收回调 */
    rt_device_set_rx_indicate(dev, rx_callback);

    /* 创建读取数据线程 */
    tid = rt_thread_create("touch", touch_thread_entry, RT_NULL, THREAD_STACK_SIZE, THREAD_PRIORITY, THREAD_TIMESLICE);
    if (tid != RT_NULL)
        rt_thread_startup(tid);

    return 0;
}

rt_err_t gt9147_get_ready(void)
{
    struct rt_i2c_msg msgs;
    struct rt_i2c_bus_device *i2c_bus = RT_NULL;
    rt_uint8_t buf[3] = {0};
    rt_base_t irq_pin = 0;
    rt_base_t rst_pin = 0;

    i2c_bus = (struct rt_i2c_bus_device *)rt_device_find("i2c0");
    if (i2c_bus == RT_NULL)
    {
        rt_kprintf("can't find i2c0 device!\n");
    }

    irq_pin = rt_pin_get("PB08");
    rst_pin = rt_pin_get("PB09");
    /* 准备重置地址 */
    rt_pin_mode(rst_pin, PIN_MODE_OUTPUT);
    rt_pin_mode(irq_pin, PIN_MODE_OUTPUT);

    rt_pin_write(irq_pin, PIN_HIGH);
    rt_thread_mdelay(10);
    rt_pin_write(rst_pin, PIN_LOW);
    rt_thread_mdelay(10);
    rt_pin_write(rst_pin, PIN_HIGH);
    rt_thread_mdelay(10);

    /* 准备工作，irq浮空输入 */
    rt_pin_mode(irq_pin, PIN_MODE_INPUT);
    rt_thread_mdelay(100);

    buf[0] = 0x80;
    buf[1] = 0x40;
    buf[2] = 0x02;

    msgs.addr  = GT9147_ADDRESS_LOW;
    msgs.flags = RT_I2C_WR;
    msgs.buf   = buf;
    msgs.len   = 3;

    if (rt_i2c_transfer(i2c_bus, &msgs, 1) == 1)
    {
#ifdef BSP_USING_TOUCH_DEBUG
        LOG_D("soft reset gt9147 success\n");
#endif
    }
    else
    {
        LOG_D("soft reset gt9147 failed\n");
        return -RT_ERROR;
    }

    buf[2] = 0x00;
    if (rt_i2c_transfer(i2c_bus, &msgs, 1) == 1)
    {
#ifdef BSP_USING_TOUCH_DEBUG
        LOG_D("get ready gt9147 success\n");
#endif
    }
    else
    {
        LOG_D("get ready gt9147 failed\n");
        return -RT_ERROR;
    }
    return RT_EOK;
}
//MSH_CMD_EXPORT(gt9147_get_ready, gt9147 get ready);

/* 在rti-fn-5阶段注册touch设备 */
int rt_hw_gt9147_port(void)
{
    struct rt_touch_config config;
    rt_uint8_t rst;
    rst = rt_pin_get("PB09");
    config.dev_name = "i2c0";
    config.irq_pin.pin  = rt_pin_get("PB08");
    config.irq_pin.mode = PIN_MODE_INPUT;
    config.user_data = &rst;
    rt_hw_gt9147_init("gt", &config);
    return 0;
}
INIT_ENV_EXPORT(rt_hw_gt9147_port);

/* 在rti-fn-6阶段注册touch回调线程 */
int touch_init(void)
{
#ifdef BSP_USING_LVGL
    touch_bg_init("gt", LV_HOR_RES_MAX, LV_VER_RES_MAX, 5);
#else
    touch_bg_init("gt", 800, 480, 5);
#endif
    return RT_EOK;
}
INIT_APP_EXPORT(touch_init);

#endif

