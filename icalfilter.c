/*
 * icalfilter -- filter an iCalendar file
 *
 * Author: Bert Bos <bert@w3.org>
 * Created: 30 Sep 2002
 */

#include "config.h"
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>
#include <ctype.h>
/*
#include <icaltime.h>
#include <icalcomponent.h>
#include <icalerror.h>
#include <icaltimezone.h>
*/
#include <libical/ical.h>
#include <libical/icalss.h>
/*
#include <icalset.h>
#include <icalfileset.h>
*/

#define PRODID "-//W3C//NONSGML icalfilter 0.1//EN"

#define ERR_OUT_OF_MEM 1	/* Program exit codes */
#define ERR_USAGE 2
#define ERR_DATE 3
#define ERR_PARSE 4
#define ERR_FILEIO 5
#define ERR_ICAL_ERR 6		/* Other error */

#define USAGE "Usage: ical2html [options] input output\n\
  -p, --class=CLASS            only (PUBLIC, CONFIDENTIAL, PRIVATE, NONE)\n\
  -P, --not-class=CLASS        exclude (PUBLIC, CONFIDENTIAL, PRIVATE, NONE)\n\
  -c, --category=CATEGORY      only events of this category\n\
  -C, --not-category=CATEGORY  exclude events of this category\n\
  input and output are iCalendar files\n"

/* Long command line options */
static struct option options[] = {
  {"class", 1, 0, 'p'},
  {"not-class", 1, 0, 'P'},
  {"category", 1, 0, 'c'},
  {"not-category", 1, 0, 'C'},
  {0, 0, 0, 0}
};

#define OPTIONS "p:P:c:C:"

/* Structure for storing applicable events */
typedef struct _event_item {
  struct icaltimetype start;
  struct icaltimetype end;
  icalcomponent *event;
} event_item;



/* fatal -- print error message and exit with errcode */
static void fatal(int errcode, const char *message,...)
{
  va_list args;
  va_start(args, message);
  vfprintf(stderr, message, args);
  va_end(args);
  exit(errcode);
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
  char *classmask = NULL, *categorymask = NULL;
  char *notclassmask = NULL, *notcategorymask = NULL;
  char c;
  const char *class;
  icalset *out;
  icalcomponent *h, *next, *newset;
  icalproperty *p;

  /* We handle errors ourselves */
  icalerror_errors_are_fatal = 0;
  icalerrno = 0;

  /* Read commandline */
  while ((c = getopt_long(argc, argv, OPTIONS, options, NULL)) != -1) {
    switch (c) {
    case 'p': classmask = strdup(optarg); break;
    case 'P': notclassmask = strdup(optarg); break;
    case 'c': categorymask = strdup(optarg); break;
    case 'C': notcategorymask = strdup(optarg); break;
    default: fatal(ERR_USAGE, USAGE);
    }
  }
  /* Get input file name */
  if (optind == argc) fatal(ERR_USAGE, USAGE);
  stream = optind == argc ? stdin : fopen(argv[optind], "r");
  if (!stream) fatal(ERR_FILEIO, "%s: %s\n", argv[optind], strerror(errno));
  optind++;

  /* Get output file name */
  if (optind == argc) fatal(ERR_USAGE, USAGE);
  (void) unlink(argv[optind]);	/* Remove output file if it already exists */
  out = icalfileset_new(argv[optind]);
  if (!out) fatal(ERR_FILEIO, "%s: %s\n", argv[optind], strerror(errno));

  /* Should have no more arguments */
  if (optind + 1 != argc) fatal(ERR_USAGE, USAGE);

  /* Create a new parser object */
  parser = icalparser_new();

  /* Tell the parser what input stream it should use */
  icalparser_set_gen_data(parser, stream);

  /* Let the parser read the file and return all components */
  if (! (comp = icalparser_parse(parser, read_stream)))
    fatal(ERR_PARSE, "Parse error: %s\n", icalerror_strerror(icalerrno));
  icalparser_free(parser);

  /* Initialize a new VCALENDAR */
  newset = icalcomponent_vanew(ICAL_VCALENDAR_COMPONENT,
			       icalproperty_new_version("2.0"),
			       icalproperty_new_prodid(PRODID),
			       0);

  /* Iterate over VEVENTs */
  for (h = icalcomponent_get_first_component(comp, ICAL_VEVENT_COMPONENT);
       h; h = next) {

    next = icalcomponent_get_next_component(comp, ICAL_VEVENT_COMPONENT);

    /* Check if the event is of the right class (unless we accept all) */
    if (classmask || notclassmask) {
      p = icalcomponent_get_first_property(h, ICAL_CLASS_PROPERTY);
      class = p ? icalvalue_as_ical_string(icalproperty_get_value(p))
	: "NONE";
      if (classmask && strcasecmp(classmask, class) != 0) continue;
      if (notclassmask && strcasecmp(notclassmask, class) == 0) continue;
    }

    /* Check if the event is of the right category (unless we accept all) */
    if (categorymask) {
      p = icalcomponent_get_first_property(h, ICAL_CATEGORIES_PROPERTY);
      while (p && strcasecmp(categorymask, icalproperty_get_categories(p)))
	p = icalcomponent_get_next_property(h, ICAL_CATEGORIES_PROPERTY);
      if (!p) continue;		/* No category was equal to categorymask */
    }
    if (notcategorymask) {
      p = icalcomponent_get_first_property(h, ICAL_CATEGORIES_PROPERTY);
      while (p && strcasecmp(notcategorymask, icalproperty_get_categories(p)))
	p = icalcomponent_get_next_property(h, ICAL_CATEGORIES_PROPERTY);
      if (p) continue;		/* Some category equal to notcategorymask */
    }

    /* The event passed our filters, so add it to the output set */
    icalcomponent_remove_component(comp, h); /* Move from comp... */
    icalcomponent_add_component(newset, h); /* ... to out */
  }

  /* Put new vcalendar in output file, flush to disk and close file */
  icalfileset_add_component(out, newset);
  icalfileset_free(out);

  /* Clean up */
  icalcomponent_free(comp);

  return 0;
}
