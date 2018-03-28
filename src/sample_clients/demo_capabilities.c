/* demo_capabilities.c
 *
 * Query capabilities string
 *
 * <copyright>
 * Copyright (C) 2014-2018 Sanford Rockowitz <rockowitz@minsoft.com>
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

#define _GNU_SOURCE        // for asprintf()

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "public/ddcutil_c_api.h"


#define DDC_ERRMSG(function_name,status_code) \
   printf("(%s) %s() returned %d (%s): %s\n",      \
          __func__, function_name, status_code,    \
          ddca_rc_name(status_code),      \
          ddca_rc_desc(status_code))


/* A simple function that opens the first detected display using
 * DDCA_create_display_ref() to locate the display.
 *
 * For more detailed examples of display detection and management,
 * see demo_display_selection.c
 *
 * Arguments:    none
 * Returns:      display handle of first detected display,
 *               NULL if not found or can't be opened
 */
DDCA_Display_Handle * open_first_display_by_dispno() {
   printf("Opening display 1...\n");
   DDCA_Display_Identifier did;
   DDCA_Display_Ref        dref;
   DDCA_Display_Handle     dh = NULL;

   ddca_create_dispno_display_identifier(1, &did);     // always succeeds
   DDCA_Status rc = ddca_create_display_ref(did, &dref);
   if (rc != 0) {
      DDC_ERRMSG("ddca_create_display_ref", rc);
   }
   else {
      rc = ddca_open_display(dref, &dh);
      if (rc != 0) {
         DDC_ERRMSG("ddca_open_display", rc);
      }
      else {
         printf("Opened display handle: %s\n", ddca_dh_repr(dh));
      }
   }
   return dh;
}


/* This is a simplified version of API function ddca_report_parsed_capabilities(),
 * illustrating use of DDCA_Capabilities.
 */
void
simple_report_parsed_capabilities(DDCA_Capabilities * pcaps)
{
   assert(pcaps && memcmp(pcaps->marker, DDCA_CAPABILITIES_MARKER, 4) == 0);
   printf("Unparsed capabilities string: %s\n", pcaps->unparsed_string);
   printf("VCP version:     %d.%d\n", pcaps->version_spec.major, pcaps->version_spec.minor);
   printf("Command codes:\n");
   for (int cmd_ndx = 0; cmd_ndx < pcaps->cmd_ct; cmd_ndx++) {
      uint8_t cur_code = pcaps->cmd_codes[cmd_ndx];
      printf("   0x%02x\n", cur_code);
   }
   printf("VCP Feature codes:\n");
   for (int code_ndx = 0; code_ndx < pcaps->vcp_code_ct; code_ndx++) {
      DDCA_Cap_Vcp * cur_vcp = &pcaps->vcp_codes[code_ndx];
      assert( memcmp(cur_vcp->marker, DDCA_CAP_VCP_MARKER, 4) == 0);
      char * feature_name = ddca_get_feature_name(cur_vcp->feature_code);
      printf("   Feature:  0x%02x (%s)\n", cur_vcp->feature_code, feature_name);
      DDCA_Feature_Value_Entry * feature_value_table;
      DDCA_Status ddcrc = ddca_get_simple_sl_value_table_by_vspec(
            cur_vcp->feature_code,
            pcaps->version_spec,
            &feature_value_table);

      if (cur_vcp->value_ct > 0) {
         printf("      Values:\n");
         for (int ndx = 0; ndx < cur_vcp->value_ct; ndx++) {
            char * value_desc = "No lookup table";
            if (ddcrc == 0) {
               value_desc = "Unrecognized feature value";
               ddca_get_simple_nc_feature_value_name_by_table(
                          feature_value_table,
                          cur_vcp->values[ndx],
                          &value_desc);
            }
            printf("         0x%02x: %s\n", cur_vcp->values[ndx], value_desc);
         }
      }
   }
}


/* Retrieves and reports the capabilities string for the first detected monitor.
 */
void demo_get_capabilities() {
   DDCA_Display_Handle dh = open_first_display_by_dispno();
   if (!dh)
      goto bye;

   char * capabilities = NULL;
   printf("Calling ddca_get_capabilities_string...\n");
   DDCA_Status rc =  ddca_get_capabilities_string(dh, &capabilities);
   if (rc != 0)
      DDC_ERRMSG("ddca_get_capabilities_string", rc);
   else
      printf("Capabilities: %s\n", capabilities);
   printf("Second call to ddca_get_capabilities() should be fast since value cached...\n");
   rc =  ddca_get_capabilities_string(dh, &capabilities);
   if (rc != 0)
      DDC_ERRMSG("ddca_get_capabilities_string", rc);
   else {
      printf("Capabilities: %s\n", capabilities);
      printf("Parse the string...\n");
        DDCA_Capabilities * pcaps = NULL;
      rc = ddca_parse_capabilities_string(
             capabilities,
             &pcaps);
      if (rc != 0)
         DDC_ERRMSG("ddca_parse_capabilities_string", rc);
      else {
         printf("Parsing succeeded.\n");
         printf("\nReport the result using local function simple_report_parsed_capabilities()...\n");
         simple_report_parsed_capabilities(pcaps);

         printf("\nReport the result using API function ddca_report_parsed_capabilities()...\n");
         DDCA_Output_Level saved_ol = ddca_set_output_level(DDCA_OL_VERBOSE);
         ddca_report_parsed_capabilities(pcaps, 0);
         ddca_free_parsed_capabilities(pcaps);

         printf("\nUse \"ddcutil capabilities\" code to display capabilities...\n");
         ddca_set_output_level(DDCA_OL_VERBOSE);  // show both unparsed and parsed capabilities
         ddca_parse_and_report_capabilities(capabilities, 1);
         ddca_set_output_level(saved_ol);
      }
   }

bye:
   return;
}


int main(int argc, char** argv) {
   demo_get_capabilities();
   return 0;
}
