#include "mpris_detection.h"

#include <gio/gio.h>
#include <string.h>

int mpris_poll(MprisStatus *status) {
    if (!status) {
        return -1;
    }

    status->playing = 0;
    status->type = 0;

    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    if (!bus) {
        return -1;
    }

    GError *error = NULL;
    GDBusProxy *dbus_proxy = g_dbus_proxy_new_sync(
        bus, G_DBUS_PROXY_FLAGS_NONE, NULL,
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        NULL, &error
    );
    if (!dbus_proxy) {
        if (error) {
            g_error_free(error);
        }
        g_object_unref(bus);
        return -1;
    }

    GVariant *reply = g_dbus_proxy_call_sync(
        dbus_proxy, "ListNames", NULL,
        G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error
    );
    if (!reply) {
        if (error) {
            g_error_free(error);
        }
        g_object_unref(dbus_proxy);
        g_object_unref(bus);
        return -1;
    }

    GVariant *array = g_variant_get_child_value(reply, 0);
    GVariantIter iter;
    g_variant_iter_init(&iter, array);
    const gchar *name;
    gboolean playing_found = FALSE;
    gboolean type_detected = FALSE;

    while (g_variant_iter_next(&iter, "s", &name)) {
        if (!g_str_has_prefix(name, "org.mpris.MediaPlayer2.")) {
            continue;
        }

        GDBusProxy *player_proxy = g_dbus_proxy_new_sync(
            bus, G_DBUS_PROXY_FLAGS_NONE, NULL,
            name,
            "/org/mpris/MediaPlayer2",
            "org.freedesktop.DBus.Properties",
            NULL, NULL
        );
        if (!player_proxy) {
            continue;
        }

        GVariant *result = g_dbus_proxy_call_sync(
            player_proxy, "Get",
            g_variant_new("(ss)", "org.mpris.MediaPlayer2.Player", "PlaybackStatus"),
            G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL
        );
        if (result) {
            GVariant *inner = g_variant_get_child_value(result, 0);
            const gchar *playback_status = NULL;
            if (g_variant_is_of_type(inner, G_VARIANT_TYPE_VARIANT)) {
                GVariant *strv = g_variant_get_variant(inner);
                if (g_variant_is_of_type(strv, G_VARIANT_TYPE_STRING)) {
                    playback_status = g_variant_get_string(strv, NULL);
                }
                g_variant_unref(strv);
            } else if (g_variant_is_of_type(inner, G_VARIANT_TYPE_STRING)) {
                playback_status = g_variant_get_string(inner, NULL);
            }
            if (playback_status && g_strcmp0(playback_status, "Playing") == 0) {
                playing_found = TRUE;
            }
            g_variant_unref(inner);
            g_variant_unref(result);
        }

        if (playing_found && !type_detected) {
            status->type = 0;
            if (g_strrstr(name, "firefox") || g_strrstr(name, "chromium") || g_strrstr(name, "chrome")) {
                status->type = 0;
                type_detected = TRUE;
            } else {
                GVariant *meta_result = g_dbus_proxy_call_sync(
                    player_proxy, "Get",
                    g_variant_new("(ss)", "org.mpris.MediaPlayer2.Player", "Metadata"),
                    G_DBUS_CALL_FLAGS_NONE, -1, NULL, NULL
                );
                if (meta_result) {
                    GVariant *meta_v = g_variant_get_child_value(meta_result, 0);
                    if (g_variant_is_of_type(meta_v, G_VARIANT_TYPE_VARIANT)) {
                        GVariant *metadata = g_variant_get_variant(meta_v);
                        GVariant *url_v = g_variant_lookup_value(metadata, "xesam:url", NULL);
                        if (url_v && g_variant_is_of_type(url_v, G_VARIANT_TYPE_STRING)) {
                            const gchar *url = g_variant_get_string(url_v, NULL);
                            if (url) {
                                if (g_str_has_suffix(url, ".mp3") ||
                                    g_str_has_suffix(url, ".ogg") ||
                                    g_str_has_suffix(url, ".flac") ||
                                    g_str_has_suffix(url, ".wav") ||
                                    g_str_has_suffix(url, ".m4a") ||
                                    g_str_has_suffix(url, ".aac")) {
                                    status->type = 1;
                                    type_detected = TRUE;
                                }
                            }
                            g_variant_unref(url_v);
                        }
                        g_variant_unref(metadata);
                    }
                    g_variant_unref(meta_v);
                    g_variant_unref(meta_result);
                }
            }
        }

        g_object_unref(player_proxy);
    }

    status->playing = playing_found ? 1 : 0;

    g_variant_unref(array);
    g_variant_unref(reply);
    g_object_unref(dbus_proxy);
    g_object_unref(bus);
    return 0;
}
