#ifndef SLIDESHOW_SCREENSAVER_H
#define SLIDESHOW_SCREENSAVER_H

#include <dirent.h>
#include <string.h>
#include "common_gfx.h"

static ScreensaverContext slideshow_ctx = {0};
static char **image_paths = NULL;
static int image_count = 0;
static int current_image_index = 0;
static GdkPixbuf *current_image = NULL;
static GdkPixbuf *current_image_original = NULL;
static GdkPixbuf *next_image = NULL;
static GdkPixbuf *next_image_original = NULL;
static const char *image_dir = "";
static float fade_alpha = 1.0f;
static gboolean in_fade = FALSE;
static int fade_step = 0;
static guint fade_timeout_id = 0;
static const int FADE_STEPS = 20;
static const int FADE_DURATION_MS = 1000;
static const int FADE_INTERVAL_MS = FADE_DURATION_MS / FADE_STEPS;

// Variables pour l'effet de zoom
static float zoom_factor = 1.0f;
static guint zoom_timeout_id = 0;
static const float ZOOM_START = 1.0f;
static const float ZOOM_END = 1.10f;
static const int ZOOM_INTERVAL_MS = 50;
static const int IMAGE_DISPLAY_TIME_MS = 10000;
static int zoom_elapsed_time = 0;
static gboolean slideshow_fade_timeout(gpointer user_data);
static gboolean slideshow_on_timeout(gpointer user_data);
static gboolean slideshow_zoom_timeout(gpointer user_data);
static GdkPixbuf* create_zoomed_image(GdkPixbuf *original, float zoom) {
    if (!original) return NULL;
    int orig_width = gdk_pixbuf_get_width(original);
    int orig_height = gdk_pixbuf_get_height(original);
    int new_width = (int)(orig_width * zoom);
    int new_height = (int)(orig_height * zoom);
    GdkPixbuf *zoomed = gdk_pixbuf_scale_simple(original, new_width, new_height, GDK_INTERP_BILINEAR);
    return zoomed;
}

static void slideshow_load_images() {
    DIR *dir = opendir(image_dir);
    if (!dir) return;
    struct dirent *entry;
    int capacity = 10;
    image_paths = g_malloc(capacity * sizeof(char *));
    image_count = 0;
    while ((entry = readdir(dir))) {
        if (entry->d_type != DT_REG) continue;
        const char *ext = strrchr(entry->d_name, '.');
        if (!ext || (strcasecmp(ext, ".jpg") != 0 && strcasecmp(ext, ".png") != 0)) continue;
        if (image_count >= capacity) {
            capacity *= 2;
            image_paths = g_realloc(image_paths, capacity * sizeof(char *));
        }
        char *full_path = g_build_filename(image_dir, entry->d_name, NULL);
        image_paths[image_count++] = full_path;
    }
    closedir(dir);
}

static void slideshow_free_images() {
    if (current_image) {
        g_object_unref(current_image);
        current_image = NULL;
    }
    if (current_image_original) {
        g_object_unref(current_image_original);
        current_image_original = NULL;
    }
    if (next_image) {
        g_object_unref(next_image);
        next_image = NULL;
    }
    if (next_image_original) {
        g_object_unref(next_image_original);
        next_image_original = NULL;
    }
    for (int i = 0; i < image_count; i++) {
        g_free(image_paths[i]);
    }
    g_free(image_paths);
    image_paths = NULL;
    image_count = 0;
    current_image_index = 0;
    fade_alpha = 1.0f;
    in_fade = FALSE;
    fade_step = 0;
    zoom_factor = ZOOM_START;
    zoom_elapsed_time = 0;
    if (fade_timeout_id) {
        g_source_remove(fade_timeout_id);
        fade_timeout_id = 0;
    }
    if (zoom_timeout_id) {
        g_source_remove(zoom_timeout_id);
        zoom_timeout_id = 0;
    }
}

static void slideshow_load_next_image() {
    if (image_count == 0) return;
    int next_index = (current_image_index + 1) % image_count;
    if (next_image) {
        g_object_unref(next_image);
        next_image = NULL;
    }
    if (next_image_original) {
        g_object_unref(next_image_original);
        next_image_original = NULL;
    }
    GdkPixbuf *loaded_image = gdk_pixbuf_new_from_file(image_paths[next_index], NULL);
    if (!loaded_image) {
        current_image_index = next_index;
        return;
    }
    int img_width = gdk_pixbuf_get_width(loaded_image);
    int img_height = gdk_pixbuf_get_height(loaded_image);
    float scale = MAX((float)slideshow_ctx.screen_width / img_width, (float)slideshow_ctx.screen_height / img_height);
    next_image_original = gdk_pixbuf_scale_simple(loaded_image, img_width * scale, img_height * scale, GDK_INTERP_BILINEAR);
    g_object_unref(loaded_image);
    next_image = create_zoomed_image(next_image_original, ZOOM_START);
    in_fade = TRUE;
    fade_step = 0;
    fade_alpha = 0.0f;
}

static gboolean slideshow_zoom_timeout(gpointer user_data) {
    if (in_fade || !current_image_original) return TRUE;
    zoom_elapsed_time += ZOOM_INTERVAL_MS;
    float progress = (float)zoom_elapsed_time / (float)IMAGE_DISPLAY_TIME_MS;
    if (progress > 1.0f) {
        progress = 1.0f;
    }
    zoom_factor = ZOOM_START + (ZOOM_END - ZOOM_START) * progress;
    if (current_image) {
        g_object_unref(current_image);
    }
    current_image = create_zoomed_image(current_image_original, zoom_factor);
    gtk_widget_queue_draw(GTK_WIDGET(user_data));
    return TRUE;
}

static gboolean slideshow_on_draw(GtkWidget *widget, GdkEventExpose *event, gpointer data) {
    ScreensaverContext *ctx = (ScreensaverContext *)data;
    cairo_t *cr = gdk_cairo_create(widget->window);
    common_update_screen_size(ctx, widget);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);
    if (current_image) {
        int img_width = gdk_pixbuf_get_width(current_image);
        int img_height = gdk_pixbuf_get_height(current_image);
        int x = (ctx->screen_width - img_width) / 2;
        int y = (ctx->screen_height - img_height) / 2;
        gdk_cairo_set_source_pixbuf(cr, current_image, x, y);
        if (in_fade) {
            cairo_paint_with_alpha(cr, 1.0f - fade_alpha);
        } else {
            cairo_paint(cr);
        }
        if (in_fade && next_image) {
            img_width = gdk_pixbuf_get_width(next_image);
            img_height = gdk_pixbuf_get_height(next_image);
            x = (ctx->screen_width - img_width) / 2;
            y = (ctx->screen_height - img_height) / 2;
            gdk_cairo_set_source_pixbuf(cr, next_image, x, y);
            cairo_paint_with_alpha(cr, fade_alpha);
        }
    }
    cairo_destroy(cr);
    return FALSE;
}

static gboolean slideshow_fade_timeout(gpointer user_data) {
    if (!in_fade) return FALSE;
    fade_step++;
    fade_alpha = (float)fade_step / FADE_STEPS;
    if (fade_step >= FADE_STEPS) {
        if (current_image) {
            g_object_unref(current_image);
        }
        if (current_image_original) {
            g_object_unref(current_image_original);
        }
        current_image = next_image;
        current_image_original = next_image_original;
        next_image = NULL;
        next_image_original = NULL;
        in_fade = FALSE;
        fade_alpha = 1.0f;
        zoom_factor = ZOOM_START;
        zoom_elapsed_time = 0;
        current_image_index = (current_image_index + 1) % image_count;
        fade_timeout_id = 0;
        slide_start_timeout(&slideshow_ctx, 1, G_SOURCE_FUNC(slideshow_on_timeout));
        zoom_timeout_id = g_timeout_add(ZOOM_INTERVAL_MS, G_SOURCE_FUNC(slideshow_zoom_timeout), user_data);
        gtk_widget_queue_draw(GTK_WIDGET(user_data));
        return FALSE;
    }
    gtk_widget_queue_draw(GTK_WIDGET(user_data));
    return TRUE;
}

static gboolean slideshow_on_timeout(gpointer user_data) {
    if (slideshow_ctx.timeout_id) {
        g_source_remove(slideshow_ctx.timeout_id);
        slideshow_ctx.timeout_id = 0;
    }

    if (zoom_timeout_id) {
        g_source_remove(zoom_timeout_id);
        zoom_timeout_id = 0;
    }

    slideshow_load_next_image();
    if (in_fade && next_image) {
        fade_timeout_id = g_timeout_add(FADE_INTERVAL_MS, G_SOURCE_FUNC(slideshow_fade_timeout), user_data);
    } else {
        slide_start_timeout(&slideshow_ctx, 1, G_SOURCE_FUNC(slideshow_on_timeout));
    }
    gtk_widget_queue_draw(GTK_WIDGET(user_data));
    return FALSE;
}

static gboolean slideshow_on_configure(GtkWidget *widget, GdkEventConfigure *event, gpointer data) {
    ScreensaverContext *ctx = (ScreensaverContext *)data;
    common_update_screen_size(ctx, widget);
    return FALSE;
}

static void slideshow_on_destroy(GtkWidget *widget, gpointer data) {
    ScreensaverContext *ctx = (ScreensaverContext *)data;
    slideshow_free_images();
    common_destroy_window(ctx);
}

static gboolean slideshow_load_first_image_delayed(gpointer user_data) {
    if (image_count > 0) {
        common_update_screen_size(&slideshow_ctx, slideshow_ctx.drawing_area);
        GdkPixbuf *loaded_image = gdk_pixbuf_new_from_file(image_paths[current_image_index], NULL);
        if (loaded_image) {
            int img_width = gdk_pixbuf_get_width(loaded_image);
            int img_height = gdk_pixbuf_get_height(loaded_image);
            float scale = MAX((float)slideshow_ctx.screen_width / img_width, (float)slideshow_ctx.screen_height / img_height);
            current_image_original = gdk_pixbuf_scale_simple(loaded_image, img_width * scale, img_height * scale, GDK_INTERP_BILINEAR);
            g_object_unref(loaded_image);
            current_image = create_zoomed_image(current_image_original, ZOOM_START);
        }
        gtk_widget_queue_draw(slideshow_ctx.drawing_area);
        current_image_index = (current_image_index + 1) % image_count;
        slide_start_timeout(&slideshow_ctx, 1, G_SOURCE_FUNC(slideshow_on_timeout));
        zoom_timeout_id = g_timeout_add(ZOOM_INTERVAL_MS, G_SOURCE_FUNC(slideshow_zoom_timeout), slideshow_ctx.drawing_area);
    }
    return FALSE;
}

static void slideshow_screensaver_show() {
    slideshow_load_images();
    if (image_count > 0) {
        common_init_window(&slideshow_ctx, G_CALLBACK(slideshow_on_draw), G_CALLBACK(slideshow_on_configure), G_CALLBACK(slideshow_on_destroy));
        g_timeout_add(50, slideshow_load_first_image_delayed, NULL);
    }
}

static void slideshow_set_image_directory(const char *dir) {
    image_dir = dir;
}

static void slideshow_screensaver_hide() {
    common_destroy_window(&slideshow_ctx);
    slideshow_free_images();
}

#endif // SLIDESHOW_SCREENSAVER_H