/* adl_shim.h
 *
 * <copyright>
 * Copyright (C) 2014-2017 Sanford Rockowitz <rockowitz@minsoft.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * </endcopyright>
 */

/** \file
 *  Interface to ADL services
 *
 *  Will be implemented by either adl/adl_impl/adl_shim.c or adl/adl_mock_impl/adl_mock_shim.c
 */

#ifndef ADL_SHIM_H_
#define ADL_SHIM_H_

/** \cond */
#include <glib.h>
#include <stdlib.h>     // wchar_t, needed by adl_structures.h
#include <stdbool.h>
/** \endcond */

#include "util/edid.h"

#include "base/core.h"
#include "base/displays.h"
#include "base/execution_stats.h"
#include "base/status_code_mgt.h"


// Initialization
extern bool            adl_debug;
bool adlshim_is_available();

// must be called before any other function (except is_adl_available()):
bool adlshim_initialize();


// Report on active displays

Parsed_Edid*
adlshim_get_parsed_edid_by_adlno(
      int iAdapterIndex,
      int iDisplayIndex);

Parsed_Edid*
adlshim_get_parsed_edid_by_display_handle(
      Display_Handle * dh);

Parsed_Edid*
adlshim_get_parsed_edid_by_display_ref(
      Display_Ref * dref);

// void adl_show_active_display(ADL_Display_Rec * pdisp, int depth);
// void adl_show_active_display_by_index(int ndx, int depth);
// void adlshim_show_active_display_by_adlno(int iAdapterIndex, int iDisplayIndex, int depth);

void
adlshim_report_active_display_by_display_ref(
      Display_Ref * dref,
      int           depth);

// int  adl_show_active_displays();   // returns number of active displays

// void report_adl_display_rec(ADL_Display_Rec * pRec, bool verbose, int depth);


// Find and validate display

// bool              adlshim_is_valid_adlno(int iAdapterIndex, int iDisplayIndex, bool emit_error_msg);

bool
adlshim_is_valid_display_ref(
      Display_Ref * dref,
      bool          emit_error_msg);

// ADL_Display_Rec * adl_get_display_by_adlno(int iAdapterIndex, int iDisplayIndex, bool emit_error_msg);

Display_Ref *
adlshim_find_display_by_mfg_model_sn(
      const char * mfg_id,
      const char * model,
      const char * sn);


Adlno
adlshim_find_adlno_by_mfg_model_sn(
      const char * mfg_id,
      const char * model,
      const char * sn);


Display_Ref *
adlshim_find_display_by_edid(
      const Byte * pEdidBytes);

#ifdef OLD
Display_Info_List
adlshim_get_valid_displays();
#endif

Modulated_Status_ADL
adlshim_get_video_card_info(
      Display_Handle * dh,
      Video_Card_Info * card_info);


// Read from and write to the display

Modulated_Status_ADL adlshim_ddc_write_only(
      Display_Handle* dh,
      Byte *  pSendMsgBuf,
      int     sendMsgLen);

Modulated_Status_ADL adlshim_ddc_read_only(
      Display_Handle * dh,
      Byte *  pRcvMsgBuf,
      int *   pRcvBytect);

//Base_Status_ADL adl_ddc_write_read(
//      int     iAdapterIndex,
//      int     iDisplayIndex,
//      Byte *  pSendMsgBuf,
//      int     sendMsgLen,
//      Byte *  pRcvMsgBuf,
//      int *   pRcvBytect);

//Base_Status_ADL adl_ddc_write_read_onecall(
//      int     iAdapterIndex,
//      int     iDisplayIndex,
//      Byte *  pSendMsgBuf,
//      int     sendMsgLen,
//      Byte *  pRcvMsgBuf,
//      int *   pRcvBytect);


// new

#define ADL_DISPLAY_DETAIL_MARKER "ADTD"
typedef
struct {
   char                  marker[4];
   int                   iAdapterIndex;
   int                   iDisplayIndex;
   bool                  supports_ddc;
   char *                xrandr_name;
   Parsed_Edid *         pEdid;
} ADL_Display_Detail;

void report_adl_display_detail(ADL_Display_Detail * detail, int depth);




int adlshim_get_valid_display_ct();

GPtrArray * adlshim_get_valid_display_details();

#endif /* ADL_SHIM_H_ */
