/** @file ddc_try_stats.h
 *
 *  Maintains statistics on DDC retries.
 *
 *  These statistics are global, not broken out by thread.
 */

// Copyright (C) 2014-2020 Sanford Rockowitz <rockowitz@minsoft.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef TRY_STATS_H_
#define TRY_STATS_H_

#include "ddcutil_types.h"

#include "base/parms.h"

void     try_data_init();
bool     try_data_lock();
void     try_data_unlock(bool this_function_owns_lock);
uint16_t try_data_get_maxtries2(DDCA_Retry_Type retry_type);
void     try_data_set_maxtries2(DDCA_Retry_Type retry_type, uint16_t new_maxtries);
void     try_data_reset2(       DDCA_Retry_Type retry_type);
void     try_data_reset2_all();
void     try_data_record_tries2(DDCA_Retry_Type retry_type, DDCA_Status rc, int tryct);
void     try_data_report2(      DDCA_Retry_Type retry_type, int depth);

#endif /* TRY_STATS_H_ */
