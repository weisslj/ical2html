/*
 * icalmerge -- merge iCalendar files, keeping the newer of any duplicate events
 *
 * Author: Bert Bos <bert@w3.org>
 * Created: 30 Sep 2002
 */

#include "config.h"
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>
#include <ctype.h>
#include <libical/icalcomponent.h>
#include <libical/icalparser.h>
#include <libical/icalset.h>
#include <libical/icalfileset.h>


#define PRODID "-//W3C//NONSGML icalmerge " VERSION "//EN"

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


#ifdef HAVE_SEARCH_H
#  include <search.h>
#else
/*
 * hsearch() on Mac OS X 10.1.2 appears to be broken: there is no
 * search.h, there is a search() in the C library, but it doesn't work
 * properly. We include some hash functions here, protected by
 * HAVE_SEARCH_H. Hopefully when search.h appears in Mac OS X,
 * hsearch() will be fixed at the same time...
 */

typedef struct entry {char *key; void *data;} ENTRY;
typedef enum {FIND, ENTER} ACTION;

static ENTRY *htab;
static int *htab_index1, *htab_index2;
static unsigned int htab_size = 0;
static unsigned int htab_inited;


/* isprime -- test if n is a prime number */
static int isprime(unsigned int n)
{
  /* Simplistic algorithm, probably good enough for now */
  unsigned int i;
  assert(n % 2);				/* n not even */
  for (i = 3; i * i < n; i += 2) if (n % i == 0) return 0;
  return 1;
}


/* hcreate -- create a hash table for at least nel entries */
static int hcreate(size_t nel)
{
  /* Change nel to next higher prime */
  for (nel |= 1; !isprime(nel); nel += 2) ;

  /* Allocate hash table and array to keep track of initialized entries */
  if (! (htab = malloc(nel * sizeof(*htab)))) return 0;
  if (! (htab_index1 = malloc(nel * sizeof(*htab_index1)))) return 0;
  if (! (htab_index2 = malloc(nel * sizeof(*htab_index2)))) return 0;
  htab_inited = 0;
  htab_size = nel;

  return 1;
}


#if 0
/* hdestroy -- deallocate hash table */
static void hdestroy(void)
{
  assert(htab_size);
  free(htab_index1);
  free(htab_index2);
  free(htab);
  htab_size = 0;
}
#endif


/* hsearch -- search for and/or insert an entry in the hash table */
static ENTRY *hsearch(ENTRY item, ACTION action)
{
  unsigned int hval, i;
  char *p;

  assert(htab_size);				/* There must be a hash table */

  /* Compute a hash value */
#if 1
  /* This function suggested by Dan Bernstein */
  for (hval = 5381, p = item.key; *p; p++) hval = (hval * 33) ^ *p;
#else
  i = hval = strlen(item.key);
  do {i--; hval = (hval << 1) + item.key[i];} while (i > 0);
#endif
  hval %= htab_size;
  /* if (action == ENTER) debug("%d\n", hval); */

  /* Look for either an empty slot or an entry with the wanted key */
  i = hval;
  while (htab_index1[i] < htab_inited
	 && htab_index2[htab_index1[i]] == i
	 && strcmp(htab[i].key, item.key) != 0) {
    i = (i + 1) % htab_size;			/* "Open" hash method */
    if (i == hval) return NULL;			/* Made full round */
  }
  /* Now we either have an empty slot or an entry with the same key */
  if (action == ENTER) {
    htab[i].key = item.key;			/* Put the item in this slot */
    htab[i].data = item.data;
    if (htab_index1[i] >= htab_inited || htab_index2[htab_index1[i]] != i) {
      /* Item was not yet used, mark it as used */
      htab_index1[i] = htab_inited;
      htab_index2[htab_inited] = i;
      htab_inited++;
    }
    return &htab[i];
  } else if (htab_index1[i] < htab_inited && htab_index2[htab_index1[i]] == i)
    return &htab[i];				/* action == FIND, found key */

  return NULL;					/* Found empty slot */
}

#endif /* HAVE_SEARCH_H */


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
    if (! hcreate(10000)) fatal(ERR_HASH, "%s\n", strerror(errno));
    first_time = 0;
  }

  /* Iterate over VEVENTs */
  for (h = icalcomponent_get_first_component(b, ICAL_VEVENT_COMPONENT);
       h; h = next) {

    next = icalcomponent_get_next_component(b, ICAL_VEVENT_COMPONENT);

    uid = icalcomponent_get_first_property(h, ICAL_UID_PROPERTY);
    /*debug("%s", uid ? icalproperty_get_uid(uid) : "NO UID!?");*/
    if (!uid) continue;				/* Error in iCalendar file */
    e1.key = strdup(icalproperty_get_uid(uid));
    if (! (e = hsearch(e1, FIND))) {

      /* New UID, add the VEVENT to the hash and to the set a */
      icalcomponent_remove_component(b, h); /* Move from b... */
      icalcomponent_add_component(*a, h); /* ... to a */
      e1.data = h;
      if (! (e = hsearch(e1, ENTER)))
	fatal(ERR_OUT_OF_MEM, "No room in hash table\n");
      /*debug(" (added %lx)\n", h);*/

    } else {

      /* Already an entry with this UID, compare modified dates */
      /*debug(" (found %lx", e->data);*/
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
	e->data = h;
	/*debug(" replaced)\n");*/
      } else {
	/*debug(" ignored)\n");*/
      }      
      free(e1.key);
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
