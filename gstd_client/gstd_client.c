#include <gstd_client.h>

    gint
gstd_client_cmd_tcp(gchar *name, gchar *arg, GstdClientData *data)
{
  gchar *cmd;
  GError *err = NULL;
  GInputStream *istream;
  GOutputStream *ostream;
  gchar buffer[1024*1024];
  gint read;

  g_return_val_if_fail (name, -1);
  g_return_val_if_fail (arg, -1);
  g_return_val_if_fail (data, -1);

  cmd = g_strconcat (name, " ", arg, NULL);

  if (!data->con)
    data->con = g_socket_client_connect_to_host (data->client,
						 data->address,
						 data->port,
						 NULL,
						 &err);
  if (err)
    goto error;

  istream = g_io_stream_get_input_stream (G_IO_STREAM(data->con));
  ostream = g_io_stream_get_output_stream (G_IO_STREAM(data->con));

  err = NULL;
  g_output_stream_write (ostream, cmd, strlen(cmd), NULL, &err);
  g_free(cmd);
  if (err)
    goto error;

  //Paranoia flush
  g_output_stream_flush(ostream, NULL, NULL);

  read = g_input_stream_read (istream, &buffer, sizeof(buffer), NULL, &err);
  if (err)
    goto error;

  //Make sure it has its sentinel and print
  buffer[read] = '\0';
  g_print("%s\n", buffer);

  // FIXME: Hack to open a new connection with every message
  g_object_unref(data->con);
  data->con = NULL;

  return 0;

  error:
  {
    g_printerr("%s\n", err->message);
    g_error_free(err);
    if (data->con) {
      g_object_unref(data->con);
      data->con = NULL;
    }
    return -1;
  }
}

