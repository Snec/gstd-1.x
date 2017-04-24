# Client library if user wants to use as code base for its own client
lib_LTLIBRARIES = libgstd-client.la

libgstd_client_la_SOURCES = gstd_client.c

libgstd_client_la_CFLAGS = $(GIO_CFLAGS)
libgstd_client_la_LDFLAGS = $(GIO_LIBS)

bin_PROGRAMS = gstd-client

gstd_client_SOURCES = main.c
gstd_client_CFLAGS = $(GIO_CFLAGS)
gstd_client_LDFLAGS = $(GIO_LIBS) -lreadline
gstd_client_LDADD = libgstd-client.la

include_HEADERS = gstd_client.h

noinst_HEADERS =
