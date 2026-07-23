#ifndef PLASMA_SCREENSAVER_H
#define PLASMA_SCREENSAVER_H

#include <math.h>
#include <stdlib.h>
#include <time.h>
#include "common_gfx.h"

#define PLASMA_FPS 30

#define SIN_LUT_SIZE 1024  // plus de précision que 360
#define TWO_PI (2.0 * M_PI)




static ScreensaverContext plasma_ctx = {0};
static double plasma_time = 0.0;
static double plasma_scale;
static double plasma_speed;
static double plasma_hue_offset;
static double plasma_saturation;
static double plasma_luminosity;
static double sin_lut[SIN_LUT_SIZE];

static void init_sin_lut() {
    for (int i = 0; i < SIN_LUT_SIZE; ++i) {
        sin_lut[i] = sin((TWO_PI * i) / SIN_LUT_SIZE);
    }
}

static inline double fast_sin(double radians) {
    while (radians < 0) radians += TWO_PI;
    while (radians >= TWO_PI) radians -= TWO_PI;
    int index = (int)((radians / TWO_PI) * SIN_LUT_SIZE);
    return sin_lut[index % SIN_LUT_SIZE];
}


static void plasma_get_rgb(int x, int y, double t, double *r, double *g, double *b) {
    double v =
        fast_sin(x * plasma_scale + t)
        + fast_sin((y * plasma_scale + t) / 2.0)
        + fast_sin((x * plasma_scale + y * plasma_scale + t) / 2.0)
        + fast_sin(sqrt((double)(x * x + y * y)) * plasma_scale / 2.0 + t);
    v = (v + 4.0) / 8.0;
    double hue = fmod(v + t * 0.07 + plasma_hue_offset, 1.0);
    double s = plasma_saturation;
    double l = plasma_luminosity;
    double c = (1.0 - fabs(2.0 * l - 1.0)) * s;
    double h = hue * 6.0;
    double xcol = c * (1.0 - fabs(fmod(h, 2.0) - 1.0));
    double m = l - c/2.0;
    double rr, gg, bb;
    if (h < 1.0)      { rr = c; gg = xcol; bb = 0; }
    else if (h < 2.0) { rr = xcol; gg = c; bb = 0; }
    else if (h < 3.0) { rr = 0; gg = c; bb = xcol; }
    else if (h < 4.0) { rr = 0; gg = xcol; bb = c; }
    else if (h < 5.0) { rr = xcol; gg = 0; bb = c; }
    else              { rr = c; gg = 0; bb = xcol; }
    *r = rr + m;
    *g = gg + m;
    *b = bb + m;
}

static gboolean plasma_on_draw(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    ScreensaverContext *ctx = (ScreensaverContext *)data;
    cairo_t *cr = gdk_cairo_create(widget->window);
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_RGB24, ctx->screen_width, ctx->screen_height);
    unsigned char *buf = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    for (int y = 0; y < ctx->screen_height; y++) {
        for (int x = 0; x < ctx->screen_width; x++) {
            double r, g, b;
            plasma_get_rgb(x, y, plasma_time, &r, &g, &b);
            int ir = (int)(r * 255.0);
            int ig = (int)(g * 255.0);
            int ib = (int)(b * 255.0);
            unsigned char *p = buf + y * stride + x * 4;
            p[2] = ir; // R
            p[1] = ig; // G
            p[0] = ib; // B
            p[3] = 0xFF;
        }
    }
    cairo_surface_mark_dirty(surface);
    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_paint(cr);
    cairo_surface_destroy(surface);
    cairo_destroy(cr);
    return FALSE;
}

static gboolean plasma_on_timeout(gpointer user_data) {
    plasma_time += plasma_speed;
    gtk_widget_queue_draw(GTK_WIDGET(user_data));
    return TRUE;
}

static gboolean plasma_on_configure(GtkWidget *widget, GdkEventConfigure *event, gpointer data) {
    ScreensaverContext *ctx = (ScreensaverContext *)data;
    common_update_screen_size(ctx, widget);
    return FALSE;
}

static void plasma_on_destroy(GtkWidget *widget, gpointer data) {
    ScreensaverContext *ctx = (ScreensaverContext *)data;
    common_destroy_window(ctx);
}

static void plasma_screensaver_show() {
    srand(time(NULL));
init_sin_lut();
       plasma_scale = 0.01 + ((double)rand() / RAND_MAX) * (0.03 - 0.01);
       plasma_speed = 0.005 + ((double)rand() / RAND_MAX) * (0.02 - 0.005);
    plasma_hue_offset = (double)rand() / RAND_MAX;
    plasma_saturation = 0.7 + ((double)rand() / RAND_MAX) * (1.0 - 0.7);
    plasma_luminosity = 0.4 + ((double)rand() / RAND_MAX) * (0.6 - 0.4);
    plasma_time = 0.0;
    common_init_window(&plasma_ctx, G_CALLBACK(plasma_on_draw), G_CALLBACK(plasma_on_configure), G_CALLBACK(plasma_on_destroy));
    common_start_timeout(&plasma_ctx, PLASMA_FPS, G_SOURCE_FUNC(plasma_on_timeout));
}

static void plasma_screensaver_hide() {
    common_destroy_window(&plasma_ctx);
}

#endif // PLASMA_SCREENSAVER_H