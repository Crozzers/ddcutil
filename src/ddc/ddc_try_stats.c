/** @file ddc_try_stats.c
 *
 *  Maintains statistics on DDC retries.
 */

// Copyright (C) 2014-2020 Sanford Rockowitz <rockowitz@minsoft.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/** \cond */
#include <assert.h>
#include <glib-2.0/glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/** \endcond */

#include "util/report_util.h"

#include "base/core.h"
#include "base/ddc_errno.h"
#include "base/parms.h"

#include "base/per_thread_data.h"    // for retry_type_name()
#include "base/thread_sleep_data.h"
#include "base/thread_retry_data.h"
#include "ddc/ddc_try_stats.h"

static GMutex try_data_mutex;
static bool debug_mutex = false;

// counters usage:
//  0  number of failures because of fatal errors
//  1  number of failures because retry exceeded
//  n>1 number of successes after n-1 tries,
//      e.g. if succeed after 1 try, recorded in counter 2



typedef
struct {
   DDCA_Retry_Type retry_type;
   int    maxtries;
   int    counters[MAX_MAX_TRIES+2];
   int    highest_maxtries;
   int    lowest_maxtries;
} Try_Data2;


static int default_maxtries[] = {
      INITIAL_MAX_WRITE_ONLY_EXCHANGE_TRIES,
      INITIAL_MAX_WRITE_READ_EXCHANGE_TRIES,
      INITIAL_MAX_MULTI_EXCHANGE_TRIES,
      INITIAL_MAX_MULTI_EXCHANGE_TRIES };

Try_Data2* try_data2[4];


/* Allocates and initializes a Try_Data data structure
 * 
 * Arguments: 
 *    stat_name   name of the statistic being recorded
 *    max_tries   maximum number of tries 
 *
 * Returns: 
 *    opaque pointer to the allocated data structure
 */
Try_Data * try_data_create(DDCA_Retry_Type retry_type, char * stat_name, int max_tries) {
   assert(strlen(stat_name) <= MAX_STAT_NAME_LENGTH);
   assert(0 <= max_tries && max_tries <= MAX_MAX_TRIES);
   Try_Data* try_data = calloc(1,sizeof(Try_Data));
   memcpy(try_data->tag, TRY_DATA_TAG,4);
   try_data->retry_type = retry_type;
   strcpy(try_data->stat_name, stat_name);
   try_data->max_tries = max_tries;
   // DBGMSG("stats_rec->counters[MAX_MAX_TRIES+1]=%d, MAX_MAX_TRIES=%d", stats_rec->counters[MAX_MAX_TRIES+1], MAX_MAX_TRIES);
   return try_data;
}

Try_Data2 * try_data_create2(DDCA_Retry_Type retry_type, int max_tries) {
   assert(0 <= max_tries && max_tries <= MAX_MAX_TRIES);
   Try_Data2* try_data = calloc(1,sizeof(Try_Data2));
   try_data->retry_type = retry_type;
   try_data->maxtries = max_tries;
   try_data->highest_maxtries = 0;
   try_data->lowest_maxtries = MAX_MAX_TRIES+1;
   return try_data;
}


void init_ddc_try_data() {
   for (int retry_type = 0; retry_type < DDCA_RETRY_TYPE_COUNT; retry_type++) {
      try_data2[retry_type] = try_data_create2(retry_type, default_maxtries[retry_type] );
   }
}


#ifdef OLD
static inline Try_Data * unopaque(void * opaque_ptr) {
   Try_Data * try_data = (Try_Data*) opaque_ptr;
   assert(try_data && memcmp(try_data->tag, TRY_DATA_TAG, 4) == 0);
   return try_data;
}
#endif


int  try_data_get_max_tries(Try_Data * stats_rec) {
   bool debug = true;
   // Try_Data * try_data = unopaque(stats_rec);
   int result =  stats_rec->max_tries;
   DBGMSF(debug, "retry type=%s, returning %d", retry_type_name(stats_rec->retry_type), result);
   return result;
}

int  try_data_get_max_tries2(DDCA_Retry_Type retry_type) {
   bool debug = false;
   // DBGMSG("retry_type=%d", retry_type);
   Try_Data2 * stats_rec = try_data2[retry_type];
   // DBGMSG("stats_rec=%p", stats_rec);
   int result =  stats_rec->maxtries;
   DBGMSF(debug, "retry type=%s, returning %d", retry_type_name(stats_rec->retry_type), result);
   return result;
}



void try_data_set_max_tries(Try_Data * stats_rec, int new_max_tries) {
   bool debug = false;
   debug = debug || debug_mutex;
   DBGMSF(debug, "Starting. stats type: %s for %s, new_max_tries: %d",
                 retry_type_name(stats_rec->retry_type),
                 stats_rec->stat_name,
                 new_max_tries);

   // Try_Data * try_data = unopaque(stats_rec);
   assert(new_max_tries >= 1 && new_max_tries <= MAX_MAX_TRIES);

   g_mutex_lock(&try_data_mutex);
   stats_rec->max_tries = new_max_tries;
   g_mutex_unlock(&try_data_mutex);

   DBGMSF(debug, "Done");
}



void try_data_set_max_tries2(DDCA_Retry_Type retry_type, int new_max_tries) {
   bool debug = false;
   debug = debug || debug_mutex;
   Try_Data2 * stats_rec = try_data2[retry_type];
   DBGMSF(debug, "Starting. stats type: %s for %s, new_max_tries: %d",
                 retry_type_name(stats_rec->retry_type),
                 retry_type_description(stats_rec->retry_type),
                 new_max_tries);

   // Try_Data * try_data = unopaque(stats_rec);
   assert(new_max_tries >= 1 && new_max_tries <= MAX_MAX_TRIES);

   g_mutex_lock(&try_data_mutex);
   stats_rec->maxtries = new_max_tries;
   if (new_max_tries < stats_rec->lowest_maxtries)
      stats_rec->lowest_maxtries = new_max_tries;
   if (new_max_tries > stats_rec->highest_maxtries)
      stats_rec->highest_maxtries = new_max_tries;

   g_mutex_unlock(&try_data_mutex);

   DBGMSF(debug, "Done");
}



void try_data_reset(Try_Data * stats_rec) {
   bool debug = true;
   debug = debug || debug_mutex;
   DBGMSF(debug, "Starting, stats type: %s", retry_type_name(stats_rec->retry_type));

   // Try_Data * try_data = unopaque(stats_rec);

   g_mutex_lock(&try_data_mutex);
   for (int ndx=0; ndx < MAX_MAX_TRIES+1; ndx++)
      stats_rec->counters[ndx] = 0;
   g_mutex_unlock(&try_data_mutex);

   DBGMSF(debug, "Done");
}

void try_data_reset2(DDCA_Retry_Type retry_type) {
   bool debug = false;
   debug = debug || debug_mutex;
   Try_Data2 * stats_rec = try_data2[retry_type];
   DBGMSF(debug, "Starting, stats type: %s", retry_type_name(retry_type));

   g_mutex_lock(&try_data_mutex);
   int val = default_maxtries[retry_type];
   stats_rec->maxtries = val;
   stats_rec->highest_maxtries = default_maxtries[retry_type];
   stats_rec->lowest_maxtries = default_maxtries[retry_type];
#ifdef WRONG_BUT_USEFUL_ELSEWHERE
   for (int ndx=0; ndx < MAX_MAX_TRIES+1; ndx++)
      try_data2[retry_type]->counters[ndx] = 0;
#endif
   g_mutex_unlock(&try_data_mutex);

   DBGMSF(debug, "Done");
}




static void record_successful_tries(Try_Data * stats_rec, int tryct){
   bool debug = false || debug_mutex;
   DBGMSF(debug, "Starting");

   // Try_Data * try_data = unopaque(stats_rec);
   assert(0 < tryct && tryct <= stats_rec->max_tries);

   g_mutex_lock(&try_data_mutex);
   stats_rec->counters[tryct+1] += 1;
   g_mutex_unlock(&try_data_mutex);

   DBGMSF(debug, "Done");
}


static void record_successful_tries2(DDCA_Retry_Type retry_type, int tryct){
   bool debug = false || debug_mutex;
   DBGMSF(debug, "Starting");
   Try_Data2 * stats_rec = try_data2[retry_type];
   // Try_Data * try_data = unopaque(stats_rec);
   assert(0 < tryct && tryct <= stats_rec->maxtries);

   g_mutex_lock(&try_data_mutex);
   stats_rec->counters[tryct+1] += 1;
   g_mutex_unlock(&try_data_mutex);

   DBGMSF(debug, "Done");
}




static void record_failed_max_tries(Try_Data * stats_rec) {
   bool debug = false || debug_mutex;
   DBGMSF(debug, "Starting");

   // Try_Data * try_data = unopaque(stats_rec);

   g_mutex_lock(&try_data_mutex);
   stats_rec->counters[1] += 1;
   g_mutex_unlock(&try_data_mutex);

   DBGMSF(debug, "Done");
}


static void record_failed_max_tries2(DDCA_Retry_Type retry_type) {
   bool debug = false || debug_mutex;
   DBGMSF(debug, "Starting");
   Try_Data2 * stats_rec = try_data2[retry_type];
   // Try_Data * try_data = unopaque(stats_rec);

   g_mutex_lock(&try_data_mutex);
   stats_rec->counters[1] += 1;
   g_mutex_unlock(&try_data_mutex);

   DBGMSF(debug, "Done");
}



static void record_failed_fatally(Try_Data * stats_rec) {
   bool debug = false || debug_mutex;
    DBGMSF(debug, "Starting");

   // Try_Data * try_data = unopaque(stats_rec);

   g_mutex_lock(&try_data_mutex);
   stats_rec->counters[0] += 1;
   g_mutex_unlock(&try_data_mutex);

   DBGMSF(debug, "Done");
}

static void record_failed_fatally2(DDCA_Retry_Type retry_type) {
   bool debug = false || debug_mutex;
    DBGMSF(debug, "Starting");

    Try_Data2 * stats_rec = try_data2[retry_type];

   // Try_Data * try_data = unopaque(stats_rec);

   g_mutex_lock(&try_data_mutex);
   stats_rec->counters[0] += 1;
   g_mutex_unlock(&try_data_mutex);

   DBGMSF(debug, "Done");
}


void try_data_record_tries(Try_Data * stats_rec, int rc, int tryct) {
   // DBGMSG("stats_rec=%p, rc=%d, tryct=%d", stats_rec, rc, tryct);
   // Try_Data * try_data = unopaque(stats_rec);
   // TODO: eliminate function calls
   if (rc == 0) {
      record_successful_tries(stats_rec, tryct);
   }
   // else if (tryct == stats_rec->max_tries) {
   // fragile, but eliminates testing for max_tries:
   else if (rc == DDCRC_RETRIES || rc == DDCRC_ALL_TRIES_ZERO) {
      record_failed_max_tries(stats_rec);
   }
   else {
      record_failed_fatally(stats_rec);
   }
}


void try_data_record_tries2(DDCA_Retry_Type retry_type, int rc, int tryct) {
   // DBGMSG("stats_rec=%p, rc=%d, tryct=%d", stats_rec, rc, tryct);
   // Try_Data * try_data = unopaque(stats_rec);
   // TODO: eliminate function calls
   if (rc == 0) {
      record_successful_tries2(retry_type, tryct);
   }
   // else if (tryct == stats_rec->max_tries) {
   // fragile, but eliminates testing for max_tries:
   else if (rc == DDCRC_RETRIES || rc == DDCRC_ALL_TRIES_ZERO) {
      record_failed_max_tries2(retry_type);
   }
   else {
      record_failed_fatally2(retry_type);
   }
}



// used to test whether there's anything to report
int try_data_get_total_attempts(Try_Data * stats_rec) {
   // Try_Data * try_data = unopaque(stats_rec);
   int total_attempts = 0;
   int ndx;
   for (ndx=0; ndx <= stats_rec->max_tries+1; ndx++) {
      total_attempts += stats_rec->counters[ndx];
   }
   return total_attempts;
}

// used to test whether there's anything to report
int try_data_get_total_attempts2(DDCA_Retry_Type retry_type) {
   // Try_Data * try_data = unopaque(stats_rec);
   Try_Data2 * stats_rec = try_data2[retry_type];
   int total_attempts = 0;
   int ndx;
   for (ndx=0; ndx <= MAX_MAX_TRIES+1; ndx++) {
      total_attempts += stats_rec->counters[ndx];
   }
   return total_attempts;
}



/** Reports a statistics record.
 *
 *  Output is written to the current FOUT destination.
 *
 *  \param stats_rec    opaque reference to stats record
 *  \param depth        logical indentation depth
 *
 *  \remark
 *  Why does this data structure need to be opaque?  (4/2017)
 */
void try_data_report(Try_Data * stats_rec, int depth) {
   // bool debug = true;
   int d1 = depth+1;
   // Try_Data * try_data = unopaque(stats_rec);
   rpt_nl();
   rpt_vstring(depth, "Retry statistics for %s", stats_rec->stat_name);

   // doesn't distinguish write vs read
   // rpt_vstring(depth, "Retry statistics for ddc %s exchange", ddc_retry_type_description(stats_rec->retry_type));
   if (try_data_get_total_attempts(stats_rec) == 0) {
      rpt_vstring(d1, "No tries attempted");
   }
   else {
      int total_successful_attempts = 0;
      int max1 = stats_rec->max_tries;

      Global_Maxtries_Accumulator acc =
             trd_get_all_threads_maxtries_range(stats_rec->retry_type);


      rpt_vstring(d1, "Max tries allowed: %d", max1);
      if (acc.min_lowest_maxtries == acc.max_highest_maxtries)
         rpt_vstring(d1, "Max tries allowed: %d", acc.min_lowest_maxtries);

      rpt_vstring(d1, "Max tries allowed range: %d..%d",
                      acc.min_lowest_maxtries, acc.max_highest_maxtries);

      int upper_bound = MAX_MAX_TRIES+1;
      while (upper_bound > 1) {
         // DBGMSG("upper_bound=%d", upper_bound);
         if (stats_rec->counters[upper_bound] != 0)
            break;
         upper_bound--;
      }
      // DBGMSG("Final upper bound: %d", upper_bound);
      // n upper_bound = 1 if no successful attempts
      char * s = (upper_bound == 1) ? " None" : "";
      rpt_vstring(d1, "Successful attempts by number of tries required:%s", s);
      if (upper_bound > 1) {
         for (int ndx=2; ndx <= upper_bound; ndx++) {
            total_successful_attempts += stats_rec->counters[ndx];
            // DBGMSG("ndx=%d", ndx);
            rpt_vstring(d1, "   %2d:  %3d", ndx-1, stats_rec->counters[ndx]);
         }
      }
      assert( ( (upper_bound == 1) && (total_successful_attempts == 0) ) ||
              ( (upper_bound > 1 ) && (total_successful_attempts >  0) )
            );
      rpt_vstring(d1, "Total successful attempts:        %3d", total_successful_attempts);
      rpt_vstring(d1, "Failed due to max tries exceeded: %3d", stats_rec->counters[1]);
      rpt_vstring(d1, "Failed due to fatal error:        %3d", stats_rec->counters[0]);
      rpt_vstring(d1, "Total attempts:                   %3d", try_data_get_total_attempts(stats_rec));
   }
}



/** Reports a statistics record.
 *
 *  Output is written to the current FOUT destination.
 *
 *  \param stats_rec    opaque reference to stats record
 *  \param depth        logical indentation depth
 *
 *  \remark
 *  Why does this data structure need to be opaque?  (4/2017)
 */
void try_data_report2(DDCA_Retry_Type retry_type, int depth) {
   // bool debug = true;
   int d1 = depth+1;
   // Try_Data * try_data = unopaque(stats_rec);
   rpt_nl();
   Try_Data2 * stats_rec = try_data2[retry_type];
   rpt_vstring(depth, "Retry statistics for %s", retry_type_description(retry_type));

   // doesn't distinguish write vs read
   // rpt_vstring(depth, "Retry statistics for ddc %s exchange", ddc_retry_type_description(stats_rec->retry_type));
   if (try_data_get_total_attempts2(retry_type) == 0) {
      rpt_vstring(d1, "No tries attempted");
   }
   else {
      int total_successful_attempts = 0;
      int max1 = stats_rec->maxtries;

      // TO REPLACE WITH LOCAL FUNCTION
      Global_Maxtries_Accumulator acc =
             trd_get_all_threads_maxtries_range(stats_rec->retry_type);


      rpt_vstring(d1, "Max tries allowed: %d", max1);
      if (acc.min_lowest_maxtries == acc.max_highest_maxtries)
         rpt_vstring(d1, "Max tries allowed: %d", acc.min_lowest_maxtries);

      rpt_vstring(d1, "Max tries allowed range: %d..%d",
                      acc.min_lowest_maxtries, acc.max_highest_maxtries);

      int upper_bound = MAX_MAX_TRIES+1;
      while (upper_bound > 1) {
         // DBGMSG("upper_bound=%d", upper_bound);
         if (stats_rec->counters[upper_bound] != 0)
            break;
         upper_bound--;
      }
      // DBGMSG("Final upper bound: %d", upper_bound);
      // n upper_bound = 1 if no successful attempts
      char * s = (upper_bound == 1) ? " None" : "";
      rpt_vstring(d1, "Successful attempts by number of tries required:%s", s);
      if (upper_bound > 1) {
         for (int ndx=2; ndx <= upper_bound; ndx++) {
            total_successful_attempts += stats_rec->counters[ndx];
            // DBGMSG("ndx=%d", ndx);
            rpt_vstring(d1, "   %2d:  %3d", ndx-1, stats_rec->counters[ndx]);
         }
      }
      assert( ( (upper_bound == 1) && (total_successful_attempts == 0) ) ||
              ( (upper_bound > 1 ) && (total_successful_attempts >  0) )
            );
      rpt_vstring(d1, "Total successful attempts:        %3d", total_successful_attempts);
      rpt_vstring(d1, "Failed due to max tries exceeded: %3d", stats_rec->counters[1]);
      rpt_vstring(d1, "Failed due to fatal error:        %3d", stats_rec->counters[0]);
      rpt_vstring(d1, "Total attempts:                   %3d", try_data_get_total_attempts2(retry_type));
   }
}

