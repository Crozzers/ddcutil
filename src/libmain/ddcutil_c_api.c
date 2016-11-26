/* ddcutil_c_api.c
 *
 * <copyright>
 * Copyright (C) 2015-2016 Sanford Rockowitz <rockowitz@minsoft.com>
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

#include <config.h>

#include <assert.h>
#include <string.h>

#include "util/report_util.h"
#include "util/string_util.h"

#include "base/build_info.h"
#include "base/core.h"
#include "base/ddc_errno.h"
#include "base/ddc_packets.h"
#include "base/displays.h"
#include "base/parms.h"
#include "base/base_services.h"

#include "vcp/vcp_feature_codes.h"

#include "adl/adl_shim.h"

#include "ddc/ddc_displays.h"
#include "ddc/ddc_dumpload.h"
#include "ddc/ddc_edid.h"
#include "ddc/ddc_multi_part_io.h"
#include "ddc/ddc_output.h"
#include "ddc/ddc_packet_io.h"
#include "ddc/ddc_read_capabilities.h"
#include "ddc/ddc_services.h"
#include "ddc/ddc_vcp_version.h"
#include "ddc/ddc_vcp.h"

#include "libmain/ddcutil_c_api.h"


#define WITH_DR(ddct_dref, action) \
   do { \
      if (!library_initialized) \
         return DDCL_UNINITIALIZED; \
      DDCA_Status rc = 0; \
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
      DDCA_Status rc = 0; \
      Display_Handle * dh = (Display_Handle *) _ddct_dh_; \
      if (dh == NULL || memcmp(dh->marker, DISPLAY_HANDLE_MARKER, 4) != 0 )  { \
         rc = DDCL_ARG; \
      } \
      else { \
         (_action_); \
      } \
      return rc; \
   } while(0);


//
// Build information
//

/*  Returns the ddcutil version as a string in the form "major.minor.micro".
 */
const char * ddca_ddcutil_version_string() {
   return BUILD_VERSION;
}


/* Indicates whether the ddcutil library was built with ADL support. .
 */
bool ddca_built_with_adl() {
   return adlshim_is_available();
}

/* Indicates whether the ddcutil library was built with support for USB connected monitors. .
 */
bool ddca_built_with_usb() {
#ifdef USE_USB
   return true;
#else
   return false;
#endif
}

// Alternative to individual ddca_built_with...() functions.
// conciseness vs documentatbility
// how to document bits?   should doxygen doc be in header instead?

/** Queries ddcutil library build options.
 *
 */
unsigned long ddca_get_build_options() {
   Byte result = 0x00;
#ifdef HAVE_ADL
   result |= DDCA_BUILT_WITH_ADL;
#endif
#ifdef USE_USB
         result |= DDCA_BUILT_WITH_USB;
#endif
#ifdef FAILSIM_ENABLED
         result |= DDCA_BUILT_WITH_FAILSIM;
#endif
   return result;
}


//
// Initialization
//

static bool library_initialized = false;

/* Initializes the ddcutil library module.
 *
 * It is not an error if this function is called more than once.
 */
void ddca_init() {
   // Note: Until init_msg_control() is called within init_base_services(),
   // FOUT is null, so DBGMSG() causes a segfault
   bool debug = true;
   if (!library_initialized) {
      init_base_services();
      init_ddc_services();
      show_recoverable_errors = false;
      library_initialized = true;
      DBGMSF(debug, "library initialization executed");
   }
   else {
      DBGMSF(debug, "library was already initialized");
   }
}


//
// Status Code Management
//

static Global_Status_Code ddca_to_global_status_code(DDCA_Status ddca_status) {
   return global_to_public_status_code(ddca_status);
}


static DDCA_Status global_to_ddca_status_code(Global_Status_Code gsc) {
   return global_to_public_status_code(gsc);
}


char * ddca_status_code_name(DDCA_Status status_code) {
   char * result = NULL;
   Global_Status_Code gsc = ddca_to_global_status_code(status_code);
   Status_Code_Info * code_info = find_global_status_code_info(gsc);
   if (code_info)
      result = code_info->name;
   return result;
}


char * ddca_status_code_desc(DDCA_Status status_code) {
   char * result = "unknown status code";
   Global_Status_Code gsc = ddca_to_global_status_code(status_code);
   Status_Code_Info * code_info = find_global_status_code_info(gsc);
   if (code_info)
   result = code_info->description;
   return result;
}


//
// Message Control
//

/* Redirects output that normally would go to STDOUT
 */
void ddca_set_fout(
      FILE * fout   /** where to write normal messages, if NULL suppress */
     )
{
   // DBGMSG("Starting. fout=%p", fout);
   if (!library_initialized)
      ddca_init();

   set_fout(fout);
}

void ddca_set_fout_to_default() {
   if (!library_initialized)
      ddca_init();
   set_fout_to_default();
}




/* Redirects output that normally would go to STDERR
 */
void ddca_set_ferr(
      FILE * ferr   /** where to write error messages, if NULL suppress */
      )
{
   if (!library_initialized)
      ddca_init();

   set_ferr(ferr);
}

void ddca_set_ferr_to_default() {
   if (!library_initialized)
      ddca_init();
   set_ferr_to_default();
}




DDCA_Output_Level ddca_get_output_level() {
   return get_output_level();
}

void ddca_set_output_level(
       DDCA_Output_Level newval
       )
{
      set_output_level(newval);
}

char * ddca_output_level_name(
      DDCA_Output_Level val
      )
{
   return output_level_name(val);
}

void ddca_enable_report_ddc_errors(bool onoff) {
   // global variable in core.c:
   show_recoverable_errors = onoff;
}

bool ddca_is_report_ddc_errors_enabled() {
   return show_recoverable_errors;
}


//
// Global Settings
//

// typedef enum{DDCT_WRITE_ONLY_TRIES, DDCT_WRITE_READ_TRIES, DDCT_MULTIPART_TRIES} DDCT_Retry_Type;

int  ddca_get_max_tries(DDCA_Retry_Type retry_type) {
   int result = 0;
   switch(retry_type) {
      case (DDCA_WRITE_ONLY_TRIES):
         result = ddc_get_max_write_only_exchange_tries();
      break;
   case (DDCA_WRITE_READ_TRIES):
      result = ddc_get_max_write_read_exchange_tries();
      break;
   case (DDCA_MULTI_PART_TRIES):
      result = ddc_get_max_multi_part_read_tries();
      break;
   }
   return result;
}


DDCA_Status ddca_set_max_tries(DDCA_Retry_Type retry_type, int max_tries) {
   DDCA_Status rc = 0;
   if (max_tries > MAX_MAX_TRIES)
      rc = DDCL_ARG;
   else {
      switch(retry_type) {
      case (DDCA_WRITE_ONLY_TRIES):
         ddc_set_max_write_only_exchange_tries(max_tries);
         break;
      case (DDCA_WRITE_READ_TRIES):
         ddc_set_max_write_read_exchange_tries(max_tries);
         break;
      case (DDCA_MULTI_PART_TRIES):
         ddc_set_max_multi_part_read_tries(max_tries);
         break;
      }
   }
   return rc;
}





//
// Display Identifiers
//

DDCA_Status ddca_create_dispno_display_identifier(int dispno, DDCA_Display_Identifier* pdid) {
   Display_Identifier* did = create_dispno_display_identifier(dispno);
   *pdid = did;
   return 0;
}

DDCA_Status ddca_create_busno_display_identifier(
               int busno,
               DDCA_Display_Identifier* pdid) {
   Display_Identifier* did = create_busno_display_identifier(busno);
   *pdid = did;
   return 0;
}

DDCA_Status ddca_create_adlno_display_identifier(
               int iAdapterIndex,
               int iDisplayIndex,
               DDCA_Display_Identifier* pdid) {
   Display_Identifier* did = create_adlno_display_identifier(iAdapterIndex, iDisplayIndex);
   *pdid = did;
   return 0;
}

DDCA_Status ddca_create_model_sn_display_identifier(
      const char* model_name,
      const char* serial_ascii,
      DDCA_Display_Identifier* pdid
     )
{
   *pdid = NULL;
   DDCA_Status rc = 0;
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
      *pdid = create_model_sn_display_identifier(model_name, serial_ascii);
   }
   return rc;
}

DDCA_Status ddca_create_edid_display_identifier(
               const Byte * edid,
               DDCA_Display_Identifier * pdid)    // 128 byte EDID
{
   *pdid = NULL;
   DDCA_Status rc = 0;
   if (edid == NULL) {
      rc = DDCL_ARG;
      *pdid = NULL;
   }
   else {
      *pdid = create_edid_display_identifier(edid);
   }
   return rc;
}

DDCA_Status ddca_create_usb_display_identifier(
               int bus,
               int device,
               DDCA_Display_Identifier* pdid) {
   Display_Identifier* did = create_usb_display_identifier(bus, device);
   *pdid = did;
   return 0;
}



DDCA_Status ddca_free_display_identifier(DDCA_Display_Identifier did) {
   DDCA_Status rc = 0;
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

DDCA_Status ddca_repr_display_identifier(DDCA_Display_Identifier ddct_did, char **repr) {
   DDCA_Status rc = 0;
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
            free(hs);
            break;
      }
      case(DISP_ID_DISPNO):
            snprintf(did_work_buf, 100,
                     "Display Id Type: %s, dispno=%d", did_type_name, pdid->dispno);
            break;
      case DISP_ID_USB:
            snprintf(did_work_buf, 100,
                     "Display Id Type: %s, dispno=%d", did_type_name, pdid->dispno);
            break;

      } // switch
      *repr = did_work_buf;
   }
   return rc;
}


//
// Display References
//

DDCA_Status ddca_create_display_ref(DDCA_Display_Identifier did, DDCT_Display_Ref* ddct_dref) {
   bool debug = false;
   if (!library_initialized)
      return DDCL_UNINITIALIZED;
   DDCA_Status rc = 0;
   Display_Identifier * pdid = (Display_Identifier *) did;
   if (pdid == NULL || memcmp(pdid->marker, DISPLAY_IDENTIFIER_MARKER, 4) != 0 )  {
     rc = DDCL_ARG;
   }
   else {
      Display_Ref* dref = get_display_ref_for_display_identifier(pdid, true /* emit_error_msg */);
      if (debug)
         DBGMSG("get_display_ref_for_display_identifier() returned %p", dref);
      if (dref)
         *ddct_dref = dref;
      else
         rc = DDCL_ARG;
   }
   return rc;
}


DDCA_Status ddca_free_display_ref(DDCT_Display_Ref ddct_dref) {
   WITH_DR(ddct_dref,
         {
         free_display_ref(dref);
         }
   );
}


// static char dref_work_buf[100];

DDCA_Status ddca_repr_display_ref(DDCT_Display_Ref ddct_dref, char** repr){
   DDCA_Status rc = 0;
   Display_Ref * dref = (Display_Ref *) ddct_dref;
   if (dref == NULL || memcmp(dref->marker, DISPLAY_REF_MARKER, 4) != 0 )  {
      rc = DDCL_ARG;
      *repr = "invalid display reference";
   }
   else {
#ifdef TOO_MUCH_WORK
      char * dref_type_name = mccs_io_mode_name(dref->ddc_io_mode);
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
      *repr = dref_short_name(dref);
   }
   return rc;
}

void        ddct_report_display_ref(DDCT_Display_Ref ddct_dref, int depth) {
   Display_Ref * dref = (Display_Ref *) ddct_dref;
   rpt_vstring(depth, "DDCT_Display_Ref at %p:", dref);
   report_display_ref(dref, depth+1);
}


DDCA_Status ddct_open_display(DDCT_Display_Ref ddct_dref, DDCT_Display_Handle * pdh) {
   if (!library_initialized)
      return DDCL_UNINITIALIZED;
   DDCA_Status rc = 0;
   *pdh = NULL;        // in case of error
   Display_Ref * dref = (Display_Ref *) ddct_dref;
   if (dref == NULL || memcmp(dref->marker, DISPLAY_REF_MARKER, 4) != 0 )  {
      rc = DDCL_ARG;
   }
   else {
     Display_Handle* dh = NULL;
     rc = ddc_open_display(dref,  CALLOPT_ERR_MSG, &dh);
     if (rc == 0)
        *pdh = dh;
     else
        rc = DDCL_ARG;     //  TEMP, need a proper status code
   }
   return rc;
}


DDCA_Status ddct_close_display(DDCT_Display_Handle ddct_dh) {
   if (!library_initialized)
      return DDCL_UNINITIALIZED;
   DDCA_Status rc = 0;
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

DDCA_Status ddct_repr_display_handle(DDCT_Display_Handle ddct_dh, char ** repr) {
   DDCA_Status rc = 0;
   Display_Handle * dh = (Display_Handle *) ddct_dh;
   if (dh == NULL || memcmp(dh->marker, DISPLAY_HANDLE_MARKER, 4) != 0 )  {
      rc = DDCL_ARG;
      *repr = "invalid display handle";
   }
   else {
      char * dh_type_name = mccs_io_mode_name(dh->io_mode);
      switch (dh->io_mode) {
      case(DISP_ID_BUSNO):
         snprintf(dh_work_buf, 100,
                  "Display Handle Type: %s, bus=/dev/i2c-%d",
                  dh_type_name, dh->busno);
         break;
      case(DISP_ID_ADL):
         snprintf(dh_work_buf, 100,
                  "Display Handle Type: %s, adlno=%d.%d",
                  dh_type_name, dh->iAdapterIndex, dh->iDisplayIndex);
         break;
      case USB_IO:
         snprintf(dh_work_buf, 100,
                  "Display Handle Type: %s, bus=%d, device=%d",
                  dh_type_name, dh->usb_bus, dh->usb_device);
         break;
      }
      *repr = dh_work_buf;
   }
   // DBGMSG("repr=%p, *repr=%p, dh_work_buf=%p", repr, *repr, dh_work_buf);
   // DBGMSG("dh_work_buf=|%s|", dh_work_buf);
   // DBGMSG("Returning rc=%d, *repr=%s", rc, *repr);
   return rc;
}


DDCA_Status ddct_get_mccs_version(DDCT_Display_Handle ddct_dh, DDCT_MCCS_Version_Spec* pspec) {
   if (!library_initialized)
      return DDCL_UNINITIALIZED;
   DDCA_Status rc = 0;
   Display_Handle * dh = (Display_Handle *) ddct_dh;
   if (dh == NULL || memcmp(dh->marker, DISPLAY_HANDLE_MARKER, 4) != 0 )  {
      rc = DDCL_ARG;
      pspec->major = 0;
      pspec->minor = 0;
   }
   else {
      // need to call function, may not yet be set
      Version_Spec vspec = get_vcp_version_by_display_handle(dh);
      pspec->major = vspec.major;
      pspec->minor = vspec.minor;
      rc = 0;
   }
   return rc;
}


DDCA_Status ddct_get_edid_by_display_ref(DDCT_Display_Ref ddct_dref, Byte** pbytes) {
   if (!library_initialized)
      return DDCL_UNINITIALIZED;
   DDCA_Status rc = 0;
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




static DDCT_MCCS_Version_Spec mccs_version_id_to_spec(DDCA_MCCS_Version_Id id) {
   Version_Spec vspec = VCP_SPEC_ANY;
   // use table instead?
   switch(id) {
   case DDCA_VANY:   vspec = VCP_SPEC_ANY;    break;
   case DDCA_V10:    vspec = VCP_SPEC_V10;    break;
   case DDCA_V20:    vspec = VCP_SPEC_V20;    break;
   case DDCA_V21:    vspec = VCP_SPEC_V21;    break;
   case DDCA_V30:    vspec = VCP_SPEC_V30;    break;
   case DDCA_V22:    vspec = VCP_SPEC_V22;    break;
   }
   DDCT_MCCS_Version_Spec converted;
   converted.major = vspec.major;
   converted.minor = vspec.minor;

   return converted;
}


static DDCT_MCCS_Version_Spec version_spec_to_mccs_version_spec(Version_Spec vspec) {
   DDCT_MCCS_Version_Spec converted;

   converted.major = vspec.major;
   converted.minor = vspec.minor;
   return converted;
}

static DDCA_MCCS_Version_Id mccs_version_spec_to_id(DDCT_MCCS_Version_Spec vspec) {
   DDCA_MCCS_Version_Id result = DDCA_VUNK;    // initialize to avoid compiler warning

   if (vspec.major == 1 && vspec.minor == 0)
      result = DDCA_V10;
   else if (vspec.major == 2 && vspec.minor == 0)
      result = DDCA_V20;
   else if (vspec.major == 2 && vspec.minor == 1)
      result = DDCA_V21;
   else if (vspec.major == 3 && vspec.minor == 0)
      result = DDCA_V30;
   else if (vspec.major == 2 && vspec.minor == 2)
      result = DDCA_V22;
   else if (vspec.major == 2 && vspec.minor == 1)
      result = DDCA_V21;
   else if (vspec.major == 0 && vspec.minor == 0)
      result = DDCA_VUNK;
   // case UNQUERIED should never arise
   else
      PROGRAM_LOGIC_ERROR("Unexpected version spec value %d.%d", vspec.major, vspec.minor);


   return result;
}




// or return a struct?
DDCA_Status ddca_get_feature_info_by_vcp_version(
      VCP_Feature_Code        feature_code,
      DDCA_MCCS_Version_Id    mccs_version_id,
      unsigned long *         flags)
{
   DDCA_Status rc = 0;
   DDCT_MCCS_Version_Spec vspec = mccs_version_id_to_spec(mccs_version_id);

   VCP_Feature_Table_Entry * pentry = vcp_find_feature_by_hexid(feature_code);
   if (!pentry) {
      *flags = 0;
      rc = DDCL_ARG;
   }
   else {
      Version_Spec vspec2 = {vspec.major, vspec.minor};
      Version_Feature_Flags vflags = get_version_specific_feature_flags(pentry, vspec2);
      *flags = 0;
      // TODO handle subvariants REWORK
      if (vflags & VCP2_RO)
         *flags |= DDCA_RO;
      if (vflags & VCP2_WO)
         *flags |= DDCA_WO;
      if (vflags & VCP2_RW)
         *flags |= DDCA_RW;
      if (vflags & VCP2_CONT)
         *flags |= DDCA_CONTINUOUS;
#ifdef OLD
      if (pentry->flags & VCP_TYPE_V2NC_V3T) {
         if (vspec.major < 3)
            *flags |= DDCA_SIMPLE_NC;
         else
            *flags |= DDCA_TABLE;
      }
#endif
      else if (vflags & VCP2_TABLE)
         *flags |= DDCA_TABLE;
      else if (vflags & VCP2_NC) {
         if (vspec.major < 3)
            *flags |= DDCA_SIMPLE_NC;
         else {
            // TODO: In V3, some features use combination of high and low bytes
            // for now, mark all as simple
            *flags |= DDCA_SIMPLE_NC;
            // alt: DDCT_COMPLEX_NC
         }
      }
   }
   return rc;
}





// or return a struct?
DDCA_Status ddca_get_feature_info_by_display(
      DDCT_Display_Handle ddct_dh,    // needed because in rare cases feature info is MCCS version dependent
      VCP_Feature_Code    feature_code,
      unsigned long *     pflags)
{
   WITH_DH(
      ddct_dh,
      {
         Version_Spec vspec = get_vcp_version_by_display_handle(ddct_dh);
         // DDCT_MCCS_Version_Spec vspec2;           // = {vspec.major, vspec.minor};
         // vspec2.major = vspec.major;
         // vspec2.minor = vspec.minor;
         DDCT_MCCS_Version_Spec vspec2     = version_spec_to_mccs_version_spec(vspec);
         DDCA_MCCS_Version_Id   version_id = mccs_version_spec_to_id(vspec2);

         rc = ddca_get_feature_info_by_vcp_version(feature_code, version_id, pflags);
      }
   );
}



// static char  default_feature_name_buffer[40];
char * ddca_get_feature_name(VCP_Feature_Code feature_code) {
   // do we want get_feature_name()'s handling of mfg specific and unrecognized codes?
   char * result = get_feature_name_by_id_only(feature_code);
   // snprintf(default_feature_name_buffer, sizeof(default_feature_name_buffer), "VCP Feature 0x%02x", feature_code);
   // return default_feature_name_buffer;
   return result;
}


// Display Inquiry

int ddca_report_active_displays(int depth) {
   return ddc_report_active_displays(depth);
}



typedef void * Feature_Value_Table;   // temp

DDCA_Status ddct_get_feature_sl_value_table(
               DDCT_Display_Handle   ddct_dh,
               VCP_Feature_Code      feature_code,
               Feature_Value_Table * value_table)
{
   return DDCL_UNIMPLEMENTED;
}

// or:
DDCA_Status ddct_get_nc_feature_value_name(
               DDCT_Display_Handle ddct_dh,    // needed because value lookup mccs version dependent
               VCP_Feature_Code    feature_code,
               Byte                feature_value,
               char**              pfeature_name)
{
   WITH_DH(ddct_dh,  {
         // this should be a function in vcp_feature_codes:
         char * feature_name = NULL;
         Version_Spec vspec = dh->vcp_version;
         Feature_Value_Entry * feature_value_entries = find_feature_values(feature_code, vspec);
         if (feature_value_entries == NULL) {
            rc = DDCL_ARG;
         }
         else {
            feature_name = get_feature_value_name(feature_value_entries, feature_value);
            if (feature_name == NULL)
               rc = DDCL_ARG;               // correct handling for value not found?
            else
               *pfeature_name = feature_name;
         }
   }
   );
}

// n.b. filles in the response buffer provided by the caller, does not allocate
DDCA_Status ddct_get_nontable_vcp_value(
               DDCT_Display_Handle             ddct_dh,
               VCP_Feature_Code                feature_code,
               DDCT_Non_Table_Value_Response * response)
{
   WITH_DH(ddct_dh,  {
       Parsed_Nontable_Vcp_Response * code_info;
       Global_Status_Code gsc = get_nontable_vcp_value(dh, feature_code,&code_info);
       // DBGMSG(" get_nontable_vcp_value() returned %s", gsc_desc(gsc));
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


// untested
DDCA_Status ddct_get_table_vcp_value(
               DDCT_Display_Handle ddct_dh,
               VCP_Feature_Code    feature_code,
               int *               value_len,
               Byte**              value_bytes)
{
   WITH_DH(ddct_dh,
      {
         Buffer * p_table_bytes = NULL;
         rc =  get_table_vcp_value(dh, feature_code, &p_table_bytes);
         if (rc == 0) {
            assert(p_table_bytes);  // avoid coverity warning
            int len = p_table_bytes->len;
            *value_len = len;
            *value_bytes = malloc(len);
            memcpy(*value_bytes, p_table_bytes->bytes, len);
            buffer_free(p_table_bytes, __func__);
         }
      }
     );
}


DDCA_Status ddct_set_continuous_vcp_value(
               DDCT_Display_Handle ddct_dh,
               VCP_Feature_Code feature_code,
               int              new_value)
{
   WITH_DH(ddct_dh,  {
         Global_Status_Code gsc = set_nontable_vcp_value(dh, feature_code, new_value);
         rc = gsc;
      } );
}


DDCA_Status ddct_set_simple_nc_vcp_value(
               DDCT_Display_Handle  ddct_dh,
               VCP_Feature_Code     feature_code,
               Byte                 new_value)
{
   return ddct_set_continuous_vcp_value(ddct_dh, feature_code, new_value);
}


DDCA_Status ddct_set_raw_vcp_value(
               DDCT_Display_Handle  ddct_dh,
               VCP_Feature_Code     feature_code,
               Byte                 hi_byte,
               Byte                 lo_byte)
{
   return ddct_set_continuous_vcp_value(ddct_dh, feature_code, hi_byte << 8 | lo_byte);
}


/* Retrieves the capabilities string for the monitor.
 *
 * Arguments:
 *   ddct_dh     display handle
 *   pcaps       address at which to return pointer to capabilities string.
 *               This string is in an internal DDC data structure and should
 *               not be freed by the caller.
 *
 * Returns:
 *   status code
 */
DDCA_Status ddct_get_capabilities_string(DDCT_Display_Handle ddct_dh, char** pcaps)
{
   WITH_DH(ddct_dh,
      {
         Global_Status_Code gsc = get_capabilities_string(dh, pcaps);
         rc = gsc;
      }
   );
}


DDCA_Status ddct_get_profile_related_values(DDCT_Display_Handle ddct_dh, char** pprofile_values_string)
{
   WITH_DH(ddct_dh,
      {
         bool debug = false;
         set_output_level(OL_PROGRAM);  // not needed for _new() variant
         DBGMSF(debug, "Before dumpvcp_to_string_by_display_handle(), pprofile_values_string=%p, *pprofile_values_string=%p",
               pprofile_values_string, *pprofile_values_string);
         rc = dumpvcp_as_string(dh, pprofile_values_string);
         DBGMSF(debug, "After dumpvcp_to_string_by_display_handle(), pprofile_values_string=%p, *pprofile_values_string=%p",
               pprofile_values_string, *pprofile_values_string);
         DBGMSF(debug, "*pprofile_values_string = |%s|", *pprofile_values_string);
      }
   );
}


// TODO: handle display as optional argument
DDCA_Status ddct_set_profile_related_values(char * profile_values_string) {
   Global_Status_Code gsc = loadvcp_by_string(profile_values_string, NULL);
   return gsc;
}
