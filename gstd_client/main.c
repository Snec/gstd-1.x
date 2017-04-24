/*
 * Gstreamer Daemon - Gst Launch under steroids
 * Copyright (C) 2015 RidgeRun Engineering <support@ridgerun.com>
 *
 * This file is part of Gstd.
 *
 * Gstd is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Gstd is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with Gstd.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <string.h>
#include <gio/gio.h>

#include <gstd_client.h>

#ifdef HAVE_LIBREADLINE
#  if defined(HAVE_READLINE_READLINE_H)
#    include <readline/readline.h>
#  elif defined(HAVE_READLINE_H)
#    include <readline.h>
#  else /* !defined(HAVE_READLINE_H) */
extern gchar *readline ();
#  endif /* !defined(HAVE_READLINE_H) */
gchar *cmdline = NULL;
#else /* !defined(HAVE_READLINE_READLINE_H) */

#endif /* HAVE_LIBREADLINE */

#ifdef HAVE_READLINE_HISTORY
#  if defined(HAVE_READLINE_HISTORY_H)
#    include <readline/history.h>
#  elif defined(HAVE_HISTORY_H)
#    include <history.h>
#  else /* !defined(HAVE_HISTORY_H) */
extern void add_history ();
extern gint write_history ();
extern gint read_history ();
#  endif /* defined(HAVE_READLINE_HISTORY_H) */
#else

#endif /* HAVE_READLINE_HISTORY */

typedef struct _GstdClientCmd GstdClientCmd;
typedef gint GstdClientCB (gchar *, gchar *, GstdClientData *);

struct _GstdClientCmd {
  gchar *name;
  GstdClientCB *func;
  gchar *doc;
  gchar *usage;
};

/* VTable */
static gint
gstd_client_cmd_quit(gchar *, gchar *, GstdClientData *);
static gint
gstd_client_cmd_warranty(gchar *, gchar *, GstdClientData *);
static gint
gstd_client_cmd_help(gchar *, gchar *, GstdClientData *);
static gint
gstd_client_cmd_sh(gchar *, gchar *, GstdClientData *);
static gint
gstd_client_cmd_source(gchar *, gchar *, GstdClientData *);
static gchar *
gstd_client_completer (const gchar *, gint);
static gint
gstd_client_execute (gchar *, GstdClientData *);
static void
gstd_client_header(gboolean quiet);

/* Global variables */

/* Control the program flow */
static gboolean quit;

/* Unblock readline on interrupt */
static sigjmp_buf ctrlc_buf;

/* List of available commands */
static GstdClientCmd cmds[] = {
  {"warranty", gstd_client_cmd_warranty, "Prints Gstd warranty", "warranty"},
  {"quit", gstd_client_cmd_quit, "Exits Gstd client", "quit"},
  {"exit", gstd_client_cmd_quit, "Exits Gstd client", "exit"},
  {"help", gstd_client_cmd_help, "Prints help information", "help [command]"},
  {"create", gstd_client_cmd_tcp, "Creates a resource at the given URI", "create <URI> [property value ...]"},
  {"read", gstd_client_cmd_tcp, "Reads the resource at the given URI", "read <URI>"},
  {"update", gstd_client_cmd_tcp, "Updates the resource at the given URI", "update <URI> [property value ...]"},
  {"delete", gstd_client_cmd_tcp, "Deletes the resource held at the given URI with the given name",
                                  "read <URI> <name>"},
  {"sh", gstd_client_cmd_sh, "Executes a shell command", "sh <command>"},
  {"source", gstd_client_cmd_source, "Sources a file with commands", "source <file>"},

  // High level commands
  {"pipeline_create", gstd_client_cmd_tcp, "Creates a new pipeline based on the name and description",
   "pipeline_create <name> <description>"},
  {"pipeline_delete", gstd_client_cmd_tcp, "Deletes the pipeline with the given name",
   "pipeline_delete <name>"},
  {"pipeline_play", gstd_client_cmd_tcp, "Sets the pipeline to playing",
   "pipeline_play <name>"},
  {"pipeline_pause", gstd_client_cmd_tcp, "Sets the pipeline to paused",
   "pipeline_pause <name>"},
  {"pipeline_stop", gstd_client_cmd_tcp, "Sets the pipeline to null",
   "pipeline_stop <name>"},
  
  {"element_set", gstd_client_cmd_tcp, "Sets a property in an element of a given pipeline",
   "element_set <pipe> <element> <property> <value>"},
  {"element_get", gstd_client_cmd_tcp, "Queries a property in an element of a given pipeline",
   "element_set <pipe> <element> <property>"},

  {"list_pipelines", gstd_client_cmd_tcp, "List the existing pipelines",
   "list_pipeline"},
  {"list_elements", gstd_client_cmd_tcp, "List the elements in a given pipeline",
   "list_elements <pipe>"},
  {"list_properties", gstd_client_cmd_tcp, "List the properties of an element in a given pipeline",
   "list_properties <pipe> <elemement>"},
  
  {NULL}
};

/* Capture the infamous CTRL-C */
static void
sig_handler(gint signo)
{
  if (signo == SIGINT) {
    g_print ("Signal received, quitting...\n");
    quit = TRUE;
    siglongjmp(ctrlc_buf, 1);
  }
}

/* Customizing readline */
static void
init_readline ()
{
  /* Allow conditional parsing of the ~/.inputrc file */
  rl_readline_name = "Gstd";

  /* Custom command completion */
  rl_completion_entry_function = gstd_client_completer;
}

/* Called with every line entered by the user */
static gint
gstd_client_execute (gchar *line, GstdClientData *data)
{
  gchar **tokens;
  gchar *name;
  gchar *arg;
  GstdClientCmd *cmd;

  g_return_val_if_fail (line, -1);
  g_return_val_if_fail (data, -1);

  tokens = g_strsplit (line, " ", 2);
  name = g_strstrip (tokens[0]);

  /* Prefer an empty string than NULL */
  if(tokens[1])
    arg = g_strstrip (tokens[1]);
  else
    arg = "";

  /* Find and execute the respective command */
  cmd = cmds;
  while (cmd->name) {
    if (!strcmp(cmd->name, name))
      return cmd->func(name, arg, data);
    cmd++;
  }
  g_printerr ("No such command `%s'\n", name);
  g_strfreev(tokens);

  return -1;
}

gint 
main (gint argc, gchar *argv[])
{
  gchar *line;
  GError *error = NULL;
  GOptionContext *context;
  gchar *prompt = "gstd> ";
  gchar *history = "~/.gstc_history";
  GstdClientData *data;
  
  /* Cmdline options */
  gboolean quiet;
  gboolean inter;
  gboolean launch;
  gboolean version;
  gchar *file;
  guint port;
  gchar *address;
  gchar **remaining;
  GOptionEntry entries[] = {
    { "quiet", 'q', 0, G_OPTION_ARG_NONE, &quiet, "Dont't print startup headers", NULL },
    { "file", 'f', 0, G_OPTION_ARG_FILENAME, &file,
      "Execute the commands in file", "script" },
    { "interactive", 'i', 0, G_OPTION_ARG_NONE, &inter, "Enter interactive mode after executing cmdline", NULL },
    { "launch", 'l', 0, G_OPTION_ARG_NONE, &launch, "Emulate gst-launch, often combined with -i", NULL },
    { "port", 'p', 0, G_OPTION_ARG_INT, &port, "Attach to the server through the given port (default 5000)",
      "port" },
    { "address", 'a', 0, G_OPTION_ARG_STRING, &address, "The IP address of the server (defaults to "
      GSTD_CLIENT_DEFAULT_ADDRESS ")", "address"},
    { "version", 'v', 0, G_OPTION_ARG_NONE, &version, "Print current gstd-client version", NULL },
    { G_OPTION_REMAINING, '0', 0, G_OPTION_ARG_STRING_ARRAY, &remaining, "commands", NULL },
    { NULL }
  };

  // Initialize default
  remaining = NULL;
  file = NULL;
  quit = FALSE;
  version = FALSE;
  inter = FALSE;
  port = GSTD_CLIENT_DEFAULT_PORT;
  address = NULL;
  quiet = FALSE;
  
  context = g_option_context_new ("[COMMANDS...] - gst-launch under steroids");
  g_option_context_add_main_entries (context, entries, NULL);
  if (!g_option_context_parse (context, &argc, &argv, &error)) {
    g_printerr ("%s\n", error->message);
    return EXIT_FAILURE;
  }

  if (!address)
    address = g_strdup(GSTD_CLIENT_DEFAULT_ADDRESS);

  // Enter interactive only if no commands nor file has been set, or if the
  // user explicitely asked for it
  inter = inter || (!file && !remaining);
  quiet = quiet || file || remaining;
  
  if (version) {
    g_print ("" PACKAGE_STRING "\n");
    g_print ("Copyright (c) 2015 RigeRun Engineering\n");
    return EXIT_SUCCESS;
  }

  signal(SIGINT, sig_handler);
  init_readline ();
  read_history (history);

  data = (GstdClientData *)g_malloc(sizeof(GstdClientData));
  data->quiet = quiet;
  data->prompt = prompt;
  data->port = port;
  data->address = address;
  data->client = g_socket_client_new();
  data->con = NULL;
  
  if (file) {
    gstd_client_cmd_source ("source", file, data);
    g_free (file);
  }

  if (remaining) {
    line = g_strjoinv (" ", remaining);
    gstd_client_execute(line, data);
    g_free (line);
    g_strfreev(remaining);
  }

  if (!inter)
    return EXIT_SUCCESS;

#ifndef HAVE_LIBREADLINE
  // No readline, no interaction
  quit = TRUE;
#endif

  gstd_client_header (quiet);
  /* Jump here in case of an interrupt, so we can exit */
  while ( sigsetjmp( ctrlc_buf, 1 ) != 0 );

  while (!quit) {
    line = readline (prompt);
    if (!line)
      break;

    /* Removing trailing and leading whitespaces in place*/
    g_strstrip(line);

    /* Skip empty lines */
    if (*line == '\0')
      continue;
      
    add_history (line);
    gstd_client_execute (line, data);

    g_free (line);
  }

  /* Free everything up */
  g_free(address);
  g_object_unref(data->client);
  if (data->con)
    g_object_unref(data->con);
  g_free(data);
  
  write_history (history);
  
  return EXIT_SUCCESS;
}

/* Generator function for command completion.  STATE lets us know whether
   to start from scratch; without any state (i.e. STATE == 0), then we
   start at the top of the list. */
static gchar *
gstd_client_completer (const gchar *text, gint state)
{
  static gint list_index, len;
  gchar *name;

  /* If this is a new word to complete, initialize now.  This includes
     saving the length of TEXT for efficiency, and initializing the index
     variable to 0. */
  if (!state) {
    list_index = 0;
    len = strlen (text);
  }

  /* Return the next name which partially matches from the command list. */

  while ((name = cmds[list_index++].name))
    if (!strncmp (name, text, len))
      return g_strdup(name);

  /* If no names matched, then return NULL. */
  return NULL;
}

/* GNU FSF recommendations for open source */
static void
gstd_client_header(gboolean quiet)
{
  const gchar *header = "Gstd-1.0  Copyright (C) 2015 RidgeRun Engineering\n"
    "This program comes with ABSOLUTELY NO WARRANTY; for details type `warranty'.\n"
    "This is free software, and you are welcome to redistribute it\n"
    "under certain conditions; read the license for more details.\n";
  if (!quiet)
    g_print ("%s", header);
}

/* GNU FSF recommendations for open source */
static gint
gstd_client_cmd_warranty(gchar *name, gchar *arg, GstdClientData *data)
{
  const gchar *warranty = "Gstd-1.0 - gst-launch under steroids\n"
    "Copyright (C) 2015 RidgeRun Engineering\n"
    "\n"
    "This program is free software: you can redistribute it and/or modify\n"
    "it under the terms of the GNU General Public License as published by\n"
    "the Free Software Foundation, either version 3 of the License, or\n"
    "(at your option) any later version.\n"
    "\n"
    "This program is distributed in the hope that it will be useful,\n"
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
    "GNU General Public License for more details.\n"
    "\n"
    "You should have received a copy of the GNU General Public License\n"
    "along with this program.  If not, see <http://www.gnu.org/licenses/>.\n";
  g_print("%s", warranty);
  return 0;
}

/* Print the help for all or a particular command */
static gint
gstd_client_cmd_help(gchar *name, gchar *arg, GstdClientData *data)
{
  GstdClientCmd *cmd = cmds;
  static gint maxlen = 0;
  gint curlen;
  const gchar *t1 = "COMMAND";
  const gchar *t2 = "DESCRIPTION";

  g_return_val_if_fail (arg, -1);

  /* One time max length calculation */
  if (!maxlen) {
    maxlen = strlen(t1);
    while (cmd->name) {
      curlen = strlen(cmd->usage);
      if (curlen > maxlen)
	maxlen = curlen;
      cmd++;
    }
    cmd = cmds;
  }

  /* Cryptic way to maintain the table column length for all rows */
#define FMT_TABLE(c1,c2) "%s%*s%s\n", c1, 4+maxlen-(gint)strlen(c1), " ", c2
  g_print (FMT_TABLE(t1, t2));
  while (cmd->name) {
    if ('\0' == arg[0] || !strcmp(arg, cmd->name))
      g_print (FMT_TABLE(cmd->usage, cmd->doc));
    cmd++;
  }
  
  return 0;
}

    static gint
gstd_client_cmd_sh(gchar *name, gchar *arg, GstdClientData *data)
{
  GError *err = NULL;
  gchar **tokens;
  gint exit_status;
  
  g_return_val_if_fail (arg, -1);

  tokens = g_strsplit (arg, " ", -1);
  
  g_spawn_sync (NULL, //CWD
		tokens, //argv
		NULL, //envp
		G_SPAWN_SEARCH_PATH_FROM_ENVP |
		G_SPAWN_CHILD_INHERITS_STDIN,
		NULL, //child_setup
		NULL, //user_data
		NULL, //stdout
		NULL, //stderr
		&exit_status,
		&err);
  g_strfreev(tokens);

  if (err) {
    g_print ("%s\n", err->message);
    g_error_free (err);
  }
  
  return exit_status;
}

static gint
gstd_client_cmd_source(gchar *name, gchar *arg, GstdClientData *data)
{
  FILE *script;
  char *line = NULL;
  size_t len = 0;
  ssize_t read;

  g_return_val_if_fail (arg, -1);
  g_return_val_if_fail (data, -1);

  script = fopen(arg, "r");
  if (!script) {
    g_printerr ("%s\n", strerror(errno));
    return -1;
  }

  while ((read = getline(&line, &len, script)) != -1) {
    if (!line)
      break;

    /* Removing trailing and leading whitespaces in place*/
    g_strstrip(line);

    if (!data->quiet)
      g_print ("%s %s\n", data->prompt, line);
    
    /* Skip empty lines */
    if (*line == '\0')
      continue;
      
    add_history (line);
    gstd_client_execute (line, data);

    g_free (line);
    line = NULL;
  }

  fclose(script);

  return EXIT_SUCCESS;
}

static gint
gstd_client_cmd_quit(gchar *name, gchar *arg, GstdClientData *data)
{
  quit = TRUE;
  return 0;
}
