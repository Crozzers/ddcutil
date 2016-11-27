/* clmain.c
 *
 * <copyright>
 * Copyright (C) 2014-2016 Sanford Rockowitz <rockowitz@minsoft.com>
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

#include <assert.h>
#include <stdio.h>

#include "base/core.h"

#include "ddc/ddc_dumpload.h"     // loadvcp.h should be elsewhere, should not be including in main

#include "libmain/ddcutil_c_api.h"


#define FUNCTION_ERRMSG(function_name,status_code) \
   printf("(%s) %s() returned %d (%s): %s\n",      \
          __func__, function_name, status_code,    \
          ddca_status_code_name(status_code),      \
          ddca_status_code_desc(status_code))


char * interpret_feature_flags_readwrite(unsigned long feature_flags) {
   char * result = NULL;
   if (feature_flags & DDCA_RW)
      result = "read write";
   else if (feature_flags & DDCA_RO)
      result = "read only";
   else if (feature_flags & DDCA_WO)
      result = "write only";
   else
      result = "unknown readwritability";
   return result;
}

char * interpret_feature_flags_type(unsigned long feature_flags) {
   char * result = NULL;
   if (feature_flags & DDCA_CONTINUOUS)
      result = "continuous";
   else if (feature_flags & DDCA_TABLE)
      result = "table";
   else if (feature_flags & DDCA_SIMPLE_NC)
      result = "simple non-continuous";
   else if (feature_flags & DDCA_COMPLEX_NC)
      result = "complex non-continuous";
   else
      result = "unknown type";
   return result;
}

void report_feature_flags(Byte feature_code, unsigned long feature_flags) {
   if ( !(feature_flags & DDCA_KNOWN) )
      printf("Feature: %02x: unknown\n", feature_code);
   else
      printf("Feature: %02x: %s, %s\n",
            feature_code,
             interpret_feature_flags_readwrite(feature_flags),
             interpret_feature_flags_type(feature_flags)
            );
}


void test_get_single_feature_info(DDCA_Display_Handle dh, Byte feature_code) {
   printf("Getting metadata for feature 0x%02x\n", feature_code);
   printf("Feature name: %s\n", ddca_get_feature_name(feature_code));
   unsigned long feature_flags;
     DDCA_Status rc = ddca_get_feature_info_by_display(
             dh,    // needed because in rare cases feature info is MCCS version dependent
             feature_code,
             &feature_flags);
     if (rc != 0)
        FUNCTION_ERRMSG("ddct_get_feature_info", rc);
     else {
        report_feature_flags(feature_code, feature_flags);
     }
}

void test_get_feature_info(DDCA_Display_Handle dh) {
   Byte feature_codes[] = {0x02, 0x03, 0x10, 0x43, 0x60};
   int feature_code_ct = sizeof(feature_codes)/sizeof(Byte);
   int ndx = 0;
   for (; ndx < feature_code_ct; ndx++)
      test_get_single_feature_info(dh, feature_codes[ndx]);
}


bool test_cont_value(DDCA_Display_Handle dh, Byte feature_code) {

   DDCA_Status rc;
   bool ok = true;
   char * feature_name = ddca_get_feature_name(feature_code);

   unsigned long feature_flags;
   rc = ddca_get_feature_info_by_display(
           dh,    // needed because in rare cases feature info is MCCS version dependent
           feature_code,
           &feature_flags);
   if (rc != 0)
      FUNCTION_ERRMSG("ddct_get_feature_info", rc);
   else {
      report_feature_flags(feature_code, feature_flags);
   }

   DDCT_Non_Table_Value_Response non_table_response;
   rc = ddct_get_nontable_vcp_value(dh, feature_code, &non_table_response);

   if (rc != 0)
      FUNCTION_ERRMSG("ddct_set_continuous_vcp_value", rc);
   else {
      printf("(%s) Feature 0x%02x (%s) current value = %d, max value = %d\n",
             __func__, feature_code, feature_name,
             non_table_response.cur_value,
             non_table_response.max_value);
      int cur_value = non_table_response.cur_value;
      int new_value = cur_value/2;
      rc = ddct_set_continuous_vcp_value(dh, feature_code, new_value);
      if (rc != 0) {
         FUNCTION_ERRMSG("ddct_set_continuous_vcp_value", rc);
      }
      else {
         printf("(%s) Setting new value succeeded.\n", __func__);
         rc = ddct_get_nontable_vcp_value(dh, feature_code, &non_table_response);
         if (rc != 0) {
            FUNCTION_ERRMSG("ddct_get_continuous_vcp_value", rc);
         }
         else {
            if ( non_table_response.cur_value != new_value) {
               printf("(%s) Current value %d does not match new value set %d", __func__, non_table_response.cur_value, new_value);
               ok = false;
            }
         }
         // reset original value
         rc = ddct_set_continuous_vcp_value(dh, feature_code, cur_value);
         if (rc != 0) {
            FUNCTION_ERRMSG("ddct_set_continuous_vcp_value", rc);
         }
         else {
            printf("(%s) Resetting original value succeeded.\n", __func__);
            rc = ddct_get_nontable_vcp_value(dh, feature_code, &non_table_response);
            if (rc != 0) {
               FUNCTION_ERRMSG("ddct_get_continuous_vcp_value", rc);
            }
            else {
               rc = ddct_get_nontable_vcp_value(dh, feature_code, &non_table_response);
               if (rc != 0) {
                  FUNCTION_ERRMSG("ddct_get_continuous_vcp_value", rc);
               }
               else {
                  if ( non_table_response.cur_value != cur_value) {
                     printf("(%s) Current value %d does not match original current value %d", __func__,
                            non_table_response.cur_value, cur_value);
                     ok = false;
                  }
               }
            }

        }

      }
   }
   return ( (rc == 0) && ok);
}

bool test_get_capabilities_string(DDCA_Display_Handle dh) {
   char * capabilities = NULL;
   DDCA_Status rc =  ddct_get_capabilities_string(dh, &capabilities);
   if (rc != 0)
      FUNCTION_ERRMSG("ddct_get_capabilities_string", rc);
   else
      printf("(%s) Capabilities: %s\n", __func__, capabilities);
   printf("(%s) Second call should be fast\n", __func__);
   rc =  ddct_get_capabilities_string(dh, &capabilities);
   if (rc != 0)
      FUNCTION_ERRMSG("ddct_get_capabilities_string", rc);
   else
      printf("(%s) Capabilities: %s\n", __func__, capabilities);
   return false;
}



int main(int argc, char** argv) {
   printf("(%s) Starting.\n", __func__);

   DDCA_Status rc;
   DDCA_Display_Identifier did;
   DDCA_Display_Ref dref;
   DDCA_Display_Handle dh = NULL;  // initialize to avoid clang analyzer warning


   ddca_init();

   printf("(%s) Built with ADL support: %s\n", __func__, (ddca_built_with_adl()) ? "yes" : "no");

   rc = ddca_set_max_tries(DDCA_WRITE_READ_TRIES, 16);
   printf("(%s) ddct_set_max_tries(..,16) returned: %d\n", __func__, rc);
   rc = ddca_set_max_tries(DDCA_WRITE_READ_TRIES, 15);
   if (rc != 0)
      FUNCTION_ERRMSG("DDCT_WRITE_READ_TRIES:ddct_set_max_tries", rc);
   rc = ddca_set_max_tries(DDCA_MULTI_PART_TRIES, 15);
   if (rc != 0)
      FUNCTION_ERRMSG("DDCT_MULTI_PART_TRIES:ddct_set_max_tries", rc);


   printf("(%s) max write only tries: %d\n", __func__, ddca_get_max_tries(DDCA_WRITE_ONLY_TRIES));
   printf("(%s) max write read tries: %d\n", __func__, ddca_get_max_tries(DDCA_WRITE_READ_TRIES));
   printf("(%s) max multi part tries: %d\n", __func__, ddca_get_max_tries(DDCA_MULTI_PART_TRIES));


   rc = ddca_create_dispno_display_identifier(2, &did);
   assert(rc == 0);
   char * did_repr = NULL;
   rc = ddca_repr_display_identifier(did, &did_repr);
   assert(rc == 0);
   printf("(%s) did=%s\n", __func__, did_repr);

   rc = ddca_create_display_ref(did, &dref);

   if (rc != 0) {
      printf("(%s) ddct_get_display_ref() returned %d (%s): %s\n",
             __func__, rc, ddca_status_code_name(rc), ddca_status_code_desc(rc));
   }
   else {
      char * dref_repr;
      rc = ddca_repr_display_ref(dref, &dref_repr);
      assert(rc == 0);
      printf("(%s) dref=%s\n", __func__, dref_repr);

      rc = ddca_open_display(dref, &dh);
      if (rc != 0) {
         FUNCTION_ERRMSG("ddct_open_display", rc);
      }
      else {
         char * dh_repr;
         /* rc = */  ddca_repr_display_handle(dh, &dh_repr);
         printf("(%s) display handle: %s\n", __func__, dh_repr);

         DDCA_MCCS_Version_Spec vspec;
         rc = ddct_get_mccs_version(dh, &vspec);
         if (rc != 0) {
            FUNCTION_ERRMSG("ddct_get_mccs_version_spec", rc);
         }
         else {
            printf("(%s) VCP version: %d.%d\n", __func__, vspec.major, vspec.minor);
         }

         test_get_feature_info(dh);
         test_cont_value(dh, 0x10);
         test_get_capabilities_string(dh);

      }
   }

   if (dh) {
      char * profile_values_string;
      DBGMSG("&profile_values_string=%p", &profile_values_string);
      rc = ddct_get_profile_related_values(dh, &profile_values_string);
      if (rc != 0) {
         FUNCTION_ERRMSG("ddct_get_profile_related_values", rc);
      }
      else {
         printf("(%s) profile values string = %s\n", __func__, profile_values_string);
      }

      printf("(%s) Calling ddct_set_profile_related_values()\n", __func__);
      rc = ddct_set_profile_related_values( profile_values_string);
      if (rc != 0) {
         FUNCTION_ERRMSG("ddct_set_profile_related_values", rc);
      }


   }


   if (dh) {
      rc = ddca_close_display(dh);
      if (rc != 0)
         FUNCTION_ERRMSG("ddct_close_display", rc);
   }
   if (dref) {
      rc = ddca_free_display_ref(dref);
      printf("(%s) ddct_free_display_ref() returned %d\n", __func__, rc);
   }
   if (did) {
      rc = ddca_free_display_identifier(did);
      printf("(%s) ddct_free_display_identifier() returned %d\n", __func__, rc);
   }
   return 0;
}
