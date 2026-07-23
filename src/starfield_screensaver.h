#ifndef STARFIELD_SCREENSAVER_H
#define STARFIELD_SCREENSAVER_H

#include <math.h>
#include <stdlib.h>
#include <time.h>
#include "common_gfx.h"

#define STARFIELD_N 600
#define STARFIELD_S1 2.0
#define STARFIELD_S2 8.0
#define STARFIELD_FPS 60
#define STARFIELD_MIN_RADIUS 3.0
#define STARFIELD_MAX_RADIUS 50.0

typedef struct { double a, r, s, rgb_r, rgb_g, rgb_b; } StarfieldStar;

static ScreensaverContext starfield_ctx = {0};
static int starfield_cx = 400, starfield_cy = 300;
static StarfieldStar starfield_stars[STARFIELD_N];

#define STARFIELD_RND ((double)rand()/(double)RAND_MAX)
#define STARFIELD_TAU 6.283185307179586
#define STARFIELD_INIT_STAR(i) do{ \
    starfield_stars[i].a = STARFIELD_RND * STARFIELD_TAU; \
    starfield_stars[i].r = STARFIELD_MIN_RADIUS + STARFIELD_RND * (STARFIELD_MAX_RADIUS - STARFIELD_MIN_RADIUS); \
    starfield_stars[i].s = STARFIELD_S1 + (STARFIELD_S2 - STARFIELD_S1) * STARFIELD_RND; \
    starfield_stars[i].rgb_r = 0.5 + STARFIELD_RND * 0.5; \
    starfield_stars[i].rgb_g = 0.5 + STARFIELD_RND * 0.5; \
    starfield_stars[i].rgb_b = 0.5 + STARFIELD_RND * 0.5; \
}while(0)

static void starfield_stars_init() { 
    srand(time(NULL));
    for(int i = 0; i < STARFIELD_N; i++) STARFIELD_INIT_STAR(i); 
}

static void starfield_stars_update() {
    for(int i = 0; i < STARFIELD_N; i++) {
        starfield_stars[i].r += starfield_stars[i].s;
        double x = starfield_cx + cos(starfield_stars[i].a) * starfield_stars[i].r;
        double y = starfield_cy + sin(starfield_stars[i].a) * starfield_stars[i].r;
        if(x < 0 || x >= starfield_ctx.screen_width || y < 0 || y >= starfield_ctx.screen_height) 
            STARFIELD_INIT_STAR(i);
    }
}

static gboolean starfield_on_draw(GtkWidget *w, GdkEventExpose *e, gpointer d) {
    ScreensaverContext *ctx = (ScreensaverContext *)d;
    cairo_t *cr = gdk_cairo_create(w->window);
    starfield_cx = ctx->screen_width / 2; 
    starfield_cy = ctx->screen_height / 2;
    cairo_set_source_rgb(cr, 0, 0, 0); 
    cairo_paint(cr);
    for(int i = 0; i < STARFIELD_N; i++) {
        double pr = starfield_stars[i].r - starfield_stars[i].s;
        double x0 = starfield_cx + cos(starfield_stars[i].a) * pr;
        double y0 = starfield_cy + sin(starfield_stars[i].a) * pr;
        double x1 = starfield_cx + cos(starfield_stars[i].a) * starfield_stars[i].r;
        double y1 = starfield_cy + sin(starfield_stars[i].a) * starfield_stars[i].r;
        cairo_set_source_rgb(cr, starfield_stars[i].rgb_r, starfield_stars[i].rgb_g, starfield_stars[i].rgb_b);
        cairo_set_line_width(cr, 1.0 + 2.0 * (starfield_stars[i].r / (starfield_cx < starfield_cy ? starfield_cx : starfield_cy)));
        cairo_move_to(cr, x0, y0); 
        cairo_line_to(cr, x1, y1); 
        cairo_stroke(cr);
    }
    cairo_destroy(cr);
    return FALSE;
}

static gboolean starfield_on_timeout(gpointer user_data) {
    starfield_stars_update();
    gtk_widget_queue_draw(GTK_WIDGET(user_data));
    return TRUE;
}

static gboolean starfield_on_configure(GtkWidget *w, GdkEventConfigure *e, gpointer d) {
    ScreensaverContext *ctx = (ScreensaverContext *)d;
    common_update_screen_size(ctx, w);
    starfield_cx = ctx->screen_width / 2;
    starfield_cy = ctx->screen_height / 2;
    return FALSE;
}

static void starfield_on_destroy(GtkWidget *widget, gpointer data) {
    ScreensaverContext *ctx = (ScreensaverContext *)data;
    common_destroy_window(ctx);
}

static void starfield_screensaver_show() {
    starfield_stars_init();
    common_init_window(&starfield_ctx, G_CALLBACK(starfield_on_draw), G_CALLBACK(starfield_on_configure), G_CALLBACK(starfield_on_destroy));
    common_start_timeout(&starfield_ctx, STARFIELD_FPS, G_SOURCE_FUNC(starfield_on_timeout));
}

static void starfield_screensaver_hide() {
    common_destroy_window(&starfield_ctx);
}

#endif // STARFIELD_SCREENSAVER_H