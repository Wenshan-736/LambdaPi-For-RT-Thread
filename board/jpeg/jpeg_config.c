/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2025-09-18     wenshan       参考自hpm-jpeg例程和lv-sjpg组件
 */

#include <rthw.h>
#include <rtthread.h>
#include <rtdevice.h>

#include "board.h"
#include "hpm_jpeg_drv.h"
#include "hpm_l1c_drv.h"
#include "hpm_jpeg.h"
#include "ff.h"
#include "jpeg_config.h"

#ifdef BSP_LVGL_JPEG_SUPPORT
#include "lvgl.h"
#endif

#define BSP_LVGL_JPEG_DECODER_SUPPORT   (0)         /* 给lvgl注册decoder，有点难弄 */

/* 宏定义 */
#ifndef BOARD_JPEG
#define BOARD_JPEG        HPM_JPEG
#endif

#define JPEG_IRQ           IRQn_JPEG
#define JPEG_IRQ_PRIORITY  4


/* 类型 */
struct jpeg_adapter {
    hpm_jpeg_job_t *job;                    /* 任务，此处实际应指向hpm_jpeg_decode_job_t类型(或者encode) */
    lv_img_dsc_t* lv_img;                   /* lvgl使用的图像格式 */
};

/* 全局变量 */

/* 函数声明 */
#if BSP_LVGL_JPEG_DECODER_SUPPORT
static lv_res_t decoder_info(lv_img_decoder_t * decoder, const void * src, lv_img_header_t * header);
static lv_res_t decoder_open(lv_img_decoder_t * decoder, lv_img_decoder_dsc_t * dsc);
static lv_res_t decoder_read_line(lv_img_decoder_t * decoder, lv_img_decoder_dsc_t * dsc, lv_coord_t x, lv_coord_t y,
                                  lv_coord_t len, uint8_t * buf);
static void decoder_close(lv_img_decoder_t * decoder, lv_img_decoder_dsc_t * dsc);
#endif

SDK_DECLARE_EXT_ISR_M(JPEG_IRQ, hpm_jpeg_isr);

/*
 * 文件函数
 */

rt_bool_t file_store(char *fname, uint8_t *buf, uint32_t size)
{
    UINT cnt = 0;
    FRESULT stat;
    static FIL s_file;

    stat = f_open(&s_file, (const TCHAR *) fname, FA_WRITE | FA_CREATE_ALWAYS);
    if (stat == FR_OK) {
        stat = f_write(&s_file, buf, size, &cnt);
        f_close(&s_file);
        if (stat != FR_OK || size != cnt) {
            printf("fail to stored file, status = %d, len = %d\n", stat, cnt);
            return false;
        } else {
            printf("%s is stored, %d bytes.\n", fname, cnt);
            return true;
        }
    } else {
        printf("fail to open file %s, status = %d\n", fname, stat);
        return false;
    }
}

rt_bool_t file_restore(char *fname, uint8_t *buf, uint32_t size)
{
    UINT cnt = 0;
    FRESULT stat;
    static FIL s_file;

    stat = f_open(&s_file, (const TCHAR *) fname, FA_READ);
    if (stat == FR_OK) {
        stat = f_read(&s_file, buf, size, &cnt);
        f_close(&s_file);
        if (stat != FR_OK || size != cnt) {
            printf("fail to read file, status = %d, len = %d\n", stat, cnt);
            return false;
        } else {
            printf("%s is read, %d bytes.\n", fname, cnt);
            return true;
        }
    } else {
        printf("fail to open file %s, status = %d\n", fname, stat);
        return false;
    }
}

uint32_t file_get_size(char *fname)
{
    FRESULT stat;
    uint32_t size = 0;
    static FIL s_file;

    stat = f_open(&s_file, fname, FA_READ);
    if (stat != FR_OK) {
        return 0;
    }

    size = f_size(&s_file);
    f_close(&s_file);

    return size;
}

/*
 * encode 相关，lvgl用不上，暂时不做
 */


/*
 * decode 相关
 */
#if BSP_LVGL_JPEG_DECODER_SUPPORT
/**
 * Get info about an SJPG / JPG image
 * @param decoder pointer to the decoder where this function belongs
 * @param src can be file name or pointer to a C array
 * @param header store the info here
 * @return LV_RES_OK: no error; LV_RES_INV: can't get the info
 */
static lv_res_t decoder_info(lv_img_decoder_t * decoder, const void * src, lv_img_header_t * header)
{
    lv_img_src_t src_type = lv_img_src_get_type(src);          /* 获取src类型，我们只看文件类型 */
    lv_res_t ret = LV_RES_OK;

    if(src_type == LV_IMG_SRC_FILE) {
        const char * fn = src;
        /* 判断输入的文件后缀名 */
        if((strcmp(lv_fs_get_ext(fn), "jpg") == 0) || (strcmp(lv_fs_get_ext(fn), "jpeg") == 0))
        {
            /* 尝试打开文件 */
            /*  */

        } else return LV_RES_INV;
    }
    return LV_RES_INV;
}

/**
 * Open SJPG image and return the decided image
 * @param decoder pointer to the decoder where this function belongs
 * @param dsc pointer to a descriptor which describes this decoding session
 * @return LV_RES_OK: no error; LV_RES_INV: can't get the info
 */
static lv_res_t decoder_open(lv_img_decoder_t * decoder, lv_img_decoder_dsc_t * dsc)
{

}

/**
 * Decode `len` pixels starting from the given `x`, `y` coordinates and store them in `buf`.
 * Required only if the "open" function can't open the whole decoded pixel array. (dsc->img_data == NULL)
 * @param decoder pointer to the decoder the function associated with
 * @param dsc pointer to decoder descriptor
 * @param x start x coordinate
 * @param y start y coordinate
 * @param len number of pixels to decode
 * @param buf a buffer to store the decoded pixels
 * @return LV_RES_OK: ok; LV_RES_INV: failed
 */
static lv_res_t decoder_read_line(lv_img_decoder_t * decoder, lv_img_decoder_dsc_t * dsc, lv_coord_t x, lv_coord_t y,
                                  lv_coord_t len, uint8_t * buf)
{

}

/**
 * Free the allocated resources
 * @param decoder pointer to the decoder where this function belongs
 * @param dsc pointer to a descriptor which describes this decoding session
 */
static void decoder_close(lv_img_decoder_t * decoder, lv_img_decoder_dsc_t * dsc)
{

}

#endif

/*
 * 需要注意函数申请了好几块内存，首先是job相关的，大致有三
 * 部分，file，image和结构体本身；其次是lv_img_dsc_t指针；
 *
 * */
lv_img_dsc_t* jpeg_file_to_lvgl(char *path)
{
    static struct jpeg_adapter jpeg_dec;    /* 控制结构体 */
    hpm_jpeg_decode_info_t djob_info;       /* 解码任务信息 */
    hpm_jpeg_file_t hpm_jpeg_file;          /* jpeg的文件信息，包含文件地址和大小 */
    hpm_jpeg_decode_cfg_t dcfg;             /* 解码配置 */
    rt_err_t err = RT_EOK;
    void *buf = RT_NULL;                    /* 把sd卡的文件读到内存中 */
    static FIL fp;
    FRESULT fret;
    UINT len;                               /* jpeg文件长度 */
    int ret;

    /* 初始化job */
    /* 解码配置,默认输出RGB565,这里jpeg主要是通过lvgl显示，所以宏定义就用lvgl的了 */
#if LV_COLOR_DEPTH == 32
    dcfg.out_format = HPM_JPEG_IMAGE_FORMAT_ARGB8888;
#else
    dcfg.out_format = HPM_JPEG_IMAGE_FORMAT_RGB565;
#endif

    dcfg.out_image_stride = 0;  /* 自动计算 */

    /* 根据解码配置申请内存给decode job（任务） */
    jpeg_dec.job = hpm_jpeg_decode_job_alloc(&dcfg);
    if (jpeg_dec.job == RT_NULL) {
        rt_kprintf("djob0 alloc failed\n");
        return RT_NULL;
    }

    /* 打开文件 */
    fret = f_open(&fp, path, FA_READ);
    if (fret != FR_OK) {
        rt_kprintf("file open failed(%d)!!!\n", fret);
        return RT_NULL;
    }
    /* 获取文件大小 */
    len = file_get_size(path);
    if (len == 0) {
        rt_kprintf("file get size failed!!!\n");
        goto ERROR;
    }
    /* 申请内存,用来保存图片的jpeg源文件 */
    buf = rt_malloc(len);
    if (buf == RT_NULL) {
        rt_kprintf("buf alloc(%u) failed\n", len);
        goto ERROR;
    }

    /* 大概就是把文件读到内存里，方便使用 */
    ret = file_restore(path, buf, len);
    if (ret) {
        hpm_jpeg_file.jpeg_buf = buf;
        hpm_jpeg_file.len = len;
        /* 填文件，参数0代表先把jpeg源文件复制到内部缓冲区，然后用内部缓冲区 */
        /* 这里应该可以用1的，可以节省一个memcpy的事件。但是需要注意file对齐和内存释放，有点麻烦 */
        ret = hpm_jpeg_decode_job_fill_file(jpeg_dec.job, &hpm_jpeg_file, 0);
        if (ret) {
            rt_kprintf("job fill failed\n");
            goto ERROR;
        }
        /* 弄到job的内部缓冲了，外面的就可以释放了 */
        rt_free(buf);
    } else {
        /* 文件没能读到内存 */
        rt_kprintf(" file read failed!!!\n");
        goto ERROR;
    }

    if(!jpeg_dec.job) goto ERROR;


    /* 解码，启动！ */
    /* 这个函数读完文件信息会尝试申请放解码的图片的缓冲空间 */
    /* 提前读出文件信息，指定缓冲有点麻烦，要用的函数jpeg中间件没露出来，让他自己去申请吧 */
    ret = hpm_jpeg_decode_job_start(jpeg_dec.job, NULL);     /* cb是回调，在中断环境运行，看job工作的怎么样了 */
    if (ret) {
        rt_kprintf("decode start failed!!!\n");
        return RT_NULL;
    }
    while (1) {
        /* 比较简单的让他1tick检查一次 */
        rt_thread_delay(1);

        /* 一直检查工作状态 */
        hpm_jpeg_decode_job_get_info(jpeg_dec.job, &djob_info);

        /* 转换完了就结束 */
        if (djob_info.status == HPM_JPEG_JOB_STATUS_FINISHED) {
            break;
        } else if (djob_info.status == HPM_JPEG_JOB_STATUS_ERROR) {
            /* 出错了就g */
            rt_kprintf("decode failed!!!\n");
            return RT_NULL;
        }
    }

    /* hpm的中间件处理结束了，释放idle，关闭文件 */
    f_close(&fp);

    /* 申请lv-img的内存并重置 */
    jpeg_dec.lv_img = rt_malloc(sizeof(lv_img_dsc_t));
    if(jpeg_dec.lv_img == RT_NULL){
        rt_kprintf("lvgl widgets img alloc failed\n");
        return RT_NULL;
    }
    rt_memset(jpeg_dec.lv_img, 0, sizeof(lv_img_dsc_t));

    /* 数据封包成lvgl能读的格式 */
    jpeg_dec.lv_img->header.always_zero = 0;
    jpeg_dec.lv_img->header.cf = LV_IMG_CF_TRUE_COLOR;
    jpeg_dec.lv_img->header.h = djob_info.image->height;
    jpeg_dec.lv_img->header.w = djob_info.image->width;
    jpeg_dec.lv_img->data_size = djob_info.image->width * djob_info.image->height;
    jpeg_dec.lv_img->data = djob_info.image->image_buf;

    return jpeg_dec.lv_img;

ERROR:
    if(buf) {
        rt_free(buf);
    }
    f_close(&fp);
    return RT_NULL;
}

/*
 * 用来释放上面的内存
 * 首先是job相关的，大致有三部分，file，image和结构体本身；
 * 其次是lv_img_dsc_t指针；
 *  */
rt_err_t jpeg_file_to_lvgl_release(lv_img_dsc_t* lv_img)
{
    struct jpeg_adapter *jpeg_dec = RT_NULL;

    jpeg_dec = rt_container_of(lv_img, struct jpeg_adapter, lv_img);

    /* 释放lvgl图片的内存 */
    if(jpeg_dec->lv_img){
        rt_free(jpeg_dec->lv_img);
    }

    /* 这函数只会返回ok */
    /* 会释放job相关的三块内存 */
    hpm_jpeg_decode_job_free(jpeg_dec->job);

    return RT_EOK;
}

/*
 * init 相关
 */

rt_err_t hpm_lvgl_decoder_init(void)
{
    hpm_jpeg_cfg_t hpm_jpeg_cfg;            /* jpeg中间件初始化配置 */
    rt_err_t ret = RT_EOK;

    /* 首先配置一下硬件和jpeg中间件 */
    /* 时钟配置 */
    clock_add_to_group(clock_jpeg, BOARD_RUNNING_CORE);
    intc_m_enable_irq_with_priority(JPEG_IRQ, JPEG_IRQ_PRIORITY);

    /* cfg里面是hpm的jpeg组件会用到的一些参数和回调函数
         * 如果不用可以不管，会调用默认的回调
     */
    rt_memset(&hpm_jpeg_cfg, 0x00, sizeof(hpm_jpeg_cfg));
    hpm_jpeg_cfg.jpeg_base = (void *)BOARD_JPEG;
    hpm_jpeg_cfg.malloc = rt_malloc;
    hpm_jpeg_cfg.free = rt_free;
    if(hpm_jpeg_init(&hpm_jpeg_cfg) != HPM_JPEG_RET_OK)
    {
        return -RT_ERROR;
    }

    /* 准备注册LVGL的Decoder设备 */
#if BSP_LVGL_JPEG_DECODER_SUPPORT
    lv_img_decoder_t * dec = lv_img_decoder_create();
    lv_img_decoder_set_info_cb(dec, decoder_info);
    lv_img_decoder_set_open_cb(dec, decoder_open);
    lv_img_decoder_set_close_cb(dec, decoder_close);
    lv_img_decoder_set_read_line_cb(dec, decoder_read_line);
#endif

    return RT_EOK;
}

#if 1
static void jpeg_thread_entry(void *parameter)
{
    lv_img_dsc_t* lv_img = RT_NULL;
    extern lv_obj_t * ui_Image1;            /* squarline生成的img控件 */

    /* 初始化jpeg和中间件 */
    hpm_lvgl_decoder_init();

    lv_img = jpeg_file_to_lvgl("0:test.jpg");
    if(lv_img == RT_NULL){
        rt_kprintf("lv_img create fail!!!\n");
        return -RT_ERROR;
    }

    lv_img_set_src(ui_Image1, lv_img);
}

int jpeg_example(void)
{
    rt_thread_t tid = RT_NULL;

    hpm_lvgl_decoder_init();

    /* 初始化显示线程 */
    tid = rt_thread_create("jpeg_exp", jpeg_thread_entry, RT_NULL, 4096, 22, 10);
    if (tid == RT_NULL)
    {
        rt_kprintf("create jpeg_exp fail.\n");
        return -RT_ERROR;
    }
    rt_thread_startup(tid);

}

MSH_CMD_EXPORT(jpeg_example, jpeg example);
#endif
