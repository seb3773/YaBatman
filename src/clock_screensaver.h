#ifndef CLOCK_SCREENSAVER_H
#define CLOCK_SCREENSAVER_H

#include <math.h>
#include "common_gfx.h"

static ScreensaverContext clock_ctx = {0};
static double clock_pos_x = 100, clock_pos_y = 100, clock_vel_x = 2.0, clock_vel_y = 1.2;
static char clock_hourstr[32] = "";

static void clock_update_time() {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    strftime(clock_hourstr, sizeof(clock_hourstr), "%H:%M:%S", tm);
}

static void clock_move_text(const cairo_text_extents_t *ext) {
    clock_pos_x += clock_vel_x;
    clock_pos_y += clock_vel_y;
    double left = clock_pos_x + ext->x_bearing;
    double right = clock_pos_x + ext->x_bearing + ext->width;
    double top = clock_pos_y + ext->y_bearing;
    double bottom = clock_pos_y + ext->y_bearing + ext->height;
    if (left < 0) { clock_pos_x = -ext->x_bearing; clock_vel_x = fabs(clock_vel_x); }
    if (right > clock_ctx.screen_width) { clock_pos_x = clock_ctx.screen_width - ext->x_bearing - ext->width; clock_vel_x = -fabs(clock_vel_x); }
    if (top < 0) { clock_pos_y = -ext->y_bearing; clock_vel_y = fabs(clock_vel_y); }
    if (bottom > clock_ctx.screen_height) { clock_pos_y = clock_ctx.screen_height - ext->y_bearing - ext->height; clock_vel_y = -fabs(clock_vel_y); }
}

static gboolean clock_on_draw(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    ScreensaverContext *ctx = (ScreensaverContext *)data;
    cairo_t *cr = gdk_cairo_create(widget->window);
    common_update_screen_size(ctx, widget);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, ctx->screen_height / 3.0);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, clock_hourstr, &ext);
    clock_move_text(&ext);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_move_to(cr, clock_pos_x, clock_pos_y);
    cairo_show_text(cr, clock_hourstr);
    cairo_destroy(cr);
    return FALSE;
}

static gboolean clock_on_timeout(gpointer user_data) {
    clock_update_time();
    gtk_widget_queue_draw(GTK_WIDGET(user_data));
    return TRUE;
}

static gboolean clock_on_configure(GtkWidget *widget, GdkEventConfigure *event, gpointer data) {
    ScreensaverContext *ctx = (ScreensaverContext *)data;
    common_update_screen_size(ctx, widget);
    return FALSE;
}

static void clock_on_destroy(GtkWidget *widget, gpointer data) {
    ScreensaverContext *ctx = (ScreensaverContext *)data;
    common_destroy_window(ctx);
}

static void clock_screensaver_show() {
    clock_update_time();
    common_init_window(&clock_ctx, G_CALLBACK(clock_on_draw), G_CALLBACK(clock_on_configure), G_CALLBACK(clock_on_destroy));
    common_start_timeout(&clock_ctx, 60, G_SOURCE_FUNC(clock_on_timeout));
}

static void clock_screensaver_hide() {
    common_destroy_window(&clock_ctx);
}

#endif // CLOCK_SCREENSAVER_H