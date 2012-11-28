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
  gchar * uri;
  guint64 * start;
  guint64 * duration;
  guint64 * in_point;
  guint64 * max_duration;
  GESTimelineObject * tlobject;

} Clip;

typedef struct App
{
  /* back-end objects */
  GESTimeline *timeline;
  GESTimelinePipeline *pipeline;
  GESTimelineLayer *layer;
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

/* Prototypes for auto-connected signal handlers ***************************/

/**
 * These are declared non-static for signal auto-connection
 */

gboolean _window_delete_event_cb (GtkObject * window, GdkEvent * event,
    App * app);
gboolean _play_activate_cb (GtkObject * button, GdkEvent * event, App * app);
gboolean _stop_activate_cb (GtkObject * button, GdkEvent * event, App * app);
void _add_file_activated_cb (GtkAction * item, App * app);
gboolean _duration_scale_change_value_cb (GtkRange * range,
    GtkScrollType unused, gdouble value, App * app);
gboolean _in_point_scale_change_value_cb (GtkRange * range,
    GtkScrollType unused, gdouble value, App * app);
gboolean _start_changed (GtkEntry * entry, GdkEvent * event, App * app);
gboolean _clip_selected (GtkTreeView * treeview, GdkEvent * event, App * app);
gboolean _delete_activate_cb (GtkObject * button, GdkEvent * event, App * app);


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

  /* get a bunch of widgets from the XML tree */

  app->start_entry = GTK_ENTRY (gtk_builder_get_object (builder,
      "start_entry"));
  app->duration_scale = GTK_HSCALE (gtk_builder_get_object (builder,
      "duration_scale"));
  app->in_point_scale = GTK_HSCALE (gtk_builder_get_object (builder,
      "in_point_scale"));
  app->timeline_treeview = GTK_TREE_VIEW (gtk_builder_get_object (builder,
      "timeline_treeview"));
  app->main_window = GTK_WIDGET (gtk_builder_get_object (builder, "window"));

  /* create the model for the treeview */

  app->timeline_store =
    gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_UINT64, G_TYPE_OBJECT);

  gtk_tree_view_set_model (app->timeline_treeview,
      GTK_TREE_MODEL (app->timeline_store));

  /* success */
  gtk_builder_connect_signals (builder, app);
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
  GESTimelineObject *obj;

  GST_DEBUG ("adding file %s", uri);

  obj = GES_TIMELINE_OBJECT (ges_timeline_filesource_new (uri));

  ges_simple_timeline_layer_add_object (GES_SIMPLE_TIMELINE_LAYER (app->layer),
      obj, -1);
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
_play_activate_cb (GtkObject * button, GdkEvent * event, App * app)
{
  gst_element_set_state (GST_ELEMENT (app->pipeline), GST_STATE_PLAYING);
}

gboolean
_stop_activate_cb (GtkObject * button, GdkEvent * event, App * app)
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

  g_object_set (G_OBJECT (dlg), "select-multiple", TRUE, NULL);

  if (gtk_dialog_run ((GtkDialog *) dlg) == GTK_RESPONSE_OK) {
    GSList *uris;
    GSList *cur;
    uris = gtk_file_chooser_get_uris (GTK_FILE_CHOOSER (dlg));
    for (cur = uris; cur; cur = cur->next)
      app_add_file (app, cur->data);
    g_slist_free (uris);
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
_start_changed (GtkEntry * entry, GdkEvent * event, App * app)
{
  return TRUE;
}

gboolean
_clip_selected (GtkTreeView * treeview, GdkEvent * event, App * app)
{
  return TRUE;
}

gboolean
_delete_activate_cb (GtkObject * button, GdkEvent * event, App * app)
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
