/* loadvcp.c
 *
 * Created on: Aug 16, 2014
 *     Author: rock
 *
 * Load/store VCP settings from/to file.
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



#include "mccs_dumpload.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <linux/limits.h>    // PATH_MAX, NAME_MAX
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "util/file_util.h"
#include "util/glib_util.h"

#include "base/ddc_errno.h"
#include "base/common.h"
#include "base/displays.h"
#include "base/ddc_packets.h"
#include "util/report_util.h"
#include "base/util.h"
#include "base/vcp_feature_values.h"

#include "i2c/i2c_bus_core.h"

#include "ddc/ddc_output.h"
#include "ddc/ddc_edid.h"
#include "ddc/ddc_packet_io.h"
#include "ddc/ddc_vcp.h"
#include "ddc/ddc_read_capabilities.h"
#include "ddc/ddc_displays.h"

#include "ddc/mccs_dumpload.h"


void report_dumpload_data(Dumpload_Data * data, int depth) {
   int d1 = depth+1;
   rpt_structure_loc("Dumpload_Data", data, depth);
   // rptIval("busno", NULL, data->busno, d1);
   // TODO: show abbreviated edidstr
   rpt_str( "mfg_id",       NULL, data->mfg_id,       d1);
   rpt_str( "model",        NULL, data->model,        d1);
   rpt_str( "serial_ascii", NULL, data->serial_ascii, d1);
   rpt_str( "edid",         NULL, data->edidstr,      d1);
   rpt_int( "vcp_value_ct", NULL, data->vcp_value_ct, d1);
   int ndx;
   for (ndx=0; ndx < data->vcp_value_ct; ndx++) {
      char buf[100];
      Single_Vcp_Value * curval = &data->vcp_value[ndx];
      snprintf(buf, 100, "0x%02x -> %d", curval->opcode, curval->value);
      rpt_str("VCP value", NULL, buf, d1);
   }
   rpt_structure_loc("vcp_values", data->vcp_values, d1);
   if (data->vcp_values)
      report_vcp_value_set(data->vcp_values, d1);
}



#ifdef USING_ITERATOR
typedef void   (*Func_Iter_Init)(void* object);
typedef char * (*Func_Next_Line)();
typedef bool   (*Func_Has_Next)();
typedef struct {
           Func_Iter_Init  func_init;
           Func_Next_Line  func_next;
           Func_Has_Next   func_has_next;
} Line_Iterator;

GPtrArray * iter_garray = NULL;
int         iter_garray_pos = 0;

void   iter_garray_init(void * pobj) {
   bool debug = true;
   GPtrArray * garray = (GPtrArray*) pobj;
   if (debug)
      DBGMSG("garray=%p", garray);
   iter_garray = garray;
   iter_garray_pos = 0;
}

char * iter_garray_next_line() {
   bool debug = true;
   if (debug)
      DBGMSG("Starting");

   char * result = g_ptr_array_index(iter_garray, iter_garray_pos++);

   if (debug) {
      DBGMSG("Returning %p", result);
      DBGMSG("Returning |%s|", result);
   }
   return result;
}

bool   iter_garray_has_next() {
   bool debug = true;
   if (debug)
      DBGMSG("Starting");

   bool result = (iter_garray_pos < iter_garray->len);

   return result;
}

Line_Iterator g_ptr_iter = {
        iter_garray_init,
        iter_garray_next_line,
        iter_garray_has_next
    };

Null_Terminated_String_Array  iter_ntsa     = NULL;
int                            iter_ntsa_pos = 0;
int                            iter_ntsa_len = 0;

void iter_ntsa_init(void* pobj) {
   bool debug = true;
   Null_Terminated_String_Array  ntsa = (Null_Terminated_String_Array) pobj;
   if (debug)
      DBGMSG("ntsa=%p", ntsa);
   iter_ntsa = ntsa;
   iter_ntsa_pos = 0;
   iter_ntsa_len = null_terminated_string_array_length(ntsa);
}

char * iter_ntsa_next_line() {
   bool debug = true;
   if (debug)
      DBGMSG("Starting");

   char * result = iter_ntsa[iter_ntsa_pos++];
   if (debug) {
      DBGMSG("Returning %p", result);
      DBGMSG("Returning |%s|", result);
   }
   return result;
}

bool   iter_ntsa_has_next() {
   bool debug = true;
   if (debug)
      DBGMSG("Starting");

   bool result = (iter_ntsa_pos < iter_ntsa_len);

   if (debug)
      DBGMSG("Returning %d", result);
   return result;
}

Line_Iterator ntsa_iter = {
      iter_ntsa_init,
      iter_ntsa_next_line,
      iter_ntsa_has_next
};
#endif


/* Given an array of strings stored in a GPtrArray,
 * convert it a Dumpload_Data structure.
 */
#ifdef USING_ITERATOR
Dumpload_Data* dumpload_data_from_iterator(Line_Iterator iter) {
#else
Dumpload_Data* create_dumpload_data_from_g_ptr_array(GPtrArray * garray) {
#endif
   bool debug = false;
   if (debug)
      DBGMSG("Starting.");
   Dumpload_Data * data = calloc(1, sizeof(Dumpload_Data));

   bool validData = true;
   data = calloc(1, sizeof(Dumpload_Data));
   data->vcp_values = vcp_value_set_new(15);      // 15 = initial size

   // size_t len = 0;
   // ssize_t read;
   int     ct;

   int     linectr = 0;

#ifdef USING_ITERATOR
   while ( (*iter.func_has_next)() ) {       // <---
#else
   while ( linectr < garray->len ) {
#endif
      char *  line = NULL;
      char    s0[32], s1[257], s2[16];
      char *  head;
      char *  rest;

#ifdef USING_ITERATOR
      line = (*iter.func_next)();           // <---
#else
      line = g_ptr_array_index(garray,linectr);
#endif
      linectr++;

      *s0 = '\0'; *s1 = '\0'; *s2 = '\0';
      head = line;
      while (*head == ' ') head++;
      ct = sscanf(head, "%31s %256s %15s", s0, s1, s2);
      if (ct > 0 && *s0 != '*' && *s0 != '#') {
         if (ct == 1) {
            printf("Invalid data at line %d: %s\n", linectr, line);
            validData = false;
         }
         else {
            rest = head + strlen(s0);;
            while (*rest == ' ') rest++;
            char * last = rest + strlen(rest) - 1;
            // we already parsed a second token, so don't need to worry that last becomes < head
            while (*last == ' ' || *last == '\n') {
               *last-- = '\0';
            }
            // DBGMSG("rest=|%s|", rest );

            if (streq(s0, "BUS")) {
               // ignore
               // ct = sscanf(s1, "%d", &data->busno);
               // if (ct == 0) {
               //    fprintf(stderr, "Invalid bus number at line %d: %s\n", linectr, line);
               //    validData = false;
               // }
            }
            else if (streq(s0, "EDID") || streq(s0, "EDIDSTR")) {
               strncpy(data->edidstr, s1, sizeof(data->edidstr));
            }
            else if (streq(s0, "MFG_ID")) {
               strncpy(data->mfg_id, s1, sizeof(data->mfg_id));
            }
            else if (streq(s0, "MODEL")) {
               strncpy(data->model, rest, sizeof(data->model));
            }
            else if (streq(s0, "SN")) {
               strncpy(data->serial_ascii, rest, sizeof(data->serial_ascii));
            }
            else if (streq(s0, "TIMESTAMP_TEXT")   ||
                     streq(s0, "TIMESTAMP_MILLIS")

                    ) {
               // do nothing, just recognize valid field
            }
            else if (streq(s0, "VCP")) {
               if (ct != 3) {
                  fprintf(stderr, "Invalid VCP data at line %d: %s\n", linectr, line);
                  validData = false;
               }
               else {
                  int ndx = data->vcp_value_ct;
                  Single_Vcp_Value * pval = &data->vcp_value[ndx];
                  bool ok = hhs_to_byte_in_buf(s1, &pval->opcode);
                  if (!ok) {
                     printf("Invalid opcode at line %d: %s", linectr, s1);
                     validData = false;
                  }
                  else {
                     ct = sscanf(s2, "%hd", &pval->value);
                     if (ct == 0) {
                        fprintf(stderr, "Invalid value for opcode at line %d: %s\n", linectr, line);
                        validData = false;
                     }
                     else {
                        data->vcp_value_ct++;

                        // new way:
                        // assume non-table for now
                        // TODO: opcode and value should be saved in local vars
                        Single_Vcp_Value * valrec = create_cont_vcp_value(
                              pval->opcode,
                              0,   // max_val
                              pval->value);
                        vcp_value_set_add(data->vcp_values, valrec);
                     }
                  }
               }  // VCP

            }
            else {
               fprintf(stderr, "Unexpected field \"%s\" at line %d: %s\n", s0, linectr, line );
               validData = false;
            }
         }    // more than 1 field on line
      }       // non-comment line
   }          // one line of file

   if (!validData)
      data = NULL;
   return data;
}




/* Apply VCP settings from a Dumpload_Data struct to
 * the monitor specified in that data structure.
 */
bool loadvcp_by_dumpload_data(Dumpload_Data* pdata) {
   bool debug = false;
   bool ok = false;
   // DBGMSG("Searching for monitor  " );
   if (debug) {
        DBGMSG("Loading VCP settings for monitor \"%s\", sn \"%s\" \n",
               pdata->model, pdata->serial_ascii);
        report_dumpload_data(pdata, 0);
   }
   Display_Ref * dref = ddc_find_display_by_model_and_sn(pdata->model, pdata->serial_ascii);
   if (!dref) {
      fprintf(stderr, "Monitor not connected: %s - %s   \n", pdata->model, pdata->serial_ascii );
   }
   else {
      // reportDisplayRef(dref, 1);
      Display_Handle * dh = ddc_open_display(dref, EXIT_IF_FAILURE);
      int ndx;
      for (ndx=0; ndx < pdata->vcp_value_ct; ndx++) {
         Byte feature_code = pdata->vcp_value[ndx].opcode;
         int  new_value    = pdata->vcp_value[ndx].value;
         // DBGMSG("feature_code=0x%02x, new_value=%d", feature_code, new_value );

         // new way
         Single_Vcp_Value * vrec = vcp_value_set_get(pdata->vcp_values, ndx);
         assert(vrec->val.nt.cur_val == new_value);
         assert(vrec->opcode == feature_code);

         int rc = set_nontable_vcp_value(dh, feature_code, new_value);
         if (rc != 0) {
            DBGMSG("set_vcp_for_DisplayHandle() returned %d   ", rc );
            DBGMSG("Terminating.  " );
            break;
         }
      }
      ok = true;
   }
   return ok;
}


bool loadvcp_by_ntsa(Null_Terminated_String_Array ntsa) {
   bool debug = false;

   Output_Level output_level = get_output_level();
   bool verbose = (output_level >= OL_VERBOSE);
   // DBGMSG("output_level=%d, verbose=%d", output_level, verbose);
   if (debug) {
      DBGMSG("Starting.  ntsa=%p", ntsa);
      verbose = true;
   }
   bool ok = false;

   GPtrArray * garray = ntsa_to_g_ptr_array(ntsa);

#ifdef USING_ITERATOR
   (*ntsa_iter.func_init)(ntsa);
   (g_ptr_iter.func_init)(garray);
   if (debug)
      DBGMSG("after func_init");
// Both ways work.   Using loadvcp_from_ntsa is simpler, can change
// dumpload_data_from_iteractor to not take iterator object
#ifdef WORKS
   Dumpload_Data * pdata = dumpload_data_from_iterator(ntsa_iter);
#endif
   Dumpload_Data * pdata = dumpload_data_from_iterator(g_ptr_iter);
#endif

   Dumpload_Data * pdata = create_dumpload_data_from_g_ptr_array(garray);

   if (debug)
      DBGMSG("dumpload_data_from_iterator() returned %p", pdata);
   if (!pdata) {
      fprintf(stderr, "Unable to load VCP data from string\n");
   }
   else {
      if (verbose) {
           printf("Loading VCP settings for monitor \"%s\", sn \"%s\" \n",
                  pdata->model, pdata->serial_ascii);
           report_dumpload_data(pdata, 0);
      }
      ok = loadvcp_by_dumpload_data(pdata);
   }
   return ok;
}



// n. called from ddct_public:

Global_Status_Code loadvcp_by_string(char * catenated) {
   // bool debug = false;
   Null_Terminated_String_Array nta = strsplit(catenated, ";");
   // if (debug) {
   // int ct = null_terminated_string_array_length(nta);
   //    DBGMSG("split into %d lines", ct);
   //    int ndx = 0;
   //    for (; ndx < ct; ndx++) {
   //       DBGMSG("nta[%d]=|%s|", ndx, nta[ndx]);
   //    }
   // }
   loadvcp_by_ntsa(nta);
   null_terminated_string_array_free(nta);
   return 0;      // temp
}


//
// Dumpvcp
//






// n. called from ddct_public.c
Global_Status_Code
dumpvcp_as_string_old(Display_Handle * dh, char ** pstring) {
   bool debug = false;
   DBGMSF(debug, "Starting");
   GPtrArray * vals = NULL;
   *pstring = NULL;
   Global_Status_Code gsc = collect_profile_related_values(dh, time(NULL), &vals);
   if (gsc == 0) {
#ifdef OLD
      int ct = vals->len;
      DBGMSG("ct = %d", ct);
      char ** pieces = calloc(ct, sizeof(char*));
      int ndx;
      for (ndx=0; ndx < ct; ndx++) {
         pieces[ndx] = g_ptr_array_index(vals,ndx);
         DBGMSG("pieces[%d] = %s", ndx, pieces[ndx]);
      }
      char * catenated = strjoin((const char**) pieces, ct, ";");
      DBGMSF(debug, "strlen(catenated)=%ld, catenated=%p, catenated=|%s|", strlen(catenated), catenated, catenated);
      *pstring = catenated;
      DBGMSF(debug, "*pstring=%p", *pstring);
#endif
      // Alternative implementation using glib:
      Null_Terminated_String_Array ntsa_pieces = g_ptr_array_to_ntsa(vals);
      // n. our Null_Terminated_String_Array is identical to glib's GStrv
      gchar sepchar = ';';
      gchar * catenated2 = g_strjoinv(&sepchar, ntsa_pieces);
      DBGMSF(debug, "catenated2=%p", catenated2);
#ifdef old
      DBGMSF(debug, "catenated2=|%s|", catenated2);
      assert(strcmp(catenated, catenated2) == 0);
#endif
      *pstring = catenated2;

      g_ptr_array_free(vals, true);
   }
   DBGMSF(debug, "Returning: %s", gsc_desc(gsc));

   return gsc;
}


/* Primary function for the DUMPVCP command.
 *
 * Writes DUMPVCP data to the in-core Dumpload_Data structure
 *
 * Arguments:
 *    dh              display handle for connected display
 *    pdumpload_data  address as which to return pointer to newly allocated
 *                    Dumpload_Data struct.  It is the responsibility of the
 *                    caller to free this data structure.
 *    msg_fh          location where to write data error messages
 *
 * Returns:
 *    status code
 */
Global_Status_Code
dumpvcp_as_dumpload_data(
      Display_Handle * dh,
      Dumpload_Data** pdumpload_data,
      FILE * msg_fh)
{
   bool debug = false;
   DBGMSF(debug, "Starting");
   Global_Status_Code gsc = 0;
   Dumpload_Data * dumped_data = calloc(1, sizeof(Dumpload_Data));

   // timestamp:
   dumped_data->timestamp_millis = time(NULL);

   // identification information from edid:
   Parsed_Edid * edid = ddc_get_parsed_edid_by_display_handle(dh);
   memcpy(dumped_data->mfg_id, edid->mfg_id, sizeof(dumped_data->mfg_id));
   memcpy(dumped_data->model,  edid->model_name, sizeof(dumped_data->model));
   memcpy(dumped_data->serial_ascii, edid->serial_ascii, sizeof(dumped_data->serial_ascii));
   memcpy(dumped_data->edidbytes, edid->bytes, 128);
   assert(sizeof(dumped_data->edidstr) == 257);
   hexstring2(edid->bytes, 128,
              NULL /* no separator */,
              true /* uppercase */,
              dumped_data->edidstr, 257);

   // VCP values
   GPtrArray* collector = g_ptr_array_sized_new(50);
   Vcp_Value_Set vset = vcp_value_set_new(50);
   gsc = collect_raw_subset_values(
             dh,
             VCP_SUBSET_PROFILE,
             vset,
             collector,
             true,                //  ignore_unsupported
             msg_fh);
   if (gsc == 0) {
      // hack for now, TODO: redo properly
      DBGMSF(debug, "collector->len=%d", collector->len);
      assert(collector->len <= 20);
      assert(collector->len == vset->len);
      int ndx = 0;
      for (;ndx < collector->len; ndx++) {
         Parsed_Vcp_Response *  val =  g_ptr_array_index(collector,ndx);
         if (val->response_type != NON_TABLE_VCP_CALL) {
            gsc = DDCL_UNIMPLEMENTED;
         }
         else {
            dumped_data->vcp_value[ndx].opcode = val->non_table_response->vcp_code;
            dumped_data->vcp_value[ndx].value = val->non_table_response->sh << 8 |
                                                val->non_table_response->sl;
         }
      }
      dumped_data->vcp_value_ct = collector->len;
      // TODO: free collector

      dumped_data->vcp_values = vset;
      for (ndx=0; ndx < collector->len; ndx++) {
         Single_Vcp_Value * vrec = vcp_value_set_get(dumped_data->vcp_values,ndx);
         assert(dumped_data->vcp_value[ndx].opcode == vrec->opcode);
         assert(dumped_data->vcp_value[ndx].value == vrec->val.nt.cur_val);
      }
   }


   if (gsc != 0 && dumped_data)
      free(dumped_data);
   else
      *pdumpload_data = dumped_data;
   if (debug) {
      DBGMSG("Returning: %s, *pdumpload_data=%p", gsc_desc(gsc), *pdumpload_data);
      report_dumpload_data(*pdumpload_data, 1);
   }
   return gsc;
}


/* Converts a Dumpload_Data structure to an array of strings
 *
 * Arguments:
 *    data     pointer to Dumpload_Data instance
 *
 * Returns:
 *    array of strings
 */
GPtrArray * convert_dumpload_data_to_string_array(Dumpload_Data * data) {
   bool debug = false;
   DBGMSF(debug, "Starting. data=%p", data);
   assert(data);
   if (debug)
      report_dumpload_data(data, 1);

   GPtrArray * strings = g_ptr_array_sized_new(30);

   collect_machine_readable_timestamp(data->timestamp_millis, strings);

   char buf[300];
   int bufsz = sizeof(buf)/sizeof(char);
   snprintf(buf, bufsz, "MFG_ID  %s",  data->mfg_id);
   g_ptr_array_add(strings, strdup(buf));
   snprintf(buf, bufsz, "MODEL   %s",  data->model);
   g_ptr_array_add(strings, strdup(buf));
   snprintf(buf, bufsz, "SN      %s",  data->serial_ascii);
   g_ptr_array_add(strings, strdup(buf));

   char hexbuf[257];
   hexstring2(data->edidbytes, 128,
              NULL /* no separator */,
              true /* uppercase */,
              hexbuf, 257);
   snprintf(buf, bufsz, "EDID    %s", hexbuf);
   g_ptr_array_add(strings, strdup(buf));

   int ndx = 0;
#ifdef OLD
   for (;ndx < data->vcp_value_ct; ndx++) {
      // n. get_formatted_value_for_feature_table_entry() also has code for table type values
      char buf[200];
      snprintf(buf, 200, "VCP %02X %5d", data->vcp_value[ndx].opcode, data->vcp_value[ndx].value);
      g_ptr_array_add(strings, strdup(buf));
   }
#endif
   for (ndx=0;ndx < data->vcp_values->len; ndx++) {
      // n. get_formatted_value_for_feature_table_entry() also has code for table type values
      Single_Vcp_Value * vrec = vcp_value_set_get(data->vcp_values,ndx);
      char buf[200];
      snprintf(buf, 200, "VCP %02X %5d",
                         vrec->opcode, vrec->val.nt.cur_val);
      g_ptr_array_add(strings, strdup(buf));
   }
   return strings;
}

/** JOints a GPtrArray containing pointers to character strings
 *  into a single string,
 *
 *  Arguments:
 *     string   GPtrArray of strings
 *     sepstr   if non-null, separator to insert between joined strings
 *
 *  Returns:
 *     joined string
 */
char * join_string_g_ptr_array(GPtrArray* strings, char * sepstr) {
   bool debug = true;

   int ct = strings->len;
   DBGMSF(debug, "ct = %d", ct);
   char ** pieces = calloc(ct, sizeof(char*));
   int ndx;
   for (ndx=0; ndx < ct; ndx++) {
      pieces[ndx] = g_ptr_array_index(strings,ndx);
      DBGMSF(debug, "pieces[%d] = %s", ndx, pieces[ndx]);
   }
   char * catenated = strjoin((const char**) pieces, ct, sepstr);
   DBGMSF(debug, "strlen(catenated)=%ld, catenated=%p, catenated=|%s|", strlen(catenated), catenated, catenated);

#ifdef GLIB_VARIANT
   // GLIB variant failing when used with file.  why?
   Null_Terminated_String_Array ntsa_pieces = g_ptr_array_to_ntsa(strings);
   if (debug) {
      DBGMSG("ntsa_pieces before call to g_strjoinv():");
      null_terminated_string_array_show(ntsa_pieces);
   }
   // n. our Null_Terminated_String_Array is identical to glib's GStrv
   gchar sepchar = ';';
   gchar * catenated2 = g_strjoinv(&sepchar, ntsa_pieces);
   DBGMSF(debug, "catenated2=%p", catenated2);
   *pstring = catenated2;
   assert(strcmp(catenated, catenated2) == 0);
#endif

   return catenated;
}



/* Returns the output of the DUMPVCP command a single string.
 * Each field is separated by a semicolon.
 *
 * The caller is responsible for freeing the returned string.
 *
 * Arguments:
 *    dh       display handle of open monnitor
 *    pstring  location at which to return string
 *
 * Returns:
 *    status code
 */
// n. called from ddct_public.c
// move to glib_util.c?
Global_Status_Code
dumpvcp_as_string(Display_Handle * dh, char ** pstring) {
   bool debug = false;
   DBGMSF(debug, "Starting");

   Global_Status_Code gsc    = 0;
   Dumpload_Data *    data   = NULL;
   FILE *             msg_fh = stdout;   // temp
   *pstring = NULL;

   gsc = dumpvcp_as_dumpload_data(dh, &data, msg_fh);
   if (gsc == 0) {
      GPtrArray * strings = convert_dumpload_data_to_string_array(data);
      *pstring = join_string_g_ptr_array(strings, ";");
   }
   DBGMSF(debug, "Returning: %s, *pstring=|%s|", gsc_desc(gsc), *pstring);
   return gsc;
}
