/** @file dyn_vcp_info.c
 *
 * Access VCP feature code descriptions at the DDC level in order to
 * incorporate user-defined per-monitor feature information.
 */

// Copyright (C) 2014-2018 Sanford Rockowitz <rockowitz@minsoft.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#include <assert.h>

#include "util/report_util.h"
#include "base/displays.h"
#include "vcp/vcp_feature_codes.h"

#include "dynvcp/dyn_vcp_info.h"

void
dbgrpt_internal_feature_metadata(
      Internal_Feature_Metadata * intmeta,
      int                         depth)
{
   int d1 = depth+1;
   rpt_vstring(depth, "Internal_Feature_Metadata at %p", intmeta);
   dbgrpt_ddca_feature_metadata(intmeta->external_metadata, d1);
   rpt_vstring(d1, "nontable_formatter:   %p", intmeta->nontable_formatter);
   rpt_vstring(d1, "vcp_nontable_formatter:   %p", intmeta->vcp_nontable_formatter);
   rpt_vstring(d1, "table_formatter:      %p", intmeta->table_formatter);
}

void
version_feature_info_to_metadata(
      DDCA_Version_Feature_Info * info,
      DDCA_Feature_Metadata * meta)
{
   assert(meta);
   assert( memcmp(meta->marker, DDCA_FEATURE_METADATA_MARKER, 4) == 0);
   meta->feature_code = info->feature_code;
   meta->feature_desc = strdup(info->desc);
   meta->feature_flags = info->feature_flags;
   meta->feature_name = strdup(info->feature_name);
   meta->sl_values = info->sl_values;    // need to copy?

}



/* Formats the value of a non-continuous feature whose value is returned in byte SL.
 * The names of possible values is stored in a value list in the feature table entry
 * for the feature.
 *
 * Arguments:
 *    code_info   parsed feature data
 *    vcp_version VCP version
 *    buffer      buffer in which to store output
 *    bufsz       buffer size
 *
 * Returns:
 *    true if formatting successful, false if not
 */
bool dyn_format_feature_detail_sl_lookup(
        Nontable_Vcp_Value *     code_info,
        // Display_Ref *            dref,
        // DDCA_MCCS_Version_Spec   vcp_version,
        DDCA_Feature_Value_Table value_table,
        char *                   buffer,
        int                      bufsz)
{
   bool debug = true;
   DBGMSF(debug, "Starting.");

   // Internal_Feature_Metadata * intmeta = NULL;


   // char * s = lookup_value_name(code_info->vcp_code, vcp_version, code_info->sl);
   // DDCA_Feature_Value_Entry * table = intmeta->external_metadata->sl_values;
   if (value_table) {
      char * s = get_feature_value_name(value_table, code_info->sl);
      if (!s)
         s = "Unrecognized value";
      snprintf(buffer, bufsz,"%s (sl=0x%02x)", s, code_info->sl);
   }
   else
      snprintf(buffer, bufsz, "0x%02x", code_info->sl);

   DBGMSF(debug, "Done.  *buffer=|%s|. Returning true", buffer);
   return true;
}




Internal_Feature_Metadata *
dyn_get_feature_metadata_by_dref(
      DDCA_Vcp_Feature_Code feature_code,
      Display_Ref *         dref,
      bool                  with_default)
{
   bool debug = true;
   DBGMSF(debug, "Starting. feature_code=0x%02x, dref=%s, with_default=%s",
                 feature_code, dref_repr_t(dref), sbool(with_default));

   Internal_Feature_Metadata * result = NULL;

   if (dref->dfr) {
      DDCA_Feature_Metadata * dfr_metadata = get_dynamic_feature_metadata(dref->dfr, feature_code);
      if (dfr_metadata) {
         result = calloc(1, sizeof(Internal_Feature_Metadata));
         result->external_metadata = dfr_metadata;
         if (dfr_metadata->feature_flags & DDCA_SIMPLE_NC) {
            if (dfr_metadata->sl_values)
               result->vcp_nontable_formatter = dyn_format_feature_detail_sl_lookup;  // HACK
            else
               result->nontable_formatter = format_feature_detail_sl_byte;
         }
         else if (dfr_metadata->feature_flags & DDCA_STD_CONT)
            result->nontable_formatter = format_feature_detail_standard_continuous;
         else if (dfr_metadata->feature_flags & DDCA_TABLE)
            result->table_formatter = default_table_feature_detail_function;
         else
            result->nontable_formatter = format_feature_detail_debug_bytes;
      }
   }
   if (!result) {
      // returns dref->vcp_version if already cached, queries monitor if not
      DDCA_MCCS_Version_Spec vspec = get_vcp_version_by_display_ref(dref);
      // DBGMSG("vspec=%d.%d", vspec.major, vspec.minor);

       VCP_Feature_Table_Entry * pentry =
              (with_default) ? vcp_find_feature_by_hexid_w_default(feature_code)
                             : vcp_find_feature_by_hexid(feature_code);
        if (pentry) {
           DDCA_Version_Feature_Info * info = extract_version_feature_info(pentry, vspec, /*version_sensitive*/ true);
           DDCA_Feature_Metadata * meta = calloc(1, sizeof(DDCA_Feature_Metadata));
           memcpy(meta->marker, DDCA_FEATURE_METADATA_MARKER, 4);
           version_feature_info_to_metadata(info, meta);
           free_version_feature_info(info);  // safe?
           result = calloc(1, sizeof(Internal_Feature_Metadata));
           result->external_metadata = meta;
           if (meta->feature_flags & DDCA_NON_TABLE) {
              if (pentry->nontable_formatter) {
                 result->nontable_formatter = pentry->nontable_formatter;
              }
              else {
                 if (meta->feature_flags & DDCA_SIMPLE_NC) {
                    if (meta->sl_values)
                       result->nontable_formatter = format_feature_detail_sl_lookup;
                    else
                       result->nontable_formatter = format_feature_detail_sl_byte;
                 }
                 else if (meta->feature_flags & DDCA_STD_CONT)
                    result->nontable_formatter = format_feature_detail_standard_continuous;
                 else
                    result->nontable_formatter = format_feature_detail_debug_bytes;
              }
           }
           else if (meta->feature_flags & DDCA_TABLE) {
              result->table_formatter = pentry->table_formatter;
              if (!result->table_formatter)
                 result->table_formatter = default_table_feature_detail_function;
           }
           else
              PROGRAM_LOGIC_ERROR("Neither DDCA_TABLE nor DDCA_NON_TABLE is set");

           if (pentry->vcp_global_flags & DDCA_SYNTHETIC)
              free_synthetic_vcp_entry(pentry);
        }
   }

   if (debug) {
      DBGMSF(debug, "Done. Returning: %p", result);
      if (result)
         dbgrpt_internal_feature_metadata(result, 1);
   }
   return result;
}

Internal_Feature_Metadata *
dyn_get_feature_metadata_by_dh(
      DDCA_Vcp_Feature_Code id,
      Display_Handle *      dh,
      bool                  with_default)
{
   // ensure dh->dref->vcp_version set without incurring additional open/close
   get_vcp_version_by_display_handle(dh);
   return dyn_get_feature_metadata_by_dref(id, dh->dref, with_default);

}






// Functions that apply formatting

bool
dyn_format_nontable_feature_detail(
        Internal_Feature_Metadata *  intmeta,
        DDCA_MCCS_Version_Spec     vcp_version,
        Nontable_Vcp_Value *       code_info,
        char *                     buffer,
        int                        bufsz)
{
   bool debug = true;
   DBGMSF(debug, "Starting. Code=0x%02x, vcp_version=%d.%d",
                 intmeta->external_metadata->feature_code, vcp_version.major, vcp_version.minor);

   DDCA_Feature_Metadata * extmeta = intmeta->external_metadata;

   bool ok = false;
   if (intmeta->nontable_formatter) {
      Format_Normal_Feature_Detail_Function ffd_func = intmeta->nontable_formatter;
        // get_nontable_feature_detail_function(vfte, vcp_version);
      ok = ffd_func(code_info, vcp_version,  buffer, bufsz);
   }
   else if (intmeta->vcp_nontable_formatter) {
      Format_Normal_Feature_Detail_Function2 ffd_func = intmeta->vcp_nontable_formatter;
      ok = ffd_func(code_info, extmeta->sl_values, buffer, bufsz);
   }
   else
      PROGRAM_LOGIC_ERROR("Neither nontable_formatter nor vcp_nontable_formatter set");

   DBGMSF(debug, "Returning: %s", sbool(ok));
   return ok;
}

bool
dyn_format_table_feature_detail(
      Internal_Feature_Metadata *  intmeta,
       DDCA_MCCS_Version_Spec     vcp_version,
       Buffer *                   accumulated_value,
       char * *                   aformatted_data
     )
{
   Format_Table_Feature_Detail_Function ffd_func = intmeta->table_formatter;
      //   get_table_feature_detail_function(vfte, vcp_version);
   bool ok = ffd_func(accumulated_value, vcp_version, aformatted_data);
   return ok;
}

#ifdef APPARENT_DUPLICATE
/* Given a feature table entry and a raw feature value,
 * return a formatted string interpretation of the value.
 *
 * Arguments:
 *    vcp_entry        vcp_feature_table_entry
 *    vcp_version      monitor VCP version
 *    valrec           feature value
 *    aformatted_data  location where to return formatted string value
 *
 * Returns:
 *    true if formatting successful, false if not
 *
 * It is the caller's responsibility to free the returned string.
 */
bool
dyn_format_feature_detail(
      Internal_Feature_Metadata *  intmeta,
       DDCA_MCCS_Version_Spec    vcp_version,
       Single_Vcp_Value *        valrec,
       char * *                  aformatted_data
     )
{
   bool debug = false;
   DBGMSF(debug, "Starting");
   bool ok = true;
   *aformatted_data = NULL;

   DBGMSF(debug, "valrec->value_type = %d", valrec->value_type);
   char * formatted_data = NULL;
   if (valrec->value_type == DDCA_NON_TABLE_VCP_VALUE) {
      DBGMSF(debug, "DDCA_NON_TABLE_VCP_VALUE");
      Nontable_Vcp_Value* nontable_value = single_vcp_value_to_nontable_vcp_value(valrec);
      char workbuf[200];
      ok = dyn_format_nontable_feature_detail(
              intmeta,
              vcp_version,
              nontable_value,
              workbuf,
              200);
      free(nontable_value);
      if (ok)
         formatted_data = strdup(workbuf);
   }
   else {        // TABLE_VCP_CALL
      DBGMSF(debug, "DDCA_TABLE_VCP_VALUE");
      ok = dyn_format_table_feature_detail(
            intmeta,
            vcp_version,
            buffer_new_with_value(valrec->val.t.bytes, valrec->val.t.bytect, __func__),
            &formatted_data);
   }

   if (ok) {
      *aformatted_data = formatted_data;
      assert(*aformatted_data);
   }
   else {
      if (formatted_data)
         free(formatted_data);
      assert(!*aformatted_data);
   }

   DBGMSF(debug, "Done.  Returning %d, *aformatted_data=%p", ok, *aformatted_data);
   return ok;
}
#endif


/* Given a feature table entry and a raw feature value,
 * return a formatted string interpretation of the value.
 *
 * Arguments:
 *    vcp_entry        vcp_feature_table_entry
 *    vcp_version      monitor VCP version
 *    valrec           feature value
 *    aformatted_data  location where to return formatted string value
 *
 * Returns:
 *    true if formatting successful, false if not
 *
 * It is the caller's responsibility to free the returned string.
 */
bool
dyn_format_feature_detail(
       Internal_Feature_Metadata * intmeta,
       DDCA_MCCS_Version_Spec    vcp_version,
       Single_Vcp_Value *        valrec,
       char * *                  aformatted_data
     )
{
   bool debug = true;
   DBGMSF(debug, "Starting");

   bool ok = true;
   *aformatted_data = NULL;

   DBGMSF(debug, "valrec->value_type = %d", valrec->value_type);
   char * formatted_data = NULL;
   if (valrec->value_type == DDCA_NON_TABLE_VCP_VALUE) {
      DBGMSF(debug, "DDCA_NON_TABLE_VCP_VALUE");
      Nontable_Vcp_Value* nontable_value = single_vcp_value_to_nontable_vcp_value(valrec);
      char workbuf[200];
      ok = dyn_format_nontable_feature_detail(
              intmeta,
              vcp_version,
              nontable_value,
              workbuf,
              200);
      free(nontable_value);
      if (ok)
         formatted_data = strdup(workbuf);
   }
   else {        // TABLE_VCP_CALL
      DBGMSF(debug, "DDCA_TABLE_VCP_VALUE");
      ok = dyn_format_table_feature_detail(
            intmeta,
            vcp_version,
            buffer_new_with_value(valrec->val.t.bytes, valrec->val.t.bytect, __func__),
            &formatted_data);
   }

   if (ok) {
      *aformatted_data = formatted_data;
      assert(*aformatted_data);
   }
   else {
      if (formatted_data)
         free(formatted_data);
      assert(!*aformatted_data);
   }

   DBGMSF(debug, "Done.  Returning %d, *aformatted_data=%p", ok, *aformatted_data);
   return ok;
}


