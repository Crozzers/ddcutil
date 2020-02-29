/** @file tuned_sleep.c
 *
 *  Perform sleep. The sleep time is determined by io mode, sleep event time,
 *  and applicable multipliers.
 */

// Copyright (C) 2019-2020 Sanford Rockowitz <rockowitz@minsoft.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include <assert.h>
#include <sys/types.h>

// for syscall
#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>

#include "util/debug_util.h"
#include "util/report_util.h"

#include "base/parms.h"
#include "base/dynamic_sleep.h"
#include "base/execution_stats.h"
#include "base/sleep.h"
#include "base/thread_sleep_data.h"

// Experimental suppression of sleeps after reads
static bool sleep_suppression_enabled = false;

void enable_sleep_suppression(bool enable) {
   sleep_suppression_enabled = enable;
}

/* Two multipliers are applied to the sleep time determined from the
 * io mode and event type.
 *
 * sleep_multiplier_factor: set globally, e.g. from arg passed on
 * command line.  Consider making thread specific.
 *
 * sleep_multiplier_ct: Per thread adjustment,initiated by io retries.
 */

// Defaults for new threads.  Default sleep multiplier factor can be adjusted,
// Default sleep multiplier count cannot.
static       double default_sleep_multiplier_factor = 1.0;
static const int    default_sleep_multiplier_count  = 1;


typedef struct {
   int    sleep_multiplier_ct;   // thread specific since can be changed dynamically
   double sleep_multiplier_factor;
} Thread_Sleep_Settings;


static Thread_Sleep_Settings *  get_thread_sleep_settings() {
   static GPrivate per_thread_key = G_PRIVATE_INIT(g_free);

   Thread_Sleep_Settings *settings = g_private_get(&per_thread_key);

   // GThread * this_thread = g_thread_self();
   // printf("(%s) this_thread=%p, settings=%p\n", __func__, this_thread, settings);

   if (!settings) {
      settings = g_new0(Thread_Sleep_Settings, 1);
      settings->sleep_multiplier_ct = default_sleep_multiplier_count;
      settings->sleep_multiplier_factor = default_sleep_multiplier_factor;
      g_private_set(&per_thread_key, settings);
   }

   // printf("(%s) Returning: %p\n", __func__, settings);
   return settings;
}

#ifdef NOT_NEEDED
void lock_default_sleep_multiplier_factor() {
}

void unlock_default_sleep_multiplier_factor() {
}
#endif


/** Sets the default sleep multiplier factor, used for the creation of any new threads.
 * This is a global value and is a floating point number.
 *
 *  \param multiplier
 *
 *  \remark Intended for use only during program initialization.  If used
 *          more generally, get and set of default sleep multiplier needs to
 *          be protected by a lock.
 *  \todo
 *  Add Sleep_Event_Type bitfield to make sleep factor dependent on event type?
 */
void set_default_sleep_multiplier_factor(double multiplier) {
   assert(multiplier > 0 && multiplier < 100);
   default_sleep_multiplier_factor = multiplier;
   // DBGMSG("Setting sleep_multiplier_factor = %6.1f",set_sleep_multiplier_ct sleep_multiplier_factor);
}

/** Gets the default sleep multiplier factor.
 *
 *  \return sleep multiplier factor
 */
double get_default_sleep_multiplier_factor() {
   return default_sleep_multiplier_factor;
}


/** Sets the sleep multiplier factor for the current thread.
 * This is a floating point number.
 *
  *  \param multiplier
  *
  *  \todo
  *  Add Sleep_Event_Type bitfield to make sleep factor dependent on event type?
  */
 void set_sleep_multiplier_factor(double multiplier) {
    assert(multiplier > 0 && multiplier < 100);
    Thread_Sleep_Settings * settings = get_thread_sleep_settings();
    settings->sleep_multiplier_factor = multiplier;
    // DBGMSG("Setting sleep_multiplier_factor = %6.1f",set_sleep_multiplier_ct default_sleep_multiplier_factor);
 }


 /** Gets the sleep multiplier factor for the current thread.
  *
  *  \return sleep multiplier factor
  */
double get_sleep_multiplier_factor() {
   Thread_Sleep_Settings * settings = get_thread_sleep_settings();
   return settings->sleep_multiplier_factor;
}


/** Gets the multiplier count for the current thread.
 *
 *  \return multiplier count
 */
int get_sleep_multiplier_ct() {
   Thread_Sleep_Settings * settings = get_thread_sleep_settings();
   return settings->sleep_multiplier_ct;
}


/** Sets the multiplier count for the current thread.
 *
 *  \parsm multipler_ct  value to set
 */
void   set_sleep_multiplier_ct(/* Sleep_Event_Type event_types,*/ int multiplier_ct) {
   assert(multiplier_ct > 0 && multiplier_ct < 100);
  Thread_Sleep_Settings * settings = get_thread_sleep_settings();
   settings->sleep_multiplier_ct = multiplier_ct;
   // DBGMSG("Setting sleep_multiplier_ct = %d", settings->sleep_multiplier_ct);
}


//
// Perform sleep
//

/** Sleep for the period of time required by the DDC protocol, as indicated
 *  by the io mode and sleep event type.
 *
 *  The time is further adjusted by the sleep factor and sleep multiplier
 *  currently in effect.
 *
 *  \todo
 *  Take into account the time since the last monitor return in the
 *  current thread.
 *  \tod
 *  Take into account per-display error statistics.  Would require
 *  error statistics be maintained on a per-display basis, either
 *  in the display reference or display handle.
 *
 * \param io_mode     communication mechanism
 * \param event_type  reason for sleep
 * \param func        name of function that invoked sleep
 * \param lineno      line number in file where sleep was invoked
 * \param filename    name of file from which sleep was invoked
 * \param msg         text to append to trace message
 */
void tuned_sleep_with_tracex(
      DDCA_IO_Mode     io_mode,
      Sleep_Event_Type event_type,
      int              special_sleep_time_millis,
      const char *     func,
      int              lineno,
      const char *     filename,
      const char *     msg)
{
   bool debug = false;
   // DBGMSF(debug, "Starting. Sleep event type = %s", sleep_event_name(event_type));
   assert( (event_type != SE_SPECIAL && special_sleep_time_millis == 0) ||
           (event_type == SE_SPECIAL && special_sleep_time_millis >  0) );

   int sleep_time_millis = 0;    // should be a default

   if (event_type == SE_SPECIAL)
      sleep_time_millis = special_sleep_time_millis;
   else {

      switch(io_mode) {

      case DDCA_IO_I2C:
         switch(event_type) {
         case (SE_WRITE_TO_READ):
               sleep_time_millis = DDC_TIMEOUT_MILLIS_DEFAULT;
               break;
         case (SE_POST_WRITE):
               sleep_time_millis = DDC_TIMEOUT_MILLIS_DEFAULT;
               break;
         case (SE_POST_OPEN):
               sleep_time_millis = DDC_TIMEOUT_MILLIS_DEFAULT;
               break;
         case (SE_POST_READ):
               sleep_time_millis = DDC_TIMEOUT_MILLIS_DEFAULT;
               if (sleep_suppression_enabled) {
                  DBGMSF(debug, "Suppressing sleep, sleep event type = %s", sleep_event_name(event_type));
                  return;  // TEMP
               }
               break;
         case (SE_POST_SAVE_SETTINGS):
               sleep_time_millis = DDC_TIMEOUT_POST_SAVE_SETTINGS;   // per DDC spec
               break;
         case SE_DDC_NULL:
              sleep_time_millis = DDC_TIMEOUT_MILLIS_NULL_RESPONSE_INCREMENT;
              break;
         case SE_PRE_EDID:
              sleep_time_millis = DDC_TIMEOUT_MILLIS_DEFAULT;
              if (sleep_suppression_enabled) {
              DBGMSF(debug, "Suppressing sleep, sleep event type = %s",
                            sleep_event_name(event_type));
              return;   // TEMP
              }
              break;
         case SE_OTHER:
              sleep_time_millis = DDC_TIMEOUT_MILLIS_DEFAULT;
              // if (sleep_suppression_enabled) {
              // DBGMSF(debug, "Suppressing sleep, sleep event type = %s",
              //               sleep_event_name(event_type));
              // return;
              // }
              break;
         case SE_PRE_MULTI_PART_READ:        // before reading capabilitis
            sleep_time_millis = 200;
            break;
         case SE_MULTI_PART_READ_TO_WRITE:
            sleep_time_millis = DDC_TIMEOUT_MILLIS_DEFAULT;
            break;
         default:
              sleep_time_millis = DDC_TIMEOUT_MILLIS_DEFAULT;
         }  // switch within DDC_IO_DEVI2C
         break;

      case DDCA_IO_ADL:
         switch(event_type) {
         case (SE_WRITE_TO_READ):
               sleep_time_millis = DDC_TIMEOUT_MILLIS_DEFAULT;
               break;
         case (SE_POST_WRITE):
               sleep_time_millis = DDC_TIMEOUT_MILLIS_DEFAULT;
               break;
         case (SE_POST_OPEN):
               sleep_time_millis = DDC_TIMEOUT_MILLIS_DEFAULT;
               break;
         case (SE_POST_SAVE_SETTINGS):
               sleep_time_millis = 200;   // per DDC spec
               break;
         default:
            sleep_time_millis = DDC_TIMEOUT_MILLIS_DEFAULT;
         }
         break;

      case DDCA_IO_USB:
         PROGRAM_LOGIC_ERROR("call_tuned_sleep() called for USB_IO\n");
         break;
      }
   }

   // TODO:
   //   get error rate (total calls, total errors), current adjustment value
   //   adjust by time since last i2c event


   double sleep_adjustment_factor = dsa_get_sleep_adjustment();

   // DBGMSG("Calling tsd_get_sleep_multiplier_factor()");
   double sleep_multiplier_factor = tsd_get_sleep_multiplier_factor();
   // DBGMSG("sleep_multiplier_factor = %5.2f", sleep_multiplier_factor);
   // crude, should be sensitive to event type?
   int sleep_multiplier_ct = tsd_get_sleep_multiplier_ct();  // per thread
   sleep_time_millis = sleep_multiplier_ct * sleep_multiplier_factor *
                       sleep_time_millis * sleep_adjustment_factor;
   if (debug) {
      DBGMSG("Before sleep. event type: %s, sleep_multiplier_ct = %d,"
             " sleep_multiplier_factor = %9.1f, sleep_adjustment_factor = %9.1f,"
             " sleep_time_millis = %d",
             sleep_event_name(event_type), sleep_multiplier_ct,
             sleep_multiplier_factor, sleep_adjustment_factor, sleep_time_millis);
   }

   record_sleep_event(event_type);

   char msg_buf[100];
   const char * evname = sleep_event_name(event_type);
   if (msg)
      g_snprintf(msg_buf, 100, "Event type: %s, %s", evname, msg);
   else
      g_snprintf(msg_buf, 100, "Event_type: %s", evname);

   sleep_millis_with_tracex(sleep_time_millis, func, lineno, filename, msg_buf);

   DBGMSF(debug, "Done");
}

