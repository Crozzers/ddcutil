/*
 * ddc_services.h
 *
 *  Created on: Nov 15, 2015
 *      Author: rock
 */

#ifndef DDC_SERVICES_H_
#define DDC_SERVICES_H_

#include <glib.h>
#include <stdio.h>
#include <time.h>

#include "base/common.h"
#include "base/ddc_base_defs.h"     // for Version_Spec
#include "base/displays.h"
#include "base/status_code_mgt.h"

#include "ddc/vcp_feature_codes.h"

void init_ddc_services();

Display_Ref* get_display_ref_for_display_identifier(Display_Identifier* pdid, bool emit_error_msg);

typedef enum {SUBSET_SCAN, SUBSET_ALL, SUBSET_SUPPORTED, SUBSET_COLORMGT, SUBSET_PROFILE} VCP_Feature_Subset;

void show_vcp_values_by_display_ref(Display_Ref * dref, VCP_Feature_Subset subset, GPtrArray * collector);

void show_single_vcp_value_by_display_ref(Display_Ref * dref, char * feature, bool force);

Global_Status_Code set_vcp_by_display_handle(Display_Handle * pDispHandle, Byte feature_code, int new_value);

Global_Status_Code set_vcp_value_top(Display_Ref * dref, char * feature, char * new_value);


Display_Info_List * ddc_get_valid_displays();
int ddc_show_active_displays(int depth);

Display_Ref* ddc_find_display_by_dispno(int dispno);

Version_Spec get_vcp_version_by_display_handle(Display_Handle * dh);
Version_Spec get_vcp_version_by_display_ref(Display_Ref * dref);

// Get capability string for monitor.
Global_Status_Code get_capabilities_buffer_by_display_handle(Display_Handle * dh, Buffer** ppCapabilitiesBuffer);
Global_Status_Code get_capabilities_string_by_display_handle(Display_Handle * dh, char** pcaps);
Global_Status_Code get_capabilities_buffer_by_display_ref(Display_Ref * pdisp, Buffer** ppCapabilitiesBuffer);
Global_Status_Code get_capabilities_string_by_display_ref(Display_Ref * pdisp, char** pcaps);

char * format_timestamp(time_t time_millis, char * buf, int bufsz);

GPtrArray * get_profile_related_values_by_display_handle(Display_Handle * dh);
GPtrArray * get_profile_related_values_by_display_ref(Display_Ref * dref);

#endif /* DDC_SERVICES_H_ */
