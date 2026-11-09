#if defined(__arm__) && !defined(__ARM_PCS_VFP)

#include "v4_hal.h"

v4_isp_alg      v4_ae_lib = { .id = 0, .libName = "ae_lib" };
v4_aud_impl     v4_aud;
v4_isp_alg      v4_awb_lib = { .id = 0, .libName = "awb_lib" };
v4_config_impl  v4_config;
v4_isp_impl     v4_isp;
v4_snr_drv_impl v4_snr_drv;
v4_rgn_impl     v4_rgn;
v4_sys_impl     v4_sys;
v4_vb_impl      v4_vb;
v4_venc_impl    v4_venc;
v4_vi_impl      v4_vi;
v4_vpss_impl    v4_vpss;

hal_chnstate v4_state[V4_VENC_CHN_NUM] = {0};
int (*v4_aud_cb)(hal_audframe*);
int (*v4_vid_cb)(char, hal_vidstream*);

char _v4_aud_chn = 0;
char _v4_aud_dev = 0;
char _v4_isp_chn = 0;
char _v4_isp_dev = 0;
char _v4_venc_dev = 0;
char _v4_vi_chn = 0;
char _v4_vi_dev = 0;
char _v4_vi_pipe = 0;
char _v4_vpss_chn = 0;
char _v4_vpss_grp = 0;

/* Extra video buffer pools for scaled channels (sub stream, JPEG/MJPEG) */
#define V4_VB_EXTRA_MAX 4
static struct {
    short width, height;
    unsigned int count;
} _v4_vb_extra[V4_VB_EXTRA_MAX];
static int _v4_vb_extra_num = 0;

/* Attributes of each VPSS channel, to tell capture-sized channels (the
   encoder scales) from scaled ones (VPSS already scaled) and to resize
   the latter for a crop */
static v4_vpss_chn _v4_vpss_attr[V4_VENC_CHN_NUM];

void v4_hal_deinit(void)
{
    v4_vpss_unload(&v4_vpss);
    v4_vi_unload(&v4_vi);
    v4_venc_unload(&v4_venc);
    v4_vb_unload(&v4_vb);
    v4_rgn_unload(&v4_rgn);
    v4_isp_unload(&v4_isp);
    v4_aud_unload(&v4_aud);
    v4_sys_unload(&v4_sys);
}

int v4_hal_init(void)
{
    int ret;

    if (ret = v4_sys_load(&v4_sys))
        return ret;
    if (ret = v4_aud_load(&v4_aud))
        return ret;
    if (ret = v4_isp_load(&v4_isp))
        return ret;
    if (ret = v4_rgn_load(&v4_rgn))
        return ret;
    if (ret = v4_vb_load(&v4_vb))
        return ret;
    if (ret = v4_venc_load(&v4_venc))
        return ret;
    if (ret = v4_vi_load(&v4_vi))
        return ret;
    if (ret = v4_vpss_load(&v4_vpss))
        return ret;

    return EXIT_SUCCESS;
}

void v4_audio_deinit(void)
{
    v4_aud.fnDisableChannel(_v4_aud_dev, _v4_aud_chn);

    v4_aud.fnDisableDevice(_v4_aud_dev);
}

int v4_audio_init(int samplerate)
{
    int ret;

    {
        v4_aud_cnf config;
        config.rate = samplerate;
        config.bit = V4_AUD_BIT_16;
        config.intf = V4_AUD_INTF_I2S_MASTER;
        config.stereoOn = 0;
        config.expandOn = 0;
        config.frmNum = 30;
        config.packNumPerFrm = 320;
        config.chnNum = 1;
        config.syncRxClkOn = 0;
        config.i2sType = V4_AUD_I2ST_INNERCODEC;
        if (ret = v4_aud.fnSetDeviceConfig(_v4_aud_dev, &config))
            return ret;
    }
    if (ret = v4_aud.fnEnableDevice(_v4_aud_dev))
        return ret;

    if (ret = v4_aud.fnEnableChannel(_v4_aud_dev, _v4_aud_chn))
        return ret;

    return EXIT_SUCCESS;
}

void *v4_audio_thread(void)
{
    int ret;

    v4_aud_frm frame;
    v4_aud_efrm echoFrame;
    memset(&frame, 0, sizeof(frame));
    memset(&echoFrame, 0, sizeof(echoFrame));

    while (keepRunning && audioOn) {
        while ((ret = v4_aud.fnGetFrame(_v4_aud_dev, _v4_aud_chn,
            &frame, &echoFrame, 0)) == 0) {
            if (v4_aud_cb) {
                hal_audframe outFrame;
                outFrame.channelCnt = 1;
                outFrame.data[0] = frame.addr[0];
                outFrame.length[0] = frame.length;
                outFrame.seq = frame.sequence;
                outFrame.timestamp = frame.timestamp;
                (v4_aud_cb)(&outFrame);
            }

            if (ret = v4_aud.fnFreeFrame(_v4_aud_dev, _v4_aud_chn,
                &frame, &echoFrame)) {
                HAL_WARNING("v4_aud", "Releasing the frame failed"
                    " with %#x!\n", ret);
            }
        }

        usleep(5000);
    }
    fprintf(stderr, "[v4_aud] Shutting down capture thread...\n");
}

int v4_channel_bind(char index)
{
    int ret;

    if (ret = v4_vpss.fnEnableChannel(_v4_vpss_grp, index))
        return ret;

    {
        v4_sys_bind source = { .module = V4_SYS_MOD_VPSS,
            .device = _v4_vpss_grp, .channel = index };
        v4_sys_bind dest = { .module = V4_SYS_MOD_VENC,
            .device = _v4_venc_dev, .channel = index };
        if (ret = v4_sys.fnBind(&source, &dest))
            return ret;
    }

    return EXIT_SUCCESS;
}

int v4_channel_create(char index, char mirror, char flip, char framerate)
{
    return v4_channel_create_scaled(index, v4_config.isp.capt.width,
        v4_config.isp.capt.height, mirror, flip, framerate);
}

/* VPSS channel scaling the capture to the given size, e.g. for a sub stream
 * whose encoder then gets frames of exactly its own size */
int v4_channel_create_scaled(char index, short width, short height,
    char mirror, char flip, char framerate)
{
    int ret;

    {
        v4_vpss_chn channel;
        memset(&channel, 0, sizeof(channel));
        channel.dest.width = width;
        channel.dest.height = height;
        channel.pixFmt = V4_PIXFMT_YVU420SP;
        channel.hdr = V4_HDR_SDR8;
        channel.srcFps = v4_config.isp.framerate;
        channel.dstFps = framerate;
        channel.mirror = mirror;
        channel.flip = flip;
        if (ret = v4_vpss.fnSetChannelConfig(_v4_vpss_grp, index, &channel))
            return ret;
        if (index >= 0 && index < V4_VENC_CHN_NUM)
            _v4_vpss_attr[(int)index] = channel;
    }

    /* A crop left from a previous user of this channel must go, an encoder
       that needs one sets it again in v4_video_create */
    if (v4_vpss.fnSetChannelCrop) {
        /* The SDK checks the rect even when disabling */
        v4_vpss_crop crop = { .enable = 0, .coord = V4_VPSS_CROPCOORD_ABS,
            .rect = { .x = 0, .y = 0, .width = width, .height = height } };
        if (ret = v4_vpss.fnSetChannelCrop(_v4_vpss_grp, index, &crop))
            HAL_WARNING("v4_vpss", "Clearing the crop of channel %d failed "
                "with %#x!\n", index, ret);
    }

    return EXIT_SUCCESS;
}

int v4_channel_grayscale(char enable)
{
    int ret, result = EXIT_SUCCESS;

    /* Keep going on errors so one odd channel does not leave the
       following ones (e.g. a sub stream) in colour */
    for (char i = 0; i < V4_VENC_CHN_NUM; i++) {
        v4_venc_para param;
        if (!v4_state[i].enable) continue;
        if (ret = v4_venc.fnGetChannelParam(i, &param)) {
            result = ret;
            continue;
        }
        param.grayscaleOn = enable;
        if (ret = v4_venc.fnSetChannelParam(i, &param))
            result = ret;
    }

    return result;
}

int v4_channel_unbind(char index)
{
    int ret;

    if (ret = v4_vpss.fnDisableChannel(_v4_vpss_grp, index))
        return ret;

    {
        v4_sys_bind source = { .module = V4_SYS_MOD_VPSS,
            .device = _v4_vpss_grp, .channel = index };
        v4_sys_bind dest = { .module = V4_SYS_MOD_VENC,
            .device = _v4_venc_dev, .channel = index };
        if (ret = v4_sys.fnUnbind(&source, &dest))
            return ret;
    }

    return EXIT_SUCCESS;
}

/* Current analog gain (22.10, 1024 = 1x) and average luma (0-255) from
 * the auto exposure; fails when the library or the ISP cannot tell */
int v4_isp_exposure(unsigned int *again, unsigned int *aveLum)
{
    /* About 5.5 KB, kept off the callers' small thread stacks */
    static v4_isp_exp info;
    static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    int ret;

    if (!v4_isp.fnQueryExposureInfo)
        return EXIT_FAILURE;

    pthread_mutex_lock(&mutex);
    if (!(ret = v4_isp.fnQueryExposureInfo(_v4_vi_pipe, &info))) {
        *again = info.again;
        *aveLum = info.aveLum;
    }
    pthread_mutex_unlock(&mutex);

    return ret;
}

void *v4_image_thread(void)
{
    int ret;

    if (ret = v4_isp.fnRun(_v4_isp_dev))
        HAL_DANGER("v4_isp", "Operation failed with %#x!\n", ret);
    HAL_INFO("v4_isp", "Shutting down ISP thread...\n");
}

int v4_pipeline_create(void)
{
    int ret;

    {
        v4_sys_oper mode[4];
        v4_sys.fnGetViVpssMode((v4_sys_oper*)mode);
        for (char i = 0; i < 4; i++)
            mode[i] = V4_SYS_OPER_VIOFF_VPSSON;
        if (ret = v4_sys.fnSetViVpssMode((v4_sys_oper*)mode))
            return ret;
    }

    if (ret = v4_sensor_config())
        return ret;

    if (ret = v4_vi.fnSetDeviceConfig(_v4_vi_dev, &v4_config.videv))
        return ret;
    if (ret = v4_vi.fnEnableDevice(_v4_vi_dev))
        return ret;

    {
        v4_vi_bind bind;
        bind.num = 1;
        bind.pipeId[0] = _v4_vi_pipe;
        if (ret = v4_vi.fnBindPipe(_v4_vi_dev, &bind))
            return ret;
    }

    {
        v4_vi_pipe pipe;
        pipe.bypass = 0;
        pipe.yuvSkipOn = 0;
        pipe.ispBypassOn = 0;
        pipe.maxSize.width = v4_config.isp.capt.width;
        pipe.maxSize.height = v4_config.isp.capt.height;
        pipe.pixFmt = V4_PIXFMT_RGB_BAYER_8BPP + v4_config.mipi.prec;
        pipe.compress = V4_COMPR_NONE;
        pipe.prec = v4_config.mipi.prec;
        pipe.nRedOn = 0;
        pipe.nRed.pixFmt = 0;
        pipe.nRed.prec = 0;
        pipe.nRed.srcRfrOrChn0 = 0;
        pipe.nRed.compress = V4_COMPR_NONE;
        pipe.sharpenOn = 1;
        pipe.srcFps = -1;
        pipe.dstFps = -1;
        pipe.discProPic = 0;
        if (ret = v4_vi.fnCreatePipe(_v4_vi_pipe, &pipe))
            return ret;
    }
    if (ret = v4_vi.fnStartPipe(_v4_vi_pipe))
        return ret;

    {
        v4_vi_chn channel;
        channel.size.width = v4_config.isp.capt.width;
        channel.size.height = v4_config.isp.capt.height;
        channel.pixFmt = V4_PIXFMT_YVU420SP;
        channel.dynRange = V4_HDR_SDR8;
        channel.videoFmt = 0;
        channel.compress = V4_COMPR_NONE;
        channel.mirror = 0;
        channel.flip = 0;
        channel.depth = 0;
        channel.srcFps = -1;
        channel.dstFps = -1;
        if (ret = v4_vi.fnSetChannelConfig(_v4_vi_pipe, _v4_vi_chn, &channel))
            return ret;
    }
    if (ret = v4_vi.fnEnableChannel(_v4_vi_pipe, _v4_vi_chn))
        return ret;

    {
        v4_snr_bus bus;
        bus.i2c = 0;
        if (ret = v4_snr_drv.obj->pfnSetBusInfo(_v4_vi_pipe, bus))
            return ret;
    }

    if (!v4_isp.handleGoke) {
        strcpy(v4_ae_lib.libName, "hisi_ae_lib");
        strcpy(v4_awb_lib.libName, "hisi_awb_lib");
    }

    if (ret = v4_snr_drv.obj->pfnRegisterCallback(_v4_vi_pipe, &v4_ae_lib, &v4_awb_lib))
        return ret;

    if (ret = v4_isp.fnRegisterAE(_v4_vi_pipe, &v4_ae_lib))
        return ret;
    if (ret = v4_isp.fnRegisterAWB(_v4_vi_pipe, &v4_awb_lib))
        return ret;
    if (ret = v4_isp.fnMemInit(_v4_vi_pipe))
        return ret;

    v4_config.isp.capt.x = 0;
    v4_config.isp.capt.y = 0;
    if (ret = v4_isp.fnSetDeviceConfig(_v4_vi_pipe, &v4_config.isp))
        return ret;
    if (ret = v4_isp.fnInit(_v4_vi_pipe))
        return ret;

    {
        v4_vpss_grp group;
        memset(&group, 0, sizeof(group));
        group.dest.width = v4_config.isp.capt.width;
        group.dest.height = v4_config.isp.capt.height;
        group.pixFmt = V4_PIXFMT_YVU420SP;
        group.hdr = V4_HDR_SDR8;
        group.srcFps = -1;
        group.dstFps = -1;
        group.nRedOn = 1;
        group.nRed.mode = V4_VPSS_NMODE_VIDEO;
        group.nRed.compress = V4_COMPR_NONE;
        group.nRed.motionCompOn = 0;
        if (ret = v4_vpss.fnCreateGroup(_v4_vpss_grp, &group))
            return ret;
    }
    if (ret = v4_vpss.fnStartGroup(_v4_vpss_grp))
        return ret;

    {
        v4_sys_bind source = { .module = V4_SYS_MOD_VIU,
            .device = _v4_vi_dev, .channel = _v4_vi_chn };
        v4_sys_bind dest = { .module = V4_SYS_MOD_VPSS,
            .device = _v4_vpss_grp, .channel = 0 };
        if (ret = v4_sys.fnBind(&source, &dest))
            return ret;
    }

    return EXIT_SUCCESS;
}

void v4_pipeline_destroy(void)
{
    v4_isp.fnExit(_v4_vi_pipe);
    v4_isp.fnUnregisterAWB(_v4_vi_pipe, &v4_awb_lib);
    v4_isp.fnUnregisterAE(_v4_vi_pipe, &v4_ae_lib);

    v4_snr_drv.obj->pfnUnRegisterCallback(_v4_vi_pipe, &v4_ae_lib, &v4_awb_lib);

    for (char grp = 0; grp < V4_VPSS_GRP_NUM; grp++)
    {
        for (char chn = 0; chn < V4_VPSS_CHN_NUM; chn++)
            v4_vpss.fnDisableChannel(grp, chn);

        {
            v4_sys_bind source = { .module = V4_SYS_MOD_VIU,
                .device = _v4_vi_dev, .channel = _v4_vi_chn };
            v4_sys_bind dest = { .module = V4_SYS_MOD_VPSS,
                .device = grp, .channel = 0 };
            v4_sys.fnUnbind(&source, &dest);
        }

        v4_vpss.fnStopGroup(grp);
        v4_vpss.fnDestroyGroup(grp);
    }

    v4_vi.fnDisableChannel(_v4_vi_pipe, _v4_vi_chn);

    v4_vi.fnStopPipe(_v4_vi_pipe);
    v4_vi.fnDestroyPipe(_v4_vi_pipe);

    v4_vi.fnDisableDevice(_v4_vi_dev);

    v4_sensor_deconfig();
}

int v4_region_create(char handle, hal_rect rect, short opacity)
{
    int ret;

    v4_sys_bind dest = { .module = V4_SYS_MOD_VENC, .device = _v4_venc_dev };
    v4_rgn_cnf region, regionCurr;
    v4_rgn_chn attrib, attribCurr;

    rect.height += rect.height & 1;
    rect.width += rect.width & 1;

    memset(&region, 0, sizeof(region));
    region.type = V4_RGN_TYPE_OVERLAY;
    region.overlay.pixFmt = V4_PIXFMT_ARGB1555;
    region.overlay.size.width = rect.width;
    region.overlay.size.height = rect.height;
    region.overlay.canvas = handle + 1;

    if (v4_rgn.fnGetRegionConfig(handle, &regionCurr)) {
        HAL_INFO("v4_rgn", "Creating region %d...\n", handle);
        if (ret = v4_rgn.fnCreateRegion(handle, &region))
            return ret;
    } else if (regionCurr.type != region.type ||
        regionCurr.overlay.size.height != region.overlay.size.height ||
        regionCurr.overlay.size.width != region.overlay.size.width) {
        HAL_INFO("v4_rgn", "Parameters are different, recreating "
            "region %d...\n", handle);
        v4_rgn.fnDetachChannel(handle, &dest);
        v4_rgn.fnDestroyRegion(handle);
        if (ret = v4_rgn.fnCreateRegion(handle, &region))
            return ret;
    }

    if (v4_rgn.fnGetChannelConfig(handle, &dest, &attribCurr))
        HAL_INFO("v4_rgn", "Attaching region %d...\n", handle);
    else if (attribCurr.overlay.point.x != rect.x || attribCurr.overlay.point.y != rect.y) {
        HAL_INFO("v4_rgn", "Position has changed, reattaching "
            "region %d...\n", handle);
        v4_rgn.fnDetachChannel(handle, &dest);
    }

    memset(&attrib, 0, sizeof(attrib));
    attrib.show = 1;
    attrib.type = V4_RGN_TYPE_OVERLAY;
    attrib.overlay.bgAlpha = 0;
    attrib.overlay.fgAlpha = opacity >> 1;
    attrib.overlay.point.x = rect.x;
    attrib.overlay.point.y = rect.y;
    attrib.overlay.layer = 7;
    attrib.overlay.attachDest = V4_RGN_DEST_MAIN;

    v4_rgn.fnAttachChannel(handle, &dest, &attrib);

    return EXIT_SUCCESS;
}

void v4_region_destroy(char handle)
{
    v4_sys_bind dest = { .module = V4_SYS_MOD_VENC, .device = _v4_venc_dev };

    v4_rgn.fnDetachChannel(handle, &dest);
    v4_rgn.fnDestroyRegion(handle);
}

int v4_region_setbitmap(int handle, hal_bitmap *bitmap)
{
    v4_rgn_bmp nativeBmp = { .data = bitmap->data, .pixFmt = V4_PIXFMT_ARGB1555,
        .size.height = bitmap->dim.height, .size.width = bitmap->dim.width };

    return v4_rgn.fnSetBitmap(handle, &nativeBmp);
}

int v4_sensor_config(void) {
    int fd;
    char v4a_device = 0;
    if (EQUALS(chip, "Hi3516AV300") ||
        EQUALS(chip, "Hi3516DV300") ||
        EQUALS(chip, "Hi3516CV500"))
        v4a_device = 1;

    if (v4a_device) {
        v4a_snr_dev config;
        memset(&config, 0, sizeof(config));
        config.device = 0;
        config.input = v4_config.input_mode;
        config.rect.width = v4_config.isp.capt.width;
        config.rect.height = v4_config.isp.capt.height;
        if (config.input == V4_SNR_INPUT_MIPI)
            memcpy(&config.mipi, &v4_config.mipi, sizeof(v4_snr_mipi));
        else if (config.input == V4_SNR_INPUT_LVDS)
            memcpy(&config.lvds, &v4_config.lvds, sizeof(v4_snr_lvds));

        if (!access(v4_snr_endp, F_OK))
            fd = open(v4_snr_endp, O_RDWR);
        else fd = open("/dev/mipi", O_RDWR);
        if (fd < 0)
            HAL_ERROR("v4_snr", "Opening imaging device has failed!\n");

        int laneMode = 0;
        ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_CONF_HSMODE, int), &laneMode);

        ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_CLKON_MIPI, unsigned int), &config.device);

        ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_RST_MIPI, unsigned int), &config.device);

        ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_CLKON_SENS, unsigned int), &config.device);

        ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_RST_SENS, unsigned int), &config.device);

        if (ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_CONF_DEV, v4a_snr_dev), &config))
            HAL_ERROR("v4_snr", "Configuring imaging device has failed!\n");

        ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_UNRST_MIPI, unsigned int), &config.device);

        ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_UNRST_SENS, unsigned int), &config.device);

        close(fd);
    } else {
        v4_snr_dev config;
        memset(&config, 0, sizeof(config));
        config.device = 0;
        config.input = v4_config.input_mode;
        config.rect.width = v4_config.isp.capt.width;
        config.rect.height = v4_config.isp.capt.height;
        if (config.input == V4_SNR_INPUT_MIPI)
            memcpy(&config.mipi, &v4_config.mipi, sizeof(v4_snr_mipi));
        else if (config.input == V4_SNR_INPUT_LVDS)
            memcpy(&config.lvds, &v4_config.lvds, sizeof(v4_snr_lvds));

        if (!access(v4_snr_endp, F_OK))
            fd = open(v4_snr_endp, O_RDWR);
        else fd = open("/dev/mipi", O_RDWR);
        if (fd < 0)
            HAL_ERROR("v4_snr", "Opening imaging device has failed!\n");

        int laneMode = 0;
        ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_CONF_HSMODE, int), &laneMode);

        ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_CLKON_MIPI, unsigned int), &config.device);

        ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_RST_MIPI, unsigned int), &config.device);

        ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_CLKON_SENS, unsigned int), &config.device);

        ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_RST_SENS, unsigned int), &config.device);

        if (ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_CONF_DEV, v4_snr_dev), &config))
            HAL_ERROR("v4_snr", "Configuring imaging device has failed!\n");

        ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_UNRST_MIPI, unsigned int), &config.device);

        ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_UNRST_SENS, unsigned int), &config.device);

        close(fd);
    }

    return EXIT_SUCCESS;
}

void v4_sensor_deconfig(void) {
    int fd;
    v4_snr_dev config;
    config.device = 0;

    if (!access(v4_snr_endp, F_OK))
        fd = open(v4_snr_endp, O_RDWR);
    else fd = open("/dev/mipi", O_RDWR);
    if (fd < 0)
        HAL_DANGER("v4_snr", "Opening imaging device has failed!\n");

    ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_CLKOFF_SENS, unsigned int), &config.device);

    ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_RST_SENS, unsigned int), &config.device);

    ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_CLKON_MIPI, unsigned int), &config.device);

    ioctl(fd, _IOW(V4_SNR_IOC_MAGIC, V4_SNR_CMD_RST_MIPI, unsigned int), &config.device);

    close(fd);
}

void v4_sensor_deinit(void)
{
    dlclose(v4_snr_drv.handle);
    v4_snr_drv.handle = NULL;
}

int v4_sensor_init(char *name, char *obj)
{
    char path[128];
    char* dirs[] = {"%s", "./%s", "/usr/lib/sensors/%s", NULL};
    char **dir = dirs;

    while (*dir) {
        sprintf(path, *dir++, name);
        if (v4_snr_drv.handle = dlopen(path, RTLD_LAZY | RTLD_GLOBAL))
            break;
    } if (!v4_snr_drv.handle)
        HAL_ERROR("v4_snr", "Failed to load the sensor driver\n");

    if (!(v4_snr_drv.obj = (v4_snr_obj*)dlsym(v4_snr_drv.handle, obj)))
        HAL_ERROR("v4_snr", "Failed to connect the sensor object\n");

    return EXIT_SUCCESS;
}

/* Smallest box of the capture aspect around width x height (even sizes),
 * e.g. 640x480 around 640x360 for a 4:3 sensor */
static void v4_video_capturebox(unsigned int width, unsigned int height,
    unsigned int *boxWidth, unsigned int *boxHeight)
{
    unsigned int captW = v4_config.isp.capt.width,
        captH = v4_config.isp.capt.height;

    *boxWidth = width;
    *boxHeight = height;
    if (!captW || !captH) return;
    if ((unsigned long long)width * captH > (unsigned long long)height * captW)
        *boxHeight = ALIGN_UP(width * captH / captW, 2);
    else
        *boxWidth = ALIGN_UP(height * captW / captH, 2);
}

/* Centred crop of a width x height picture to the aspect of the encoded
 * size, so the output keeps square pixels instead of being squeezed;
 * 0 when both aspects match within 1 % (plain scale) */
static int v4_video_cropcentre(unsigned int width, unsigned int height,
    unsigned int outW, unsigned int outH, unsigned int align, v4_common_rect *rect)
{
    long long out = (long long)outW * height, in = (long long)outH * width;

    if ((out > in ? out - in : in - out) * 100 <= in) return 0;

    if (out > in) {
        rect->width = width;
        rect->height = ALIGN_BACK(width * outH / outW, 2);
        rect->x = 0;
        rect->y = ALIGN_BACK((height - rect->height) / 2, align);
    } else {
        rect->width = ALIGN_BACK(height * outW / outH, 2);
        rect->height = height;
        rect->x = ALIGN_BACK((width - rect->width) / 2, align);
        rect->y = 0;
    }

    return 1;
}

/* Per channel, so main and sub can have different aspects:
 * - capture-sized VPSS channel (main stream): the encoder crops the
 *   capture before its own scaling (VENC ChnParam crop);
 * - scaled channel (sub stream): VPSS crops AFTER scaling on this SDK, so
 *   the channel scales to the capture-aspect box around the encoded size
 *   and crops that to the encoded size.
 * Origins go even first, then 16-aligned. The rect kept for the API is in
 * capture coordinates either way. */
static void v4_video_crop(char index, hal_vidconfig *config)
{
    int ret = EXIT_FAILURE;
    char scaled;
    unsigned int width, height, captW = v4_config.isp.capt.width,
        captH = v4_config.isp.capt.height;
    v4_common_rect rect;
    static const unsigned int aligns[] = { 2, 16 };
    v4_vpss_chn *channel = &_v4_vpss_attr[(int)index];

    v4_state[index].cropOn = 0;
    if (config->codec != HAL_VIDCODEC_H264 && config->codec != HAL_VIDCODEC_H265)
        return;
    if (!captW || !captH || !channel->dest.width || !channel->dest.height)
        return;

    scaled = channel->dest.width == config->width &&
        channel->dest.height == config->height;
    if (scaled)
        v4_video_capturebox(config->width, config->height, &width, &height);
    else {
        width = channel->dest.width;
        height = channel->dest.height;
    }

    if (!v4_video_cropcentre(width, height, config->width, config->height,
        aligns[0], &rect))
        return;

    if (scaled) {
        if (!v4_vpss.fnSetChannelCrop) {
            HAL_WARNING("v4_vpss", "Channel crop is unavailable on this library, "
                "channel %d is scaled to %ux%u!\n", index, config->width,
                config->height);
            return;
        }
        channel->dest.width = width;
        channel->dest.height = height;
        if (ret = v4_vpss.fnSetChannelConfig(_v4_vpss_grp, index, channel)) {
            HAL_WARNING("v4_vpss", "Resizing channel %d to %ux%u failed with "
                "%#x!\n", index, width, height, ret);
            channel->dest.width = config->width;
            channel->dest.height = config->height;
            v4_vpss.fnSetChannelConfig(_v4_vpss_grp, index, channel);
            return;
        }
    }

    for (int i = 0; i < 2; i++) {
        v4_video_cropcentre(width, height, config->width, config->height,
            aligns[i], &rect);
        if (scaled) {
            /* Exactly the encoded size out of the box */
            rect.width = config->width;
            rect.height = config->height;
            rect.x = ALIGN_BACK((width - rect.width) / 2, aligns[i]);
            rect.y = ALIGN_BACK((height - rect.height) / 2, aligns[i]);
            v4_vpss_crop crop = { .enable = 1, .coord = V4_VPSS_CROPCOORD_ABS,
                .rect = rect };
            ret = v4_vpss.fnSetChannelCrop(_v4_vpss_grp, index, &crop);
        } else {
            v4_venc_para param;
            if (!(ret = v4_venc.fnGetChannelParam(index, &param))) {
                param.cropOn = 1;
                param.crop = rect;
                ret = v4_venc.fnSetChannelParam(index, &param);
            }
        }
        if (!ret) break;
        HAL_WARNING("v4_venc", "Cropping channel %d to %ux%u at %d,%d of %ux%u "
            "failed with %#x!\n", index, rect.width, rect.height, rect.x,
            rect.y, width, height, ret);
    }
    if (ret) {
        if (scaled) {
            channel->dest.width = config->width;
            channel->dest.height = config->height;
            v4_vpss.fnSetChannelConfig(_v4_vpss_grp, index, channel);
        }
        return;
    }

    HAL_INFO("v4_venc", "Channel %d crops %ux%u at %d,%d of %ux%u (%s) "
        "for %ux%u\n", index, rect.width, rect.height, rect.x, rect.y,
        width, height, scaled ? "VPSS" : "VENC", config->width, config->height);

    v4_state[index].cropOn = 1;
    v4_state[index].cropX = rect.x * captW / width;
    v4_state[index].cropY = rect.y * captH / height;
    v4_state[index].cropW = rect.width * captW / width;
    v4_state[index].cropH = rect.height * captH / height;
}

/* QP delta between I and P frames (-10 to 30). Automatic: gop / fps as
 * before, but 0 with intra refresh, where the GOP start is not a keyframe
 * and a lower QP there only inflates that one frame. */
static int v4_video_ipqpdelta(hal_vidconfig *config)
{
    int delta = config->ipQpDelta;
    if (delta == HAL_IPQPDELTA_AUTO)
        delta = config->intraRefresh ? 0 :
            config->gop / (config->framerate ? config->framerate : 1);
    return MAX(-10, MIN(delta, 30));
}

int v4_video_create(char index, hal_vidconfig *config)
{
    int ret;
    v4_venc_chn channel;
    memset(&channel, 0, sizeof(channel));

    channel.gop.mode = V4_VENC_GOPMODE_NORMALP;
    if (config->codec == HAL_VIDCODEC_JPG || config->codec == HAL_VIDCODEC_MJPG) {
        channel.attrib.codec = V4_VENC_CODEC_MJPG;
        switch (config->mode) {
            case HAL_VIDMODE_CBR:
                channel.rate.mode = V4_VENC_RATEMODE_MJPGCBR;
                channel.rate.mjpgCbr = (v4_venc_rate_mjpgbr){ .statTime = 1, .srcFps = config->framerate,
                    .dstFps = config->framerate, .maxBitrate = config->bitrate }; break;
            case HAL_VIDMODE_VBR:
                channel.rate.mode = V4_VENC_RATEMODE_MJPGVBR;
                channel.rate.mjpgVbr = (v4_venc_rate_mjpgbr){ .statTime = 1,
                    .srcFps = config->framerate, .dstFps = config->framerate,
                    .maxBitrate = MAX(config->bitrate, config->maxBitrate) }; break;
            case HAL_VIDMODE_QP:
                channel.rate.mode = V4_VENC_RATEMODE_MJPGQP;
                channel.rate.mjpgQp = (v4_venc_rate_mjpgqp){ .srcFps = config->framerate,
                    .dstFps = config->framerate, .quality = config->maxQual }; break;
            default:
                HAL_ERROR("v4_venc", "MJPEG encoder can only support CBR, VBR or fixed QP modes!");
        }
    } else if (config->codec == HAL_VIDCODEC_H265) {
        channel.attrib.codec = V4_VENC_CODEC_H265;
        channel.gop.normalP.ipQualDelta = v4_video_ipqpdelta(config);
        switch (config->mode) {
            case HAL_VIDMODE_CBR:
                channel.rate.mode = V4_VENC_RATEMODE_H265CBR;
                channel.rate.h265Cbr = (v4_venc_rate_h26xbr){ .gop = config->gop,
                    .statTime = 1, .srcFps = config->framerate, .dstFps = config->framerate,
                    .maxBitrate = config->bitrate }; break;
            case HAL_VIDMODE_VBR:
                channel.rate.mode = V4_VENC_RATEMODE_H265VBR;
                channel.rate.h265Vbr = (v4_venc_rate_h26xbr){ .gop = config->gop,
                    .statTime = 1, .srcFps = config->framerate, .dstFps = config->framerate,
                    .maxBitrate = MAX(config->bitrate, config->maxBitrate) }; break;
            case HAL_VIDMODE_QP:
                channel.rate.mode = V4_VENC_RATEMODE_H265QP;
                channel.rate.h265Qp = (v4_venc_rate_h26xqp){ .gop = config->gop,
                    .srcFps = config->framerate, .dstFps = config->framerate, .interQual = config->maxQual,
                    .predQual = config->minQual, .bipredQual = config->minQual }; break;
            case HAL_VIDMODE_AVBR:
                channel.rate.mode = V4_VENC_RATEMODE_H265AVBR;
                channel.rate.h265Avbr = (v4_venc_rate_h26xbr){ .gop = config->gop,
                    .statTime = 1, .srcFps = config->framerate, .dstFps = config->framerate,
                    .maxBitrate = config->bitrate }; break;
            default:
                HAL_ERROR("v4_venc", "H.265 encoder does not support this mode!");
        }
    } else if (config->codec == HAL_VIDCODEC_H264) {
        channel.attrib.codec = V4_VENC_CODEC_H264;
        channel.gop.normalP.ipQualDelta = v4_video_ipqpdelta(config);
        switch (config->mode) {
            case HAL_VIDMODE_CBR:
                channel.rate.mode = V4_VENC_RATEMODE_H264CBR;
                channel.rate.h264Cbr = (v4_venc_rate_h26xbr){ .gop = config->gop,
                    .statTime = 1, .srcFps = config->framerate, .dstFps = config->framerate,
                    .maxBitrate = config->bitrate }; break;
            case HAL_VIDMODE_VBR:
                channel.rate.mode = V4_VENC_RATEMODE_H264VBR;
                channel.rate.h264Vbr = (v4_venc_rate_h26xbr){ .gop = config->gop,
                    .statTime = 1, .srcFps = config->framerate, .dstFps = config->framerate,
                    .maxBitrate = MAX(config->bitrate, config->maxBitrate) }; break;
            case HAL_VIDMODE_QP:
                channel.rate.mode = V4_VENC_RATEMODE_H264QP;
                channel.rate.h264Qp = (v4_venc_rate_h26xqp){ .gop = config->gop,
                    .srcFps = config->framerate, .dstFps = config->framerate, .interQual = config->maxQual,
                    .predQual = config->minQual, .bipredQual = config->minQual }; break;
            case HAL_VIDMODE_AVBR:
                channel.rate.mode = V4_VENC_RATEMODE_H264AVBR;
                channel.rate.h264Avbr = (v4_venc_rate_h26xbr){ .gop = config->gop,
                    .statTime = 1, .srcFps = config->framerate, .dstFps = config->framerate,
                    .maxBitrate = config->bitrate }; break;
            default:
                HAL_ERROR("v4_venc", "H.264 encoder does not support this mode!");
        }
    } else HAL_ERROR("v4_venc", "This codec is not supported by the hardware!");
    channel.attrib.maxPic.width = config->width;
    channel.attrib.maxPic.height = config->height;
    /* Stream buffer as in the vendor sample (sample_comm_venc.c, MPP V4):
       JPEG/MJPEG take ALIGN_UP(w, 16) * ALIGN_UP(h, 16), H.26x take
       ALIGN_UP(w * h * 3 / 4, 64). The samples run with the module defaults
       (MiniBufMode 0), where the JPEG encoder rejects w * h * 3 / 4 with
       ILLEGAL_PARAM; with JpegeMiniBufMode 1 (v4_venc_modparams) it takes
       either. */
    if (channel.attrib.codec == V4_VENC_CODEC_MJPG)
        channel.attrib.bufSize = ALIGN_UP(config->width, 16) * ALIGN_UP(config->height, 16);
    else
        channel.attrib.bufSize = ALIGN_UP(config->height * config->width * 3 / 4, 64);
    if (channel.attrib.codec == V4_VENC_CODEC_H264)
        channel.attrib.profile = MAX(config->profile, 2);
    channel.attrib.byFrame = 1;
    channel.attrib.pic.width = config->width;
    channel.attrib.pic.height = config->height;

    if (ret = v4_venc.fnCreateChannel(index, &channel))
        return ret;

    v4_state[index].irefActive = 0;
    v4_state[index].irefNum = 0;

    v4_video_crop(index, config);

    if ((config->codec == HAL_VIDCODEC_H264 || config->codec == HAL_VIDCODEC_H265) &&
        config->intraRefresh) {
        if (!v4_venc.fnSetIntraRefresh)
            HAL_WARNING("v4_venc", "Intra refresh is unavailable on this library!\n");
        else {
            v4_venc_iref iref = { .enable = 1,
                .mode = config->intraRefreshMode == HAL_IREFMODE_COLUMN ?
                    V4_VENC_INTRAREFRESH_COLUMN : V4_VENC_INTRAREFRESH_ROW,
                .num = config->intraRefreshNum, .reqIQp = config->intraRefreshQp };
            /* The sweep (CTU/MB rows or columns, num per frame) has to fit
               in one GOP, else the SDK rejects it (ILLEGAL_PARAM) and the
               channel silently falls back to IDR GOPs: refresh more lines
               per frame instead, the stored setting stays as asked */
            unsigned int unit = config->codec == HAL_VIDCODEC_H265 ? 32 : 16;
            unsigned int lines = (iref.mode == V4_VENC_INTRAREFRESH_COLUMN ?
                config->width : config->height);
            unsigned int gop = config->gop ? config->gop : 1;
            lines = (lines + unit - 1) / unit;
            if (gop * iref.num < lines) {
                iref.num = (lines + gop - 1) / gop;
                HAL_WARNING("v4_venc", "Intra refresh on channel %d: %u %s "
                    "do not fit in gop %u at %u per frame, using %u\n", index,
                    lines, iref.mode ? "columns" : "rows", gop,
                    config->intraRefreshNum, iref.num);
            }
            if (ret = v4_venc.fnSetIntraRefresh(index, &iref))
                HAL_WARNING("v4_venc", "Enabling intra refresh on channel %d "
                    "failed with %#x!\n", index, ret);
            else {
                v4_state[index].irefActive = 1;
                v4_state[index].irefNum = iref.num;
                HAL_INFO("v4_venc", "Intra refresh enabled on channel %d "
                    "(%s, %u, QP %u)\n", index, iref.mode ? "column" : "row",
                    iref.num, iref.reqIQp);
            }
        }
    }

    {
        int count = -1;
        if (config->codec != HAL_VIDCODEC_JPG &&
            (ret = v4_venc.fnStartReceivingEx(index, &count)))
            return ret;
    }

    v4_state[index].payload = config->codec;

    /* Channels (re)created after the stream thread started need their
       descriptor too, or the thread would wait on a stale one */
    if (v4_state[(int)index].mainLoop && config->codec != HAL_VIDCODEC_JPG) {
        int fd = v4_venc.fnGetDescriptor(index);
        if (fd >= 0) v4_state[(int)index].fileDesc = fd;
    }

    return EXIT_SUCCESS;
}

int v4_video_destroy(char index)
{
    int ret;

    v4_state[index].enable = 0;
    v4_state[index].payload = HAL_VIDCODEC_UNSPEC;
    v4_state[index].irefActive = 0;
    v4_state[index].irefNum = 0;
    v4_state[index].cropOn = 0;

    v4_venc.fnStopReceiving(index);

    {
        v4_sys_bind source = { .module = V4_SYS_MOD_VPSS,
            .device = _v4_vpss_grp, .channel = index };
        v4_sys_bind dest = { .module = V4_SYS_MOD_VENC,
            .device = _v4_venc_dev, .channel = index };
        /* Not fatal: the snapshot channel is only bound during a request,
           and a channel whose start failed may never have been bound */
        v4_sys.fnUnbind(&source, &dest);
    }

    if (ret = v4_venc.fnDestroyChannel(index))
        return ret;

    if (ret = v4_vpss.fnDisableChannel(_v4_vpss_grp, index))
        return ret;

    return EXIT_SUCCESS;
}

int v4_video_destroy_all(void)
{
    int ret;

    for (char i = 0; i < V4_VENC_CHN_NUM; i++)
        if (v4_state[i].enable)
            if (ret = v4_video_destroy(i))
                return ret;

    return EXIT_SUCCESS;
}

void v4_video_request_idr(char index)
{
    v4_venc.fnRequestIdr(index, 1);
}

int v4_video_snapshot_grab(char index, hal_jpegdata *jpeg)
{
    int ret;
    v4_venc_stat stat;
    v4_venc_strm strm;
    memset(&stat, 0, sizeof(stat));
    memset(&strm, 0, sizeof(strm));

    /* Same order as the persistent channels (sub stream): the encoder
       starts receiving first, then the VPSS channel is enabled and bound */
    int count = 1;
    ret = v4_venc.fnStartReceivingEx(index, &count);
    HAL_INFO("v4_venc", "Snapshot %d: StartRecvFrame(1) rc %#x\n", index, ret);
    if (ret) {
        HAL_DANGER("v4_venc", "Requesting one frame "
            "%d failed with %#x!\n", index, ret);
        goto finish;
    }

    ret = v4_vpss.fnEnableChannel(_v4_vpss_grp, index);
    HAL_INFO("v4_venc", "Snapshot %d: VPSS_EnableChn rc %#x\n", index, ret);
    if (ret) {
        HAL_DANGER("v4_venc", "Enabling the VPSS channel "
            "%d failed with %#x!\n", index, ret);
        goto finish;
    }

    {
        v4_sys_bind source = { .module = V4_SYS_MOD_VPSS,
            .device = _v4_vpss_grp, .channel = index };
        v4_sys_bind dest = { .module = V4_SYS_MOD_VENC,
            .device = _v4_venc_dev, .channel = index };
        ret = v4_sys.fnBind(&source, &dest);
        HAL_INFO("v4_venc", "Snapshot %d: SYS_Bind rc %#x\n", index, ret);
        if (ret) {
            HAL_DANGER("v4_venc", "Binding the encoder channel "
                "%d failed with %#x!\n", index, ret);
            goto finish;
        }
    }

    int fd = v4_venc.fnGetDescriptor(index);

    struct timeval timeout = { .tv_sec = 2, .tv_usec = 0 };
    fd_set readFds;
    FD_ZERO(&readFds);
    FD_SET(fd, &readFds);
    ret = select(fd + 1, &readFds, NULL, NULL, &timeout);
    if (ret < 0) {
        HAL_DANGER("v4_venc", "Select operation failed!\n");
        ret = EXIT_FAILURE;
        goto finish;
    } else if (ret == 0) {
        /* Tells "no frame reached the encoder" (leftRecvPics still 1)
           from "frame received but not encoded" */
        int qret = v4_venc.fnQuery(index, &stat);
        HAL_INFO("v4_venc", "Snapshot %d: no stream in 2 s, Query rc %#x, "
            "leftPics %u, leftRecvPics %u, leftEncPics %u, curPacks %u\n",
            index, qret, stat.leftPics, stat.leftRecvPics,
            stat.leftEncPics, stat.curPacks);
        HAL_DANGER("v4_venc", "Capture stream timed out!\n");
        ret = EXIT_FAILURE;
        goto finish;
    }

    if (FD_ISSET(fd, &readFds)) {
        if (ret = v4_venc.fnQuery(index, &stat)) {
            HAL_DANGER("v4_venc", "Querying the encoder channel "
                "%d failed with %#x!\n", index, ret);
            goto finish;
        }

        if (!stat.curPacks) {
            HAL_DANGER("v4_venc", "Current frame is empty, skipping it!\n");
            ret = EXIT_FAILURE;
            goto finish;
        }

        v4_venc_pack packs[8];
        if (stat.curPacks > 8)
            strm.packet = (v4_venc_pack*)malloc(sizeof(v4_venc_pack) * stat.curPacks);
        else
            strm.packet = packs;

        if (!strm.packet) {
            HAL_DANGER("v4_venc", "Memory allocation on channel %d failed!\n", index);
            ret = EXIT_FAILURE;
            goto finish;
        }
        strm.count = stat.curPacks;

        ret = v4_venc.fnGetStream(index, &strm, stat.curPacks);
        HAL_INFO("v4_venc", "Snapshot %d: GetStream rc %#x, %u packs\n",
            index, ret, stat.curPacks);
        if (ret) {
            HAL_DANGER("v4_venc", "Getting the stream on "
                "channel %d failed with %#x!\n", index, ret);
            if (stat.curPacks > 8) free(strm.packet);
            strm.packet = NULL;
            goto finish;
        }

        {
            jpeg->jpegSize = 0;
            for (unsigned int i = 0; i < strm.count; i++) {
                v4_venc_pack *pack = &strm.packet[i];
                unsigned int packLen = pack->length - pack->offset;
                unsigned char *packData = pack->data + pack->offset;

                unsigned int newLen = jpeg->jpegSize + packLen;
                if (newLen > jpeg->length) {
                    jpeg->data = realloc(jpeg->data, newLen);
                    jpeg->length = newLen;
                }
                memcpy(jpeg->data + jpeg->jpegSize, packData, packLen);
                jpeg->jpegSize += packLen;
            }
        }

        v4_venc.fnFreeStream(index, &strm);
        if (stat.curPacks > 8) free(strm.packet);
    }

finish:
    v4_venc.fnFreeDescriptor(index);

    v4_venc.fnStopReceiving(index);

    v4_channel_unbind(index);

    return ret;
}

void *v4_video_thread(void)
{
    int ret, maxFd = 0;

    for (int i = 0; i < V4_VENC_CHN_NUM; i++) {
        if (!v4_state[i].enable) continue;
        if (!v4_state[i].mainLoop) continue;

        ret = v4_venc.fnGetDescriptor(i);
        if (ret < 0) {
            HAL_DANGER("v4_venc", "Getting the encoder descriptor failed with %#x!\n", ret);
            return (void*)0;
        }
        v4_state[i].fileDesc = ret;

        if (maxFd <= v4_state[i].fileDesc)
            maxFd = v4_state[i].fileDesc;
    }

    v4_venc_stat stat;
    v4_venc_strm stream;
    struct timeval timeout;
    fd_set readFds;

    while (keepRunning) {
        FD_ZERO(&readFds);
        maxFd = 0;
        for(int i = 0; i < V4_VENC_CHN_NUM; i++) {
            if (!v4_state[i].enable) continue;
            if (!v4_state[i].mainLoop) continue;
            FD_SET(v4_state[i].fileDesc, &readFds);
            if (maxFd <= v4_state[i].fileDesc)
                maxFd = v4_state[i].fileDesc;
        }

        timeout.tv_sec = 2;
        timeout.tv_usec = 0;
        ret = select(maxFd + 1, &readFds, NULL, NULL, &timeout);
        if (ret < 0) {
            HAL_DANGER("v4_venc", "Select operation failed!\n");
            break;
        } else if (ret == 0) {
            HAL_WARNING("v4_venc", "Main stream loop timed out!\n");
            continue;
        } else {
            for (int i = 0; i < V4_VENC_CHN_NUM; i++) {
                if (!v4_state[i].enable) continue;
                if (!v4_state[i].mainLoop) continue;
                if (FD_ISSET(v4_state[i].fileDesc, &readFds)) {
                    memset(&stream, 0, sizeof(stream));

                    if (ret = v4_venc.fnQuery(i, &stat)) {
                        HAL_DANGER("v4_venc", "Querying the encoder channel "
                            "%d failed with %#x!\n", i, ret);
                        break;
                    }

                    if (!stat.curPacks) {
                        HAL_WARNING("v4_venc", "Current frame is empty, skipping it!\n");
                        continue;
                    }

                    v4_venc_pack packs[8];
                    if (stat.curPacks > 8)
                        stream.packet = (v4_venc_pack*)malloc(sizeof(v4_venc_pack) * stat.curPacks);
                    else
                        stream.packet = packs;

                    if (!stream.packet) {
                        HAL_DANGER("v4_venc", "Memory allocation on channel %d failed!\n", i);
                        break;
                    }
                    stream.count = stat.curPacks;

                    if (ret = v4_venc.fnGetStream(i, &stream, 40)) {
                        HAL_DANGER("v4_venc", "Getting the stream on "
                            "channel %d failed with %#x!\n", i, ret);
                        if (stat.curPacks > 8) free(stream.packet);
                        break;
                    }

                    if (v4_vid_cb) {
                        hal_vidstream outStrm;
                        hal_vidpack outPack[stream.count];
                        outStrm.count = stream.count;
                        outStrm.seq = stream.sequence;
                        for (int j = 0; j < stream.count; j++) {
                            v4_venc_pack *pack = &stream.packet[j];
                            outPack[j].data = pack->data;
                            outPack[j].length = pack->length;
                            outPack[j].naluCnt = 1;
                            outPack[j].nalu[0].length = pack->length;
                            outPack[j].nalu[0].offset = pack->offset;
                            switch (v4_state[i].payload) {
                                case HAL_VIDCODEC_H264:
                                    outPack[j].nalu[0].type = pack->naluType.h264Nalu;
                                    break;
                                case HAL_VIDCODEC_H265:
                                    outPack[j].nalu[0].type = pack->naluType.h265Nalu;
                                    break;
                            }
                            outPack[j].offset = pack->offset;
                            outPack[j].timestamp = pack->timestamp;
                        }
                        outStrm.pack = outPack;
                        (*v4_vid_cb)(i, &outStrm);
                    }

                    if (ret = v4_venc.fnFreeStream(i, &stream)) {
                        HAL_WARNING("v4_venc", "Releasing the stream on "
                            "channel %d failed with %#x!\n", i, ret);
                    }
                    if (stat.curPacks > 8) free(stream.packet);
                }
            }
        }
    }

    HAL_INFO("v4_venc", "Shutting down encoding thread...\n");
}

void v4_system_deinit(void)
{
    v4_sys.fnExit();
    v4_vb.fnExit();

    v4_sensor_deinit();
}

/* Encoder module parameters, as majestic sets them: mini stream buffers for
 * H.264/H.265/JPEG and a cached stream buffer. They live in the kernel
 * modules until the next boot and can only be changed while no encoder
 * channel exists, so this runs once before the first channel. */
static void v4_venc_modparams(void)
{
    static const char *name[] = { "", "VencBufferCache", "H264E MiniBufMode",
        "H265E MiniBufMode", "JpegeMiniBufMode" };
    char line[160] = "";
    int ret, pos = 0;

    if (!v4_venc.fnGetModParam || !v4_venc.fnSetModParam) return;

    for (int type = V4_VENC_MODTYPE_VENC; type <= V4_VENC_MODTYPE_JPEGE; type++) {
        v4_venc_mod param;
        unsigned int *value, old;
        memset(&param, 0, sizeof(param));
        param.type = type;
        if ((ret = v4_venc.fnGetModParam(&param))) {
            HAL_WARNING("v4_venc", "Reading the %s module parameter failed "
                "with %#x!\n", name[type], ret);
            continue;
        }
        switch (type) {
            case V4_VENC_MODTYPE_VENC:  value = &param.venc.bufferCache; break;
            case V4_VENC_MODTYPE_H264E: value = &param.h264e.miniBufMode; break;
            case V4_VENC_MODTYPE_H265E: value = &param.h265e.miniBufMode; break;
            default:                    value = &param.jpege.miniBufMode; break;
        }
        old = *value;
        if (old != 1) {
            *value = 1;
            if ((ret = v4_venc.fnSetModParam(&param))) {
                HAL_WARNING("v4_venc", "Setting %s to 1 failed with %#x!\n",
                    name[type], ret);
                continue;
            }
        }
        if (pos < (int)sizeof(line))
            pos += snprintf(line + pos, sizeof(line) - pos, "%s%s %u->1",
                pos ? ", " : "", name[type], old);
    }

    if (pos) HAL_INFO("v4_venc", "Module parameters: %s\n", line);
}

int v4_system_init(char *snrConfig)
{
    int ret;

    printf("App built with headers v%s\n", V4_SYS_API);

    {
        v4_sys_ver version;
        v4_sys.fnGetVersion(&version);
        puts(version.version);
    }

    if (v4_parse_sensor_config(snrConfig, &v4_config) != CONFIG_OK)
        HAL_ERROR("v4_sys", "Can't load sensor config\n");
    strncpy(sensor, v4_config.sensor_type, sizeof(sensor) - 1);

    if (ret = v4_sensor_init(v4_config.dll_file, v4_config.sensor_type))
        return ret;

    v4_sys.fnExit();
    v4_vb.fnExit();

    {
        v4_vb_pool pool;
        memset(&pool, 0, sizeof(pool));

        pool.count = 2;
        pool.comm[0].blockSize = v4_buffer_calculate_vi(v4_config.isp.size.width,
            v4_config.isp.size.height, V4_PIXFMT_RGB_BAYER_8BPP + v4_config.mipi.prec,
            V4_COMPR_NONE, 8);
        pool.comm[0].blockCnt = 3;
        pool.comm[1].blockSize = v4_buffer_calculate_venc(
            v4_config.isp.size.width, v4_config.isp.size.height,
            V4_PIXFMT_YVU420SP, 8);
        pool.comm[1].blockCnt = 2;
        /* Smaller frames get blocks from the best fitting pool, so the
           scaled channels do not wait for the full-size ones (both
           full-size pools run down to 0 free blocks with the main stream
           alone). Equal sizes share one pool, a reservation as large as
           the capture adds its blocks to the full-size YUV pool. */
        for (int i = 0; i < _v4_vb_extra_num; i++) {
            /* A scaled H.26x channel of another aspect runs at the
               capture-aspect box around its size (see v4_video_crop) */
            unsigned int width, height;
            v4_video_capturebox(_v4_vb_extra[i].width, _v4_vb_extra[i].height,
                &width, &height);
            unsigned long long size = v4_buffer_calculate_venc(
                width, height, V4_PIXFMT_YVU420SP, 8);
            unsigned int p;
            if (size >= pool.comm[1].blockSize) p = 1;
            else for (p = 2; p < pool.count; p++)
                if (pool.comm[p].blockSize == size) break;
            if (p == pool.count) {
                pool.comm[p].blockSize = size;
                pool.count++;
            }
            pool.comm[p].blockCnt += _v4_vb_extra[i].count;
        }
        for (unsigned int i = 0; i < pool.count; i++)
            HAL_INFO("v4_sys", "VB pool %u: %u blocks of %llu bytes\n",
                i, pool.comm[i].blockCnt, pool.comm[i].blockSize);

        if (ret = v4_vb.fnConfigPool(&pool))
            return ret;
    }
    if (ret = v4_vb.fnInit())
        return ret;

    if (ret = v4_sys.fnInit())
        return ret;

    v4_venc_modparams();

    return EXIT_SUCCESS;
}

/* Before v4_system_init: a scaled channel of this size will run, so give
 * it its own blocks (count = frames it may hold at once) */
void v4_system_reserve(short width, short height, unsigned int count)
{
    if (!width || !height || !count) return;
    if (_v4_vb_extra_num >= V4_VB_EXTRA_MAX) {
        HAL_WARNING("v4_sys", "Too many buffer reservations, "
            "ignoring %dx%d!\n", width, height);
        return;
    }
    _v4_vb_extra[_v4_vb_extra_num].width = width;
    _v4_vb_extra[_v4_vb_extra_num].height = height;
    _v4_vb_extra[_v4_vb_extra_num].count = count;
    _v4_vb_extra_num++;
}

float v4_system_readtemp(void)
{
    char v4a_device = 0;
    int val, prep;
    float result = 0.0 / 0.0;

    if (EQUALS(chip, "Hi3516AV300") ||
        EQUALS(chip, "Hi3516DV300") ||
        EQUALS(chip, "Hi3516CV500"))
        v4a_device = 1;

    prep = v4a_device ? V4A_SYS_TEMP_PREP : V4_SYS_TEMP_PREP;

    if (hal_registry(v4a_device ? V4A_SYS_TEMP_CFG : V4_SYS_TEMP_CFG, &val, OP_READ) && prep != val)
        hal_registry(v4a_device ? V4A_SYS_TEMP_CFG : V4_SYS_TEMP_CFG, &prep, OP_WRITE);

    if (!hal_registry(v4a_device ? V4A_SYS_TEMP_VAL : V4_SYS_TEMP_VAL, &val, OP_READ))
        return result;

    result = val & ((1 << 10) - 1);
    return ((result - (v4a_device ? 136 : 117)) / (v4a_device ? 793 : 798)) * 165 - 40;
}

int v4_system_identify(void)
{
    if (hisi_identify() != 4)
        return 0;

    plat = HAL_PLATFORM_V4;
    strcpy(family, "hisi-gen4");
    chnCount = V4_VENC_CHN_NUM;
    chnState = (hal_chnstate*)v4_state;
    aud_thread = v4_audio_thread;
    isp_thread = v4_image_thread;
    vid_thread = v4_video_thread;
    return 1;
}

#endif
