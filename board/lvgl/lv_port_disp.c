
/*
 *   Change Logs:
 *   Date           Author       Notes
 *   2025-09-26     wenshan      修改自hpm的lvgl实验。添加非DIRECT模式下
  *                                                               的pdma加速lvgl-draw-ctx的blend操作
 *
 */
#include "board.h"
#include "hpm_l1c_drv.h"
#include "hpm_lcdc_drv.h"
#include "hpm_pdma_drv.h"
#include "lvgl.h"
#include "rtthread.h"
#include "hpm_rtt_interrupt_util.h"

#define LVGL_CONFIG_FLUSH_DIRECT_MODE_ENABLE    0
#define LVGL_CONFIG_DIRECT_MODE_VSYNC_ENABLE    1

/*
 * 下面这个东西还没调好，甚至很多情况不如软件
 * */
#define BSP_LVGL_USING_CUSTOM_DRAW_CTX          0

#ifndef RUNNING_CORE_INDEX
#define RUNNING_CORE_INDEX HPM_CORE0
#endif

#define LCD_CONTROLLER BOARD_LCD_BASE
#define LCD_LAYER_INDEX (0)
#define LCD_LAYER_DONE_MASK   (1U<<LCD_LAYER_INDEX)
#define LCD_IRQ_NUM  BOARD_LCD_IRQ

#ifndef HPM_LCD_IRQ_PRIORITY
#define HPM_LCD_IRQ_PRIORITY  7
#endif

#define LVGL_PDMA_IRQ_NUM IRQn_PDMA_D0
#define LVGL_PDMA_IRQ_PRIORITY 6
#define LVGL_PDMA_BASE HPM_PDMA

#define LV_LCD_WIDTH (BOARD_LCD_WIDTH)
#define LV_LCD_HEIGHT (BOARD_LCD_HEIGHT)

#define LV_FB_SIZE_ALIGNED HPM_L1C_CACHELINE_ALIGN_UP(LV_LCD_WIDTH * LV_LCD_HEIGHT)

#define LVGL_PDMA_CLOCK clock_pdma
#if LVGL_CONFIG_FLUSH_DIRECT_MODE_ENABLE

#ifndef HPM_LVGL_FRAMEBUFFER_NONCACHEABLE
static lv_color_t __attribute__((section(".framebuffer"), aligned(HPM_L1C_CACHELINE_SIZE))) lv_framebuffer0[LV_FB_SIZE_ALIGNED];
static lv_color_t __attribute__((section(".framebuffer"), aligned(HPM_L1C_CACHELINE_SIZE))) lv_framebuffer1[LV_FB_SIZE_ALIGNED];
static lv_color_t __attribute__((section(".framebuffer"), aligned(HPM_L1C_CACHELINE_SIZE))) lcdc_framebuffer[LV_FB_SIZE_ALIGNED];
#else
static lv_color_t __attribute__((section(".noncacheable"), aligned(HPM_L1C_CACHELINE_SIZE))) lv_framebuffer0[LV_FB_SIZE_ALIGNED];
static lv_color_t __attribute__((section(".noncacheable"), aligned(HPM_L1C_CACHELINE_SIZE))) lv_framebuffer1[LV_FB_SIZE_ALIGNED];
static lv_color_t __attribute__((section(".noncacheable"), aligned(HPM_L1C_CACHELINE_SIZE))) lcdc_framebuffer[LV_FB_SIZE_ALIGNED];
#endif

#else

#ifndef HPM_LVGL_FRAMEBUFFER_NONCACHEABLE
static lv_color_t __attribute__((section(".framebuffer"), aligned(HPM_L1C_CACHELINE_SIZE))) lv_framebuffer0[LV_FB_SIZE_ALIGNED];
static lv_color_t __attribute__((section(".framebuffer"), aligned(HPM_L1C_CACHELINE_SIZE))) lv_framebuffer1[LV_FB_SIZE_ALIGNED];
#else
static lv_color_t __attribute__((section(".noncacheable"), aligned(HPM_L1C_CACHELINE_SIZE))) lv_framebuffer0[LV_FB_SIZE_ALIGNED];
static lv_color_t __attribute__((section(".noncacheable"), aligned(HPM_L1C_CACHELINE_SIZE))) lv_framebuffer1[LV_FB_SIZE_ALIGNED];
#endif
static lv_color_t *lcdc_framebuffer = lv_framebuffer1;

#include "lv_draw_sw_blend.h"
#include "lv_draw_sw.h"
/* 不在sw-ctx上做额外的更改，只是重命名一下 */
typedef lv_draw_sw_ctx_t hpm_draw_ctx_t;

#endif/*LVGL_CONFIG_FLUSH_DIRECT_MODE_ENABLE*/

#if LVGL_CONFIG_FLUSH_DIRECT_MODE_ENABLE
struct pdma_ctx {
    struct {
        pdma_plane_config_t plane_src_cfg;
        pdma_output_config_t output_cfg;
        display_yuv2rgb_coef_t yuv2rgb_coef;
    } cfg;
    lv_area_t inv_areas[LV_INV_BUF_SIZE];
    uint8_t inv_area_joined[LV_INV_BUF_SIZE];
    volatile uint16_t inv_p;
};
#endif

struct lv_adapter {
    lv_disp_draw_buf_t draw_buf;
    lv_disp_drv_t disp_drv;
#if defined(CONFIG_LV_TOUCH)
    lv_indev_drv_t indev_touch_drv;
#endif
    volatile uint32_t wait_flush_buffer;
    volatile uint32_t lcdc_buffer;
#if LVGL_CONFIG_FLUSH_DIRECT_MODE_ENABLE
    struct pdma_ctx pdma_ctx;
    volatile uint32_t direct_vsync;
#endif
};

static struct lv_adapter lv_adapter_ctx;

#ifdef BSP_USING_RTT_LCD_DRIVER
struct rt_device *g_lcd_dev = RT_NULL;
#endif
#if LVGL_CONFIG_FLUSH_DIRECT_MODE_ENABLE
static void lvgl_pdma_init(struct lv_adapter *ctx)
{
    struct pdma_ctx *pdma_ctx = &ctx->pdma_ctx;
    pdma_config_t config;
    clock_add_to_group(LVGL_PDMA_CLOCK, BOARD_RUNNING_CORE);

#if LV_COLOR_DEPTH == 32
    display_pixel_format_t pixel_format = display_pixel_format_argb8888;
#else
    display_pixel_format_t pixel_format = display_pixel_format_rgb565;
#endif
    pdma_get_default_config(LVGL_PDMA_BASE, &config, pixel_format);

    config.enable_plane = pdma_plane_src;
    config.block_size = pdma_blocksize_8x8;

    pdma_init(LVGL_PDMA_BASE, &config);
    pdma_plane_config_t *plane_src_cfg = &pdma_ctx->cfg.plane_src_cfg;
    pdma_output_config_t *output_cfg = &pdma_ctx->cfg.output_cfg;
    display_yuv2rgb_coef_t *yuv2rgb_coef = &pdma_ctx->cfg.yuv2rgb_coef;

    pdma_get_default_plane_config(LVGL_PDMA_BASE, plane_src_cfg, pixel_format);
    pdma_get_default_yuv2rgb_coef_config(LVGL_PDMA_BASE, yuv2rgb_coef, pixel_format);
    pdma_get_default_output_config(LVGL_PDMA_BASE, output_cfg, pixel_format);

    intc_m_enable_irq_with_priority(LVGL_PDMA_IRQ_NUM, LVGL_PDMA_IRQ_PRIORITY);
}

static void lvgl_pdma_blit(struct lv_adapter *ctx, void *dst, uint16_t dst_stride, void *src, uint16_t src_stride,
                    uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    uint32_t pixel_size = sizeof(lv_color_t);
    struct pdma_ctx *pdma_ctx = &ctx->pdma_ctx;
    pdma_plane_config_t *plane_src_cfg = &pdma_ctx->cfg.plane_src_cfg;
    pdma_output_config_t *output_cfg = &pdma_ctx->cfg.output_cfg;
    pdma_stop(LVGL_PDMA_BASE);

    plane_src_cfg->buffer = (uint32_t)src + (y * src_stride + x) * pixel_size;
    plane_src_cfg->width = width;
    plane_src_cfg->height = height;
    plane_src_cfg->pitch = src_stride * pixel_size;
    plane_src_cfg->background = 0xFFFFFFFF;
    pdma_config_planes(LVGL_PDMA_BASE, plane_src_cfg, NULL, NULL);

    output_cfg->plane[pdma_plane_src].x = 0;
    output_cfg->plane[pdma_plane_src].y = 0;
    output_cfg->plane[pdma_plane_src].width = width;
    output_cfg->plane[pdma_plane_src].height = height;

    output_cfg->alphablend.src_alpha = 0xFF;
    output_cfg->alphablend.src_alpha_op = display_alpha_op_override;
    output_cfg->alphablend.mode = display_alphablend_mode_clear;

    output_cfg->width = width;
    output_cfg->height = height;
    output_cfg->buffer = (uint32_t)dst + (y * dst_stride + x) * pixel_size;
    output_cfg->pitch = dst_stride * pixel_size;

    pdma_config_output(LVGL_PDMA_BASE, output_cfg);
    pdma_start(LVGL_PDMA_BASE);
    pdma_enable_irq(LVGL_PDMA_BASE, PDMA_CTRL_PDMA_DONE_IRQ_EN_MASK, true);
}

static void lvgl_pdma_done(struct lv_adapter *ctx)
{
    struct pdma_ctx *pdma_ctx = &ctx->pdma_ctx;
    if (pdma_ctx->inv_p == 0) {
        lv_disp_flush_ready(&ctx->disp_drv);
        pdma_stop(LVGL_PDMA_BASE);
        return;
    }

    while (pdma_ctx->inv_p > 0) {
        pdma_ctx->inv_p--;
        /*area should be ignored*/
        if (pdma_ctx->inv_area_joined[pdma_ctx->inv_p]) {
            if (pdma_ctx->inv_p == 0) {
                lv_disp_flush_ready(&ctx->disp_drv);
                pdma_stop(LVGL_PDMA_BASE);
            }
            continue;
        }

        lv_area_t *area = &pdma_ctx->inv_areas[pdma_ctx->inv_p];
        lvgl_pdma_blit(ctx, (void *)ctx->lcdc_buffer, LV_LCD_WIDTH,
                        (void *)ctx->wait_flush_buffer,  LV_LCD_WIDTH,
                    area->x1, area->y1, lv_area_get_width(area), lv_area_get_height(area));
        break;
    }
}

void lvgl_pdma_start(struct lv_adapter *ctx)
{
    struct pdma_ctx *pdma_ctx = &ctx->pdma_ctx;

    if (pdma_ctx->inv_p == 0) {
        /*Never to be run*/
        lv_disp_flush_ready(&ctx->disp_drv);
        return;
    }

    while (pdma_ctx->inv_p > 0) {
        pdma_ctx->inv_p--;
        /*area should be ignored*/
        if (pdma_ctx->inv_area_joined[pdma_ctx->inv_p]) {
            if (pdma_ctx->inv_p == 0) {
                /*Never to be run*/
                lv_disp_flush_ready(&ctx->disp_drv);
                pdma_stop(LVGL_PDMA_BASE);
            }
            continue;
        }

        lv_area_t *area = &pdma_ctx->inv_areas[pdma_ctx->inv_p];
        lvgl_pdma_blit(ctx, (void *)ctx->lcdc_buffer, LV_LCD_WIDTH,
                        (void *)ctx->wait_flush_buffer,  LV_LCD_WIDTH,
                    area->x1, area->y1, lv_area_get_width(area), lv_area_get_height(area));
        break;
    }
}

static void lv_flush_display_direct(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    struct lv_adapter *ctx = disp_drv->user_data;
    uint32_t wait_flush_buffer = core_local_mem_to_sys_address(RUNNING_CORE_INDEX, (uint32_t)color_p);

    if (!lv_disp_flush_is_last(disp_drv)) {
        lv_disp_flush_ready(disp_drv);
        return;
    }
#ifndef HPM_LVGL_FRAMEBUFFER_NONCACHEABLE
    if (l1c_dc_is_enabled()) {
        uint32_t aligned_start = HPM_L1C_CACHELINE_ALIGN_DOWN(wait_flush_buffer);
        uint32_t aligned_end = HPM_L1C_CACHELINE_ALIGN_UP(wait_flush_buffer + disp_drv->draw_buf->size * sizeof(lv_color_t));
        uint32_t aligned_size = aligned_end - aligned_start;
        l1c_dc_writeback(aligned_start, aligned_size);
    }
#endif
    lv_disp_t *disp = _lv_refr_get_disp_refreshing();
    struct pdma_ctx *pdma_ctx = &ctx->pdma_ctx;
    ctx->wait_flush_buffer = wait_flush_buffer;
    pdma_ctx->inv_p = disp->inv_p;
    memcpy(pdma_ctx->inv_area_joined, disp->inv_area_joined, sizeof(disp->inv_area_joined));
    memcpy(pdma_ctx->inv_areas, disp->inv_areas, sizeof(disp->inv_areas));
#if LVGL_CONFIG_DIRECT_MODE_VSYNC_ENABLE
#ifndef BSP_USING_RTT_LCD_DRIVER
    while (!ctx->direct_vsync) {
    }
    ctx->direct_vsync = 0;
#else
    if (!g_lcd_dev)
    {
        return;
    }
    g_lcd_dev->control(g_lcd_dev, RTGRAPHIC_CTRL_WAIT_VSYNC, RT_NULL);
#endif
#endif

    lvgl_pdma_start(ctx);
}

static void lvgl_pdma_isr(void)
{
    pdma_enable_irq(LVGL_PDMA_BASE, PDMA_CTRL_PDMA_DONE_IRQ_EN_MASK, false);
    pdma_stop(LVGL_PDMA_BASE);
    lvgl_pdma_done(&lv_adapter_ctx);
}

RTT_DECLARE_EXT_ISR_M(LVGL_PDMA_IRQ_NUM, lvgl_pdma_isr);

#ifndef BSP_USING_RTT_LCD_DRIVER
static void hpm_lcdc_isr(void)
{
    volatile uint32_t s = lcdc_get_dma_status(LCD_CONTROLLER);
    lcdc_clear_dma_status(LCD_CONTROLLER, s);

    if (s & (LCD_LAYER_DONE_MASK << LCDC_DMA_ST_DMA0_DONE_SHIFT)) {
        lv_adapter_ctx.direct_vsync = 1;
    }
}
RTT_DECLARE_EXT_ISR_M(LCD_IRQ_NUM, hpm_lcdc_isr)
#endif

#else

/* ---------------------------lvgl draw ctx start----------------------------  */
#if BSP_LVGL_USING_CUSTOM_DRAW_CTX
/* pdma-sem */
static rt_sem_t lvgl_pdma_sem = RT_NULL;

typedef struct {
    uint32_t u;
    display_pixel_format_t format;
} color;

/*
 * 填充颜色
 * */
void fill(void *dst, uint32_t dst_width,                /* dst */
                uint32_t width, uint32_t height,        /* 要填充的宽和高 */
                uint32_t color,uint32_t alpha,          /* 颜色（ARGB8888），不透明度 */
                display_pixel_format_t format)          /* 颜色格式 */
{
    hpm_stat_t stat;
    uint32_t status;

//    if (l1c_dc_is_enabled()) {
//        uint32_t aligned_start = HPM_L1C_CACHELINE_ALIGN_DOWN((uint32_t)dst);
//        uint32_t aligned_end = HPM_L1C_CACHELINE_ALIGN_UP((uint32_t)dst + dst_width * (height) * display_get_pixel_size_in_byte(format));
//        uint32_t aligned_size = aligned_end - aligned_start;
//        l1c_dc_flush(aligned_start, aligned_size);
//    }
    rt_sem_take(lvgl_pdma_sem, RT_WAITING_FOREVER);
    pdma_stop(HPM_PDMA);

    stat = pdma_fill_color(HPM_PDMA, (uint32_t)dst, dst_width,
                          width, height, color,
                          alpha, format,
                          0, &status);
    pdma_enable_irq(LVGL_PDMA_BASE, PDMA_CTRL_PDMA_DONE_IRQ_EN_MASK, true);


    if (stat != status_success) {
        rt_kprintf("ERROR: pdma fill failed: 0x%x\n", status);
    }
}

/*
 * 填充数据
 * */
void blit(void *dst, uint32_t dst_width,                                /* dst */
                const void *src, uint32_t src_width,                          /* src */
                uint32_t x, uint32_t y, display_pixel_format_t format,  /* dst上的位置，单位:像素 */
                uint32_t width, uint32_t height, uint32_t alpha)        /* 要填的宽高，不透明度 */
{
    hpm_stat_t stat;
    uint32_t status;

//    if (l1c_dc_is_enabled()) {
//        uint32_t aligned_start = HPM_L1C_CACHELINE_ALIGN_DOWN((uint32_t)dst);
//        uint32_t aligned_end = HPM_L1C_CACHELINE_ALIGN_UP((uint32_t)dst + dst_width * (y + height) * display_get_pixel_size_in_byte(format));
//        uint32_t aligned_size = aligned_end - aligned_start;
//        l1c_dc_flush(aligned_start, aligned_size);
//
//        aligned_start = HPM_L1C_CACHELINE_ALIGN_DOWN((uint32_t)src);
//        aligned_end = HPM_L1C_CACHELINE_ALIGN_UP((uint32_t)src + src_width * (y + height) * display_get_pixel_size_in_byte(format));
//        aligned_size = aligned_end - aligned_start;
//        l1c_dc_flush(aligned_start, aligned_size);
//    }
    /* 是做填充的，大致流程是配置输入和输出图层，然后启动pdma
     */
    rt_sem_take(lvgl_pdma_sem, RT_WAITING_FOREVER);

    pdma_stop(HPM_PDMA);
    stat = pdma_blit(HPM_PDMA,
            (uint32_t)core_local_mem_to_sys_address(HPM_CORE0, (uint32_t)dst),
            dst_width,
            (uint32_t)core_local_mem_to_sys_address(HPM_CORE0, (uint32_t)src),
            src_width,
            x, y, width, height, alpha, format, 0, &status);

    pdma_enable_irq(LVGL_PDMA_BASE, PDMA_CTRL_PDMA_DONE_IRQ_EN_MASK, true);

    if (stat != status_success) {
        rt_kprintf("ERROR: pdma blit failed: 0x%x\n", status);
    }
}

/* pdma中断 */
static void lvgl_pdma_isr(void)
{
    pdma_enable_irq(LVGL_PDMA_BASE, PDMA_CTRL_PDMA_DONE_IRQ_EN_MASK, false);

    /* 不管是出错还是运行完成pdma都该停了 */
    pdma_stop(LVGL_PDMA_BASE);
    /* 释放pdma的sem */
    rt_sem_release(lvgl_pdma_sem);

}
RTT_DECLARE_EXT_ISR_M(LVGL_PDMA_IRQ_NUM, lvgl_pdma_isr);

/* 适用于interset得到的重叠区域common_area，算common_area的左上角相较于父区域的相对偏移
 * area1：父区域，area2：重叠区域common_area
 *  */
static lv_point_t lv_area_get_offset(const lv_area_t * area1, const lv_area_t * area2)
{
    lv_point_t offset = {x: area2->x1 - area1->x1, y: area2->y1 - area1->y1};
    return offset;
}

/* pdma加速blend（图像混合）操作 */
void hpm_draw_blend(lv_draw_ctx_t * draw_ctx, const lv_draw_sw_blend_dsc_t * dsc)
{
    /* blend可能有好几种操作，可以看LV_BLEND_MODE_NORMAL
     * 定义的enum，但这里暂时先只处理LV_BLEND_MODE_NORMAL相关
     * 的部分，其他的后面有时间再做添加 */
    if(dsc->blend_mode != LV_BLEND_MODE_NORMAL) {
        lv_draw_sw_blend_basic(draw_ctx, dsc);
        return;
    }

    /* LV_BLEND_MODE_NORMAL只是做两图像重叠简单的混合 */
    /* 先获取两个图像共用的部分，也就是需要实际操作的部分 */
    lv_area_t draw_area;
    /* 没共用部分说明出错了 */
    if(!_lv_area_intersect(&draw_area, dsc->blend_area, draw_ctx->clip_area)) return;
    // + draw_ctx->buf_area has the entire draw buffer location
    // + draw_ctx->clip_area has the current draw buffer location
    // + dsc->blend_area has the location of the area intended to be painted - image etc.
    // + draw_area has the area actually being painted
    // All coordinates are relative to the screen.

    /* 获取要填的区域的信息 */
    lv_coord_t src_width = lv_area_get_width(&draw_area);
    lv_coord_t src_height = lv_area_get_height(&draw_area);

    /* pdma最小要操作8,小于8让sw去做 */
    if((src_height < 8) || (src_width < 8)){
        lv_draw_sw_blend_basic(draw_ctx, dsc);
        return;
    }
    /* 获取buf的stride */
    lv_coord_t dest_stride = lv_area_get_width(draw_ctx->buf_area);

    const lv_opa_t * mask = dsc->mask_buf;

    /* 蒙版 */
    if(mask != NULL) {
        /* 蒙版不知道咋整好，如果是画线，底层会一行一行的调用blend函数，要判断一下高度和宽度
         * 我看hpm的函数config的block_size是8*8和16*16，小于8的就直接调用软件了
         * 暂时蒙版不太熟悉，等后面更了解了一点，再试试能不能适配下蒙版
         *  */
        lv_draw_sw_blend_basic(draw_ctx, dsc);
        return;
    }
    /* 没有蒙版，简单绘制 */
    else {
        /* 获取颜色格式 */
#if LV_COLOR_DEPTH == 32
        display_pixel_format_t format = display_pixel_format_argb8888;
#else
        display_pixel_format_t format = display_pixel_format_rgb565;
#endif
        /* 纯色blend */
        if(dsc->src_buf == NULL) {
            /* fill颜色需要转成32位的 */
#if LV_COLOR_DEPTH == 32
            uint32_t color = ((dsc->color.ch.red) << 16) |
                    ((dsc->color.ch.green) << 8) |
                    ((dsc->color.ch.blue));
#else
            uint32_t color = (((dsc->color.ch.red) * 255 / 31) << 16) |
                    (((dsc->color.ch.green) * 255 / 63) << 8) |
                    ((dsc->color.ch.blue) * 255 / 31);
#endif

            /* 按理来说从绝对位置转换成draw_area相较于buf_area的相对位置 */
            /* 但是如果用全刷新模式的话，每次来的buf都是整个屏幕的缓冲，可以偷懒一下 */
//            lv_area_move(&draw_area, -draw_ctx->buf_area->x1,
//                         -draw_ctx->buf_area->y1);

            /* 算偏移 */
            void * dst = (void*)( ((uint32_t)draw_ctx->buf) + ((dest_stride * draw_area.y1) + draw_area.x1) * display_get_pixel_size_in_byte(format) );
            fill(dst, dest_stride, src_width, src_height,
                                color, dsc->opa, format);
        }
        /* 有图像数据 */
        else {
            blit(draw_ctx->buf, dest_stride,
                    dsc->src_buf, src_width,
                    draw_area.x1, draw_area.y1, format,
                    src_width, src_height, dsc->opa);
        }
    }
}

/* ctx初始化 */
void hpm_draw_ctx_init(lv_disp_drv_t * drv, lv_draw_ctx_t * draw_ctx)
{
    /* 因为只做几个函数的改动，就在sw的基础上就行了  */
    lv_draw_sw_init_ctx(drv, draw_ctx);

    /* hpm_draw_ctx_t类型就是lv_draw_sw_ctx_t */
    hpm_draw_ctx_t * my_draw_ctx = (hpm_draw_ctx_t *)draw_ctx;
    intc_m_enable_irq_with_priority(LVGL_PDMA_IRQ_NUM, LVGL_PDMA_IRQ_PRIORITY);

    /* 改几个渲染用的函数，用pdma */
    my_draw_ctx->blend = hpm_draw_blend;
    /* 补：没有很多适合pdma去做的函数，基本底层都会调用blend，主要需要修改的还是这个
     * lvgl有些图形绘制比较复杂，比如旋转某个角度，如果是90倍数与其
          *  改他的draw_transform，不如自己调sdk的函数，其他还是软件来干吧 */
}
/* ---------------------------lvgl draw ctx end----------------------------  */
#endif /* BSP_LVGL_USING_CUSTOM_DRAW_CTX */

static void lv_flush_display_full(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    struct lv_adapter *ctx = disp_drv->user_data;
    uint32_t wait_flush_buffer = core_local_mem_to_sys_address(RUNNING_CORE_INDEX, (uint32_t)color_p);

    /* Never to be run */
    if (ctx->wait_flush_buffer) {
        printf("Warning: discard %p lvgl buffer\n", color_p);
        return;
    }

#ifndef HPM_LVGL_FRAMEBUFFER_NONCACHEABLE
    if (l1c_dc_is_enabled()) {
       uint32_t aligned_start = HPM_L1C_CACHELINE_ALIGN_DOWN(wait_flush_buffer);
       uint32_t aligned_end = HPM_L1C_CACHELINE_ALIGN_UP(wait_flush_buffer + disp_drv->draw_buf->size * sizeof(lv_color_t));
       uint32_t aligned_size = aligned_end - aligned_start;
       l1c_dc_writeback(aligned_start, aligned_size);
    }
#endif
    ctx->wait_flush_buffer = wait_flush_buffer;
#ifdef BSP_USING_RTT_LCD_DRIVER
    g_lcd_dev->control(g_lcd_dev, RTGRAPHIC_CTRL_WAIT_VSYNC, RT_NULL);
    g_lcd_dev->control(g_lcd_dev, RTGRAPHIC_CTRL_RECT_UPDATE, (void *)wait_flush_buffer);
    g_lcd_dev->control(g_lcd_dev, RTGRAPHIC_CTRL_WAIT_VSYNC, RT_NULL);
    lv_adapter_ctx.wait_flush_buffer = 0;
    lv_disp_flush_ready(&lv_adapter_ctx.disp_drv);
#endif
}

#ifndef BSP_USING_RTT_LCD_DRIVER
static void hpm_lcdc_isr(void)
{
    volatile uint32_t s = lcdc_get_dma_status(LCD_CONTROLLER);
    lcdc_clear_dma_status(LCD_CONTROLLER, s);

    if ((lv_adapter_ctx.wait_flush_buffer) &&\
        lcdc_layer_control_shadow_loaded(LCD_CONTROLLER, LCD_LAYER_INDEX) &&\
        (s & (LCD_LAYER_DONE_MASK << LCDC_DMA_ST_DMA0_DONE_SHIFT))) {
        lcdc_layer_set_next_buffer(LCD_CONTROLLER, LCD_LAYER_INDEX, (uint32_t)lv_adapter_ctx.wait_flush_buffer);
        lv_adapter_ctx.wait_flush_buffer = 0;
        lv_disp_flush_ready(&lv_adapter_ctx.disp_drv);
    }
}
RTT_DECLARE_EXT_ISR_M(LCD_IRQ_NUM, hpm_lcdc_isr)
#endif

#endif/*LVGL_CONFIG_FLUSH_DIRECT_MODE_ENABLE*/

#ifndef BSP_USING_RTT_LCD_DRIVER
static void hpm_lcdc_init(void)
{
    display_pixel_format_t pixel_format;
    lcdc_config_t config = {0};
    lcdc_get_default_config(LCD_CONTROLLER, &config);
    board_panel_para_to_lcdc(&config);

#if LV_COLOR_DEPTH == 32
    pixel_format = display_pixel_format_argb8888;
#elif LV_COLOR_DEPTH == 16
    pixel_format = display_pixel_format_rgb565;
#else
#error only support 16 or 32 color depth
#endif

    lcdc_init(LCD_CONTROLLER, &config);

    memset(lcdc_framebuffer, 0, LV_LCD_WIDTH * LV_LCD_HEIGHT * sizeof(lv_color_t));
    lcdc_layer_config_t layer;
    lcdc_get_default_layer_config(LCD_CONTROLLER, &layer, pixel_format, LCD_LAYER_INDEX);

    layer.position_x = 0;
    layer.position_y = 0;
    layer.width = LV_LCD_WIDTH;
    layer.height = LV_LCD_HEIGHT;
    layer.buffer = (uint32_t)core_local_mem_to_sys_address(RUNNING_CORE_INDEX, (uint32_t)lcdc_framebuffer);
    layer.background.u = 0;

    if (status_success != lcdc_config_layer(LCD_CONTROLLER, LCD_LAYER_INDEX, &layer, true)) {
        printf("failed to configure layer\n");
        while (1) {
        }
    }

    lcdc_turn_on_display(LCD_CONTROLLER);
    lcdc_enable_interrupt(LCD_CONTROLLER, LCD_LAYER_DONE_MASK << 16);
    intc_m_enable_irq_with_priority(LCD_IRQ_NUM, HPM_LCD_IRQ_PRIORITY);
    lv_adapter_ctx.lcdc_buffer = layer.buffer;
}
#endif

static void lv_disp_init(void)
{
    lv_disp_draw_buf_t *draw_buf = &lv_adapter_ctx.draw_buf;
    lv_disp_drv_t *disp_drv = &lv_adapter_ctx.disp_drv;
#ifndef BSP_USING_RTT_LCD_DRIVER
    board_init_lcd();

    hpm_lcdc_init();
#else
    struct rt_device_graphic_info info;
    g_lcd_dev = rt_device_find("lcd0");
    if (!g_lcd_dev)
    {
        return;
    }
    info.bits_per_pixel = LV_COLOR_DEPTH;
    info.height = LV_LCD_HEIGHT;
    info.width = LV_LCD_WIDTH;
#if LV_COLOR_DEPTH == 32
    info.pixel_format  = RTGRAPHIC_PIXEL_FORMAT_ARGB888;
#else
    info.pixel_format  = RTGRAPHIC_PIXEL_FORMAT_RGB565;
#endif
    // g_lcd_dev->control(g_lcd_dev, RTGRAPHIC_CTRL_SET_MODE, &info);
    g_lcd_dev->control(g_lcd_dev, RTGRAPHIC_CTRL_GET_INFO, &info);
    lv_adapter_ctx.lcdc_buffer = (uint32_t)info.framebuffer;
#endif
    lv_disp_draw_buf_init(draw_buf, lv_framebuffer0, lv_framebuffer1, LV_LCD_WIDTH * LV_LCD_HEIGHT);
    /* 里面还设置了渲染用的方式软件，硬件加速 */
    lv_disp_drv_init(disp_drv);

    disp_drv->hor_res = LV_LCD_WIDTH;
    disp_drv->ver_res = LV_LCD_HEIGHT;
    disp_drv->draw_buf = draw_buf;

#if LVGL_CONFIG_FLUSH_DIRECT_MODE_ENABLE
    disp_drv->direct_mode = 1;
    disp_drv->flush_cb = lv_flush_display_direct;
    lvgl_pdma_init(&lv_adapter_ctx);
#else
    disp_drv->full_refresh = 1;
    disp_drv->flush_cb = lv_flush_display_full;

#if BSP_LVGL_USING_CUSTOM_DRAW_CTX
    /* 下面都是新增的，lvgl的gpu加速相关的 */
    /* pdma的sem */
    lvgl_pdma_sem = rt_sem_create("pdma_sem", 1, RT_IPC_FLAG_PRIO);
    if (lvgl_pdma_sem == RT_NULL)
    {
        rt_kprintf("create dynamic semaphore pdma_sem failed.\n");
    }
    clock_add_to_group(LVGL_PDMA_CLOCK, BOARD_RUNNING_CORE);
    /* 在全刷新模式额外改一下渲染的函数，在sw渲染的基础上 */
    disp_drv->draw_ctx_init = hpm_draw_ctx_init;
    disp_drv->draw_ctx_size = sizeof(hpm_draw_ctx_t);
#endif

#endif

    disp_drv->user_data = &lv_adapter_ctx;

    /* 里面还初始化了渲染用的ctx相关的东西 */
    lv_disp_drv_register(disp_drv);
}


void lv_port_disp_init(void)
{
    lv_disp_init();
}

