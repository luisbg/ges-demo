/* GStreamer Editing Services
 * Copyright (C) Luis de Bethencourt <luis.debethencourt@collabora.co.uk>
 *               2012 Collabora Ltd
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <ges/ges.h>

/* Application Data ********************************************************/

/**
 * Contains most of the application data so that signal handlers
 * and other callbacks have easy access.
 */

typedef struct Clip
{
  guint id;
  gchar * uri;
  guint64 start;
  guint64 duration;
  guint64 in_point;
  guint64 max_duration;
  GESTimelineObject * tlobject;

} Clip;

typedef struct App
{
  /* back-end objects */
  GESTimeline *timeline;
  GESTimelinePipeline *pipeline;
  GESTrack *audio_track;
  GESTrack *video_track;
  guint audio_tracks;
  guint video_tracks;

  GtkListStore *timeline_store;
  GList *objects;
  Clip *selected_object;

  GstState state;

  /* widgets */
  GtkWidget *main_window;
  GtkTreeView *timeline_treeview;
  GtkEntry *start_entry;
  GtkHScale *duration_scale;
  GtkHScale *in_point_scale;

} App;

enum
{
  COL_ID = 0,
  COL_URI,
  COL_START,
  COL_DURATION,
  COL_IN_POINT,
  COL_MAX_DURATION,
  COL_TIMELINEOBJ
} ;

/* Prototypes for auto-connected signal handlers ***************************/

/**
 * These are declared non-static for signal auto-connection
 */

gboolean _window_delete_event_cb (GtkObject * window, GdkEvent * event,
    App * app);
gboolean _play_activate_cb (GtkObject * button, App * app);
gboolean _stop_activate_cb (GtkObject * button, App * app);
void _add_file_activated_cb (GtkAction * item, App * app);
gboolean _duration_scale_change_value_cb (GtkRange * range,
    GtkScrollType unused, gdouble value, App * app);
gboolean _in_point_scale_change_value_cb (GtkRange * range,
    GtkScrollType unused, gdouble value, App * app);
gboolean _start_changed (GtkEntry * entry, App * app);
gboolean _clip_selected (GtkTreeView * treeview, App * app);
gboolean _delete_activate_cb (GtkObject * button, App * app);


/* Backend callbacks ********************************************************/

static void
project_bus_message_cb (GstBus * bus, GstMessage * message,
    GMainLoop * mainloop)
{
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
      g_printerr ("ERROR\n");
      g_main_loop_quit (mainloop);
      break;
    case GST_MESSAGE_EOS:
      g_printerr ("Done\n");
      g_main_loop_quit (mainloop);
      break;
    default:
      break;
  }
}

static void
bus_message_cb (GstBus * bus, GstMessage * message, App * app)
{
  const GstStructure *s;
  s = gst_message_get_structure (message);

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
      g_print ("ERROR\n");
      break;
    case GST_MESSAGE_EOS:
      gst_element_set_state (GST_ELEMENT (app->pipeline), GST_STATE_READY);
      break;
    case GST_MESSAGE_STATE_CHANGED:
      if (s && GST_MESSAGE_SRC (message) == GST_OBJECT_CAST (app->pipeline)) {
        GstState old, new, pending;
        gst_message_parse_state_changed (message, &old, &new, &pending);
        app->state = new;
      }
      break;
    default:
      break;
  }
}

/* Static UI Callbacks ******************************************************/

static gboolean
check_time (const gchar * time)
{
  static GRegex *re = NULL;

  if (!re) {
    if (NULL == (re =
            g_regex_new ("^[0-9][0-9]:[0-5][0-9]:[0-5][0-9](\\.[0-9]+)?$",
                G_REGEX_EXTENDED, 0, NULL)))
      return FALSE;
  }

  if (g_regex_match (re, time, 0, NULL))
    return TRUE;
  return FALSE;
}

static guint64
str_to_time (const gchar * str)
{
  guint64 ret;
  guint64 h, m;
  gdouble s;
  gchar buf[15];

  buf[0] = str[0];
  buf[1] = str[1];
  buf[2] = '\0';

  h = strtoull (buf, NULL, 10);

  buf[0] = str[3];
  buf[1] = str[4];
  buf[2] = '\0';

  m = strtoull (buf, NULL, 10);

  strncpy (buf, &str[6], sizeof (buf));
  s = strtod (buf, NULL);

  ret = (h * 3600 * GST_SECOND) +
      (m * 60 * GST_SECOND) + ((guint64) (s * GST_SECOND));

  return ret;
}

/* UI Initialization ********************************************************/

static gboolean
create_ui (App * app)
{
  GtkBuilder *builder;
  GtkTreeView *timeline;
  GtkTreeViewColumn *duration_col;
  GtkCellRenderer *duration_renderer;
  GtkCellRenderer *background_type_renderer;
  GtkListStore *backgrounds;

  /* construct widget tree */

  builder = gtk_builder_new ();
  gtk_builder_add_from_file (builder, "data/ges-demo.glade", NULL);
  gtk_builder_connect_signals (builder, app);

  app->timeline_treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder,
      "timeline_treeview"));

  /* create the model for the treeview */

  app->timeline_store =
    gtk_list_store_new (5, G_TYPE_UINT64, G_TYPE_STRING, G_TYPE_LONG,
        G_TYPE_LONG, G_TYPE_LONG);
  gtk_tree_view_set_model (app->timeline_treeview,
      GTK_TREE_MODEL (app->timeline_store));

  /* get a bunch of widgets from the XML tree */

  app->start_entry = GTK_ENTRY (gtk_builder_get_object (builder,
      "start_entry"));
  app->duration_scale = GTK_HSCALE (gtk_builder_get_object (builder,
      "duration_scale"));
  app->in_point_scale = GTK_HSCALE (gtk_builder_get_object (builder,
      "in_point_scale"));
  app->main_window = GTK_WIDGET (gtk_builder_get_object (builder, "window"));

  /* success */
  g_object_unref (G_OBJECT (builder));

  return TRUE;
}

/* application methods ******************************************************/

static void selection_foreach (GtkTreeModel * model, GtkTreePath * path,
    GtkTreeIter * iter, gpointer user);

typedef struct
{
  GList *objects;
  guint n;
} select_info;

static void
app_add_file (App * app, gchar * uri)
{
  Clip *clip = g_new0 (Clip, 1);

  GESTimelineObject *src;
  GESTimelineLayer *layer;
  GstDiscoverer *disc;
  GstDiscovererInfo *info;
  GtkTreeIter iter;
  guint idf;
  guint64 duration;

  GST_DEBUG ("adding file %s", uri);

  idf = g_list_length (app->objects);

  /* Add the file to the GES Timeline */
  src = GES_TIMELINE_OBJECT (ges_timeline_filesource_new (uri));
  ges_timeline_object_set_priority (src, 0);

  layer = (GESTimelineLayer *) ges_timeline_layer_new ();
  ges_timeline_layer_add_object (layer, src);
  ges_timeline_add_layer (app->timeline, layer);

  disc = gst_discoverer_new (50000000000, NULL);
  info = gst_discoverer_discover_uri (disc, uri, NULL);
  duration = gst_discoverer_info_get_duration (info);

  gtk_list_store_append (app->timeline_store, &iter);
  gtk_list_store_set (app->timeline_store, &iter,
                      COL_ID, idf,
                      COL_URI, uri,
                      COL_START, 0,
                      COL_DURATION, duration,
                      COL_IN_POINT, 0,
                      -1);

  clip->id = idf;
  clip->uri = uri;
  clip->start = 0;
  clip->duration = duration;
  clip->in_point = 0;
  clip->max_duration = duration;
  clip->tlobject = src;
  app->objects = g_list_append (app->objects, clip);
}

static void
app_add_audio_track (App * app)
{
  if (app->audio_tracks)
    return;

  app->audio_track = ges_track_audio_raw_new ();
  ges_timeline_add_track (app->timeline, app->audio_track);
}

static void
app_remove_audio_track (App * app)
{
  if (!app->audio_tracks)
    return;

  ges_timeline_remove_track (app->timeline, app->audio_track);
  app->audio_track = NULL;
}

static void
app_add_video_track (App * app)
{
  if (app->video_tracks)
    return;

  app->video_track = ges_track_video_raw_new ();
  ges_timeline_add_track (app->timeline, app->video_track);
}

static void
app_remove_video_track (App * app)
{
  if (!app->video_tracks)
    return;

  ges_timeline_remove_track (app->timeline, app->video_track);
  app->video_track = NULL;
}

static void
app_dispose (App * app)
{
  g_print ("Closing gst-editing-services demo\n");

  if (app) {
    if (app->pipeline) {
      gst_element_set_state (GST_ELEMENT (app->pipeline), GST_STATE_NULL);
      gst_object_unref (app->pipeline);
    }

    g_free (app);
  }

  gtk_main_quit ();
}

static App *
app_init (void)
{
  App *app = g_new0 (App, 1);
  GESTrack *audio = NULL, *video = NULL;
  GstBus *bus;

  app->timeline = ges_timeline_new ();
  app->pipeline = ges_timeline_pipeline_new ();
  ges_timeline_pipeline_add_timeline (app->pipeline, app->timeline);

  bus = gst_pipeline_get_bus (GST_PIPELINE (app->pipeline));
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", G_CALLBACK (bus_message_cb), app);

  /* add base audio and video track */
  app->audio_track = ges_track_audio_raw_new ();
  app->video_track = ges_track_video_raw_new ();

  ges_timeline_add_track (app->timeline, app->audio_track);
  ges_timeline_add_track (app->timeline, app->video_track);

  return app;
}

/* UI callbacks  ************************************************************/

gboolean
_play_activate_cb (GtkObject * button, App * app)
{
  gst_element_set_state (GST_ELEMENT (app->pipeline), GST_STATE_PLAYING);
}

gboolean
_stop_activate_cb (GtkObject * button, App * app)
{
  gst_element_set_state (GST_ELEMENT (app->pipeline), GST_STATE_READY);
}

gboolean
_window_delete_event_cb (GtkObject * window, GdkEvent * event, App * app)
{
  app_dispose (app);
}

void
_add_file_activated_cb (GtkAction * item, App * app)
{
  GtkFileChooserDialog *dlg;

  GST_DEBUG ("add file signal handler");

  dlg = (GtkFileChooserDialog *) gtk_file_chooser_dialog_new ("Add File...",
      GTK_WINDOW (app->main_window),
      GTK_FILE_CHOOSER_ACTION_OPEN,
      GTK_STOCK_CANCEL,
      GTK_RESPONSE_CANCEL, GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);

  if (gtk_dialog_run ((GtkDialog *) dlg) == GTK_RESPONSE_OK) {
    GSList *uri;
    uri = gtk_file_chooser_get_uris (GTK_FILE_CHOOSER (dlg));
    g_print ("Adding: %s\n", uri->data);
    app_add_file (app, uri->data);
  }

  gtk_widget_destroy ((GtkWidget *) dlg);
}

gboolean
_duration_scale_change_value_cb (GtkRange * range, GtkScrollType unused,
    gdouble value, App * app)
{
  guint64 duration, maxduration;
  maxduration =
      ges_timeline_filesource_get_max_duration (GES_TIMELINE_FILE_SOURCE
      (app->selected_object->tlobject));
  duration = (value < maxduration ? (value > 0 ? value : 0) : maxduration);
  g_object_set (G_OBJECT (app->selected_object->tlobject), "duration",
      (guint64) duration, NULL);

  return TRUE;
}


gboolean
_in_point_scale_change_value_cb (GtkRange * range, GtkScrollType unused,
    gdouble value, App * app)
{
  guint64 in_point, maxduration;
  maxduration =
      ges_timeline_filesource_get_max_duration (GES_TIMELINE_FILE_SOURCE
      (app->selected_object->tlobject)) -
      GES_TIMELINE_OBJECT_DURATION (app->selected_object->tlobject);
  in_point = (value < maxduration ? (value > 0 ? value : 0) : maxduration);
  g_object_set (G_OBJECT (app->selected_object->tlobject), "in-point",
      (guint64) in_point, NULL);

  return TRUE;
}

gboolean
_start_changed (GtkEntry * entry, App * app)
{
  return TRUE;
}

void
prioritize (GESTimelineObject *in_obj)
{
  return;
}

void
update_properties (App * app)
{
  guint64 duration_scale;
  gchar * start_txt;

  start_txt = g_strdup_printf ("%d", app->selected_object->start);
  duration_scale = app->selected_object->max_duration -
      app->selected_object->in_point;

  g_print ("selected: %s\n", app->selected_object->uri);
  g_print ("start: %d\n", app->selected_object->start);
  g_print ("duration: %d\n", app->selected_object->duration);
  g_print ("in point: %d\n", app->selected_object->in_point);
  g_print ("max duration: %d\n\n", app->selected_object->max_duration);

  gtk_entry_set_text (app->start_entry, start_txt);
  gtk_range_set_range (GTK_RANGE (app->duration_scale), 0.0,
      (gdouble) duration_scale);
  gtk_range_set_value (GTK_RANGE (app->duration_scale),
      (gdouble) app->selected_object->duration);
  gtk_range_set_range (GTK_RANGE (app->in_point_scale), 0.0,
      (gdouble) app->selected_object->max_duration);
  gtk_range_set_value (GTK_RANGE (app->in_point_scale),
      (gdouble) app->selected_object->in_point);
}

gboolean
_clip_selected (GtkTreeView * treeview, App * app)
{
  Clip *clip;

  GtkTreeSelection * selection;
  GtkTreeIter row_iter;
  gboolean selected;

  selection = gtk_tree_view_get_selection (app->timeline_treeview);
  selected = gtk_tree_selection_get_selected (selection, NULL, &row_iter);

  if (!selected) {
    prioritize (NULL);
  } else {
    guint idf;

    gtk_tree_model_get (GTK_TREE_MODEL (app->timeline_store), &row_iter, COL_ID,
        &idf, -1);

    app->selected_object = g_list_nth_data (app->objects, idf);
    update_properties (app);
    prioritize (app->selected_object->tlobject);
  }

  return TRUE;
}

gboolean
_delete_activate_cb (GtkObject * button, App * app)
{
  return TRUE;
}

/* main *********************************************************************/

int
main (int argc, char *argv[])
{
  App *app;

  /* intialize GStreamer and GES */
  gst_init (&argc, &argv);
  ges_init ();

  /* initialize UI */
  gtk_init (&argc, &argv);

  /* initialize and run App */
  app = app_init ();
  create_ui (app);
  gtk_main ();

  return 0;
}
