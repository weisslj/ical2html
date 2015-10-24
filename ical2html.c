/*
 * ical2html -- create an HTML table from icalendar data
 *
 * Makes a calendar for the *local* time zone. You can change what is
 * "local" by setting the TZ environment variable, e.g.,
 *
 *     export TZ=Europe/Paris
 *
 * Author: Bert Bos <bert@w3.org>
 * Created: 22 Sep 2002
 */

#include "config.h"
#include <stdio.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <getopt.h>
#include <ctype.h>
#include <libical/ical.h>
/*
#include <icaltime.h>
#include <icalcomponent.h>
#include <icalparser.h>
#include <icalerror.h>
#include <icaltimezone.h>
*/

#define INC 20			/* Used for realloc() */

#define ERR_OUT_OF_MEM 1	/* Program exit codes */
#define ERR_USAGE 2
#define ERR_DATE 3
#define ERR_PARSE 4

#define USAGE "Usage: ical2html [options] start duration [file]\n\
  -p, --class=CLASS            only (PUBLIC, CONFIDENTIAL, PRIVATE, NONE)\n\
  -P, --not-class=CLASS        exclude (PUBLIC, CONFIDENTIAL, PRIVATE, NONE)\n\
  -c, --category=CATEGORY      only events of this category\n\
  -C, --not-category=CATEGORY  exclude events of this category\n\
  -d, --description            include event's long description in a <PRE>\n\
  -l, --location               include event's location in that <PRE>\n\
  -f, --footer=TEXT            add text at the bottom of the HTML file\n\
  -z, --timezone=country/city  adjust for this timezone (default: GMT)\n\
  -m, --monday                 draw Monday as first week day (Sunday is default)\n\
  start is of the form yyyymmdd, e.g., 20020927 (27 Sep 2002)\n\
  duration is in days or weeks, e.g., P5W (5 weeks) or P60D (60 days)\n\
  file is an iCalendar file, default is standard input\n"

/* Long command line options */
static struct option options[] = {
  {"class", 1, 0, 'p'},
  {"not-class", 1, 0, 'P'},
  {"category", 1, 0, 'c'},
  {"not-category", 1, 0, 'C'},
  {"description", 0, 0, 'd'},
  {"location", 0, 0, 'l'},
  {"footer", 1, 0, 'f'},
  {"timezone", 1, 0, 'z'},
  {"monday", 0, 0, 'm'},
  {0, 0, 0, 0}
};

#define OPTIONS "dlmp:P:c:C:f:z:"

static const char *months[] = {"", "January", "February", "March", "April",
			       "May", "June", "July", "August", "September",
			       "October", "November", "December"};

/* Structure for storing applicable events */
typedef struct _event_item {
  struct icaltimetype start;
  struct icaltimetype end;
  icalcomponent *event;
} event_item;

static event_item *events = NULL;
static int nrevents = 0;


/* fatal -- print error message and exit with errcode */
static void fatal(int errcode, const char *message,...)
{
  va_list args;
  va_start(args, message);
  vfprintf(stderr, message, args);
  va_end(args);
  exit(errcode);
}


/* compare_events -- compare two event_items (for qsort) */
static int compare_events(const void *aa, const void *bb)
{
  event_item *a = (event_item *)aa;
  event_item *b = (event_item *)bb;
  int h;

  h = icaltime_compare(a->start, b->start);
  if (h == 0) h = icaltime_compare(a->end, b->end);
  return h;
}


/* read_stream -- read size bytes into s from stream d */
static char* read_stream(char *s, size_t size, void *d)  
{  
  return fgets(s, size, (FILE*)d); 
} 


/* print_header -- print boilerplate at start of output */
static void print_header(struct icaltimetype start, struct icaldurationtype dur)
{
  struct icaltimetype end = icaltime_add(start, dur);

  printf("<!doctype html public \"-//W3C//DTD HTML 4.01//EN\"\n");
  printf("  \"http://www.w3.org/TR/html4/strict.dtd\">\n");
  printf("<meta http-equiv=\"Content-Type\" ");
  printf("content=\"text/html;charset=UTF-8\">\n");
  printf("<title>Calendar %d %s %d - %d %s %d</title>\n",
	 start.day, months[start.month], start.year,
	 end.day, months[end.month], end.year);
  printf("<link rel=stylesheet href=\"calendar.css\">\n\n");
}


/* print_footer -- print boilerplate at end of output */
static void print_footer(const char *footer)
{
  if (footer) printf("%s\n", footer);
}


/* print_as_one_word -- replace non-alphanumeric characters by dashes */
static void print_as_one_word(const char *s)
{
  const char *t;

  for (t = s; *t; t++)
    if (isalnum(*t)) putchar(*t);
    else putchar('-');
}


/* print_escaped -- printf with <, >, & and " escaped */
static void print_escaped(const char *s)
{
  const char *t;

  for (t = s; *t; t++) 
    switch (*t) {
    case '<': printf("&lt;"); break;
    case '>': printf("&gt;"); break;
    case '&': printf("&amp;"); break;
    default: putchar(*t);
    }
}


#if 0
/* print_attribute -- printf with &, " and newlines escaped */
static void print_attribute(const char *s)
{
  const char *t;

  for (t = s; *t; t++) 
    switch (*t) {
    case '<': printf("&lt;"); break;
    case '>': printf("&gt;"); break;
    case '&': printf("&amp;"); break;
    case '"': printf("&quot;"); break;
    case '\n': printf("&#10;"); break;
    default: putchar(*t);
    }
}
#endif


/* print_event -- print HTML paragraph for one event */
static void print_event(const event_item ev, const int do_description, const int do_location)
{
  icaltimezone *utc = icaltimezone_get_utc_timezone();
  icaltimetype start_utc, end_utc;
  icalproperty *p, *desc, *loc;
  int first;

  printf("<div class=vevent><p class=\"");

  /* Add all categories to the class attribute */
  first = 1;
  p = icalcomponent_get_first_property(ev.event, ICAL_CATEGORIES_PROPERTY);
  while (p) {
    if (first) first = 0; else printf(" ");
    print_as_one_word(icalproperty_get_categories(p));
    p = icalcomponent_get_next_property(ev.event, ICAL_CATEGORIES_PROPERTY);
  }
  printf("\">\n<span class=categories>");

  /* Also add all categories as content */
  first = 1;
  p = icalcomponent_get_first_property(ev.event, ICAL_CATEGORIES_PROPERTY);
  while (p) {
    if (first) first = 0; else printf(", ");
    print_as_one_word(icalproperty_get_categories(p));
    p = icalcomponent_get_next_property(ev.event, ICAL_CATEGORIES_PROPERTY);
  }
  printf("</span>\n");

  /* If there is a time, print it */
  start_utc = icaltime_convert_to_zone(ev.start, utc);
  end_utc = icaltime_convert_to_zone(ev.end, utc);
  if (ev.start.hour || ev.start.minute || ev.end.hour || ev.end.minute)
    printf("<span class=time><abbr class=dtstart\n\
title=\"%04d%02d%02dT%02d%02d%02dZ\">%02d:%02d</abbr>-<abbr class=dtend\n\
title=\"%04d%02d%02dT%02d%02d%02dZ\">%02d:%02d</abbr></span>\n",
	   start_utc.year, start_utc.month, start_utc.day, start_utc.hour,
	   start_utc.minute, start_utc.second,
	   ev.start.hour, ev.start.minute,
	   end_utc.year, end_utc.month, end_utc.day, end_utc.hour,
	   end_utc.minute, end_utc.second,
	   ev.end.hour, ev.end.minute);
  else
    printf("<span class=notime><abbr class=dtstart\n\
title=\"%04d%02d%02d\">(whole</abbr> <abbr class=duration\n\
title=\"1D\">day)</abbr></span>\n", start_utc.year, start_utc.month,
	   start_utc.day);

  /* Print the summary */
  printf("<span class=summary>");
  p = icalcomponent_get_first_property(ev.event, ICAL_SUMMARY_PROPERTY);
  if (p) print_escaped(icalproperty_get_summary(p));
  printf("</span>\n");

  /* If we want descriptions, check if there is one */
  if (do_description)
    desc = icalcomponent_get_first_property(ev.event, ICAL_DESCRIPTION_PROPERTY);
  else
    desc = NULL;

  /* If we want locations, check if there is one */
  if (do_location)
    loc = icalcomponent_get_first_property(ev.event, ICAL_LOCATION_PROPERTY);
  else
    loc = NULL;

  /* If we have a description and/or location, print them */
  if (desc || loc) printf("<pre>");
  if (desc) {
    printf("<span class=description>");
    print_escaped(icalproperty_get_description(desc));
    printf("</span>");
  }
  if (desc && loc) printf("\n");
  if (loc) {
    printf("<b class=location>");
    print_escaped(icalproperty_get_description(loc));
    printf("</b>");
  }
  if (desc || loc) printf("</pre>\n");

  printf("</div>\n\n");
}


/* print_calendar -- print monthly calendars with events */
static void print_calendar(const struct icaltimetype start,
			   const struct icaldurationtype duration,
			   const int nrevents, const event_item events[],
			   const int do_description, const int do_location,
			   const int starts_on_monday)
{
  struct icaltimetype day;
  struct icaltimetype end;
  char s[9];
  int y, m, d, w;
  int i = 0;	/* Loop over events */
  int skip;	/* How many days to skip of that week until 1st */
  int lastDay;
  if (starts_on_monday) lastDay = 2; else lastDay = 1;

  end = icaltime_add(start, duration);

  /* Loop over the years in our period */
  for (y = start.year; y <= end.year; y++) {

    /* Loop over the months in this year */
    for (m = (y == start.year ? start.month : 1);
	 m <= (y == end.year ? end.month : 12); m++) {

      /* Is there a more efficient way to set day than via a string? */
      sprintf(s, "%04d%02d01", y, m);
      day = icaltime_from_string(s);

      printf("<table><caption>%s %d</caption>\n", months[m], y);
      printf("<thead><tr>");
      if (starts_on_monday) {
         printf("<th>Monday<th>Tuesday<th>Wednesday");
         printf("<th>Thursday<th>Friday<th>Saturday<th>Sunday\n");
      }
      else {
         printf("<th>Sunday<th>Monday<th>Tuesday<th>Wednesday");
         printf("<th>Thursday<th>Friday<th>Saturday\n");

      }
      printf("<tbody>\n");

      w = icaltime_day_of_week(day);
      if (starts_on_monday)
         if (w == 1) skip = 6; else skip = w-2;
      else
         skip = w-1;

      if (skip != 0) {
         for(; skip > 0; skip--) printf("<td class=skip>\n");
      }

      /* Skip events before this day (can only occur at very start) */
      for (; i < nrevents
	     && icaltime_compare_date_only(events[i].start, day) < 0; i++) ;

      /* Loop over the days in this month */
      for (d = 1; d <= icaltime_days_in_month(m, y); d++) {

	sprintf(s, "%04d%02d%02d", y, m, d);
	day = icaltime_from_string(s);
	w = icaltime_day_of_week(day);
	if (w == lastDay)
	{
		printf("<tr>\n");
	}

	printf("<td><p class=date>%d\n\n", d);

	/* Print all events on this day (the events are sorted) */
	for (; i < nrevents
	       && icaltime_compare_date_only(events[i].start, day) == 0; i++)
		print_event(events[i], do_description, do_location);
      }

      printf("</table>\n\n");
    }

  }
}


/* add_to_queue -- add event to global queue of events to print */
static void add_to_queue(icalcomponent *ev, const struct icaltimetype start,
			 const struct icaltimetype end,
			 icaltimezone *tz)
{
  int n = (nrevents/INC + 1) * INC;

  if (!(events = realloc(events, n * sizeof(*events))))
    fatal(ERR_OUT_OF_MEM, "Out of memory\n");


  events[nrevents].start = icaltime_convert_to_zone(start, tz);
  events[nrevents].end = icaltime_convert_to_zone(end, tz);
  events[nrevents].event = ev;
  nrevents++;
}


/* iterate -- process each component in the collection */
static void iterate(icalcomponent *c, struct icaltimetype periodstart,
		    struct icaldurationtype duration,
		    const char *classmask, const char *categorymask,
		    const char *notclassmask, const char *notcategorymask,
		    icaltimezone *tz)
{
  const struct icaldurationtype one = {0, 1, 0, 0, 0, 0};
  struct icaltimetype periodend, dtstart, dtend, next, nextend, d;
  icalcomponent *h; 
  icalproperty *p;
  struct icaldurationtype dur;    
  icalproperty *rrule;
  struct icalrecurrencetype recur;
  icalrecur_iterator *ritr;
  const char *class;

  periodend = icaltime_add(periodstart, duration);

  /* Iterate over VEVENTs */
  for (h = icalcomponent_get_first_component(c, ICAL_VEVENT_COMPONENT); h;
       h = icalcomponent_get_next_component(c, ICAL_VEVENT_COMPONENT)) {

    /* Check if the event is of the right class (unless we accept all) */
    if (classmask || notclassmask) {
      p = icalcomponent_get_first_property(h, ICAL_CLASS_PROPERTY);
      class = p ? icalproperty_get_value_as_string(p) : "PUBLIC";
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

    /* Get start time and recurrence rule (if any) */
    dtstart = icalcomponent_get_dtstart(h);
    rrule = icalcomponent_get_first_property(h, ICAL_RRULE_PROPERTY);

    if (!rrule) {		/* Not recurring */

      /* Check if this event is at least partially within our period */
      if (icaltime_compare(dtstart, periodend) <= 0) {
	dtend = icalcomponent_get_dtend(h);
	if (dtend.year + dtend.month + dtend.day + dtend.hour + dtend.minute +
	    dtend.second == 0) dtend = dtstart;
	if (icaltime_compare(periodstart, dtend) <= 0) {

	  /* Add to as many days as it spans */
	  d = dtstart;
	  do {
	    add_to_queue(h, d, dtend, tz);
	    d = icaltime_add(d, one);
	  } while (icaltime_compare(d, dtend) < 0);
	}
      }

    } else {			/* Recurring event */

      if (icaltime_compare(dtstart, periodend) <= 0) {
      
	/* Iterate to find occurrences within our period */
	dur = icalcomponent_get_duration(h);
	recur = icalproperty_get_rrule(rrule);
	ritr = icalrecur_iterator_new(recur, dtstart);
	while (next = icalrecur_iterator_next(ritr),
	       !icaltime_is_null_time(next)
	       && icaltime_compare(next, periodend) <= 0) {

	  nextend = icaltime_add(next, dur);
	  if (icaltime_compare(nextend, periodstart) >= 0)
	    add_to_queue(h, next, nextend, tz);
	}

	/* Clean up */
	icalrecur_iterator_free(ritr);
      }
    }
  }
}


/* main */
int main(int argc, char *argv[])
{
  FILE* stream;
  icalcomponent *comp;
  icalparser *parser;
  struct icaltimetype periodstart;
  struct icaldurationtype duration;
  char *footer = NULL, *class = NULL, *category = NULL;
  char *not_class = NULL, *not_category = NULL;
  char c;
  int dummy1, dummy2, dummy3, do_description = 0, do_location = 0;
  icaltimezone *tz;
  int starts_on_monday = 0;

  /* We handle errors ourselves */
  icalerror_errors_are_fatal = 0;
  icalerror_clear_errno();

  /* icaltimezone_set_tzid_prefix("/kde.org/Olson_20080523_1/"); */
  /* icaltimezone_set_tzid_prefix(""); */
  set_zone_directory("/usr/share/apps/libical/zoneinfo/"); /* TO DO */
  tz = icaltimezone_get_utc_timezone();		/* Default */

  /* Read commandline */
  while ((c = getopt_long(argc, argv, OPTIONS, options, NULL)) != -1) {
    switch (c) {
    case 'p': class = strdup(optarg); break;
    case 'P': not_class = strdup(optarg); break;
    case 'c': category = strdup(optarg); break;
    case 'C': not_category = strdup(optarg); break;
    case 'd': do_description = 1; break;
    case 'l': do_location = 1; break;
    case 'f': footer = strdup(optarg); break;
    case 'z': tz = icaltimezone_get_builtin_timezone(optarg); break;
    case 'm': starts_on_monday = 1; break;
    default: fatal(ERR_USAGE, USAGE);
    }
  }
  if (optind == argc) fatal(ERR_USAGE, USAGE);
  if (sscanf(argv[optind], "%04d%02d%02d", &dummy1, &dummy2, &dummy3) < 3)
    fatal(ERR_DATE, "Incorrect date '%s', must be YYYYMMDD.\n", argv[optind]);
  periodstart = icaltime_from_string(argv[optind]);
  if (icalerrno) ;		/* TO DO */
  optind++;
  if (optind == argc) fatal(ERR_USAGE, USAGE);
  duration = icaldurationtype_from_string(argv[optind]);
  /*
  if (icalerrno)
    fatal(ERR_DATE, "Incorrect duration '%s', must be PnW or PnD.\n", argv[optind]);
  */
  optind++;
  stream = optind == argc ? stdin : fopen(argv[optind], "r");
  if (!stream) {perror(argv[optind]); exit(1);}
  if (optind + 1 < argc) fatal(ERR_USAGE, USAGE);

  /* Create a new parser object */
  parser = icalparser_new();

  /* Tell the parser what input stream it should use */
  icalparser_set_gen_data(parser, stream);

  /* Let the parser read the file and return all components */
  if (! (comp = icalparser_parse(parser, read_stream)))
    fatal(ERR_PARSE, "Parse error: %s\n", icalerror_strerror(icalerrno));

  /* Process the resulting list of components */
  iterate(comp, periodstart, duration,
	  class, not_class, category, not_category, tz);

  /* Sort the result */
  qsort(events, nrevents, sizeof(*events), compare_events);

  /* Print the sorted results */
  print_header(periodstart, duration);
  print_calendar(periodstart, duration, nrevents, events, do_description, do_location, starts_on_monday);
  print_footer(footer);

  /* Clean up */
  icalcomponent_free(comp);
  icalparser_free(parser); 

  return 0;
}
