/*
 * icalfilter -- filter an iCalendar file
 *
 * Author: Bert Bos <bert@w3.org>
 * Created: 30 Sep 2002
 * Version: $Id: icalmerge.c,v 1.2 2003/07/30 13:00:36 bbos Exp $
 */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>
#include <ctype.h>
#include <ical.h>
#include <icalss.h>
#undef PACKAGE_BUGREPORT	/* Why are they in ical.h? */
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#undef PACKAGE
#undef VERSION
#include "config.h"
#ifdef HAVE_SEARCH_H
#  include <search.h>
#else
#  include "search-freebsd.h"
#endif


#define PRODID "-//W3C//NONSGML icalmerge " ## VERSION ## "//EN"

#define ERR_OUT_OF_MEM 1	/* Program exit codes */
#define ERR_USAGE 2
#define ERR_DATE 3
#define ERR_PARSE 4
#define ERR_FILEIO 5
#define ERR_ICAL_ERR 6		/* Other error */
#define ERR_HASH 7		/* Hash failure */

#define USAGE "Usage: ical2html input [input...] output\n\
  inputs and output are iCalendar files\n"

/* Long command line options */
static struct option options[] = {
  {0, 0, 0, 0}
};

#define OPTIONS ""



/* fatal -- print error message and exit with errcode */
static void fatal(int errcode, const char *message,...)
{
  va_list args;
  va_start(args, message);
  vfprintf(stderr, message, args);
  va_end(args);
  exit(errcode);
}


/* debug -- print message on stderr */
static void debug(const char *message,...)
{
#ifdef DEBUG
  va_list args;
  va_start(args, message);
  vfprintf(stderr, message, args);
  va_end(args);
#endif /* DEBUG */
}


/* merge -- add b to a, keeping only newer entries in case of duplicates */
static void merge(icalcomponent **a, icalcomponent *b)
{
  static int first_time = 1;
  icalcomponent *h, *next;
  icalproperty *mod_a, *mod_b;
  struct icaltimetype modif_a, modif_b;
  icalproperty *uid;
  ENTRY *e, e1;

  /* Create the hash table */
  if (first_time) {
    if (! hcreate(2000)) fatal(ERR_HASH, "%s\n", strerror(errno));
    first_time = 0;
  }

  /* Iterate over VEVENTs */
  for (h = icalcomponent_get_first_component(b, ICAL_VEVENT_COMPONENT);
       h; h = next) {

    next = icalcomponent_get_next_component(b, ICAL_VEVENT_COMPONENT);

    uid = icalcomponent_get_first_property(h, ICAL_UID_PROPERTY);
    debug("%s", uid ? icalproperty_get_uid(uid) : "NO UID!?");
    if (!uid) continue;				/* Error in iCalendar file */
    e1.key = strdup(icalproperty_get_uid(uid));
    if (! (e = hsearch(e1, FIND))) {

      /* New UID, add the VEVENT to the hash and to the set a */
      icalcomponent_remove_component(b, h); /* Move from b... */
      icalcomponent_add_component(*a, h); /* ... to a */
      e1.data = h;
      if (! (e = hsearch(e1, ENTER))) fatal(ERR_HASH, "%s\n", strerror(errno));
      debug(" (added)\n");

    } else {

      /* Already an entry with this UID, compare modified dates */
      mod_a = icalcomponent_get_first_property((icalcomponent*)e->data,
					       ICAL_LASTMODIFIED_PROPERTY);
      if (!mod_a) continue;	/* Hmmm... */
      modif_a = icalproperty_get_lastmodified(mod_a);

      mod_b = icalcomponent_get_first_property(h, ICAL_LASTMODIFIED_PROPERTY);
      if (!mod_b) continue;	/* Hmmm... */
      modif_b = icalproperty_get_lastmodified(mod_b);

      if (icaltime_compare(modif_a, modif_b) == -1) {
	/* a is older than b, so replace it */
	icalcomponent_remove_component(*a, (icalcomponent*)e->data);
	icalcomponent_remove_component(b, h); /* Move from b... */
	icalcomponent_add_component(*a, h); /* ... to a */
	free(e->key);
	e1.data = h;
	if (!(e = hsearch(e1, ENTER))) fatal(ERR_HASH, "%s\n", strerror(errno));
	debug(" (replaced)\n");
      } else {
	free(e1.key);
	debug(" (ignored)\n");
      }      
    }
  }
}


/* read_stream -- read size bytes into s from stream d */
static char* read_stream(char *s, size_t size, void *d)  
{  
  return fgets(s, size, (FILE*)d); 
} 


/* main */
int main(int argc, char *argv[])
{
  FILE* stream;
  icalcomponent *comp;
  icalparser *parser;
  char c;
  icalset *out;
  icalcomponent *newset;

  /* We handle errors ourselves */
  icalerror_errors_are_fatal = 0;
  icalerrno = 0;

  /* Read commandline */
  while ((c = getopt_long(argc, argv, OPTIONS, options, NULL)) != -1) {
    switch (c) {
    default: fatal(ERR_USAGE, USAGE);
    }
  }

  /* Initialize a new, empty icalendar */
  newset = icalcomponent_vanew(ICAL_VCALENDAR_COMPONENT,
			       icalproperty_new_version("2.0"),
			       icalproperty_new_prodid(PRODID),
			       0);

  /* Loop over remaining file arguments, except the last */
  if (optind >= argc - 1) fatal(ERR_USAGE, USAGE);
  while (optind != argc - 1) {

    /* Open the file */
    stream = fopen(argv[optind], "r");
    if (!stream) fatal(ERR_FILEIO, "%s: %s\n", argv[optind], strerror(errno));

    /* Create a new parser object */
    /* Tell the parser what input stream it should use */
    /* Let the parser read the file and return all components */
    parser = icalparser_new();
    icalparser_set_gen_data(parser, stream);
    if (! (comp = icalparser_parse(parser, read_stream)))
      fatal(ERR_PARSE, "Parse error: %s\n", icalerror_strerror(icalerrno));
    icalparser_free(parser);
    if (fclose(stream) != 0)
      fatal(ERR_FILEIO, "%s: %s\n", argv[optind], strerror(errno));

    /* Add all found components to a hash table, removing duplicates */
    merge(&newset, comp);
    
    optind++;
  }

  /* Get output file name */
  (void) unlink(argv[optind]);	/* Remove output file if it already exists */
  out = icalfileset_new(argv[optind]);
  if (!out) fatal(ERR_FILEIO, "%s: %s\n", argv[optind], strerror(errno));

  /* Put new icalendar in output file, flush to disk and close file */
  icalfileset_add_component(out, newset);
  icalfileset_free(out);

  /* Clean up */
  /* icalcomponent_free(newset); */

  return 0;
}
