/*
 * ical2html -- create an HTML table from icalendar data
 *
 * Author: Bert Bos <bert@w3.org>
 * Created: 22 Sep 2002
 * Version: $Id: ical2html.c,v 1.1 2002/09/28 20:41:07 bbos Exp $
 */

#include <stdio.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>
#include <ctype.h>
#include <ical.h>
#include "config.h"

#define INC 20			/* Used for realloc() */

#define USAGE "Usage: ical2html [options] start end [file]\n\
  -p, --class=CLASS            PUBLIC, CONFIDENTIAL, PRIVATE, NONE\n\
  -P, --not-class=CLASS        PUBLIC, CONFIDENTIAL, PRIVATE, NONE\n\
  -c, --category=CATEGORY      print only events of this category\n\
  -C, --not-category=CATEGORY  print all but events of this category\n\
  start and end are of the form yyyymmdd[Thhmmss[Z]]\n"

#define ERR_OUT_OF_MEM 1
#define ERR_USAGE 2
#define ERR_DATE 3
#define ERR_PARSE 4


/* Long command line options */
static struct option options[] = {
  {"class", 1, 0, 'p'},
  {"not-class", 1, 0, 'P'},
  {"category", 1, 0, 'c'},
  {"not-category", 1, 0, 'C'},
  {0, 0, 0, 0}
};

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
static void print_header(struct icaltimetype start, struct icaltimetype end)
{
  printf("<!doctype html public \"-//W3C//DTD HTML 4.01//EN\"\n");
  printf("  \"http://www.w3.org/TR/html4/strict.dtd\">\n");
  printf("<meta http-equiv=\"Content-Type\" ");
  printf("content=\"text/html;charset=UTF-8\">\n");
  printf("<title>Calendar %02d/%02d/%04d -  %02d/%02d/%04d</title>\n",
	 start.day, start.month, start.year, end.day, end.month, end.year);
  printf("<link rel=stylesheet href=\"calendar.css\">\n\n");
}


/* print_footer -- print boilerplate at end of output */
static void print_footer(void)
{
  /* Point to documentation, ics file, generation date... */
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


/* print_event -- print HTML paragraph for one event */
static void print_event(const event_item ev)
{
  icalproperty *p, *q;

  printf("<div class=\"event");

  /* Add all categories as HTML classes */
  p = icalcomponent_get_first_property(ev.event, ICAL_CATEGORIES_PROPERTY);
  while (p) {
    printf(" ");
    print_as_one_word(icalproperty_get_categories(p));
    p = icalcomponent_get_next_property(ev.event, ICAL_CATEGORIES_PROPERTY);
  }
  printf("\">");

  /* If there is a time, print it */
  if (ev.start.hour || ev.start.minute || ev.end.hour || ev.end.minute)
    printf("<p><span class=time>%02d:%02d-%02d:%02d</span> ",
	   ev.start.hour, ev.start.minute, ev.end.hour, ev.end.minute);
  else
    printf("<p class=notime>");

  /* Print the summary */
  printf("<span class=summary>");
  p = icalcomponent_get_first_property(ev.event, ICAL_SUMMARY_PROPERTY);
  if (p) print_escaped(icalproperty_get_summary(p));
  printf("</span>\n");

  /* If there is a description, print it */
  q = icalcomponent_get_first_property(ev.event, ICAL_DESCRIPTION_PROPERTY);
  if (q) {
    printf("<pre class=description>");
    print_escaped(icalproperty_get_description(q));
    printf("</pre>\n");
  }

  printf("</div>\n\n");
}


/* print_calendar -- print monthly calendars with events */
static void print_calendar(const struct icaltimetype start,
			   const struct icaltimetype end,
			   const int nrevents, const event_item events[])
{
  static const char *months[] = {"", "January", "February", "March", "April",
				 "May", "June", "July", "August", "September",
				 "October", "November", "December"};
  struct icaltimetype day = {0, 0, 0, 0, 0, 0, 0, 0, NULL};
  int y, m, d, w;
  int i = 0;			/* Loop over events */

  /* Loop over the years in our period */
  for (y = start.year; y <= end.year; y++) {
    day.year = y;

    /* Loop over the months in this year */
    for (m = (y == start.year ? start.month : 1);
	 m <= (y == end.year ? end.month : 12); m++) {
      day.month = m;
      printf("<table><caption>%s %d</caption>\n", months[m], y);
      printf("<thead><tr><th>Sunday<th>Monday<th>Tuesday<th>Wednesday");
      printf("<th>Thursday<th>Friday<th>Saturday\n");
      printf("<tbody>\n");
      day.day = 1;
      w = icaltime_day_of_week(day);
      if (w != 1) {		/* Empty cells until 1st of month */
	printf("<tr>\n");
	for (; w > 1; w--) printf("<td class=skip>\n");
      }

      /* Skip events before this day (can only occur at very start) */
      for (; i < nrevents
	     && icaltime_compare_date_only(events[i].start, day) < 0; i++) ;

      /* Loop over the days in this month */
      for (d = 1; d <= icaltime_days_in_month(m, y); d++) {
	day.day = d;
	w = icaltime_day_of_week(day);
	if (w == 1) printf("<tr>\n");
	printf("<td><p class=date>%d\n\n", d);

	/* Print all events on this day (the events are sorted) */
	for (; i < nrevents
	       && icaltime_compare_date_only(events[i].start, day) == 0; i++)
	  print_event(events[i]);
      }

      printf("</table>\n\n");
    }

  }
}


/* add_to_queue -- add event to global queue of events to print */
static void add_to_queue(icalcomponent *ev, const struct icaltimetype start,
			 const struct icaltimetype end)
{
  int n = (nrevents/INC + 1) * INC;

  if (!(events = realloc(events, n * sizeof(*events))))
    fatal(ERR_OUT_OF_MEM, "Out of memory\n");

  events[nrevents].start = start;
  events[nrevents].end = end;
  events[nrevents].event = ev;
  nrevents++;
}


/* iterate -- process each component in the collection */
static void iterate(icalcomponent *c, struct icaltimetype periodstart,
		    struct icaltimetype periodend,
		    const char *classmask, const char *categorymask,
		    const char *notclassmask, const char *notcategorymask)
{
  const struct icaldurationtype one = {0, 1, 0, 0, 0, 0};
  struct icaltimetype dtstart, dtend, next, nextend, d;
  icalcomponent *h; 
  icalproperty *p;
  struct icaldurationtype dur;    
  icalproperty *rrule;
  struct icalrecurrencetype recur;
  icalrecur_iterator *ritr;
  const char *class;

  /* Iterate over VEVENTs */
  for (h = icalcomponent_get_first_component(c, ICAL_VEVENT_COMPONENT); h;
       h = icalcomponent_get_next_component(c, ICAL_VEVENT_COMPONENT)) {

    /* Check if the event is of the right class (unless we accept all) */
    if (classmask || notclassmask) {
      p = icalcomponent_get_first_property(h, ICAL_CLASS_PROPERTY);
      class = p ? icalproperty_get_class(p) : "NONE";
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
	if (icaltime_compare(periodstart, dtend) <= 0) {

	  /* Add to as many days as it spans */
	  for (d = dtstart; icaltime_compare(d, dtend) <= 0;
	       d = icaltime_add(d, one)) add_to_queue(h, d, dtend);
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
	    add_to_queue(h, next, nextend);
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
  struct icaltimetype periodend, periodstart;
  char *class = NULL, *category = NULL;
  char *not_class = NULL, *not_category = NULL;
  char c;

  /* We handle errors ourselves */
  icalerror_errors_are_fatal = 0;
  icalerrno = 0;

  /* Read commandline */
  while ((c = getopt_long(argc, argv, "p:P:c:C:", options, NULL)) != -1) {
    switch (c) {
    case 'p': class = strdup(optarg); break;
    case 'P': not_class = strdup(optarg); break;
    case 'c': category = strdup(optarg); break;
    case 'C': not_category = strdup(optarg); break;
    default: fatal(ERR_USAGE, USAGE);
    }
  }
  if (optind == argc) fatal(ERR_USAGE, USAGE);
  periodstart = icaltime_from_string(argv[optind]);
  if (icalerrno)
    fatal(ERR_DATE, "Incorrect date '%s', must be YYYYMMDD.\n", argv[optind]);
  optind++;
  if (optind == argc) fatal(ERR_USAGE, USAGE);
  periodend = icaltime_from_string(argv[optind]);
  if (icalerrno)
    fatal(ERR_DATE, "Incorrect date '%s', must be YYYYMMDD.\n", argv[optind]);
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
  iterate(comp, periodstart, periodend,
	  class, not_class, category, not_category);

  /* Sort the result */
  qsort(events, nrevents, sizeof(*events), compare_events);

  /* Print the sorted results */
  print_header(periodstart, periodend);
  print_calendar(periodstart, periodend, nrevents, events);
  print_footer();

  /* Clean up */
  icalcomponent_free(comp);
  icalparser_free(parser); 

  return 0;
}
