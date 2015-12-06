/*
 * ddct_public.c
 *
 *  Created on: Dec 1, 2015
 *      Author: rock
 */

#include <string.h>

#include "util/string_util.h"

#include "base/ddc_errno.h"
#include "base/displays.h"
#include "base/ddc_packets.h"
#include "base/parms.h"

#include "adl/adl_shim.h"

#include "ddc/ddc_multi_part_io.h"
#include "ddc/ddc_packet_io.h"
#include "ddc/ddc_services.h"
#include "ddc/ddc_vcp.h"
#include "ddc/vcp_feature_codes.h"

#include "libmain/ddct_public.h"

#define WITH_DR(ddct_dref, action) \
   do { \
      if (!library_initialized) \
         return DDCL_UNINITIALIZED; \
      DDCT_Status rc = 0; \
      Display_Ref * dref = (Display_Ref *) ddct_dref; \
      if (dref == NULL || memcmp(dref->marker, DISPLAY_REF_MARKER, 4) != 0 )  { \
         rc = DDCL_ARG; \
      } \
      else { \
         (action); \
      } \
      return rc; \
   } while(0);


#define WITH_DH(_ddct_dh_, _action_) \
   do { \
      if (!library_initialized) \
         return DDCL_UNINITIALIZED; \
      DDCT_Status rc = 0; \
      Display_Handle * dh = (Display_Handle *) _ddct_dh_; \
      if (dh == NULL || memcmp(dh->marker, DISPLAY_HANDLE_MARKER, 4) != 0 )  { \
         rc = DDCL_ARG; \
      } \
      else { \
         (_action_); \
      } \
      return rc; \
   } while(0);



static bool library_initialized = false;

void ddct_init() {
   init_ddc_services();
   library_initialized = true;
}

bool ddct_built_with_adl() {
   return adlshim_is_available();
}


char * ddct_status_code_name(DDCT_Status status_code) {
   char * result = NULL;
   Status_Code_Info * code_info = find_global_status_code_info(status_code);
   if (code_info)
      result = code_info->name;
   return result;
}

char * ddct_status_code_desc(DDCT_Status status_code) {
   char * result = "unknown status code";
   Status_Code_Info * code_info = find_global_status_code_info(status_code);
   if (code_info)
   result = code_info->description;
   return result;
}

// typedef enum{DDCT_WRITE_ONLY_TRIES, DDCT_WRITE_READ_TRIES, DDCT_MULTIPART_TRIES} DDCT_Retry_Type;

int  ddct_get_max_tries(DDCT_Retry_Type retry_type) {
   int result = 0;
   switch(retry_type) {
      case (DDCT_WRITE_ONLY_TRIES):
         result = ddc_get_max_write_only_exchange_tries();
      break;
   case (DDCT_WRITE_READ_TRIES):
      result = ddc_get_max_write_read_exchange_tries();
      break;
   case (DDCT_MULTI_PART_TRIES):
      result = ddc_get_max_multi_part_read_tries();
      break;
   }
   return result;
}


DDCT_Status ddct_set_max_tries(DDCT_Retry_Type retry_type, int max_tries) {
   DDCT_Status rc = 0;
   if (max_tries > MAX_MAX_TRIES)
      rc = DDCL_ARG;
   else {
      switch(retry_type) {
      case (DDCT_WRITE_ONLY_TRIES):
         ddc_set_max_write_only_exchange_tries(max_tries);
         break;
      case (DDCT_WRITE_READ_TRIES):
         ddc_set_max_write_read_exchange_tries(max_tries);
         break;
      case (DDCT_MULTI_PART_TRIES):
         ddc_set_max_multi_part_read_tries(max_tries);
         break;
      }
   }
   return rc;
}



DDCT_Status ddct_create_dispno_display_identifier(int dispno, DDCT_Display_Identifier* pdid) {
   Display_Identifier* did = create_dispno_display_identifier(dispno);
   *pdid = did;
   return 0;
}

DDCT_Status ddct_create_busno_display_identifier(
               int busno,
               DDCT_Display_Identifier* pdid) {
   Display_Identifier* did = create_busno_display_identifier(busno);
   *pdid = did;
   return 0;
}

DDCT_Status ddct_create_adlno_display_identifier(
               int iAdapterIndex,
               int iDisplayIndex,
               DDCT_Display_Identifier* pdid) {
   Display_Identifier* did = create_adlno_display_identifier(iAdapterIndex, iDisplayIndex);
   *pdid = did;
   return 0;
}

DDCT_Status ddct_create_mon_ser_display_identifier(
      char* model_name,
      char* serial_ascii,
      DDCT_Display_Identifier* pdid
     )
{
   *pdid = NULL;
   DDCT_Status rc = 0;
   if (model_name == NULL  ||
       strlen(model_name) >= EDID_MODEL_NAME_FIELD_SIZE ||
       serial_ascii == NULL ||
       strlen(serial_ascii) >= EDID_SERIAL_ASCII_FIELD_SIZE
      )
   {
      rc = DDCL_ARG;
      *pdid = NULL;
   }
   else {
      *pdid = create_mon_ser_display_identifier(model_name, serial_ascii);
   }
   return rc;
}

DDCT_Status ddct_free_display_identifier(DDCT_Display_Identifier did) {
   DDCT_Status rc = 0;
   Display_Identifier * pdid = (Display_Identifier *) did;
   if (pdid == NULL || memcmp(pdid->marker, DISPLAY_IDENTIFIER_MARKER, 4) != 0 )  {
     rc = DDCL_ARG;
   }
   else {
     free_display_identifier(pdid);
   }
   return rc;
}


static char did_work_buf[100];

DDCT_Status ddct_repr_display_identifier(DDCT_Display_Identifier ddct_did, char **repr) {
   DDCT_Status rc = 0;
   Display_Identifier * pdid = (Display_Identifier *) ddct_did;
   if (pdid == NULL || memcmp(pdid->marker, DISPLAY_IDENTIFIER_MARKER, 4) != 0 )  {
     rc = DDCL_ARG;
     *repr = "invalid display identifier";
   }
   else {
      char * did_type_name = display_id_type_name(pdid->id_type);
      switch (pdid->id_type) {
      case(DISP_ID_BUSNO):
            snprintf(did_work_buf, 100,
                     "Display Id Type: %s, bus=/dev/i2c-%d", did_type_name, pdid->busno);
            break;
      case(DISP_ID_ADL):
            snprintf(did_work_buf, 100,
                     "Display Id Type: %s, adlno=%d.%d", did_type_name, pdid->iAdapterIndex, pdid->iDisplayIndex);
            break;
      case(DISP_ID_MONSER):
            snprintf(did_work_buf, 100,
                     "Display Id Type: %s, model=%s, sn=%s", did_type_name, pdid->model_name, pdid->serial_ascii);
            break;
      case(DISP_ID_EDID):
      {
            char * hs = hexstring(pdid->edidbytes, 128);
            snprintf(did_work_buf, 100,
                     "Display Id Type: %s, edid=%8s...%8s", did_type_name, hs, hs+248);
            break;
      }
      case(DISP_ID_DISPNO):
            snprintf(did_work_buf, 100,
                     "Display Id Type: %s, dispno=%d", did_type_name, pdid->dispno);

      } // switch
      *repr = did_work_buf;
   }
   return rc;
}


DDCT_Status ddct_get_display_ref(DDCT_Display_Identifier did, DDCT_Display_Ref* ddct_dref) {
   bool debug = false;
   if (!library_initialized)
      return DDCL_UNINITIALIZED;
   DDCT_Status rc = 0;
   Display_Identifier * pdid = (Display_Identifier *) did;
   if (pdid == NULL || memcmp(pdid->marker, DISPLAY_IDENTIFIER_MARKER, 4) != 0 )  {
     rc = DDCL_ARG;
   }
   else {
      Display_Ref* dref = get_display_ref_for_display_identifier(pdid, true /* emit_error_msg */);
      if (debug)
         printf("(%s) get_display_ref_for_display_identifier() returned %p\n", __func__, dref);
      if (dref)
         *ddct_dref = dref;
      else
         rc = DDCL_ARG;
   }
   return rc;
}


DDCT_Status ddct_free_display_ref(DDCT_Display_Ref ddct_dref) {
   WITH_DR(ddct_dref,
         {
         free_display_ref(dref);
         }
   );
}


// static char dref_work_buf[100];

DDCT_Status ddct_repr_display_ref(DDCT_Display_Ref ddct_dref, char** repr){
   DDCT_Status rc = 0;
   Display_Ref * dref = (Display_Ref *) ddct_dref;
   if (dref == NULL || memcmp(dref->marker, DISPLAY_REF_MARKER, 4) != 0 )  {
      rc = DDCL_ARG;
      *repr = "invalid display reference";
   }
   else {
#ifdef TOO_MUCH_WORK
      char * dref_type_name = ddc_io_mode_name(dref->ddc_io_mode);
      switch (dref->ddc_io_mode) {
      case(DISP_ID_BUSNO):
         snprintf(dref_work_buf, 100,
                  "Display Ref Type: %s, bus=/dev/i2c-%d", dref_type_name, dref->busno);
         break;
      case(DISP_ID_ADL):
         snprintf(dref_work_buf, 100,
                  "Display Ref Type: %s, adlno=%d.%d", dref_type_name, dref->iAdapterIndex, dref->iDisplayIndex);
         break;
      }
      *repr = did_work_buf;
#endif
      *repr = display_ref_short_name(dref);
   }
   return rc;
}


DDCT_Status ddct_open_display(DDCT_Display_Ref ddct_dref, DDCT_Display_Handle * pdh) {
   if (!library_initialized)
      return DDCL_UNINITIALIZED;
   DDCT_Status rc = 0;
   Display_Ref * dref = (Display_Ref *) ddct_dref;
   if (dref == NULL || memcmp(dref->marker, DISPLAY_REF_MARKER, 4) != 0 )  {
      rc = DDCL_ARG;
   }
   else {
     // TODO: fix ddc_open_display for RETURN_ERROR_IF_FAILURE
     Display_Handle* dh = ddc_open_display(dref,  RETURN_ERROR_IF_FAILURE);
     if (dh)
        *pdh = dh;
     else
        rc = DDCL_ARG;     //  TEMP, need a proper status code
   }
   return rc;
}

DDCT_Status ddct_close_display(DDCT_Display_Handle ddct_dh) {
   if (!library_initialized)
      return DDCL_UNINITIALIZED;
   DDCT_Status rc = 0;
   Display_Handle * dh = (Display_Handle *) ddct_dh;
   if (dh == NULL || memcmp(dh->marker, DISPLAY_HANDLE_MARKER, 4) != 0 )  {
      rc = DDCL_ARG;
   }
   else {
      // TODO: ddc_close_display() needs an action if failure parm,
      // should return status code
      ddc_close_display(dh);
      rc = 0;    // is this what to do?
   }
   return rc;
}

static char dh_work_buf[100];

DDCT_Status ddct_repr_display_handle(DDCT_Display_Handle ddct_dh, char ** repr) {
   DDCT_Status rc = 0;
   Display_Ref * dh = (Display_Ref *) ddct_dh;
   if (dh == NULL || memcmp(dh->marker, DISPLAY_HANDLE_MARKER, 4) != 0 )  {
      rc = DDCL_ARG;
      *repr = "invalid display handle";
   }
   else {
      char * dh_type_name = ddc_io_mode_name(dh->ddc_io_mode);
      switch (dh->ddc_io_mode) {
      case(DISP_ID_BUSNO):
         snprintf(dh_work_buf, 100,
                  "Display Handle Type: %s, bus=/dev/i2c-%d", dh_type_name, dh->busno);
         break;
      case(DISP_ID_ADL):
         snprintf(dh_work_buf, 100,
                  "Display Handle Type: %s, adlno=%d.%d", dh_type_name, dh->iAdapterIndex, dh->iDisplayIndex);
         break;
      }
      *repr = did_work_buf;
   }
   return rc;
}


DDCT_Status ddct_get_mccs_version(DDCT_Display_Handle ddct_dh, DDCT_MCCS_Version_Spec* pspec) {
   if (!library_initialized)
      return DDCL_UNINITIALIZED;
   DDCT_Status rc = 0;
   Display_Handle * dh = (Display_Handle *) ddct_dh;
   if (dh == NULL || memcmp(dh->marker, DISPLAY_HANDLE_MARKER, 4) != 0 )  {
      rc = DDCL_ARG;
      pspec->major = 0;
      pspec->minor = 0;
   }
   else {
      // no: need to call function, may not yet be set
      Version_Spec vspec = get_vcp_version_by_display_handle(dh);
      pspec->major = vspec.major;
      pspec->minor = vspec.minor;
      rc = 0;
   }
   return rc;
}


DDCT_Status ddct_get_edid_by_display_ref(DDCT_Display_Ref ddct_dref, Byte** pbytes) {
   if (!library_initialized)
      return DDCL_UNINITIALIZED;
   DDCT_Status rc = 0;
   Display_Ref * dref = (Display_Ref *) ddct_dref;
   if (dref == NULL || memcmp(dref->marker, DISPLAY_REF_MARKER, 4) != 0 )  {
      rc = DDCL_ARG;
   }
   else {
      Parsed_Edid*  edid = ddc_get_parsed_edid_by_display_ref(dref);
      *pbytes = edid->bytes;
   }
   return rc;

}

// static char  default_feature_name_buffer[40];

// or return a struct?
DDCT_Status ddct_get_feature_info(VCP_Feature_Code feature_code, unsigned long * flags) {
   return DDCL_UNIMPLEMENTED;
}

char *      ddct_get_feature_name(VCP_Feature_Code feature_code) {
   // do we want get_feature_name()'s handling of mfg specific and unrecognzied codes?
   char * result = get_feature_name(feature_code);
   // snprintf(default_feature_name_buffer, sizeof(default_feature_name_buffer), "VCP Feature 0x%02x", feature_code);
   // return default_feature_name_buffer;
   return result;
}

typedef void * Feature_Value_Table;   // temp

DDCT_Status ddct_get_feature_sl_value_table(
               DDCT_Display_Handle   ddct_dh,
               VCP_Feature_Code      feature_code,
               Feature_Value_Table * value_table)
{
   return DDCL_UNIMPLEMENTED;
}

// or:
DDCT_Status ddct_get_nc_feature_value_name(
               DDCT_Display_Handle ddct_dh,    // needed because value lookup mccs version dependent
               VCP_Feature_Code    feature_code,
               Byte                feature_value,
               char**              pfeature_name)
{
   WITH_DH(ddct_dh,  {
         // this should be a function in vcp_feature_codes:
         char * feature_name = NULL;
         Version_Spec vspec = dh->vcp_version;
         Feature_Value_Entry * feature_value_entries = find_feature_values_new(feature_code, vspec);
         if (feature_value_entries == NULL) {
            rc = DDCL_ARG;
         }
         else {
            feature_name = find_value_name_new(feature_value_entries, feature_value);
            if (feature_name == NULL)
               rc = DDCL_ARG;               // correct handling for value not found?
            else
               *pfeature_name = feature_name;
         }
   }
   );
}


DDCT_Status ddct_get_nontable_vcp_value(
               DDCT_Display_Handle             ddct_dh,
               VCP_Feature_Code                feature_code,
               DDCT_Non_Table_Value_Response * response)
{
   WITH_DH(ddct_dh,  {
       Interpreted_Vcp_Code * code_info;
       Global_Status_Code gsc = get_vcp_by_display_handle(dh, feature_code,&code_info);
       if (gsc == 0) {
          response->cur_value = code_info->cur_value;
          response->max_value = code_info->max_value;
          response->mh        = code_info->mh;
          response->ml        = code_info->ml;
          response->sh        = code_info->sh;
          response->sl        = code_info->sl;
       }
       else rc = gsc;    // ??
    } );
}


DDCT_Status ddct_set_nontable_vcp_value(
               DDCT_Display_Handle ddct_dh,
               VCP_Feature_Code feature_code,
               int              new_value)
{
   WITH_DH(ddct_dh,  {
         Global_Status_Code gsc = set_vcp_by_display_handle(dh, feature_code, new_value);
         rc = gsc;
      } );
}

// caller allocate buffer, or should function?
// for now function allocates buffer, caller needs to free
// todo: lower level functions should cache capabilities string;
DDCT_Status ddct_get_capabilities_string(DDCT_Display_Handle ddct_dh, char** pcaps)
{
   WITH_DH(ddct_dh,
      {
         Global_Status_Code gsc = get_capabilities_string_by_display_handle(dh, pcaps);
         rc = gsc;
      }
   );
}
