#ifndef _GSTD_CLIENT_H
#define _GSTD_CLIENT_H

#include <stdlib.h>
#include <stdio.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <string.h>
#include <gio/gio.h>

#define GSTD_CLIENT_DEFAULT_ADDRESS "localhost"
#define GSTD_CLIENT_DEFAULT_PORT 5000

typedef struct _GstdClientData GstdClientData;

struct _GstdClientData {
  gboolean quiet;
  gchar *prompt;
  guint port;
  gchar *address;
  GSocketClient *client;
  GSocketConnection *con;
};

gint
gstd_client_cmd_tcp(gchar *, gchar *, GstdClientData *);

#endif /* _GSTD_CLIENT_H */
