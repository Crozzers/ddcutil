/* app_getvcp.c
 *
 * Created on: Jan 1, 2016
 *     Author: rock
 *
 * <copyright>
 * Copyright (C) 2014-2015 Sanford Rockowitz <rockowitz@minsoft.com>
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

#ifndef SRC_APP_DDCTOOL_APP_GETVCP_C_
#define SRC_APP_DDCTOOL_APP_GETVCP_C_

#include <errno.h>
#include <stdio.h>

#include "util/string_util.h"

#include "base/ddc_errno.h"
#include "base/msg_control.h"
#include "base/status_code_mgt.h"
// #include "base/linux_errno.h"
#include "base/msg_control.h"

#include "i2c/i2c_bus_core.h"

#include "ddc/ddc_packet_io.h"
#include "ddc/vcp_feature_codes.h"
#include "ddc/ddc_vcp_version.h"
#include "app_ddctool/app_getvcp.h"
#include "../ddc/ddc_output.h"


Global_Status_Code
app_show_single_vcp_value_by_dh(Display_Handle * dh, char * feature, bool force) {
   bool debug = false;
   DBGMSF(debug, "Starting. Getting feature %s for %s",
                 feature, display_handle_repr(dh) );

   Global_Status_Code         gsc = 0;
   Byte                       feature_id;
   VCP_Feature_Table_Entry *  entry = NULL;

   Version_Spec vspec = get_vcp_version_by_display_handle(dh);
   if ( hhs_to_byte_in_buf(feature, &feature_id) )
      entry = vcp_find_feature_by_hexid(feature_id);
   {
      if (!entry && force) {
         entry = vcp_create_dummy_feature_for_hexid(feature_id);
      }
      if (entry) {
         if (!is_feature_readable_by_vcp_version(entry, vspec)) {
            char * feature_name =  get_version_sensitive_feature_name(entry, vspec);
            Version_Feature_Flags vflags = get_version_sensitive_feature_flags(entry, vspec);
            if (vflags & VCP2_DEPRECATED)
               printf("Feature %02x (%s) is deprecated in MCCS %d.%d\n",
                      feature_id, feature_name, vspec.major, vspec.minor);
            else
               printf("Feature %02x (%s) is not readable\n", feature_id, feature_name);
            // gsc = modulate_rc(-EINVAL, RR_ERRNO);    // TEMP - what is appropriate?
            gsc = DDCL_INVALID_OPERATION;
         }
      }
   }

   if (!entry) {
      printf("Unrecognized VCP feature code: %s\n", feature);
      // gsc = modulate_rc(-EINVAL, RR_ERRNO);
      gsc = DDCL_UNKNOWN_FEATURE;
   }
   if (gsc == 0) {
      // DBGMSG("calling show_vcp_for_vcp_code_table_entry_by_display_ref()");
      // show_value_for_feature_table_entry_by_display_handle(dh, entry, NULL, false);
      // gsc = 0;    // until show_value_... has a status code

      char * formatted_value = NULL;
      gsc =
      get_formatted_value_for_feature_table_entry(
            dh,
            entry,
            false,      /* suppress_unsupported */
            true,       /* prefix_value_with_feature_code */
            &formatted_value,
            stdout);    /* msg_fh */
      if (formatted_value)
         printf("%s\n", formatted_value);
   }

   DBGMSF(debug, "Done.  Returning: %s", gsc_desc(gsc));
   return gsc;
}


Global_Status_Code
app_show_single_vcp_value_by_display_ref(Display_Ref * dref, char * feature, bool force) {
   Display_Handle * dh = ddc_open_display(dref, EXIT_IF_FAILURE);
   Global_Status_Code gsc = app_show_single_vcp_value_by_dh(dh, feature, force);
   ddc_close_display(dh);
   return gsc;
}


/* Shows the VCP values for all features in a VCP feature subset.
 *
 * Arguments:
 *    pdisp      display reference
 *    subset     feature subset
 *    collector  accumulates output
 *
 * Returns:
 *    nothing
 */
void app_show_vcp_subset_values_by_display_ref(Display_Ref * dref, VCP_Feature_Subset subset, bool show_unsupported)  {
   // DBGMSG("Starting.  subset=%d   ", subset );
   // need to ensure that bus info initialized
   bool validDisp = true;
   if (dref->ddc_io_mode == DDC_IO_DEVI2C) {
      // Is this needed?  or checked by openDisplay?
      Bus_Info * bus_info = i2c_get_bus_info(dref->busno);
      if (!bus_info ||  !(bus_info->flags & I2C_BUS_ADDR_0X37) ) {
         printf("Address 0x37 not detected on bus %d. I2C communication not available.\n", dref->busno );
         validDisp = false;
      }
   }
   else {
      validDisp = true;    // already checked
   }

   if (validDisp) {
      GPtrArray * collector = NULL;
      Display_Handle * pDispHandle = ddc_open_display(dref, EXIT_IF_FAILURE);
      show_vcp_values_by_display_handle(pDispHandle, subset, collector, show_unsupported);
      ddc_close_display(pDispHandle);
   }
}



#endif /* SRC_APP_DDCTOOL_APP_GETVCP_C_ */
