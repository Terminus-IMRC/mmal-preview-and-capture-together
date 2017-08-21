/*
 * Copyright (c) 2017 Sugizaki Yukimasa (ysugi@idein.jp)
 * All rights reserved.
 *
 * This software is licensed under a Modified (3-Clause) BSD License.
 * You should have received a copy of this license along with this
 * software. If not, contact the copyright holder above.
 */

#include <interface/mmal/mmal.h>
#include <interface/mmal/util/mmal_connection.h>
#include <interface/mmal/util/mmal_util_params.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>



/* Interval between each capture, in ms. */
/* Issue 1: If this value is 100, exposure will not be controlled for IMX219. */
#define INTERVAL_BETWEEN_CAPTURES 1000

/* Formats for the preview port of camera. */
#define ENCODING_PREVIEW MMAL_ENCODING_I420
#define WIDTH_PREVIEW  1024
#define HEIGHT_PREVIEW 768

/* Formats for the capture port of camera. */
/* Issue 2: If this encoding is OPAQUE, MMAL freezes after the first capture. */
#define ENCODING_CAPTURE MMAL_ENCODING_RGB24
#define WIDTH_CAPTURE  512
#define HEIGHT_CAPTURE 512



#define CAMERA_PREVIEW_PORT 0
#define CAMERA_CAPTURE_PORT 2
#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480

#define _check_mmal(x) \
    do { \
        MMAL_STATUS_T status = (x); \
        if (status != MMAL_SUCCESS) { \
            fprintf(stderr, "%s:%d: MMAL call failed: 0x%08x\n", __FILE__, __LINE__, status); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)


static MMAL_STATUS_T config_port(MMAL_PORT_T *port, const MMAL_FOURCC_T encoding, const int width, const int height)
{
    port->format->encoding = encoding;
    port->format->es->video.width  = VCOS_ALIGN_UP(width,  32);
    port->format->es->video.height = VCOS_ALIGN_UP(height, 16);
    port->format->es->video.crop.x = 0;
    port->format->es->video.crop.y = 0;
    port->format->es->video.crop.width  = width;
    port->format->es->video.crop.height = height;
    return mmal_port_format_commit(port);
}

static double get_time()
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return (double) t.tv_sec + t.tv_usec * 1e-6;
}

static double start_time;

static void callback_control(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
    fprintf(stderr, "%f: %s is called by %s\n", get_time() - start_time, __func__, port->name);
    mmal_buffer_header_release(buffer);
}

int main()
{
    MMAL_CONNECTION_T *conn_preview_render = NULL, *conn_capture_render = NULL;
    MMAL_COMPONENT_T *cp_camera = NULL, *cp_render_1 = NULL, *cp_render_2 = NULL;

    start_time = get_time();

    /* Setup the camera component. */
    _check_mmal(mmal_component_create("vc.ril.camera", &cp_camera));
    _check_mmal(mmal_port_enable(cp_camera->control, callback_control));
    _check_mmal(config_port(cp_camera->output[CAMERA_PREVIEW_PORT], ENCODING_PREVIEW, WIDTH_PREVIEW, HEIGHT_PREVIEW));
    _check_mmal(config_port(cp_camera->output[CAMERA_CAPTURE_PORT], ENCODING_CAPTURE, WIDTH_CAPTURE, HEIGHT_CAPTURE));
    _check_mmal(mmal_component_enable(cp_camera));

    /* Setup the render_1 component. */
    _check_mmal(mmal_component_create("vc.ril.video_render", &cp_render_1));
    _check_mmal(mmal_port_enable(cp_render_1->control, callback_control));
    _check_mmal(config_port(cp_render_1->input[0], ENCODING_PREVIEW, WIDTH_PREVIEW, HEIGHT_PREVIEW));
    {
        MMAL_RECT_T dest_rect = {
            .x = 0, .y = 0,
            .width = SCREEN_WIDTH / 2, .height = SCREEN_HEIGHT
        };
        MMAL_DISPLAYREGION_T displayregion = {
            {MMAL_PARAMETER_DISPLAYREGION, sizeof(displayregion)},
            .fullscreen = 0,
            .dest_rect = dest_rect,
            .set = MMAL_DISPLAY_SET_FULLSCREEN | MMAL_DISPLAY_SET_DEST_RECT
        };
        _check_mmal(mmal_port_parameter_set(cp_render_1->input[0], &displayregion.hdr));
    }
    _check_mmal(mmal_component_enable(cp_render_1));

    /* Setup the render_2 component. */
    _check_mmal(mmal_component_create("vc.ril.video_render", &cp_render_2));
    _check_mmal(mmal_port_enable(cp_render_2->control, callback_control));
    _check_mmal(config_port(cp_render_2->input[0], ENCODING_CAPTURE, WIDTH_CAPTURE, HEIGHT_CAPTURE));
    {
        MMAL_RECT_T dest_rect = {
            .x = SCREEN_WIDTH / 2, .y = 0,
            .width = SCREEN_WIDTH / 2, .height = SCREEN_HEIGHT
        };
        MMAL_DISPLAYREGION_T displayregion = {
            {MMAL_PARAMETER_DISPLAYREGION, sizeof(displayregion)},
            .fullscreen = 0,
            .dest_rect = dest_rect,
            .set = MMAL_DISPLAY_SET_FULLSCREEN | MMAL_DISPLAY_SET_DEST_RECT
        };
        _check_mmal(mmal_port_parameter_set(cp_render_2->input[0], &displayregion.hdr));
    }
    _check_mmal(mmal_component_enable(cp_render_2));

    /* Connect camera[PREVIEW] -- [0]render_1. */
    _check_mmal(mmal_connection_create(&conn_preview_render, cp_camera->output[CAMERA_PREVIEW_PORT], cp_render_1->input[0], MMAL_CONNECTION_FLAG_TUNNELLING));
    _check_mmal(mmal_connection_enable(conn_preview_render));

    /* Connect camera[CAPTURE] -- [0]render_2. */
    _check_mmal(mmal_connection_create(&conn_capture_render, cp_camera->output[CAMERA_CAPTURE_PORT], cp_render_2->input[0], MMAL_CONNECTION_FLAG_TUNNELLING));
    _check_mmal(mmal_connection_enable(conn_capture_render));

    for (; ; ) {
        fprintf(stderr, "%f: Setting MMAL_PARAMETER_CAPTURE to TRUE\n", get_time() - start_time);
        _check_mmal(mmal_port_parameter_set_boolean(cp_camera->output[CAMERA_CAPTURE_PORT], MMAL_PARAMETER_CAPTURE, MMAL_TRUE));
        for (; ; ) {
            vcos_sleep(INTERVAL_BETWEEN_CAPTURES);
            MMAL_BOOL_T b;
            _check_mmal(mmal_port_parameter_get_boolean(cp_camera->output[CAMERA_CAPTURE_PORT], MMAL_PARAMETER_CAPTURE, &b));
            if (b == MMAL_FALSE)
                break;
        }
    }

    return 0;
}
