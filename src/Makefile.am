NULL =

public_headers = 

c_sources = \
	ges-demo.c

CLEANFILES =

bin_PROGRAMS = gesdemo

gesdemo_SOURCES = $(c_sources)
gesdemo_CFLAGS = $(GST_CFLAGS) $(GTK2_CFLAGS) -DGESDEMO_DATA_DIR="\"$(pkgdatadir)\""
gesdemo_LDADD = $(GST_LIBS) $(GTK2_LIBS)

noinst_HEADERS = $(public_headers)
