/* ddcutil_c_api.c
 *
 * <copyright>
 * Copyright (C) 2015-2017 Sanford Rockowitz <rockowitz@minsoft.com>
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
 * **ddcutil** C API implementation
 */

#include <config.h>

/** \cond */
#include <assert.h>
#include <errno.h>
#include <string.h>
/** \endcond */

#include "util/data_structures.h"
#include "util/report_util.h"
#include "util/string_util.h"
#include "util/udev_util.h"

#include "base/build_info.h"
#include "base/core.h"
#include "base/ddc_errno.h"
#include "base/ddc_packets.h"
#include "base/displays.h"
#include "base/base_init.h"
#include "base/execution_stats.h"
#include "base/parms.h"
#include "vcp/vcp_feature_codes.h"
#include "vcp/parse_capabilities.h"
#include "vcp/parsed_capabilities_feature.h"

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

#include "public/ddcutil_c_api.h"


#define WITH_DR(ddca_dref, action) \
   do { \
      if (!library_initialized) \
         return DDCL_UNINITIALIZED; \
      DDCA_Status psc = 0; \
      Display_Ref * dref = (Display_Ref *) ddca_dref; \
      if (dref == NULL || memcmp(dref->marker, DISPLAY_REF_MARKER, 4) != 0 )  { \
         psc = DDCL_ARG; \
      } \
      else { \
         (action); \
      } \
      return psc; \
   } while(0);


#define WITH_DH(_ddca_dh_, _action_) \
   do { \
      if (!library_initialized) \
         return DDCL_UNINITIALIZED; \
      DDCA_Status psc = 0; \
      Display_Handle * dh = (Display_Handle *) _ddca_dh_; \
      if ( !dh || memcmp(dh->marker, DISPLAY_HANDLE_MARKER, 4) != 0 )  { \
         psc = DDCL_ARG; \
      } \
      else { \
         (_action_); \
      } \
      return psc; \
   } while(0);



static inline bool valid_display_handle(Display_Handle * dh) {
   return (dh && memcmp(dh->marker, DISPLAY_HANDLE_MARKER, 4) == 0);
}

static inline bool valid_display_ref(Display_Ref * dref) {
   return (dref && memcmp(dref->marker, DISPLAY_REF_MARKER, 4) == 0);
}


//
// Library Build Information
//


DDCA_Ddcutil_Version_Spec ddca_ddcutil_version(void) {
   static DDCA_Ddcutil_Version_Spec vspec = {255,255,255};
   static bool vspec_init = false;

   if (!vspec_init) {
      int ct = sscanf(BUILD_VERSION, "%hhu.%hhu.%hhu", &vspec.major, &vspec.minor, &vspec.micro);
      assert(ct == 3);
      vspec_init = true;
   }
   // DBGMSG("Returning: %d.%d.%d", vspec.major, vspec.minor, vspec.micro);
   return vspec;

}


/*  Returns the ddcutil version as a string in the form "major.minor.micro".
 */
const char *
ddca_ddcutil_version_string() {
   return BUILD_VERSION;
}


/* Indicates whether the ddcutil library was built with ADL support. .
 */
bool
ddca_built_with_adl() {
#ifdef HAVE_ADL
   return true;
#else
   return false;
#endif

}

/* Indicates whether the ddcutil library was built with support for USB connected monitors. .
 */
bool
ddca_built_with_usb() {
#ifdef USE_USB
   return true;
#else
   return false;
#endif
}

/* Indicates whether ADL successfully initialized.
 * (e.g. fglrx driver not found)
 *
 * @return true/false
 */
bool
ddca_adl_is_available() {
   return adlshim_is_available();
}


// Alternative to individual ddca_built_with...() functions.
// conciseness vs documentatbility
// how to document bits?   should doxygen doc be in header instead?

uint8_t
ddca_get_build_options() {
   uint8_t result = 0x00;
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

/** Initializes the ddcutil library module.
 *
 * It is not an error if this function is called more than once.
 */
void __attribute__ ((constructor))
_ddca_init() {
   // Note: Until init_msg_control() is called within init_base_services(),
   // FOUT is null, so DBGMSG() causes a segfault
   bool debug = true;
   if (!library_initialized) {
      init_base_services();
      init_ddc_services();
      set_output_level(DDCA_OL_NORMAL);
      show_recoverable_errors = false;
      library_initialized = true;
      DBGMSF(debug, "library initialization executed");
   }
   else {
      DBGMSF(debug, "library was already initialized");
   }
}


#ifdef WRONG
/** Template for callback function registered with ddca_register_abort_func() */
typedef void (*DDCA_Abort_Func)(DDCA_Status psc);

static jmp_buf abort_buf;

static DDCA_Abort_Func  abort_func = NULL;

// PROBLEM: If abort_func() returns, some function gets 0 as it's return value,
// which causes unpredictable behavior


/** Register a function to be called when an internal abort occurs in libddcutil.
 *
 *  @param[in]   func callback function
 */
void ddca_register_abort_func(DDCA_Abort_Func func) {
   DBGMSG("func=%p", func);
   abort_func = func;

   int jmprc = setjmp(abort_buf);
   if (jmprc) {

      Public_Status_Code psc = global_to_public_status_code(jmprc);
      if (abort_func)
         abort_func(psc);
      fprintf(stderr, "Aborting. Internal status code = %d\n", jmprc);
      exit(EXIT_FAILURE);
   }
   DBGMSG("Calling register_jmp_buf()...");
   register_jmp_buf(&abort_buf);
}
#endif


void
ddca_register_jmp_buf(jmp_buf* jb) {
   register_jmp_buf(jb);
}


DDCA_Global_Failure_Information *
ddca_get_global_failure_information()
{
   // return NULL if !global_failure_information.info_set_fg, or always return pointer
   // i.e. is it better if caller checks for NULL or checks info_set_fg?
   // return &global_failure_information;
   return (global_failure_information.info_set_fg)
                  ? &global_failure_information
                  : NULL;
}


//
// Status Code Management
//

#ifdef OLD
static Global_Status_ Code
ddca_to_global_status_code(DDCA_Status ddca_status) {
   return global_to_public_status_code(ddca_status);
}


// should be static, but not currently used, if static get warning
DDCA_Status
global_to_ddca_status_code(Global_Status_ Code gsc) {
   return global_to_public_status_code(gsc);
}
#endif

char *
ddca_status_code_name(DDCA_Status status_code) {
   char * result = NULL;
   // Global_ Status_Code gsc = ddca_to_global_ status_code(status_code);
   // Status_Code_Info * code_info = find_global_status_code_info(gsc);
   Status_Code_Info * code_info = find_status_code_info(status_code);
   if (code_info)
      result = code_info->name;
   return result;
}


char *
ddca_status_code_desc(DDCA_Status status_code) {
   char * result = "unknown status code";
   // Global_ Status_Code gsc = ddca_to_global_status_code(status_code);
   // Status_Code_Info * code_info = find_global_status_code_info(gsc);
   Status_Code_Info * code_info = find_status_code_info(status_code);
   if (code_info)
      result = code_info->description;
   return result;
}


//
// Message Control
//

// Redirects output that normally would go to STDOUT
void
ddca_set_fout(FILE * fout) {
   // DBGMSG("Starting. fout=%p", fout);
   // if (!library_initialized)
   //    _ddca_init();

   set_fout(fout);
}


void
ddca_set_fout_to_default() {
   // if (!library_initialized)
   //    _ddca_init();
   set_fout_to_default();
}


// Redirects output that normally would go to STDERR
void ddca_set_ferr(FILE * ferr) {
   // if (!library_initialized)
   //    _ddca_init();

   set_ferr(ferr);
}


void ddca_set_ferr_to_default() {
   // if (!library_initialized)
   //    _ddca_init();
   set_ferr_to_default();
}


DDCA_Output_Level
ddca_get_output_level() {
   return get_output_level();
}

void
ddca_set_output_level(
       DDCA_Output_Level newval)
{
      set_output_level(newval);
}

char *
ddca_output_level_name(DDCA_Output_Level val) {
   return output_level_name(val);
}


void
ddca_enable_report_ddc_errors(bool onoff) {
   // global variable in core.c:
   show_recoverable_errors = onoff;
}

bool
ddca_is_report_ddc_errors_enabled() {
   return show_recoverable_errors;
}


//
// Global Settings
//

int
ddca_get_max_max_tries() {
   return MAX_MAX_TRIES;
}

int
ddca_get_max_tries(DDCA_Retry_Type retry_type) {
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


DDCA_Status
ddca_set_max_tries(
      DDCA_Retry_Type retry_type,
      int             max_tries)
{
   DDCA_Status rc = 0;
   if (max_tries < 1 || max_tries > MAX_MAX_TRIES)
      rc = -EINVAL;
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



void ddca_set_verify_setvcp(bool onoff) {
   set_verify_setvcp(onoff);
}

bool ddca_get_verify_setvcp() {
   return get_verify_setvcp();
}



// TODO: Add functions to access ddcutil's runtime error statistics


#ifdef FUTURE

/** Gets the I2C timeout in milliseconds for the specified timeout class.
 * @param timeout_type timeout type
 * @return timeout in milliseconds
 */
int
ddca_get_timeout_millis(
      DDCA_Timeout_Type timeout_type) {
   return 0;    // *** UNIMPLEMENTED ***
}

/** Sets the I2C timeout in milliseconds for the specified timeout class
 * @param timeout_type  timeout class
 * @param millisec      timeout to set, in milliseconds
 */
void
ddca_set_timeout_millis(
      DDCA_Timeout_Type timeout_type,
      int               millisec)
{
   // *** UNIMPLEMENTED
}
#endif


//
// Statistics
//

void ddca_reset_stats() {
   ddc_reset_stats_main();
}

// TODO: Functions that return stats in data structures
void ddca_show_stats(DDCA_Stats_Type stats_types, int depth) {
   ddc_report_stats_main( stats_types,    // stats to show
                          depth);         // logical indentation depth
}


//
// Display Identifiers
//

DDCA_Status
ddca_create_dispno_display_identifier(
      int                      dispno,
      DDCA_Display_Identifier* p_did)
{
   Display_Identifier* did = create_dispno_display_identifier(dispno);
   *p_did = did;
   return 0;
}


DDCA_Status
ddca_create_busno_display_identifier(
      int busno,
      DDCA_Display_Identifier* p_did)
{
   Display_Identifier* did = create_busno_display_identifier(busno);
   *p_did = did;
   return 0;
}


DDCA_Status
ddca_create_adlno_display_identifier(
      int                      iAdapterIndex,
      int                      iDisplayIndex,
      DDCA_Display_Identifier* p_did)
{
   Display_Identifier* did = create_adlno_display_identifier(iAdapterIndex, iDisplayIndex);
   *p_did = did;
   return 0;
}


DDCA_Status
ddca_create_mfg_model_sn_display_identifier(
      const char*              mfg_id,
      const char*              model_name,
      const char*              serial_ascii,
      DDCA_Display_Identifier* p_did
     )
{
   *p_did = NULL;
   DDCA_Status rc = 0;
   // break up the invalid argument tests for clarity
   // at least 1 argument must be specified
   if (  ( !mfg_id       || strlen(mfg_id)       == 0)  &&
         ( !model_name   || strlen(model_name)   == 0)  &&
         ( !serial_ascii || strlen(serial_ascii) == 0)
      )
      rc = DDCL_ARG;

   // check that no arguments are too long
   if ( (model_name   && strlen(model_name)   >= EDID_MODEL_NAME_FIELD_SIZE)  ||
        (mfg_id       && strlen(mfg_id)       >= EDID_MFG_ID_FIELD_SIZE)      ||
        (serial_ascii && strlen(serial_ascii) >= EDID_SERIAL_ASCII_FIELD_SIZE)
      )
      rc = DDCL_ARG;

   else {
      *p_did = create_mfg_model_sn_display_identifier(
                     mfg_id, model_name, serial_ascii);
   }
   return rc;
}


DDCA_Status
ddca_create_edid_display_identifier(
      const Byte *              edid,
      DDCA_Display_Identifier * p_did)    // 128 byte EDID
{
   *p_did = NULL;
   DDCA_Status rc = 0;
   if (edid == NULL) {
      rc = DDCL_ARG;
      *p_did = NULL;
   }
   else {
      *p_did = create_edid_display_identifier(edid);
   }
   return rc;
}


DDCA_Status
ddca_create_usb_display_identifier(
      int                      bus,
      int                      device,
      DDCA_Display_Identifier* p_did)
{
   Display_Identifier* did = create_usb_display_identifier(bus, device);
   *p_did = did;
   return 0;
}


DDCA_Status
ddca_create_usb_hiddev_display_identifier(
      int                      hiddev_devno,
      DDCA_Display_Identifier* p_did)
{
   Display_Identifier* did = create_usb_hiddev_display_identifier(hiddev_devno);
   *p_did = did;
   return 0;
}



DDCA_Status
ddca_free_display_identifier(
      DDCA_Display_Identifier did)
{
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

char *
ddca_repr_display_identifier(DDCA_Display_Identifier ddca_did) {
   char * result = NULL;
   Display_Identifier * pdid = (Display_Identifier *) ddca_did;
   if (pdid != NULL && memcmp(pdid->marker, DISPLAY_IDENTIFIER_MARKER, 4) == 0 )  {
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
                     "Display Id Type: %s, mfg=%s, model=%s, sn=%s",
                     did_type_name, pdid->mfg_id, pdid->model_name, pdid->serial_ascii);
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
                     "Display Id Type: %s, usb bus:device=%d.%d", did_type_name, pdid->usb_bus, pdid->usb_device);;
            break;
      case DISP_ID_HIDDEV:
            snprintf(did_work_buf, 100,
                     "Display Id Type: %s, hiddev_devno=%d", did_type_name, pdid->hiddev_devno);
            break;

      } // switch
      result = did_work_buf;
   }
   return result;
}


//
// Display References
//

DDCA_Status ddca_get_display_ref(DDCA_Display_Identifier did, DDCA_Display_Ref* ddca_dref) {
   bool debug = false;
   if (!library_initialized)
      return DDCL_UNINITIALIZED;

   ddc_ensure_displays_detected();

   DDCA_Status rc = 0;
   Display_Identifier * pdid = (Display_Identifier *) did;
   if (pdid == NULL || memcmp(pdid->marker, DISPLAY_IDENTIFIER_MARKER, 4) != 0 )  {
     rc = DDCL_ARG;
   }
   else {
      Display_Ref* dref = get_display_ref_for_display_identifier(pdid, CALLOPT_ERR_MSG);
      if (debug)
         DBGMSG("get_display_ref_for_display_identifier() returned %p", dref);
      if (dref)
         *ddca_dref = dref;
      else
         rc = DDCL_ARG;
   }
   return rc;
}


DDCA_Status ddca_free_display_ref(DDCA_Display_Ref ddca_dref) {
   WITH_DR(ddca_dref,
         {
            if (dref->flags & DREF_TRANSIENT)
               free_display_ref(dref);
         }
   );
}


// static char dref_work_buf[100];

char *
ddca_repr_display_ref(DDCA_Display_Ref ddca_dref){
   char * result = NULL;
   Display_Ref * dref = (Display_Ref *) ddca_dref;
   if (dref != NULL && memcmp(dref->marker, DISPLAY_REF_MARKER, 4) == 0 )  {
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
      result = dref_short_name(dref);
   }
   return result;
}

void
ddca_report_display_ref(
      DDCA_Display_Ref ddca_dref,
      int              depth)
{
   Display_Ref * dref = (Display_Ref *) ddca_dref;
   rpt_vstring(depth, "DDCT_Display_Ref at %p:", dref);
   report_display_ref(dref, depth+1);
}


DDCA_Status
ddca_open_display(
      DDCA_Display_Ref      ddca_dref,
      DDCA_Display_Handle * p_dh)
{
   if (!library_initialized)
      return DDCL_UNINITIALIZED;

   ddc_ensure_displays_detected();

   DDCA_Status rc = 0;
   *p_dh = NULL;        // in case of error
   Display_Ref * dref = (Display_Ref *) ddca_dref;
   if (dref == NULL || memcmp(dref->marker, DISPLAY_REF_MARKER, 4) != 0 )  {
      rc = DDCL_ARG;
   }
   else {
     Display_Handle* dh = NULL;
     rc = ddc_open_display(dref,  CALLOPT_ERR_MSG, &dh);
     if (rc == 0)
        *p_dh = dh;
     else
        rc = DDCL_ARG;     //  TEMP, need a proper status code
   }
   return rc;
}


DDCA_Status
ddca_close_display(DDCA_Display_Handle ddca_dh) {
   if (!library_initialized)
      return DDCL_UNINITIALIZED;
   DDCA_Status rc = 0;
   Display_Handle * dh = (Display_Handle *) ddca_dh;
   // if (dh == NULL || memcmp(dh->marker, DISPLAY_HANDLE_MARKER, 4) != 0 )  {
   if (!valid_display_handle(dh)) {
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

char *
ddca_repr_display_handle(DDCA_Display_Handle ddca_dh) {
   char * repr = NULL;
   Display_Handle * dh = (Display_Handle *) ddca_dh;
   // if (dh == NULL || memcmp(dh->marker, DISPLAY_HANDLE_MARKER, 4) != 0 )  {
   if (!valid_display_handle(dh)) {
      repr = NULL;
   }
   else {
      char * dh_type_name = mccs_io_mode_name(dh->dref->io_mode);
      switch (dh->dref->io_mode) {
      case(DISP_ID_BUSNO):
         snprintf(dh_work_buf, 100,
                  "Display Handle Type: %s, bus=/dev/i2c-%d",
                  dh_type_name, dh->dref->busno);
         break;
      case(DISP_ID_ADL):
         snprintf(dh_work_buf, 100,
                  "Display Handle Type: %s, adlno=%d.%d",
                  dh_type_name, dh->dref->iAdapterIndex, dh->dref->iDisplayIndex);
         break;
      case DDCA_IO_USB:
         snprintf(dh_work_buf, 100,
                  "Display Handle Type: %s, %s/hiddev%d, usb bus:device=%d:%d]",
                  dh_type_name,
                  hiddev_directory(), dh->dref->usb_hiddev_devno,
                  dh->dref->usb_bus, dh->dref->usb_device
                  );
         break;
      }
      repr = dh_work_buf;
   }
   // DBGMSG("repr=%p, *repr=%p, dh_work_buf=%p", repr, *repr, dh_work_buf);
   // DBGMSG("dh_work_buf=|%s|", dh_work_buf);
   // DBGMSG("Returning rc=%d, *repr=%s", rc, *repr);
   return repr;
}


DDCA_Status
ddca_get_mccs_version(
      DDCA_Display_Handle     ddca_dh,
      DDCA_MCCS_Version_Spec* p_spec)
{
   if (!library_initialized)
      return DDCL_UNINITIALIZED;
   DDCA_Status rc = 0;
   Display_Handle * dh = (Display_Handle *) ddca_dh;
   if (dh == NULL || memcmp(dh->marker, DISPLAY_HANDLE_MARKER, 4) != 0 )  {
      rc = DDCL_ARG;
      p_spec->major = 0;
      p_spec->minor = 0;
   }
   else {
      // need to call function, may not yet be set
      DDCA_MCCS_Version_Spec vspec = get_vcp_version_by_display_handle(dh);
      p_spec->major = vspec.major;
      p_spec->minor = vspec.minor;
      rc = 0;
   }
   return rc;
}


DDCA_Status
ddca_get_mccs_version_id(
      DDCA_Display_Handle    ddca_dh,
      DDCA_MCCS_Version_Id*  p_id)
{
   DDCA_MCCS_Version_Spec vspec;
   DDCA_Status rc = ddca_get_mccs_version(ddca_dh, &vspec);
   if (rc == 0) {
      DDCA_MCCS_Version_Id  version_id = mccs_version_spec_to_id(vspec);
      *p_id = version_id;
   }
   else {
      *p_id = DDCA_VNONE;
   }
   return rc;
}


char *
ddca_mccs_version_id_name(DDCA_MCCS_Version_Id version_id) {
   return vcp_version_id_name(version_id);
}

char *
ddca_mccs_version_id_desc(DDCA_MCCS_Version_Id version_id) {
   return format_vcp_version_id(version_id);
}

#ifdef OLD
// or should this return status code?
DDCA_Display_Info_List *
ddca_get_displays_old()
{
   ddc_ensure_displays_detected();

   // PROGRAM_LOGIC_ERROR("---> pseudo failure");
   Display_Info_List * info_list = ddc_get_valid_displays();
   int true_ct = 0;         // number of valid displays
   for (int ndx = 0; ndx < info_list->ct; ndx++) {
      Display_Info drec = info_list->info_recs[ndx];
      if (drec.dispno != -1)    // ignore invalid displays
         true_ct++;
   }

   int reqd_size =   offsetof(DDCA_Display_Info_List,info) + true_ct * sizeof(DDCA_Display_Info);
   DDCA_Display_Info_List * result_list = calloc(1,reqd_size);
   result_list->ct = true_ct;
   // DBGMSG("sizeof(DDCA_Display_Info) = %d, sizeof(Display_Info_List) = %d, reqd_size=%d, true_ct=%d, offsetof(DDCA_Display_Info_List,info) = %d",
   //       sizeof(DDCA_Display_Info), sizeof(DDCA_Display_Info_List), reqd_size, true_ct, offsetof(DDCA_Display_Info_List,info));

   int true_ctr = 0;
   for (int ndx = 0; ndx < info_list->ct; ndx++) {
      Display_Info drec = info_list->info_recs[ndx];
      if (drec.dispno != -1) {
         DDCA_Display_Info * curinfo = &result_list->info[true_ctr++];
         memcpy(curinfo->marker, DDCA_DISPLAY_INFO_MARKER, 4);
         curinfo->dispno        = drec.dispno;
         Display_Ref * dref     = drec.dref;
#ifdef OLD
         curinfo->io_mode       = dref->io_mode;
         curinfo->i2c_busno     = dref->busno;
         curinfo->iAdapterIndex = dref->iAdapterIndex;
         curinfo->iDisplayIndex = dref->iDisplayIndex;
         curinfo->usb_bus       = dref->usb_bus;
         curinfo->usb_device    = dref->usb_device;
#endif
         curinfo->loc.io_mode = dref->io_mode;
         switch (dref->io_mode) {
         case DDCA_IO_DEVI2C:
            curinfo->loc.i2c_busno = dref->busno;
            break;
         case DDCA_IO_ADL:
            curinfo->loc.adlno.iAdapterIndex = dref->iAdapterIndex;
            curinfo->loc.adlno.iDisplayIndex = dref->iDisplayIndex;
            break;
         case DDCA_IO_USB:
            curinfo->usb_bus    = dref->usb_bus;
            curinfo->usb_device = dref->usb_device;
            curinfo->loc.hiddev_devno = dref->usb_hiddev_devno;
            break;
         }
         curinfo->edid_bytes    = drec.edid->bytes;
         // or should these be memcpy'd instead of just pointers, can edid go away?
         curinfo->mfg_id        = drec.edid->mfg_id;
         curinfo->model_name    = drec.edid->model_name;
         curinfo->sn            = drec.edid->serial_ascii;
         curinfo->ddca_dref     = dref;
      }
   }
   free_display_info_list(info_list);

   // DBGMSG("Returning %p", result_list);
   return result_list;
}
#endif


// or should this return status code?
DDCA_Display_Info_List *
ddca_get_displays()
{
   ddc_ensure_displays_detected();

   GPtrArray * all_displays = ddc_get_all_displays();

   int true_ct = 0;         // number of valid displays
   for (int ndx = 0; ndx < all_displays->len; ndx++) {
      Display_Ref * dref = g_ptr_array_index(all_displays, ndx);
      if (dref->dispno != -1)    // ignore invalid displays
         true_ct++;
   }

   int reqd_size =   offsetof(DDCA_Display_Info_List,info) + true_ct * sizeof(DDCA_Display_Info);
   DDCA_Display_Info_List * result_list = calloc(1,reqd_size);
   result_list->ct = true_ct;
   // DBGMSG("sizeof(DDCA_Display_Info) = %d, sizeof(Display_Info_List) = %d, reqd_size=%d, true_ct=%d, offsetof(DDCA_Display_Info_List,info) = %d",
   //       sizeof(DDCA_Display_Info), sizeof(DDCA_Display_Info_List), reqd_size, true_ct, offsetof(DDCA_Display_Info_List,info));

   int true_ctr = 0;
   for (int ndx = 0; ndx < all_displays->len; ndx++) {
      Display_Ref * dref = g_ptr_array_index(all_displays, ndx);
      if (dref->dispno != -1) {
         DDCA_Display_Info * curinfo = &result_list->info[true_ctr++];
         memcpy(curinfo->marker, DDCA_DISPLAY_INFO_MARKER, 4);
         curinfo->dispno        = dref->dispno;
         curinfo->loc.io_mode = dref->io_mode;
         switch (dref->io_mode) {
         case DDCA_IO_DEVI2C:
            curinfo->loc.i2c_busno = dref->busno;
            break;
         case DDCA_IO_ADL:
            curinfo->loc.adlno.iAdapterIndex = dref->iAdapterIndex;
            curinfo->loc.adlno.iDisplayIndex = dref->iDisplayIndex;
            break;
         case DDCA_IO_USB:
            curinfo->usb_bus    = dref->usb_bus;
            curinfo->usb_device = dref->usb_device;
            curinfo->loc.hiddev_devno = dref->usb_hiddev_devno;
            break;
         }
         curinfo->edid_bytes    = dref->pedid->bytes;
         // or should these be memcpy'd instead of just pointers, can edid go away?
         curinfo->mfg_id        = dref->pedid->mfg_id;
         curinfo->model_name    = dref->pedid->model_name;
         curinfo->sn            = dref->pedid->serial_ascii;
         curinfo->ddca_dref     = dref;
      }
   }

   // DBGMSG("Returning %p", result_list);
   return result_list;
}


void
ddca_report_display_info(
      DDCA_Display_Info * dinfo,
      int                 depth)
{
   assert(dinfo);
   assert(memcmp(dinfo->marker, DDCA_DISPLAY_INFO_MARKER, 4) == 0);
   int d0 = depth;
   int d1 = depth+1;
   rpt_vstring(d0, "Display number:  %d", dinfo->dispno);
#ifdef OLD
   rpt_vstring(d1, "IO mode:         %s", mccs_io_mode_name(dinfo->io_mode));
   switch(dinfo->io_mode) {
   case (DDCA_IO_DEVI2C):
         rpt_vstring(d1, "I2C bus number:     %d", dinfo->i2c_busno);
         break;
   case (DDCA_IO_ADL):
         rpt_vstring(d1, "ADL adapter.display:  %d.%d", dinfo->iAdapterIndex, dinfo->iDisplayIndex);
         break;
   case (DDCA_IO_USB):
         rpt_vstring(d1, "USB bus.device:       %d.%d", dinfo->usb_bus, dinfo->usb_device);
         break;
   }
#endif

   rpt_vstring(d1, "IO mode:         %s", mccs_io_mode_name(dinfo->loc.io_mode));
   switch(dinfo->loc.io_mode) {
   case (DDCA_IO_DEVI2C):
         rpt_vstring(d1, "I2C bus number:     %d", dinfo->loc.i2c_busno);
         break;
   case (DDCA_IO_ADL):
         rpt_vstring(d1, "ADL adapter.display:  %d.%d",
                         dinfo->loc.adlno.iAdapterIndex, dinfo->loc.adlno.iDisplayIndex);
         break;
   case (DDCA_IO_USB):
         rpt_vstring(d1, "USB bus.device:       %d.%d",
                         dinfo->usb_bus, dinfo->usb_device);
         rpt_vstring(d1, "USB hiddev number:    %d", dinfo->loc.hiddev_devno);
         break;
   }

   char * edidstr = hexstring(dinfo->edid_bytes, 128);
   rpt_vstring(d1, "Mfg Id:         %s", dinfo->mfg_id);
   rpt_vstring(d1, "Model:          %s", dinfo->model_name);
   rpt_vstring(d1, "Serial number:  %s", dinfo->sn);
   rpt_vstring(d1, "EDID:           %s", edidstr);
   rpt_vstring(d1, "ddca_dref:      %p", dinfo->ddca_dref);
   free(edidstr);
}


void
ddca_report_display_info_list(
      DDCA_Display_Info_List * dlist,
      int                      depth)
{
   int d1 = depth+1;
   rpt_vstring(depth, "Found %d displays", dlist->ct);
   for (int ndx=0; ndx<dlist->ct; ndx++) {
      ddca_report_display_info(&dlist->info[ndx], d1);
   }
}


DDCA_Status
ddca_get_edid_by_display_ref(
      DDCA_Display_Ref ddca_dref,
      uint8_t**        p_bytes)
{
   DDCA_Status rc = 0;
   *p_bytes = NULL;

   if (!library_initialized) {
      rc = DDCL_UNINITIALIZED;
      goto bye;
   }

   Display_Ref * dref = (Display_Ref *) ddca_dref;
   // if (dref == NULL || memcmp(dref->marker, DISPLAY_REF_MARKER, 4) != 0 )  {
   if ( !valid_display_ref(dref) )  {
      rc = DDCL_ARG;
      goto bye;
   }

   Parsed_Edid*  edid = ddc_get_parsed_edid_by_display_ref(dref);
   *p_bytes = edid->bytes;

bye:
   return rc;
}


#ifdef OLD
// or return a struct?
DDCA_Status ddca_get_feature_flags_by_vcp_version(
      DDCA_Vcp_Feature_Code         feature_code,
      DDCA_MCCS_Version_Id          mccs_version_id,
      DDCA_Version_Feature_Flags *  flags)
{
   DDCA_Status rc = 0;
   DDCA_MCCS_Version_Spec vspec = mccs_version_id_to_spec(mccs_version_id);

   VCP_Feature_Table_Entry * pentry = vcp_find_feature_by_hexid(feature_code);
   if (!pentry) {
      *flags = 0;
      rc = DDCL_ARG;
   }
   else {
      DDCA_Version_Feature_Flags vflags = get_version_specific_feature_flags(pentry, vspec);
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
      else if (vflags & DDCA_TABLE)
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
#endif


DDCA_Status
ddca_get_feature_info_by_vcp_version(
      DDCA_Vcp_Feature_Code       feature_code,
   // DDCT_MCCS_Version_Spec vspec,
      DDCA_MCCS_Version_Id   mccs_version_id,
      DDCA_Version_Feature_Info** p_info)
{
   bool debug = false;
   DBGMSF(debug, "Starting. feature_code=0x%02x, mccs_version_id=%d", feature_code, mccs_version_id);

   DDCA_Status psc = 0;
   *p_info = NULL;
   // DDCA_MCCS_Version_Spec vspec = mccs_version_id_to_spec(mccs_version_id);

   // or should this be a version sensitive call?
   DDCA_Version_Feature_Info * info =  get_version_feature_info(
         feature_code,
         mccs_version_id,
         false,                       // with_default
         true);                       // false => version specific, true=> version sensitive
   if (!info)
      psc = DDCL_ARG;
   else
      *p_info = info;

   DBGMSF(debug, "Returning:%d, *p_info=%p", psc, *p_info);
   return psc;

}


DDCA_Status
ddca_get_feature_info_by_display(
      DDCA_Display_Handle      ddca_dh,    // needed because in rare cases feature info is MCCS version dependent
      DDCA_Vcp_Feature_Code         feature_code,
      DDCA_Version_Feature_Info **  p_info)
{
   WITH_DH(
      ddca_dh,
      {
         DDCA_MCCS_Version_Spec vspec = get_vcp_version_by_display_handle(ddca_dh);
         // DDCT_MCCS_Version_Spec vspec2;           // = {vspec.major, vspec.minor};
         // vspec2.major = vspec.major;
         // vspec2.minor = vspec.minor;
         DDCA_MCCS_Version_Id   version_id = mccs_version_spec_to_id(vspec);

         psc = ddca_get_feature_info_by_vcp_version(feature_code, version_id, p_info);
      }
   );
}


// static char  default_feature_name_buffer[40];
char *
ddca_get_feature_name(DDCA_Vcp_Feature_Code feature_code) {
   // do we want get_feature_name()'s handling of mfg specific and unrecognized codes?
   char * result = get_feature_name_by_id_only(feature_code);
   // snprintf(default_feature_name_buffer, sizeof(default_feature_name_buffer), "VCP Feature 0x%02x", feature_code);
   // return default_feature_name_buffer;
   return result;
}


//
// Display Inquiry
//


DDCA_Status
ddca_get_simple_sl_value_table(
      DDCA_Vcp_Feature_Code           feature_code,
      DDCA_MCCS_Version_Id       mccs_version_id,
      DDCA_Feature_Value_Entry** pvalue_table)
{
   DDCA_Status rc = 0;
   *pvalue_table = NULL;
   DDCA_MCCS_Version_Spec vspec = mccs_version_id_to_spec(mccs_version_id);

   VCP_Feature_Table_Entry * pentry = vcp_find_feature_by_hexid(feature_code);
   if (!pentry) {
        *pvalue_table = NULL;
        rc = DDCL_ARG;
  }
  else {
     DDCA_MCCS_Version_Spec vspec2 = {vspec.major, vspec.minor};
     DDCA_Version_Feature_Flags vflags = get_version_specific_feature_flags(pentry, vspec2);
     if (!(vflags & DDCA_SIMPLE_NC)) {
        *pvalue_table = NULL;
        rc = DDCL_ARG;    // need better code
     }
     else  {
        DDCA_Feature_Value_Entry * table = get_version_specific_sl_values(pentry, vspec2);
        DDCA_Feature_Value_Entry * table2 = (DDCA_Feature_Value_Entry*) table;    // identical definitions
        *pvalue_table = table2;
        rc = 0;
     }
  }
   return rc;
}



// typedef void * Feature_Value_Table;   // temp



// or:
DDCA_Status
ddct_get_nc_feature_value_name(
      DDCA_Display_Handle  ddct_dh,    // needed because value lookup mccs version dependent
      DDCA_Vcp_Feature_Code     feature_code,
      Byte                 feature_value,
      char**               p_feature_name)
{
   WITH_DH(ddct_dh,  {
         // this should be a function in vcp_feature_codes:
         char * feature_name = NULL;
         DDCA_MCCS_Version_Spec vspec = dh->dref->vcp_version;
         DDCA_Feature_Value_Entry * feature_value_entries = find_feature_values(feature_code, vspec);
         if (feature_value_entries == NULL) {
            psc = DDCL_ARG;
         }
         else {
            feature_name = get_feature_value_name(feature_value_entries, feature_value);
            if (feature_name == NULL)
               psc = DDCL_ARG;               // correct handling for value not found?
            else
               *p_feature_name = feature_name;
         }
   }
   );
}


/** Gets the value of a non-table VCP feature.
 *
 *  \param ddca_dh      handle of open display
 *  \param feature_code VCP feature code
 *  \param response     pointer to existing #DDCA_Non_Table_Value_Response that is filled in
 *  \return             status code
 */
DDCA_Status
ddca_get_nontable_vcp_value(
      DDCA_Display_Handle             ddca_dh,
      DDCA_Vcp_Feature_Code           feature_code,
      DDCA_Non_Table_Value_Response * response)
{
   WITH_DH(ddca_dh,  {
       Parsed_Nontable_Vcp_Response * code_info;
       psc = get_nontable_vcp_value(
                dh,
                feature_code,
                &code_info);
       // DBGMSG(" get_nontable_vcp_value() returned %s", gsc_desc(gsc));
       if (psc == 0) {
          response->feature_code = code_info->vcp_code;
          // response->cur_value = code_info->cur_value;
          // response->max_value = code_info->max_value;
          response->nc.mh        = code_info->mh;
          response->nc.ml        = code_info->ml;
          response->nc.sh        = code_info->sh;
          response->nc.sl        = code_info->sl;
          free(code_info);
       }
       // else psc = global_to_public_status_code(gsc);
    } );
}


// Partial code for getting formatted value
//
//Parsed_Vcp_Response pvr;
//pvr.response_type = NON_TABLE_VCP_VALUE;
//pvr.non_table_response = response;
//
//Single_Vcp_Value * valrec =
// create_single_vcp_value_by_parsed_vcp_response(
//       feature_code,
//       *pvr);
//
//bool ok = vcp_format_feature_detail(
//       VCP_Feature_Table_Entry * vcp_entry,
//       DDCA_MCCS_Version_Spec              vcp_version,
//       Single_Vcp_Value *        valrec,
//#ifdef OLD
//       Parsed_Vcp_Response *     raw_data,
//#endif
//       char * *                  aformatted_data
//     )
//



// untested
DDCA_Status
ddca_get_table_vcp_value(
      DDCA_Display_Handle ddca_dh,
      DDCA_Vcp_Feature_Code    feature_code,
      int *               value_len,
      Byte**              value_bytes)
{
   WITH_DH(ddca_dh,
      {
         Buffer * p_table_bytes = NULL;
         psc =  get_table_vcp_value(dh, feature_code, &p_table_bytes);
         if (psc == 0) {
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


// alt

DDCA_Status
ddca_get_vcp_value(
      DDCA_Display_Handle  ddca_dh,
      DDCA_Vcp_Feature_Code     feature_code,
      DDCA_Vcp_Value_Type       call_type,   // why is this needed?   look it up from dh and feature_code
      DDCA_Single_Vcp_Value **  pvalrec)
{


   WITH_DH(ddca_dh,
         {
               *pvalrec = NULL;
               psc = get_vcp_value(dh, feature_code, call_type, pvalrec);
               //psc = global_to_public_status_code(gsc);
         }
   );
}


DDCA_Status
ddca_get_formatted_vcp_value(
      DDCA_Display_Handle *   ddca_dh,
      DDCA_Vcp_Feature_Code        feature_code,
      char**                  p_formatted_value)
{
   WITH_DH(ddca_dh,
         {
               *p_formatted_value = NULL;
               DDCA_MCCS_Version_Spec vspec      = get_vcp_version_by_display_handle(dh);
               // DDCA_MCCS_Version_Id   version_id = mccs_version_spec_to_id(vspec);

               VCP_Feature_Table_Entry * pentry = vcp_find_feature_by_hexid(feature_code);
               if (!pentry) {
#ifdef ALT
               DDCA_Version_Feature_Info * feature_info =
               get_version_specific_feature_info(
                     feature_code,
                     true,                    //    with_default
                     version_id);
               assert(feature_info);
               if (!feature_info) {
#endif
                  psc = DDCL_ARG;
               }
               else {
                  DDCA_Version_Feature_Flags flags = get_version_sensitive_feature_flags(pentry, vspec);
                  // Version_Feature_Flags flags = feature_info->internal_feature_flags;
                   // n. will default to NON_TABLE_VCP_VALUE if not a known code
                   DDCA_Vcp_Value_Type call_type = (flags & DDCA_TABLE) ?  DDCA_TABLE_VCP_VALUE : DDCA_NON_TABLE_VCP_VALUE;
                   DDCA_Single_Vcp_Value * pvalrec;
                   psc = get_vcp_value(dh, feature_code, call_type, &pvalrec);
                   if (psc == 0) {
                      bool ok =
                      vcp_format_feature_detail(
                             pentry,
                             vspec,
                             pvalrec,
                             p_formatted_value
                           );
                      if (!ok) {
                         psc = DDCL_OTHER;    // ** WRONG CODE ***
                         assert(!p_formatted_value);
                      }
                   }

                   // psc = global_to_public_status_code(gsc);
               }
         }
   )
}


DDCA_Status
ddca_set_single_vcp_value(
      DDCA_Display_Handle     ddca_dh,
      DDCA_Single_Vcp_Value * valrec)
   {
      WITH_DH(ddca_dh,  {
            psc = set_vcp_value(dh, valrec);
            // psc = global_to_public_status_code(gsc);
         } );
   }




DDCA_Status
ddca_set_continuous_vcp_value(
      DDCA_Display_Handle   ddca_dh,
      DDCA_Vcp_Feature_Code feature_code,
      int                   new_value)
{
#ifdef OLD
   WITH_DH(ddca_dh,  {
         psc = set_nontable_vcp_value(dh, feature_code, new_value);
         // psc = global_to_public_status_code(gsc);
      } );
#endif
   DDCA_Single_Vcp_Value valrec;
   valrec.opcode = feature_code;
   valrec.value_type = DDCA_NON_TABLE_VCP_VALUE;
   valrec.val.c.cur_val = new_value;
   return ddca_set_single_vcp_value(ddca_dh, &valrec);
}


DDCA_Status
ddca_set_simple_nc_vcp_value(
      DDCA_Display_Handle    ddca_dh,
      DDCA_Vcp_Feature_Code  feature_code,
      Byte                   new_value)
{
   return ddca_set_continuous_vcp_value(ddca_dh, feature_code, new_value);
}


DDCA_Status
ddca_set_raw_vcp_value(
      DDCA_Display_Handle   ddca_dh,
      DDCA_Vcp_Feature_Code      feature_code,
      Byte                  hi_byte,
      Byte                  lo_byte)
{
   return ddca_set_continuous_vcp_value(ddca_dh, feature_code, hi_byte << 8 | lo_byte);
}


//
// Monitor Capabilities
//

DDCA_Status
ddca_get_capabilities_string(
      DDCA_Display_Handle  ddca_dh,
      char**               pcaps)
{
   WITH_DH(ddca_dh,
      {
         char * p_cap_string = NULL;
         psc = get_capabilities_string(dh, &p_cap_string);
         if (psc == 0) {
            // make copy to ensure caller does not muck around in ddcutil's
            // internal data structures
            *pcaps = strdup(p_cap_string);
         }
         // psc = public_to_global_ status_code(gsc);
      }
   );
}


DDCA_Status
ddca_parse_capabilities_string(
      char *                   capabilities_string,
      DDCA_Capabilities **     p_parsed_capabilities)
{
   bool debug = false;
   DBGMSF(debug, "Starting. capabilities_string: |%s|", capabilities_string);
   DDCA_Status psc = DDCL_OTHER;       // DDCL_BAD_DATA?
   DBGMSF(debug, "psc initialized to %s", psc_desc(psc));
   DDCA_Capabilities * result = NULL;

   // need to control messages?
   Parsed_Capabilities * pcaps = parse_capabilities_string(capabilities_string);
   if (pcaps) {
      if (debug) {
         DBGMSG("Parsing succeeded. ");
         report_parsed_capabilities(pcaps);
         DBGMSG("Convert to DDCA_Capabilities...");
      }
      result = calloc(1, sizeof(DDCA_Capabilities));
      memcpy(result->marker, DDCA_CAPABILITIES_MARKER, 4);
      result->unparsed_string= strdup(capabilities_string);     // needed?
      result->version_spec = pcaps->parsed_mccs_version;
      // n. needen't set vcp_code_ct if !pcaps, calloc() has done it
      if (pcaps->vcp_features) {
         result->vcp_code_ct = pcaps->vcp_features->len;
         result->vcp_codes = calloc(result->vcp_code_ct, sizeof(DDCA_Cap_Vcp));
         // DBGMSF(debug, "allocate %d bytes at %p", result->vcp_code_ct * sizeof(DDCA_Cap_Vcp), result->vcp_codes);
         for (int ndx = 0; ndx < result->vcp_code_ct; ndx++) {
            DDCA_Cap_Vcp * cur_cap_vcp = &result->vcp_codes[ndx];
            // DBGMSF(debug, "cur_cap_vcp = %p", &result->vcp_codes[ndx]);
            memcpy(cur_cap_vcp->marker, DDCA_CAP_VCP_MARKER, 4);
            Capabilities_Feature_Record * cur_cfr = g_ptr_array_index(pcaps->vcp_features, ndx);
            // DBGMSF(debug, "Capabilities_Feature_Record * cur_cfr = %p", cur_cfr);
            assert(memcmp(cur_cfr->marker, CAPABILITIES_FEATURE_MARKER, 4) == 0);
            // if (debug)
            //    show_capabilities_feature(cur_cfr, result->version_spec);
            cur_cap_vcp->feature_code = cur_cfr->feature_id;
            // DBGMSF(debug, "cur_cfr = %p, feature_code - 0x%02x", cur_cfr, cur_cfr->feature_id);

            // cur_cap_vcp->raw_values = strdup(cur_cfr->value_string);
            // TODO: get values from Byte_Bit_Flags cur_cfr->bbflags
#ifdef OLD_BVA
            Byte_Value_Array bva = cur_cfr->values;
            if (bva) {
               cur_cap_vcp->value_ct = bva_length(bva);
               cur_cap_vcp->values = bva_bytes(bva);     // makes copy of bytes
            }
#endif
            if (cur_cfr->bbflags) {
               cur_cap_vcp->value_ct = bbf_count_set(cur_cfr->bbflags);
               cur_cap_vcp->values   = calloc(1, cur_cap_vcp->value_ct);
               bbf_to_bytes(cur_cfr->bbflags, cur_cap_vcp->values, cur_cap_vcp->value_ct);
            }
         }
      }
      psc = 0;
      free_parsed_capabilities(pcaps);
   }

   *p_parsed_capabilities = result;
   DBGMSF(debug, "Done. Returning: %d", psc);
   return psc;
}


void
ddca_free_parsed_capabilities(
      DDCA_Capabilities * pcaps)
{
   bool debug = false;
   if (pcaps) {
      assert(memcmp(pcaps->marker, DDCA_CAPABILITIES_MARKER, 4) == 0);
      free(pcaps->unparsed_string);

      DBGMSF(debug, "vcp_code_ct = %d", pcaps->vcp_code_ct);
      for (int ndx = 0; ndx < pcaps->vcp_code_ct; ndx++) {
         DDCA_Cap_Vcp * cur_vcp = &pcaps->vcp_codes[ndx];
         assert(memcmp(cur_vcp->marker, DDCA_CAP_VCP_MARKER, 4) == 0);
         free(cur_vcp->values);
         cur_vcp->marker[3] = 'x';
      }

      pcaps->marker[3] = 'x';
      free(pcaps);
   }
}


void
ddca_report_parsed_capabilities(
      DDCA_Capabilities * pcaps,
      int                 depth)
{
   bool debug = false;
   DBGMSF(debug, "Starting");
   assert(pcaps && memcmp(pcaps->marker, DDCA_CAPABILITIES_MARKER, 4) == 0);
   int d1 = depth+1;
   int d2 = depth+2;

   rpt_structure_loc("DDCA_Capabilities", pcaps, depth);
   rpt_vstring(d1, "unparsed string: %s", pcaps->unparsed_string);
   rpt_vstring(d1, "VCP version:     %d.%d", pcaps->version_spec.major, pcaps->version_spec.minor);
   rpt_vstring(d1, "VCP codes:");
   for (int code_ndx = 0; code_ndx < pcaps->vcp_code_ct; code_ndx++) {
      DDCA_Cap_Vcp * cur_vcp = &pcaps->vcp_codes[code_ndx];
      assert( memcmp(cur_vcp->marker, DDCA_CAP_VCP_MARKER, 4) == 0);
      rpt_vstring(d2, "Feature code:  0x%02x", cur_vcp->feature_code);
      if (cur_vcp->value_ct > 0) {
         char * hs =  hexstring(cur_vcp->values, cur_vcp->value_ct);
         rpt_vstring(d2, "Values:     %s", hs);
         free(hs);
      }
   }

}


DDCA_Status
ddca_get_profile_related_values(
      DDCA_Display_Handle ddca_dh,
      char**              pprofile_values_string)
{
   WITH_DH(ddca_dh,
      {
         bool debug = false;
         DBGMSF(debug, "Before dumpvcp_to_string_by_display_handle(), pprofile_values_string=%p, *pprofile_values_string=%p",
               pprofile_values_string, *pprofile_values_string);
         psc = dumpvcp_as_string(dh, pprofile_values_string);
         DBGMSF(debug, "After dumpvcp_to_string_by_display_handle(), pprofile_values_string=%p, *pprofile_values_string=%p",
               pprofile_values_string, *pprofile_values_string);
         DBGMSF(debug, "*pprofile_values_string = |%s|", *pprofile_values_string);
      }
   );
}


// TODO: handle display as optional argument
DDCA_Status
ddca_set_profile_related_values(
      char * profile_values_string)
{
   Public_Status_Code psc = loadvcp_by_string(profile_values_string, NULL);
   return psc;
}


//
// Reports
//

int
ddca_report_active_displays(int depth) {
   // return ddc_report_active_displays(depth);
   return ddc_report_displays(DDC_REPORT_VALID_DISPLAYS_ONLY, 0);
}
