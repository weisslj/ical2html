bin_PROGRAMS = ical2html icalfilter icalmerge

ical2html_SOURCES = ical2html.c
ical2html_LDADD = @LIBOBJS@ @PTHREAD_LIBS@

icalfilter_SOURCES = icalfilter.c
icalfilter_LDADD = @LIBOBJS@ @PTHREAD_LIBS@

icalmerge_SOURCES = icalmerge.c
icalmerge_LDADD = @LIBOBJS@ @PTHREAD_LIBS@

dist_data_DATA = calendar.css

EXTRA_DIST = getopt.h
