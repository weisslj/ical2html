/*
 * Convert a date/time in UTC to a date/time in the local time zone.
 * (Seems to be missing from libical v 0.23)
 *
 * Author: Bert Bos <bert@w3.org>
 * Created: 18 Dec 2002
 * Version: $Id: icaltime_as_local.c,v 1.1 2002/12/18 14:47:52 bbos Exp $
 */

#include <time.h>
#include <ical.h>
#include "config.h"


#if !HAVE_icaltime_as_local

struct icaltimetype icaltime_as_local(struct icaltimetype tt)
{
  time_t t;
  struct tm *tm;
  struct icaltimetype h;
    
  t = icaltime_as_timet(tt);			/* Convert to epoch */
  tm = localtime(&t);				/* Convert to local time */
  h.year = tm->tm_year + 1900;			/* Make an icaltimetype */
  h.month = tm->tm_mon + 1;
  h.day = tm->tm_mday;
  h.hour = tt.is_date ? 0 : tm->tm_hour;
  h.minute = tt.is_date ? 0 : tm->tm_min;
  h.second = tt.is_date ? 0 : tm->tm_sec;
  h.is_utc = 0;
  h.is_date = tt.is_date;
  return h;
}

#endif
