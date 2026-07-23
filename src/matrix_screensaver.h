#ifndef MATRIX_SCREENSAVER_H
#define MATRIX_SCREENSAVER_H

#include <math.h>
#include "common_gfx.h"

#define MATRIX_COLS 80
#define MATRIX_FONT_SIZE 22
#define MATRIX_FPS 30
#define MATRIX_CHARSET "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@#$%&*"

static ScreensaverContext matrix_ctx = {0};
static int matrix_cols = MATRIX_COLS, matrix_rows = 0;
static int *matrix_y = NULL;
static char *matrix_charset = MATRIX_CHARSET;
static int matrix_charset_len = 0;

static void matrix_init_cols() {
    if (matrix_y) free(matrix_y);
    matrix_cols = matrix_ctx.screen_width / MATRIX_FONT_SIZE;
    matrix_rows = matrix_ctx.screen_height / MATRIX_FONT_SIZE;
    matrix_y = (int*)malloc(matrix_cols * sizeof(int));
    for (int i = 0; i < matrix_cols; i++)
        matrix_y[i] = rand() % matrix_rows;
}

static gboolean matrix_on_draw(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    ScreensaverContext *ctx = (ScreensaverContext *)data;
    cairo_t *cr = gdk_cairo_create(widget->window);
    common_update_screen_size(ctx, widget);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, MATRIX_FONT_SIZE);
    for (int x = 0; x < matrix_cols; x++) {
        int y = matrix_y[x];
        int head_y = y * MATRIX_FONT_SIZE;
        char c = matrix_charset[rand() % matrix_charset_len];
        cairo_set_source_rgb(cr, 0.7, 1.0, 0.7); // Vert clair
        cairo_move_to(cr, x * MATRIX_FONT_SIZE, head_y);
        cairo_show_text(cr, (char[]){c,0});
        for (int k = 1; k < 12; k++) {
            int tail_y = (y - k + matrix_rows) % matrix_rows * MATRIX_FONT_SIZE;
            double fade = 1.0 - k * 0.08;
            char t = matrix_charset[rand() % matrix_charset_len];
            cairo_set_source_rgba(cr, 0.2, 1.0, 0.2, fade);
            cairo_move_to(cr, x * MATRIX_FONT_SIZE, tail_y);
            cairo_show_text(cr, (char[]){t,0});
        }
    }
    cairo_destroy(cr);
    return FALSE;
}

static gboolean matrix_on_timeout(gpointer user_data) {
    for (int x = 0; x < matrix_cols; x++) {
        if (rand() % 5 == 0)
            matrix_y[x] = (matrix_y[x] + 1) % matrix_rows;
    }
    gtk_widget_queue_draw(GTK_WIDGET(user_data));
    return TRUE;
}

static gboolean matrix_on_configure(GtkWidget *widget, GdkEventConfigure *event, gpointer data) {
    ScreensaverContext *ctx = (ScreensaverContext *)data;
    common_update_screen_size(ctx, widget);
    matrix_init_cols();
    return FALSE;
}

static void matrix_on_destroy(GtkWidget *widget, gpointer data) {
    ScreensaverContext *ctx = (ScreensaverContext *)data;
    if (matrix_y) { free(matrix_y); matrix_y = NULL; }
    common_destroy_window(ctx);
}

static void matrix_screensaver_show() {
    srand(time(NULL));
    matrix_charset_len = strlen(matrix_charset);
    matrix_init_cols();
    common_init_window(&matrix_ctx, G_CALLBACK(matrix_on_draw), G_CALLBACK(matrix_on_configure), G_CALLBACK(matrix_on_destroy));
    common_start_timeout(&matrix_ctx, MATRIX_FPS, G_SOURCE_FUNC(matrix_on_timeout));
}

static void matrix_screensaver_hide() {
    common_destroy_window(&matrix_ctx);
}

#endif // MATRIX_SCREENSAVER_H