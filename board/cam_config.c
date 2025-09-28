/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-09-17     wenshan     修改自hpm的网络摄像头实验
 *                                                          增加cam-双buffer开关，增加cam-example的msh命令
 *                                                          将cam的tc和eof的信号量改为事件，避免溢出
 */


#include <rthw.h>
#include <rtthread.h>
#include <rtdevice.h>

#include "board.h"
#include "hpm_cam_drv.h"
#include "hpm_camera.h"
#include "hpm_lcdc_drv.h"
#include "hpm_gpio_drv.h"
#include "hpm_l1c_drv.h"

#include "hpm_rtt_interrupt_util.h"

/* cam related */
#ifndef BOARD_CAM
#define BOARD_CAM         HPM_CAM0
#endif

#ifndef BOARD_CAM_IRQ
#define BOARD_CAM_IRQ     IRQn_CAM0
#endif

#define CAM_I2C          BOARD_CAM_I2C_BASE
#define CAMERA_INTERFACE camera_interface_dvp

/* 感觉一直采集有点费资源了,一般
 * 时候就单次采集，不用双buffer,
 * 实际上是cam初始化绑的buffer的
 * 个数
 * */
#define CAM_ENABLE_TWO_BUFFER           (0)

/* 这里不能乱填，不然cam-device初始化可能过不了，组合可以参考camera_resolution_t枚举 */
#define IMAGE_WIDTH      480
#define IMAGE_HEIGHT     272

/* in order to align 16byte for jpeg, buffer + 256 */
#define CAM_BUF_LEN        (IMAGE_HEIGHT * IMAGE_WIDTH * 2)
#define CAM_BUF_COUNT      (2)

#define PIXEL_FORMAT       display_pixel_format_rgb565

ATTR_PLACE_AT_WITH_ALIGNMENT(".framebuffer", HPM_L1C_CACHELINE_SIZE) uint8_t cam_buffers[CAM_BUF_COUNT][CAM_BUF_LEN + 256];

/* cam isr与线程同步事件 */
static rt_event_t cam_tc_evt = RT_NULL;
static rt_event_t cam_eof_evt = RT_NULL;
#define CAM_EVENT_TRANSFER_COMPLETE_FLAG        (1 << 0)
#define CAM_EVENT_END_OF_FRAME_FLAG             (1 << 1)

/* 用来做缓冲的 */
uint8_t *front_buffer, *back_buffer;

void isr_cam(void)
{
    rt_base_t level;
    level = rt_hw_interrupt_disable();
    if ((cam_check_status(BOARD_CAM, cam_status_fb1_dma_transfer_done) == true) &&
        (BOARD_CAM->INT_EN & cam_irq_fb1_dma_transfer_done)) {
        cam_clear_status(BOARD_CAM, cam_status_fb1_dma_transfer_done);
        rt_event_send(cam_tc_evt, CAM_EVENT_TRANSFER_COMPLETE_FLAG);
    }
#if CAM_ENABLE_TWO_BUFFER
    if ((cam_check_status(BOARD_CAM, cam_status_fb2_dma_transfer_done) == true) &&
        (BOARD_CAM->INT_EN & cam_irq_fb2_dma_transfer_done)) {
        cam_clear_status(BOARD_CAM, cam_status_fb2_dma_transfer_done);
        rt_event_send(cam_tc_evt, CAM_EVENT_TRANSFER_COMPLETE_FLAG);
    }
#endif
    if ((cam_check_status(BOARD_CAM, cam_status_end_of_frame) == true) &&
        (BOARD_CAM->INT_EN & cam_irq_end_of_frame)) {
        cam_clear_status(BOARD_CAM, cam_status_end_of_frame);
        rt_event_send(cam_eof_evt, CAM_EVENT_END_OF_FRAME_FLAG);
    }
    rt_hw_interrupt_enable(level);
}
RTT_DECLARE_EXT_ISR_M(BOARD_CAM_IRQ, isr_cam)

/*
 * sensor configuration
 *
 */

void cam_delay_ms(uint32_t ms)
{
    rt_thread_mdelay(ms);
}

/*
 * 配置摄像头，ov5640
 */
void init_camera_device(void)
{
    camera_context_t camera_context = {0};
    camera_config_t camera_config = {0};
    camera_context.i2c_device_addr = CAMERA_DEVICE_ADDR;
    camera_context.ptr = CAM_I2C;
    camera_context.delay_ms = cam_delay_ms;
#ifdef BOARD_SUPPORT_CAM_RESET
    camera_context.write_rst = board_write_cam_rst;
#endif
#ifdef BOARD_SUPPORT_CAM_PWDN
    camera_context.write_pwdn = board_write_cam_pwdn;
#endif

    camera_config.width = IMAGE_WIDTH;
    camera_config.height = IMAGE_HEIGHT;
    camera_config.interface = CAMERA_INTERFACE;
    camera_config.pixel_format = PIXEL_FORMAT;

    if (CAMERA_INTERFACE == camera_interface_dvp) {
        camera_device_get_dvp_param(&camera_context, &camera_config);
    }

    if (status_success != camera_device_init(&camera_context, &camera_config)) {
        rt_kprintf("failed to init camera device\n");
        while (1) {
            camera_context.delay_ms(10);
        };
    }
}

/*
 *  配置HPM上的CAM接口
 */
void init_cam(void)
{
    cam_config_t cam_config;

    cam_get_default_config(BOARD_CAM, &cam_config, PIXEL_FORMAT);

    cam_config.width = IMAGE_WIDTH;
    cam_config.height = IMAGE_HEIGHT;
    cam_config.hsync_active_low = true;
    cam_config.buffer1 = core_local_mem_to_sys_address(BOARD_RUNNING_CORE, (uint32_t)cam_buffers[0]);

#if CAM_ENABLE_TWO_BUFFER
    cam_config.buffer2 = core_local_mem_to_sys_address(BOARD_RUNNING_CORE, (uint32_t)cam_buffers[1]);
#endif

    if (PIXEL_FORMAT == display_pixel_format_rgb565) {
        cam_config.color_format = CAM_COLOR_FORMAT_RGB565;
    } else if (PIXEL_FORMAT == display_pixel_format_y8) {
        cam_config.color_format = CAM_COLOR_FORMAT_YCBCR422_YUV422;
        cam_config.data_store_mode = CAM_DATA_STORE_MODE_Y_ONLY;
    }
    cam_init(BOARD_CAM, &cam_config);
#if CAM_ENABLE_TWO_BUFFER
    cam_enable_irq(BOARD_CAM, cam_irq_fb1_dma_transfer_done | cam_irq_fb2_dma_transfer_done);
#else
    cam_enable_irq(BOARD_CAM, cam_irq_fb1_dma_transfer_done);
#endif
    intc_m_enable_irq_with_priority(BOARD_CAM_IRQ, 4);
}

/*
 * 使用CAM需要调用的初始化函数
 *  */
int camera_init(void)
{
    clock_add_to_group(clock_jpeg, BOARD_RUNNING_CORE);
    clock_add_to_group(clock_pdma, BOARD_RUNNING_CORE);

    /* cam-transfer-complete每次采集完都会触发 */
    cam_tc_evt = rt_event_create("cam_tc", RT_IPC_FLAG_PRIO);
    if (cam_tc_evt == RT_NULL) {
        return 1;
    }

    /* cam-eof中断只有在接受到一次cam-tc信号量时候才会开启，接受一次之后就会立即关闭 */
    cam_eof_evt = rt_event_create("cam_eof", RT_IPC_FLAG_PRIO);
    if (cam_eof_evt == RT_NULL) {
        return 1;
    }

    front_buffer = cam_buffers[0];
    back_buffer = cam_buffers[1];
    board_init_cam_clock(BOARD_CAM);

#if !defined(BSP_USING_I2C0)
    /* 这个如果使能i2c0的话重复初始化了 */
    board_init_i2c(CAM_I2C);
#endif

    board_init_cam_pins();

    /* 初始化摄像头设备：ov5640 */
    init_camera_device();

    /* 初始化hpm的cam接口 */
    init_cam();

    cam_start(BOARD_CAM);
    rt_thread_mdelay(100);
    return 0;
}

/*
 *      一个简单的获取图像的接口，可以调用开始采集并
 *      获取图像，函数会阻塞，当返回值是RT_EOK时候图
 *      像刷新成功，图像保存到了back_buffer指针
 *
 *      补：单缓使用while
 */
rt_base_t cam_data_refresh(void)
{
    uint32_t jpg_size = 0, count = 0;
    rt_err_t result;
    rt_base_t sta = RT_EOK;
    uint8_t *tmp;
    rt_uint32_t e;

    cam_start(BOARD_CAM);

    /* 等传输结束 */
    result = rt_event_recv(cam_tc_evt, CAM_EVENT_TRANSFER_COMPLETE_FLAG, \
                            RT_EVENT_FLAG_AND|RT_EVENT_FLAG_CLEAR,\
                            RT_WAITING_FOREVER,&e);
    if (result != RT_EOK) {
        rt_kprintf("cam tc take a dynamic event, failed.\n");
        rt_event_delete(cam_tc_evt);
        return RT_FALSE;
    } else {
        cam_clear_status(BOARD_CAM, cam_status_end_of_frame);
        cam_enable_irq(BOARD_CAM, cam_irq_end_of_frame);
        /* 等帧结束 */
        result = rt_event_recv(cam_eof_evt, CAM_EVENT_END_OF_FRAME_FLAG, \
                                    RT_EVENT_FLAG_AND|RT_EVENT_FLAG_CLEAR,\
                                    RT_WAITING_FOREVER,&e);
        if (result != RT_EOK) {
            rt_kprintf("cam eof take a dynamic event, failed.\n");
            rt_event_delete(cam_eof_evt);
            return RT_FALSE;
        }
        /* cam暂停采集，先准备处理数据 */
        cam_disable_irq(BOARD_CAM, cam_irq_end_of_frame);

        /* 不能用safe-stop，里面有while，单缓用stop有点问题 */
#if CAM_ENABLE_TWO_BUFFER
        cam_stop(BOARD_CAM);
#else
        cam_clear_status(BOARD_CAM, cam_status_end_of_frame);
        while (cam_check_status(BOARD_CAM, cam_status_end_of_frame) == false) {
            rt_thread_delay(5);
        }
        cam_stop(BOARD_CAM);
#endif
        /* 换缓冲区 */
        tmp = front_buffer;
        front_buffer = back_buffer;
        back_buffer = tmp;

        /* 更新buffer地址 */
        cam_update_buffer(BOARD_CAM, (uint32_t)front_buffer);
#if CAM_ENABLE_TWO_BUFFER
        BOARD_CAM->DMASA_FB2 = (uint32_t)front_buffer;
#endif
        if (l1c_dc_is_enabled()) {
            l1c_dc_invalidate((uint32_t)back_buffer, CAM_BUF_LEN);
        }

        /* 处理数据 */

        /* 恢复采集 */
//        cam_start(BOARD_CAM);
    }
    return sta;
}


#ifdef BSP_USING_CAM_EXAMPLE

/*
 * 使用时需要注意里面用到了lvgl的img控件，变量是ui_Image1
 * 这个是squarline生成的，如果不用squarline可以自己创建一
 * 个lvgl工程，然后变量改成自己的img控件名字
 * */
#include "lvgl.h"

static lv_img_dsc_t cam_img_dsc = {
    .header.always_zero = 0,                                // 必须为0
    .header.w = IMAGE_WIDTH,                                // 图像宽度
    .header.h = IMAGE_HEIGHT,                               // 图像高度
    .header.cf = LV_IMG_CF_TRUE_COLOR,                      // 颜色格式为真彩色
    .data_size = IMAGE_WIDTH * IMAGE_HEIGHT * 2,            // 数据大小（RGB565每个像素2字节）
    .data = (const uint8_t*)&cam_buffers                    // 指向CAM数据缓冲区
};

static void cam_thread_entry(void *parameter)
{
    extern lv_obj_t * ui_Image1;    /* squarline生成的img控件 */
    rt_err_t ret = RT_EOK;

    while(1)
    {
//        rt_thread_delay(100);

        /* 拍摄并等待数据 */
        ret = cam_data_refresh();

        if(ret != RT_EOK)
        {
            rt_kprintf("camera data refresh fail,error code = %d.\n", ret);
            continue;
        }
        cam_img_dsc.data = (const uint8_t*)back_buffer;
        lv_img_set_src(ui_Image1, &cam_img_dsc);
    }
}

int cam_example(void)
{
    rt_thread_t tid = RT_NULL;

    /* 相机功能初始化 */
    camera_init();

    /* 初始化显示线程 */
    tid = rt_thread_create("cam_exp", cam_thread_entry, RT_NULL, 1024, 15, 10);
    if (tid == RT_NULL)
    {
        rt_kprintf("create cam_exp fail.\n");
        return -RT_ERROR;
    }
    rt_thread_startup(tid);

    /* 开启相机采集 */
    cam_start(BOARD_CAM);
    return RT_EOK;
}

MSH_CMD_EXPORT(cam_example, cam example);

#endif

