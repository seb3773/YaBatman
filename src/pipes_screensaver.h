#ifndef PIPES_SCREENSAVER_H
#define PIPES_SCREENSAVER_H

#include <math.h>
#include "common_gfx.h"

#define PIPES_NUM_PIPES 8
#define PIPES_PIPE_LENGTH 600
#define PIPES_STEP 16
#define PIPES_FPS 30
#define PIPES_BORDER_OFFSET 4

typedef struct {
    double x, y, z;
} PipesPoint3D;

typedef struct {
    double x, y;
} PipesPoint2D;

typedef struct {
    PipesPoint3D points3d[PIPES_PIPE_LENGTH + 1];
    double r, g, b;
    int progress;
} PipesPipe;

static ScreensaverContext pipes_ctx = {0};
static int screen_cx = 400, screen_cy = 300;
static double camera_z = 400.0;
static PipesPipe pipes_pipes[PIPES_NUM_PIPES];

static void pipes_lighten_color(double r, double g, double b, double *lr, double *lg, double *lb) {
    double factor = 0.6;
    *lr = r + (1.0 - r) * factor;
    *lg = g + (1.0 - g) * factor;
    *lb = b + (1.0 - b) * factor;
}

static void pipes_project_point(const PipesPoint3D *p3d, PipesPoint2D *p2d) {
    double factor = camera_z / (camera_z + p3d->z);
    p2d->x = screen_cx + p3d->x * factor;
    p2d->y = screen_cy + p3d->y * factor;
}

static void pipes_generate_pipe(PipesPipe *p) {
    p->r = (double)rand() / RAND_MAX;
    p->g = (double)rand() / RAND_MAX;
    p->b = (double)rand() / RAND_MAX;
    p->points3d[0].x = (rand() % pipes_ctx.screen_width) - pipes_ctx.screen_width/2;
    p->points3d[0].y = (rand() % pipes_ctx.screen_height) - pipes_ctx.screen_height/2;
    p->points3d[0].z = (rand() % 250) - 125;
    int dir = rand() % 6;
    int dx = 0, dy = 0, dz = 0;
    if (dir == 0) dx = 1;
    else if (dir == 1) dx = -1;
    else if (dir == 2) dy = 1;
    else if (dir == 3) dy = -1;
    else if (dir == 4) dz = 1;
    else dz = -1;
    for (int i = 1; i <= PIPES_PIPE_LENGTH; i++) {
        if (rand() % 10 == 0) {
            int newdir = rand() % 6;
            if (newdir == 0)      { dx = 1; dy = dz = 0; }
            else if (newdir == 1) { dx = -1; dy = dz = 0; }
            else if (newdir == 2) { dy = 1; dx = dz = 0; }
            else if (newdir == 3) { dy = -1; dx = dz = 0; }
            else if (newdir == 4) { dz = 1; dx = dy = 0; }
            else                  { dz = -1; dx = dy = 0; }
        }
        double nx = p->points3d[i-1].x + PIPES_STEP * dx;
        double ny = p->points3d[i-1].y + PIPES_STEP * dy;
        double nz = p->points3d[i-1].z + PIPES_STEP * dz;
        if (nx < -pipes_ctx.screen_width/2 || nx > pipes_ctx.screen_width/2) dx = -dx;
        if (ny < -pipes_ctx.screen_height/2 || ny > pipes_ctx.screen_height/2) dy = -dy;
        if (nz < -200 || nz > 200) dz = -dz;
        nx = p->points3d[i-1].x + PIPES_STEP * dx;
        ny = p->points3d[i-1].y + PIPES_STEP * dy;
        nz = p->points3d[i-1].z + PIPES_STEP * dz;
        p->points3d[i].x = nx;
        p->points3d[i].y = ny;
        p->points3d[i].z = nz;
    }
    p->progress = 1;
}

static void pipes_init() {
    for (int i = 0; i < PIPES_NUM_PIPES; i++)
        pipes_generate_pipe(&pipes_pipes[i]);
}

static gboolean pipes_on_draw(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    ScreensaverContext *ctx = (ScreensaverContext *)data;
    cairo_t *cr = gdk_cairo_create(widget->window);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);
    for (int i = 0; i < PIPES_NUM_PIPES; i++) {
        PipesPipe *p = &pipes_pipes[i];
        double lr, lg, lb;
        pipes_lighten_color(p->r, p->g, p->b, &lr, &lg, &lb);
        PipesPoint2D proj[PIPES_PIPE_LENGTH + 1];
        for (int j = 0; j <= p->progress; j++)
            pipes_project_point(&p->points3d[j], &proj[j]);
        cairo_set_source_rgb(cr, lr, lg, lb);
        cairo_set_line_width(cr, 18.0);
        cairo_move_to(cr, proj[0].x - PIPES_BORDER_OFFSET, proj[0].y - PIPES_BORDER_OFFSET);
        for (int j = 1; j <= p->progress; j++)
            cairo_line_to(cr, proj[j].x - PIPES_BORDER_OFFSET, proj[j].y - PIPES_BORDER_OFFSET);
        cairo_stroke(cr);
        cairo_set_source_rgb(cr, p->r, p->g, p->b);
        cairo_set_line_width(cr, 8.0);
        cairo_move_to(cr, proj[0].x, proj[0].y);
        for (int j = 1; j <= p->progress; j++)
            cairo_line_to(cr, proj[j].x, proj[j].y);
        cairo_stroke(cr);
    }
    cairo_destroy(cr);
    return FALSE;
}

static gboolean pipes_on_timeout(gpointer user_data) {
    gboolean redraw = FALSE;
    gboolean all_finished = TRUE;
    for (int i = 0; i < PIPES_NUM_PIPES; i++) {
        if (pipes_pipes[i].progress < PIPES_PIPE_LENGTH) {
            pipes_pipes[i].progress++;
            redraw = TRUE;
            all_finished = FALSE;
        }
    }
    if (all_finished) {
        pipes_init();
        redraw = TRUE;
    }
    if (redraw) gtk_widget_queue_draw(GTK_WIDGET(user_data));
    return TRUE;
}

static gboolean pipes_on_configure(GtkWidget *widget, GdkEventConfigure *event, gpointer data) {
    ScreensaverContext *ctx = (ScreensaverContext *)data;
    common_update_screen_size(ctx, widget);
    screen_cx = ctx->screen_width / 2;
    screen_cy = ctx->screen_height / 2;
    pipes_init();
    return FALSE;
}

static void pipes_on_destroy(GtkWidget *widget, gpointer data) {
    ScreensaverContext *ctx = (ScreensaverContext *)data;
    common_destroy_window(ctx);
}

static void pipes_screensaver_show() {
    srand(time(NULL));
    GdkScreen *screen = gdk_screen_get_default();
    pipes_ctx.screen_width = gdk_screen_get_width(screen);
    pipes_ctx.screen_height = gdk_screen_get_height(screen);
    screen_cx = pipes_ctx.screen_width / 2;
    screen_cy = pipes_ctx.screen_height / 2;
    pipes_init();
    common_init_window(&pipes_ctx, G_CALLBACK(pipes_on_draw), G_CALLBACK(pipes_on_configure), G_CALLBACK(pipes_on_destroy));
    common_start_timeout(&pipes_ctx, PIPES_FPS, G_SOURCE_FUNC(pipes_on_timeout));
}

static void pipes_screensaver_hide() {
    common_destroy_window(&pipes_ctx);
}

#endif // PIPES_SCREENSAVER_H