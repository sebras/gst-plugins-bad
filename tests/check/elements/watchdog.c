/* GStreamer
 *
 * unit test for watchdog
 * Copyright (C) 2014 Sebastian Rasmussen <sebras@hotmail.com>
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

#include <gst/gst.h>
#include <gst/check/gstcheck.h>

static GstElement *pipeline;
static GstElement *watchdog;
static GstBus *bus;
static GstPad *srcpad;
static GstPad *sinkpad;
static gint triggers = 0;
static gint errors = 0;
static gint eos = 0;
static gint bus_watch = 0;

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static gboolean
bus_message_handler (GstBus * bus, GstMessage * message, gpointer user_data)
{
  switch (message->type) {
    case GST_MESSAGE_ERROR:
    {
      GError *err = NULL;
      gchar *debug = NULL;
      gst_message_parse_error (message, &err, &debug);
      if (g_error_matches (err, GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED) &&
          !g_strcmp0 (err->message, "Watchdog triggered")) {
        g_atomic_int_inc (&triggers);
        GST_WARNING ("%" GST_PTR_FORMAT, message);
      } else {
        GST_WARNING ("%" GST_PTR_FORMAT, message);
        g_atomic_int_inc (&errors);
      }
    }
      break;
    case GST_MESSAGE_EOS:
      GST_WARNING ("%" GST_PTR_FORMAT, message);
      g_atomic_int_set (&eos, 1);
      break;
    default:
      GST_ERROR ("%" GST_PTR_FORMAT, message);
      break;
  }

  return TRUE;
}

static gboolean
event_func (GstPad * pad, GstObject * noparent, GstEvent * event)
{
  if (event->type == GST_EVENT_EOS) {
    GstPad *peerpad;
    GstObject *parent;
    peerpad = gst_pad_get_peer (pad);
    parent = gst_pad_get_parent (GST_OBJECT (peerpad));
    gst_object_unref (peerpad);
    GST_ERROR ("posting EOS message from %" GST_PTR_FORMAT, parent);
    fail_unless (gst_element_post_message (GST_ELEMENT (parent),
            gst_message_new_eos (parent)));
    GST_ERROR ("DONE");
  }
  return gst_pad_event_default (pad, noparent, event);
}

static void
setup (void)
{
  GstCaps *caps;

  pipeline = gst_pipeline_new (NULL);
  fail_unless (GST_IS_PIPELINE (pipeline));

  bus = gst_element_get_bus (pipeline);
  fail_unless (GST_IS_BUS (bus));

  g_atomic_int_set (&eos, 0);
  g_atomic_int_set (&errors, 0);
  g_atomic_int_set (&triggers, 0);
  bus_watch = gst_bus_add_watch (bus, bus_message_handler, NULL);
  fail_if (bus_watch == 0);

  watchdog = gst_element_factory_make ("watchdog", NULL);
  fail_unless (watchdog != NULL);
  fail_unless (gst_bin_add (GST_BIN (pipeline), watchdog));

  srcpad = gst_check_setup_src_pad (watchdog, &srctemplate);
  sinkpad = gst_check_setup_sink_pad (watchdog, &sinktemplate);

  fail_unless (gst_pad_set_active (srcpad, TRUE));
  fail_unless (gst_pad_set_active (sinkpad, TRUE));

  caps = gst_caps_from_string ("application/x-raw");
  gst_check_setup_events (srcpad, watchdog, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  gst_pad_set_chain_function (sinkpad, gst_check_chain_func);
  gst_pad_set_event_function (sinkpad, event_func);
}

static void
teardown (void)
{
  gst_check_teardown_sink_pad (watchdog);
  gst_check_teardown_src_pad (watchdog);
  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);
  g_source_remove (bus_watch);
  gst_object_unref (pipeline);
}

GST_START_TEST (test_watchdog_timeout_property)
{
  //GstMessage *msg;
  //GError *err = NULL;
  GstBuffer *buf;
  GstEvent *evt;

  g_object_set (watchdog, "timeout", 1, NULL);
  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PLAYING),
      GST_STATE_CHANGE_SUCCESS);

  buf = gst_buffer_new_allocate (NULL, 0, NULL);
  fail_unless (GST_IS_BUFFER (buf));
  fail_unless_equals_int (gst_pad_push (srcpad, buf), GST_FLOW_OK);

  evt = gst_event_new_eos ();
  fail_unless (gst_pad_push_event (srcpad, evt));

  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);

  fail_unless_equals_int (gst_element_get_state (pipeline, NULL, NULL,
          GST_CLOCK_TIME_NONE), GST_STATE_CHANGE_SUCCESS);

  fail_unless_equals_int (triggers, 0);
  fail_unless_equals_int (errors, 0);
  fail_unless_equals_int (eos, 0);

  //g_object_set (watchdog, "timeout", G_MAXINT, NULL);
  //fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PLAYING),
  //    GST_STATE_CHANGE_SUCCESS);

  //buf = gst_buffer_new_allocate (NULL, 0, NULL);
  //fail_unless (GST_IS_BUFFER (buf));
  //fail_unless_equals_int (gst_pad_push (srcpad, buf), GST_FLOW_OK);

  //evt = gst_event_new_eos ();
  //fail_unless (gst_pad_push_event (srcpad, evt));

  //fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
  //    GST_STATE_CHANGE_SUCCESS);

  //fail_unless_equals_int (triggers, 0);
  //fail_unless_equals_int (errors, 0);
  //fail_unless_equals_int (eos, 0);
}

GST_END_TEST;

GST_START_TEST (test_watchdog_null_state)
{
  GstMessage *msg;
  g_object_set (watchdog, "timeout", 1, NULL);
  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_NULL),
      GST_STATE_CHANGE_SUCCESS);
  msg =
      gst_bus_timed_pop_filtered (bus, GST_SECOND,
      GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
  fail_unless (msg == NULL);
}

GST_END_TEST;

GST_START_TEST (test_watchdog_ready_state)
{
  GstMessage *msg;
  g_object_set (watchdog, "timeout", 1, NULL);
  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_READY),
      GST_STATE_CHANGE_SUCCESS);
  msg =
      gst_bus_timed_pop_filtered (bus, GST_SECOND,
      GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
  fail_unless (msg == NULL);
}

GST_END_TEST;

GST_START_TEST (test_watchdog_paused_state)
{
  GstMessage *msg;
  GstEvent *evt;
  GError *err = NULL;

  g_object_set (watchdog, "timeout", 1, NULL);
  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PAUSED),
      GST_STATE_CHANGE_SUCCESS);

  evt = gst_event_new_eos ();
  fail_unless (gst_pad_push_event (srcpad, evt));

  msg =
      gst_bus_timed_pop_filtered (bus, GST_SECOND,
      GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
  fail_unless (msg != NULL);
  fail_unless_equals_int (msg->type, GST_MESSAGE_ERROR);
  gst_message_parse_error (msg, &err, NULL);
  fail_unless (g_error_matches (err, GST_STREAM_ERROR,
          GST_STREAM_ERROR_FAILED));
  fail_unless (!g_strcmp0 (err->message, "Watchdog triggered"));
}

GST_END_TEST;

GST_START_TEST (test_watchdog_playing_state_timeout)
{
  GstMessage *msg;
  GstEvent *evt;
  GError *err = NULL;

  g_object_set (watchdog, "timeout", 1, NULL);
  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PLAYING),
      GST_STATE_CHANGE_SUCCESS);

  evt = gst_event_new_eos ();
  fail_unless (gst_pad_push_event (srcpad, evt));

  msg =
      gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
  fail_unless (msg != NULL);
  fail_unless_equals_int (msg->type, GST_MESSAGE_ERROR);
  gst_message_parse_error (msg, &err, NULL);
  fail_unless (g_error_matches (err, GST_STREAM_ERROR,
          GST_STREAM_ERROR_FAILED));
  fail_unless (!g_strcmp0 (err->message, "Watchdog triggered"));
}

GST_END_TEST;

GST_START_TEST (test_watchdog_playing_state_no_timeout)
{
  GstMessage *msg;
  GError *err = NULL;
  GstBuffer *buf;

  g_object_set (watchdog, "timeout", GST_TIME_AS_MSECONDS (3600 * GST_SECOND),
      NULL);
  fail_unless_equals_int (gst_element_set_state (pipeline, GST_STATE_PLAYING),
      GST_STATE_CHANGE_SUCCESS);

  buf = gst_buffer_new_allocate (NULL, 0, NULL);
  fail_unless (GST_IS_BUFFER (buf));
  fail_unless_equals_int (gst_pad_push (srcpad, buf), GST_FLOW_OK);

  msg =
      gst_bus_timed_pop_filtered (bus, GST_SECOND,
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
  fail_unless (msg == NULL);

  g_object_set (watchdog, "timeout", 1, NULL);

  msg =
      gst_bus_timed_pop_filtered (bus, GST_SECOND,
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
  fail_unless (msg == NULL);

  buf = gst_buffer_new_allocate (NULL, 0, NULL);
  fail_unless (GST_IS_BUFFER (buf));
  fail_unless_equals_int (gst_pad_push (srcpad, buf), GST_FLOW_OK);

  msg =
      gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
  fail_unless (msg != NULL);
  fail_unless_equals_int (msg->type, GST_MESSAGE_ERROR);
  gst_message_parse_error (msg, &err, NULL);
  fail_unless (g_error_matches (err, GST_STREAM_ERROR,
          GST_STREAM_ERROR_FAILED));
  fail_unless (!g_strcmp0 (err->message, "Watchdog triggered"));
}

GST_END_TEST;

static Suite *
watchdog_suite (void)
{
  Suite *s = suite_create ("watchdog");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_checked_fixture (tc_chain, setup, teardown);
  if (1)
    tcase_add_test (tc_chain, test_watchdog_timeout_property);
  if (0)
    tcase_add_test (tc_chain, test_watchdog_null_state);
  if (0)
    tcase_add_test (tc_chain, test_watchdog_ready_state);
  if (0)
    tcase_add_test (tc_chain, test_watchdog_paused_state);
  if (0)
    tcase_add_test (tc_chain, test_watchdog_playing_state_timeout);
  if (0)
    tcase_add_test (tc_chain, test_watchdog_playing_state_no_timeout);

  return s;
}

GST_CHECK_MAIN (watchdog);
