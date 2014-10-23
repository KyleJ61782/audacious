/*
 * infopopup.c
 * Copyright 2006-2012 William Pitcock, Giacomo Lozito, John Lindgren, and
 *                     Thomas Lange
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the documentation
 *    provided with the distribution.
 *
 * This software is provided "as is" and without any warranty, express or
 * implied. In no event shall the authors be liable for any damages arising from
 * the use of this software.
 */

#include <gtk/gtk.h>
#include <string.h>

#include <libaudcore/audstrings.h>
#include <libaudcore/drct.h>
#include <libaudcore/hook.h>
#include <libaudcore/i18n.h>
#include <libaudcore/playlist.h>
#include <libaudcore/runtime.h>
#include <libaudcore/tuple.h>

#include "internal.h"
#include "libaudgui.h"
#include "libaudgui-gtk.h"

#define IMAGE_SIZE 96

static void infopopup_move_to_mouse (GtkWidget * infopopup);

static struct {
    GtkWidget * title_header, * title_label;
    GtkWidget * artist_header, * artist_label;
    GtkWidget * album_header, * album_label;
    GtkWidget * genre_header, * genre_label;
    GtkWidget * year_header, * year_label;
    GtkWidget * track_header, * track_label;
    GtkWidget * length_header, * length_label;
    GtkWidget * image;
    GtkWidget * progress;
} widgets;

static String current_file;
static GtkWidget * infopopup_queued;
static int progress_source = 0;

/* returns false if album art fetch was queued */
static bool infopopup_display_image (const char * filename)
{
    bool queued;
    GdkPixbuf * pb = audgui_pixbuf_request (filename, & queued);
    if (! pb)
        return ! queued;

    audgui_pixbuf_scale_within (& pb, IMAGE_SIZE);
    gtk_image_set_from_pixbuf ((GtkImage *) widgets.image, pb);
    gtk_widget_show (widgets.image);

    g_object_unref (pb);
    return true;
}

static void infopopup_art_ready (const char * filename)
{
    if (! infopopup_queued || strcmp (filename, current_file))
        return;

    infopopup_display_image (filename);
    audgui_show_unique_window (AUDGUI_INFOPOPUP_WINDOW, infopopup_queued);
    infopopup_queued = nullptr;
}

static gboolean infopopup_progress_cb (void * unused)
{
    String filename;
    int length = 0, time = 0;

    if (aud_drct_get_playing ())
    {
        filename = aud_drct_get_filename ();
        length = aud_drct_get_length ();
        time = aud_drct_get_time ();
    }

    if (aud_get_bool (nullptr, "filepopup_showprogressbar") && filename &&
     current_file && ! strcmp (filename, current_file) && length > 0)
    {
        gtk_progress_bar_set_fraction ((GtkProgressBar *) widgets.progress, time / (float) length);
        gtk_progress_bar_set_text ((GtkProgressBar *) widgets.progress, str_format_time (time));
        gtk_widget_show (widgets.progress);
    }
    else
        gtk_widget_hide (widgets.progress);

    return true;
}

static void infopopup_add_category (GtkWidget * grid, int position,
 const char * text, GtkWidget * * header, GtkWidget * * label)
{
    * header = gtk_label_new (nullptr);
    * label = gtk_label_new (nullptr);

    gtk_widget_set_halign (* header, GTK_ALIGN_END);
    gtk_widget_set_halign (* label, GTK_ALIGN_START);

    char * markup = g_markup_printf_escaped ("<span style=\"italic\">%s</span>", text);
    gtk_label_set_markup ((GtkLabel *) * header, markup);
    g_free (markup);

    gtk_grid_attach ((GtkGrid *) grid, * header, 0, position, 1, 1);
    gtk_grid_attach ((GtkGrid *) grid, * label, 1, position, 1, 1);

    gtk_widget_set_no_show_all (* header, true);
    gtk_widget_set_no_show_all (* label, true);
}

static void infopopup_destroyed (void)
{
    hook_dissociate ("art ready", (HookFunction) infopopup_art_ready);

    if (progress_source)
    {
        g_source_remove (progress_source);
        progress_source = 0;
    }

    memset (& widgets, 0, sizeof widgets);

    current_file = String ();
    infopopup_queued = nullptr;
}

static GtkWidget * infopopup_create (void)
{
    GtkWidget * infopopup = gtk_window_new (GTK_WINDOW_POPUP);
    gtk_window_set_type_hint ((GtkWindow *) infopopup, GDK_WINDOW_TYPE_HINT_TOOLTIP);
    gtk_window_set_decorated ((GtkWindow *) infopopup, false);
    gtk_container_set_border_width ((GtkContainer *) infopopup, 4);

    GtkWidget * hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL,  6);
    gtk_container_add ((GtkContainer *) infopopup, hbox);

    widgets.image = gtk_image_new ();
    gtk_widget_set_size_request (widgets.image, IMAGE_SIZE, IMAGE_SIZE);
    gtk_box_pack_start ((GtkBox *) hbox, widgets.image, false, false, 0);
    gtk_widget_set_no_show_all (widgets.image, true);

    GtkWidget * grid = gtk_grid_new ();
    gtk_grid_set_column_spacing ((GtkGrid *) grid, 6);
    gtk_box_pack_start ((GtkBox *) hbox, grid, true, true, 0);

    infopopup_add_category (grid, 0, _("Title"), & widgets.title_header, & widgets.title_label);
    infopopup_add_category (grid, 1, _("Artist"), & widgets.artist_header, & widgets.artist_label);
    infopopup_add_category (grid, 2, _("Album"), & widgets.album_header, & widgets.album_label);
    infopopup_add_category (grid, 3, _("Genre"), & widgets.genre_header, & widgets.genre_label);
    infopopup_add_category (grid, 4, _("Year"), & widgets.year_header, & widgets.year_label);
    infopopup_add_category (grid, 5, _("Track"), & widgets.track_header, & widgets.track_label);
    infopopup_add_category (grid, 6, _("Length"), & widgets.length_header, & widgets.length_label);

    /* track progress */
    widgets.progress = gtk_progress_bar_new ();
    gtk_widget_set_margin_top (widgets.progress, 6);
    gtk_progress_bar_set_show_text ((GtkProgressBar *) widgets.progress, true);
    gtk_progress_bar_set_text ((GtkProgressBar *) widgets.progress, "");
    gtk_grid_attach ((GtkGrid *) grid, widgets.progress, 0, 7, 2, 1);

    /* do not show the track progress */
    gtk_widget_set_no_show_all (widgets.progress, true);

    g_signal_connect (infopopup, "realize", (GCallback) infopopup_move_to_mouse, nullptr);

    return infopopup;
}

static void infopopup_set_field (GtkWidget * header, GtkWidget * label, const char * text)
{
    if (text)
    {
        gtk_label_set_text ((GtkLabel *) label, text);
        gtk_widget_show (header);
        gtk_widget_show (label);
    }
    else
    {
        gtk_widget_hide (header);
        gtk_widget_hide (label);
    }
}

static void infopopup_set_fields (const Tuple & tuple, const char * title)
{
    /* use title from tuple if possible */
    String title2 = tuple.get_str (FIELD_TITLE);
    if (! title2)
        title2 = String (title);

    String artist = tuple.get_str (FIELD_ARTIST);
    String album = tuple.get_str (FIELD_ALBUM);
    String genre = tuple.get_str (FIELD_GENRE);

    infopopup_set_field (widgets.title_header, widgets.title_label, title2);
    infopopup_set_field (widgets.artist_header, widgets.artist_label, artist);
    infopopup_set_field (widgets.album_header, widgets.album_label, album);
    infopopup_set_field (widgets.genre_header, widgets.genre_label, genre);

    int value = tuple.get_int (FIELD_LENGTH);
    infopopup_set_field (widgets.length_header, widgets.length_label,
     (value > 0) ? (const char *) str_format_time (value) : nullptr);

    value = tuple.get_int (FIELD_YEAR);
    infopopup_set_field (widgets.year_header, widgets.year_label,
     (value > 0) ? (const char *) int_to_str (value) : nullptr);

    value = tuple.get_int (FIELD_TRACK_NUMBER);
    infopopup_set_field (widgets.track_header, widgets.track_label,
     (value > 0) ? (const char *) int_to_str (value) : nullptr);
}

static void infopopup_move_to_mouse (GtkWidget * infopopup)
{
    GdkScreen * screen = gtk_widget_get_screen (infopopup);
    GdkRectangle geom;
    int x, y, h, w;

    audgui_get_mouse_coords (screen, & x, & y);
    audgui_get_monitor_geometry (screen, x, y, & geom);
    gtk_window_get_size ((GtkWindow *) infopopup, & w, & h);

    /* If we show the popup right under the cursor, the underlying window gets
     * a "leave-notify-event" and immediately hides the popup again.  So, we
     * offset the popup slightly. */
    if (x + w > geom.x + geom.width)
        x -= w + 3;
    else
        x += 3;

    if (y + h > geom.y + geom.height)
        y -= h + 3;
    else
        y += 3;

    gtk_window_move ((GtkWindow *) infopopup, x, y);
}

static void infopopup_show (const char * filename, const Tuple & tuple,
 const char * title)
{
    audgui_infopopup_hide ();

    current_file = String (filename);

    GtkWidget * infopopup = infopopup_create ();
    infopopup_set_fields (tuple, title);

    hook_associate ("art ready", (HookFunction) infopopup_art_ready, nullptr);

    g_signal_connect (infopopup, "destroy", (GCallback) infopopup_destroyed, nullptr);

    /* start a timer that updates a progress bar if the tooltip
       is shown for the song that is being currently played */
    if (! progress_source)
        progress_source = g_timeout_add (500, infopopup_progress_cb, nullptr);

    /* immediately run the callback once to update progressbar status */
    infopopup_progress_cb (nullptr);

    if (infopopup_display_image (filename))
        audgui_show_unique_window (AUDGUI_INFOPOPUP_WINDOW, infopopup);
    else
        infopopup_queued = infopopup;
}

EXPORT void audgui_infopopup_show (int playlist, int entry)
{
    String filename = aud_playlist_entry_get_filename (playlist, entry);
    String title = aud_playlist_entry_get_title (playlist, entry, false);
    Tuple tuple = aud_playlist_entry_get_tuple (playlist, entry, false);

    if (filename && title && tuple)
        infopopup_show (filename, tuple, title);
}

EXPORT void audgui_infopopup_show_current (void)
{
    int playlist = aud_playlist_get_playing ();
    if (playlist < 0)
        playlist = aud_playlist_get_active ();

    int position = aud_playlist_get_position (playlist);
    if (position < 0)
        return;

    audgui_infopopup_show (playlist, position);
}

EXPORT void audgui_infopopup_hide (void)
{
    audgui_hide_unique_window (AUDGUI_INFOPOPUP_WINDOW);

    if (infopopup_queued)
        gtk_widget_destroy (infopopup_queued);
}
