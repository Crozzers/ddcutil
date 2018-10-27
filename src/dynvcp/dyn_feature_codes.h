/** @file dyn_feature_codes.h
 *
 * Access VCP feature code descriptions at the DDC level in order to
 * incorporate user-defined per-monitor feature information.
 */

// Copyright (C) 2014-2018 Sanford Rockowitz <rockowitz@minsoft.com>
// SPDX-License-Identifier: GPL-2.0-or-later


#ifndef DYN_FEATURE_CODES_H_
#define DYN_FEATURE_CODES_H_

#include "ddcutil_types.h"

#include "vcp/vcp_feature_codes.h"

#include "ddc/ddc_vcp_version.h"


// Extends DDCA_Feature_Metadata with  fields not exposed in API
/** Describes a VCP feature code, tailored for a specific VCP version */
typedef
struct {
   DDCA_Feature_Metadata *                external_metadata;

   // fields not in DDCA_Feature_Metadata:
   Format_Normal_Feature_Detail_Function  nontable_formatter;
   Format_Normal_Feature_Detail_Function2 vcp_nontable_formatter;
   Format_Table_Feature_Detail_Function   table_formatter;
} Internal_Feature_Metadata;

void
dbgrpt_internal_feature_metadata(
      Internal_Feature_Metadata * intmeta,
      int                         depth);

Internal_Feature_Metadata *
dyn_get_feature_metadata_by_mmk_and_vspec(
     DDCA_Vcp_Feature_Code    feature_code,
     DDCA_Monitor_Model_Key   mmk,
     DDCA_MCCS_Version_Spec   vspec,
     bool                     with_default);

Internal_Feature_Metadata *
dyn_get_feature_metadata_by_dref(
      DDCA_Vcp_Feature_Code       id,
      Display_Ref *               dref,
      bool                        with_default);

Internal_Feature_Metadata *
dyn_get_feature_metadata_by_dh(
      DDCA_Vcp_Feature_Code       id,
      Display_Handle *            dh,
      bool                        with_default);

bool
dyn_format_nontable_feature_detail(
      Internal_Feature_Metadata * intmeta,
      DDCA_MCCS_Version_Spec      vcp_version,
      Nontable_Vcp_Value *        code_info,
      char *                      buffer,
      int                         bufsz);

bool
dyn_format_table_feature_detail(
      Internal_Feature_Metadata * intmeta,
      DDCA_MCCS_Version_Spec      vcp_version,
      Buffer *                    accumulated_value,
      char * *                    aformatted_data);

bool
dyn_format_feature_detail(
      Internal_Feature_Metadata * intmeta,
      DDCA_MCCS_Version_Spec      vcp_version,
#ifdef SINGLE_VCP_VALUE
      Single_Vcp_Value *          valrec,
#else
      DDCA_Any_Vcp_Value *        valrec,
#endif
      char * *                    aformatted_data);

char *
dyn_get_feature_name(
      Byte                       feature_code,
      Display_Ref*               dref);


void init_dyn_feature_codes() {
   func_name_table_add(dyn_format_nontable_feature_detail, "dyn_format_nontable_feature_detail");
   func_name_table_add(dyn_format_table_feature_detail, "dyn_format_table_feature_detail");
   func_name_table_add(dyn_format_feature_detail, "dyn_format_feature_detail");
   dbgrpt_func_name_table(0);
}

#endif /* DYN_FEATURE_CODES_H_ */
