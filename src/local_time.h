#ifndef CARHMI_LOCAL_TIME_H
#define CARHMI_LOCAL_TIME_H

#include "dmutc.h"

/*
 * The time the HMI shows: dmutc's UTC as Polish civil time - CET (UTC+1),
 * CEST (UTC+2) from the last Sunday of March to the last Sunday of October,
 * both at 01:00 UTC (the EU rule).
 */

/** @brief The local calendar of a UTC time. 0, else dmutc's error (the calendar cleared) */
int local_time(const dmutc_time_t* utc, dmutc_calendar_t* local);

/** @brief "HH:MM" of now, into out. 0, else dmutc's error (-ENODEV: no RTC; out is "--:--") */
int local_time_text(char* out, size_t size);

#endif
