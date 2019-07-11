/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 *
 * @author Rama, Meka(v.meka@samsung.com)
           Sangwoo, Park(sw5771.park@samsung.com)
           Jamie Oh (jung-min.oh@samsung.com)
 * @date   2011-07-28
 *
 */

#define HWC_REMOVE_DEPRECATED_VERSIONS 1

#include <sys/resource.h>
#include <cutils/log.h>
#include <cutils/atomic.h>
#include <EGL/egl.h>
#include <GLES/gl.h>
#include <hardware_legacy/uevent.h>
#include "SecHWCUtils.h"

static IMG_gralloc_module_public_t *gpsGrallocModule;

static int hwc_device_open(const struct hw_module_t* module, const char* name,
                           struct hw_device_t** device);

static struct hw_module_methods_t hwc_module_methods = {
    open: hwc_device_open
};

hwc_module_t HAL_MODULE_INFO_SYM = {
    common: {
        tag: HARDWARE_MODULE_TAG,
        module_api_version: HWC_MODULE_API_VERSION_0_1,
        hal_api_version: HARDWARE_HAL_API_VERSION,
        id: HWC_HARDWARE_MODULE_ID,
        name: "Samsung S5PC11X hwcomposer module",
        author: "SAMSUNG",
        methods: &hwc_module_methods,
    }
};

static void dump_layer(hwc_layer_1_t const* l) {
    ALOGD("\ttype=%d, flags=%08x, handle=%p, tr=%02x, blend=%04x, {%d,%d,%d,%d}, {%d,%d,%d,%d}",
            l->compositionType, l->flags, l->handle, l->transform, l->blending,
            l->sourceCrop.left,
            l->sourceCrop.top,
            l->sourceCrop.right,
            l->sourceCrop.bottom,
            l->displayFrame.left,
            l->displayFrame.top,
            l->displayFrame.right,
            l->displayFrame.bottom);
}

static void reset_win_rect_info(hwc_win_info_t *win)
{
    win->rect_info.x = 0;
    win->rect_info.y = 0;
    win->rect_info.w = 0;
    win->rect_info.h = 0;
    return;
}

static int hwc_prepare(hwc_composer_device_1_t *dev,
                       size_t numDisplays, hwc_display_contents_1_t** displays)
{

    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    int overlay_win_cnt = 0;
    int compositionType = 0;
    int ret;

    // Compat
    hwc_display_contents_1_t* list = NULL;
    if (numDisplays > 0) {
        list = displays[0];
    }

    //if geometry is not changed, there is no need to do any work here
    if( !list || (!(list->flags & HWC_GEOMETRY_CHANGED)))
        return 0;

    //all the windows are free here....
    for (int i = 0; i < NUM_OF_WIN; i++) {
        ctx->win[i].status = HWC_WIN_FREE;
        ctx->win[i].buf_index = 0;
    }
    ctx->num_of_hwc_layer = 0;
    ctx->num_of_fb_layer = 0;
    ALOGV("%s:: hwc_prepare list->numHwLayers %d", __func__, list->numHwLayers);

    for (int i = 0; i < list->numHwLayers ; i++) {
        hwc_layer_1_t* cur = &list->hwLayers[i];

        if (overlay_win_cnt < NUM_OF_WIN) {
            cur->compositionType = HWC_FRAMEBUFFER;
            ctx->num_of_fb_layer++;
        }
    }

    if(list->numHwLayers != (ctx->num_of_fb_layer + ctx->num_of_hwc_layer))
        ALOGV("%s:: numHwLayers %d num_of_fb_layer %d num_of_hwc_layer %d ",
                __func__, list->numHwLayers, ctx->num_of_fb_layer,
                ctx->num_of_hwc_layer);

    if (overlay_win_cnt < NUM_OF_WIN) {
        //turn off the free windows
        for (int i = overlay_win_cnt; i < NUM_OF_WIN; i++) {
            window_hide(&ctx->win[i]);
            reset_win_rect_info(&ctx->win[i]);
        }
    }
    return 0;
}

static int hwc_set(hwc_composer_device_1_t *dev,
                   size_t numDisplays, hwc_display_contents_1_t** displays)
{
    struct hwc_context_t *ctx = (struct hwc_context_t *)dev;
    unsigned int phyAddr[MAX_NUM_PLANES];
    int skipped_window_mask = 0;
    hwc_layer_1_t* cur;
    struct hwc_win_info_t *win;
    int ret;
    struct sec_img src_img;
    struct sec_img dst_img;
    struct sec_rect src_rect;
    struct sec_rect dst_rect;

    // Only support one display
    hwc_display_t dpy = displays[0]->dpy;
    hwc_surface_t sur = displays[0]->sur;
    hwc_display_contents_1_t* list = displays[0];

    if (dpy == NULL && sur == NULL && list == NULL) {
        // release our resources, the screen is turning off
        // in our case, there is nothing to do.
        ctx->num_of_fb_layer_prev = 0;
        return 0;
    }

    bool need_swap_buffers = ctx->num_of_fb_layer > 0;

    /*
     * H/W composer documentation states:
     * There is an implicit layer containing opaque black
     * pixels behind all the layers in the list.
     * It is the responsibility of the hwcomposer module to make
     * sure black pixels are output (or blended from).
     *
     * Since we're using a blitter, we need to erase the frame-buffer when
     * switching to all-overlay mode.
     *
     */
    if (ctx->num_of_hwc_layer &&
        ctx->num_of_fb_layer==0 && ctx->num_of_fb_layer_prev) {
        /* we're clearing the screen using GLES here, this is very
         * hack-ish, ideal we would use the fimc (if it can do it) */
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);
        need_swap_buffers = true;
    }

    ctx->num_of_fb_layer_prev = ctx->num_of_fb_layer;

    if (need_swap_buffers || !list) {
        EGLBoolean sucess = eglSwapBuffers((EGLDisplay)dpy, (EGLSurface)sur);
        if (!sucess) {
            return HWC_EGL_ERROR;
        }
    }

    if (!list) {
        /* turn off the all windows */
        for (int i = 0; i < NUM_OF_WIN; i++) {
            window_hide(&ctx->win[i]);
            reset_win_rect_info(&ctx->win[i]);
            ctx->win[i].status = HWC_WIN_FREE;
        }
        ctx->num_of_hwc_layer = 0;
        return 0;
    }

    if(ctx->num_of_hwc_layer > NUM_OF_WIN)
        ctx->num_of_hwc_layer = NUM_OF_WIN;

    /* compose hardware layers here */
    for (uint32_t i = 0; i < ctx->num_of_hwc_layer; i++) {
        win = &ctx->win[i];
        if (win->status == HWC_WIN_RESERVED) {
            cur = &list->hwLayers[win->layer_index];

            if (cur->compositionType == HWC_OVERLAY) {
		ALOGE("%s:: error : HWC_OVERLAY type was set but isn't supported");
            } else {
                ALOGE("%s:: error : layer %d compositionType should have been \
                        HWC_OVERLAY", __func__, win->layer_index);
                skipped_window_mask |= (1 << i);
                continue;
            }
         } else {
             ALOGE("%s:: error : window status should have been HWC_WIN_RESERVED \
                     by now... ", __func__);
             skipped_window_mask |= (1 << i);
             continue;
         }
    }

    if (skipped_window_mask) {
        //turn off the free windows
        for (int i = 0; i < NUM_OF_WIN; i++) {
            if (skipped_window_mask & (1 << i))
                window_hide(&ctx->win[i]);
        }
    }

#if defined(BOARD_USES_HDMI)
    hdmi_device_t* hdmi = ctx->hdmi;
    if (ctx->num_of_hwc_layer == 1 && hdmi) {
        if ((src_img.format == HAL_PIXEL_FORMAT_CUSTOM_YCbCr_420_SP_TILED)||
                (src_img.format == HAL_PIXEL_FORMAT_CUSTOM_YCrCb_420_SP)) {
            ADDRS * addr = (ADDRS *)(src_img.base);
            hdmi->blit(hdmi,
                       src_img.w,
                       src_img.h,
                       src_img.format,
                       (unsigned int)addr->addr_y,
                       (unsigned int)addr->addr_cbcr,
                       (unsigned int)addr->addr_cbcr,
                       0, 0,
                       HDMI_MODE_VIDEO,
                       ctx->num_of_hwc_layer);
        } else if ((src_img.format == HAL_PIXEL_FORMAT_YCbCr_420_SP) ||
                    (src_img.format == HAL_PIXEL_FORMAT_YCrCb_420_SP) ||
                    (src_img.format == HAL_PIXEL_FORMAT_YCbCr_420_P) ||
                    (src_img.format == HAL_PIXEL_FORMAT_YV12)) {
            hdmi->blit(hdmi,
                       src_img.w,
                       src_img.h,
                       src_img.format,
                       (unsigned int)ctx->fimc.params.src.buf_addr_phy_rgb_y,
                       (unsigned int)ctx->fimc.params.src.buf_addr_phy_cb,
                       (unsigned int)ctx->fimc.params.src.buf_addr_phy_cr,
                       0, 0,
                       HDMI_MODE_VIDEO,
                       ctx->num_of_hwc_layer);
        } else {
            ALOGE("%s: Unsupported format = %d for hdmi", __func__, src_img.format);
        }
    }
#endif

    return 0;
}

static void hwc_registerProcs(struct hwc_composer_device_1* dev,
        hwc_procs_t const* procs)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    ctx->procs = const_cast<hwc_procs_t *>(procs);
}

static int hwc_blank(struct hwc_composer_device_1 *dev,
        int disp, int blank)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    if (blank) {
        // release our resources, the screen is turning off
        // in our case, there is nothing to do.
        ctx->num_of_fb_layer_prev = 0;
        return 0;
    }
    else {
        // No need to unblank, will unblank on set()
        return 0;
    }
}

static int hwc_query(struct hwc_composer_device_1* dev,
        int what, int* value)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;

    switch (what) {
    case HWC_BACKGROUND_LAYER_SUPPORTED:
        // we don't support the background layer yet
        value[0] = 0;
        break;
    case HWC_VSYNC_PERIOD:
        // vsync period in nanosecond
        value[0] = 1000000000.0 / gpsGrallocModule->psFrameBufferDevice->base.fps;
        break;
    default:
        // unsupported query
        return -EINVAL;
    }
    return 0;
}

#ifdef VSYNC_IOCTL
// Linux version of a manual reset event to control when
// and when not to ask the video card for a VSYNC.  This
// stops the worker thread from asking for a VSYNC when
// there is nothing useful to do with it and more closely
// mimicks the original uevent mechanism
int vsync_enable = 0;
pthread_mutex_t vsync_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t vsync_condition = PTHREAD_COND_INITIALIZER;
#endif

static int hwc_eventControl(struct hwc_composer_device_1* dev, int dpy,
        int event, int enabled)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;

    switch (event) {
    case HWC_EVENT_VSYNC:
        int val = !!enabled;
        int err = ioctl(ctx->global_lcd_win.fd, S3CFB_SET_VSYNC_INT, &val);
        if (err < 0)
            return -errno;

#if VSYNC_IOCTL
        // Enable or disable the ability for the worker thread
        // to ask for VSYNC events from the video driver
        pthread_mutex_lock(&vsync_mutex);
        if(enabled) {
            vsync_enable = 1;
            pthread_cond_broadcast(&vsync_condition);
        }
        else vsync_enable = 0;
        pthread_mutex_unlock(&vsync_mutex);
#endif

        return 0;
    }

    return -EINVAL;
}

void handle_vsync_uevent(hwc_context_t *ctx, const char *buff, int len)
{
    uint64_t timestamp = 0;
    const char *s = buff;

    if(!ctx->procs || !ctx->procs->vsync)
       return;

    s += strlen(s) + 1;

    while(*s) {
        if (!strncmp(s, "VSYNC=", strlen("VSYNC=")))
            timestamp = strtoull(s + strlen("VSYNC="), NULL, 0);

        s += strlen(s) + 1;
        if (s - buff >= len)
            break;
    }

    ctx->procs->vsync(ctx->procs, 0, timestamp);
}

static void *hwc_vsync_thread(void *data)
{
    hwc_context_t *ctx = (hwc_context_t *)(data);
#ifdef VSYNC_IOCTL
    uint64_t timestamp = 0;
#else
    char uevent_desc[4096];
    memset(uevent_desc, 0, sizeof(uevent_desc));
#endif

    setpriority(PRIO_PROCESS, 0, HAL_PRIORITY_URGENT_DISPLAY);

#ifndef VSYNC_IOCTL
    uevent_init();
#endif
    while(true) {
#ifdef VSYNC_IOCTL
        // Only continue if hwc_eventControl is enabled, otherwise
        // just sit here and wait until it is.  This stops the code
        // from constantly looking for the VSYNC event with the screen
        // turned off.
        pthread_mutex_lock(&vsync_mutex);
        if(!vsync_enable) pthread_cond_wait(&vsync_condition, &vsync_mutex);
        pthread_mutex_unlock(&vsync_mutex);

        timestamp = 0;          // Reset the timestamp value

        // S3CFB_WAIT_FOR_VSYNC is a custom IOCTL I added to wait for
        // the VSYNC interrupt, and then return the timestamp that was
        // originally being communicated via a uevent.  The uevent was
        // spamming the UEventObserver and events/0 process with more
        // information than this device could really deal with every 18ms
        int res = ioctl(ctx->global_lcd_win.fd, S3CFB_WAIT_FOR_VSYNC, &timestamp);
        if(res > 0) {
            if(!ctx->procs || !ctx->procs->vsync) continue;
            ctx->procs->vsync(ctx->procs, 0, timestamp);
        }
#else
        int len = uevent_next_event(uevent_desc, sizeof(uevent_desc) - 2);

        bool vsync = !strcmp(uevent_desc, "change@/devices/platform/s3cfb");
        if(vsync)
            handle_vsync_uevent(ctx, uevent_desc, len);
#endif
    }

    return NULL;
}

static int hwc_device_close(struct hw_device_t *dev)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    int ret = 0;
    int i;

    if (ctx) {
        if (window_close(&ctx->global_lcd_win) < 0) {
            ALOGE("%s::window_close() fail", __func__);
            ret = -1;
        }

        for (i = 0; i < NUM_OF_WIN; i++) {
            if (window_close(&ctx->win[i]) < 0) {
                ALOGE("%s::window_close() fail", __func__);
                ret = -1;
            }
        }

        // TODO: stop vsync_thread

        free(ctx);
    }
    return ret;
}

static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device)
{
    int status = 0;
    int err;
    struct hwc_win_info_t *win;
#if defined(BOARD_USES_HDMI)
    struct hw_module_t    *hdmi_module;
#endif

    if(hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
                (const hw_module_t**)&gpsGrallocModule))
        return -EINVAL;

    if(strcmp(gpsGrallocModule->base.common.author, "Imagination Technologies"))
        return -EINVAL;

    if (strcmp(name, HWC_HARDWARE_COMPOSER))
        return -EINVAL;

    struct hwc_context_t *dev;
    dev = (hwc_context_t*)malloc(sizeof(*dev));

    /* initialize our state here */
    memset(dev, 0, sizeof(*dev));

    /* initialize the procs */
    dev->device.common.tag = HARDWARE_DEVICE_TAG;
    dev->device.common.version = HWC_DEVICE_API_VERSION_1_0;
    dev->device.common.module = const_cast<hw_module_t*>(module);
    dev->device.common.close = hwc_device_close;

    dev->device.prepare = hwc_prepare;
    dev->device.set = hwc_set;
    dev->device.eventControl = hwc_eventControl;
    dev->device.blank = hwc_blank;
    dev->device.query = hwc_query;
    dev->device.registerProcs = hwc_registerProcs;

    *device = &dev->device.common;

#if defined(BOARD_USES_HDMI)
    dev->hdmi = NULL;
    if(hw_get_module(HDMI_HARDWARE_MODULE_ID,
                (const hw_module_t**)&hdmi_module)) {
        ALOGE("%s:: HDMI device not present", __func__);
    } else {
        int ret = module->methods->open(hdmi_module, "hdmi-composer",
                (hw_device_t **)&dev->hdmi);
        if(ret < 0) {
            ALOGE("%s:: Failed to open hdmi device : %s", __func__, strerror(ret));
        }
    }
#endif

    /* initializing */
    memset(&(dev->fimc), 0, sizeof(s5p_fimc_t));
    dev->fimc.dev_fd = -1;

    /* open WIN0 & WIN1 here */
    for (int i = 0; i < NUM_OF_WIN; i++) {
        if (window_open(&(dev->win[i]), i) < 0) {
             ALOGE("%s:: Failed to open window %d device ", __func__, i);
             status = -EINVAL;
             goto err;
        }
    }

    /* open window 2, used to query global LCD info */
    if (window_open(&dev->global_lcd_win, 2) < 0) {
        ALOGE("%s:: Failed to open window 2 device ", __func__);
        status = -EINVAL;
        goto err;
    }

    /* get default window config */
    if (window_get_global_lcd_info(dev) < 0) {
        ALOGE("%s::window_get_global_lcd_info is failed : %s",
                __func__, strerror(errno));
        status = -EINVAL;
        goto err;
    }

    dev->lcd_info.yres_virtual = dev->lcd_info.yres * NUM_OF_WIN_BUF;

    /* initialize the window context */
    for (int i = 0; i < NUM_OF_WIN; i++) {
        win = &dev->win[i];
        memcpy(&win->lcd_info, &dev->lcd_info, sizeof(struct fb_var_screeninfo));
        memcpy(&win->var_info, &dev->lcd_info, sizeof(struct fb_var_screeninfo));

        win->rect_info.x = 0;
        win->rect_info.y = 0;
        win->rect_info.w = win->var_info.xres;
        win->rect_info.h = win->var_info.yres;

        if (window_set_pos(win) < 0) {
            ALOGE("%s::window_set_pos is failed : %s",
                    __func__, strerror(errno));
            status = -EINVAL;
            goto err;
        }

        if (window_get_info(win) < 0) {
            ALOGE("%s::window_get_info is failed : %s",
                    __func__, strerror(errno));
            status = -EINVAL;
            goto err;
        }

        win->size = win->fix_info.line_length * win->var_info.yres;

        if (!win->fix_info.smem_start){
            ALOGE("%s:: win-%d failed to get the reserved memory", __func__, i);
            status = -EINVAL;
            goto err;
        }

        for (int j = 0; j < NUM_OF_WIN_BUF; j++) {
            win->addr[j] = win->fix_info.smem_start + (win->size * j);
            ALOGI("%s::win-%d add[%d] %x ", __func__, i, j, win->addr[j]);
        }
    }

    err = pthread_create(&dev->vsync_thread, NULL, hwc_vsync_thread, dev);
    if (err) {
        ALOGE("%s::pthread_create() failed : %s", __func__, strerror(err));
        status = -err;
        goto err;
    }

    ALOGD("%s:: success\n", __func__);

    return 0;

err:
    if (window_close(&dev->global_lcd_win) < 0)
        ALOGE("%s::window_close() fail", __func__);

    for (int i = 0; i < NUM_OF_WIN; i++) {
        if (window_close(&dev->win[i]) < 0)
            ALOGE("%s::window_close() fail", __func__);
    }

    return status;
}
