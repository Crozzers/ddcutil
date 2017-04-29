/* ddc_displays.c
 *
 * <copyright>
 * Copyright (C) 2014-2017 Sanford Rockowitz <rockowitz@minsoft.com>
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
 * Access displays, whether DDC, ADL, or USB
 */

#include <config.h>

/** \cond */
#include <assert.h>
#include <errno.h>
#include <glib.h>
#include <string.h>
#include <time.h>
/** \endcond */

#include "util/debug_util.h"
#include "util/failsim.h"
#include "util/report_util.h"
#include "util/udev_util.h"

#include "base/adl_errors.h"
#include "base/ddc_errno.h"
#include "base/ddc_packets.h"
#include "base/linux_errno.h"
#include "base/parms.h"

#include "vcp/vcp_feature_codes.h"

#include "i2c/i2c_bus_core.h"
#include "i2c/i2c_do_io.h"

#include "adl/adl_shim.h"
#ifdef HAVE_ADL
#include "adl/adl_impl/adl_intf.h"
#endif

#ifdef USE_USB
#include "usb/usb_displays.h"
#endif

#include "ddc/ddc_packet_io.h"
#include "ddc/ddc_vcp.h"
#include "ddc/ddc_vcp_version.h"

#include "ddc/ddc_displays.h"

// on banner, async  detect: 1.7 sec, non-async 3.4 sec
const int ASYNC_THRESHOLD = 3;

// Trace class for this file
// static Trace_Group TRACE_GROUP = TRC_DDC;   // currently unused


static GPtrArray * all_displays = NULL;    // all detected displays
static int dispno_max = 0;                 // highest assigned display number


/** Checks that DDC communication is working by trying to read the value
 *  of feature x10 (brightness).
 *
 *  \param dh  #Display_Handle of open display
 *  \retval true communication successful
 *  \retval false communication failed
 *
 *  \remark
 *  It has been observed that DDC communication can fail even if slave address x37
 *  is valid on the I2C bus.
 *  \remark
 *  ADL does not notice that a reported display, e.g. Dell 1905FP, does not support
 *  DDC.
 *  \remark
 *  If a validly structured DDC response is received, e.g. with the unsupported feature
 *  bit set or a DDC Null response, communication is considered successful.
 *  \remark
 */
static
bool check_ddc_communication(Display_Handle * dh) {
   bool debug = false;
   DBGMSF(debug, "Starting. dh=%s", dh_repr_t(dh));

   bool result = true;

   DDCA_Single_Vcp_Value * pvalrec;

   // verbose output is distracting since this function is called when querying for other things
   // DDCA_Output_Level olev = get_output_level();
   // if (olev == DDCA_OL_VERBOSE)
   //    set_output_level(DDCA_OL_NORMAL);

   Public_Status_Code psc = get_vcp_value(dh, 0x10, DDCA_NON_TABLE_VCP_VALUE, &pvalrec);

   // if (olev == DDCA_OL_VERBOSE)
   //    set_output_level(olev);

   if (psc != 0 && psc != DDCRC_REPORTED_UNSUPPORTED && psc != DDCRC_DETERMINED_UNSUPPORTED) {
      result = false;
      DBGMSF(debug, "Error getting value for brightness VCP feature 0x10. gsc=%s\n", psc_desc(psc) );
   }

   DBGMSF(debug, "Returning: %s", bool_repr(result));
   return result;
}


/** Checks whether the monitor uses a DDC Null response to report
 *  and unspupported VCP code by attempted to read feature 0x00.
 *
 *  \param dh  #Display_Handle of monitor
 *  \retval true  DDC Null Response was received
 *  \retval false any other response
 *
 *  \remark
 *  Monitors should set the unsupported feature bit in a valid DDC
 *  response, but a few monitors (mis)use the Null Reponse instead.
 *  \remark
 *  Note that this test is not perfect, as a Null Response might
 *  in fact indicate a transient error, but that is rare.
 */
static
bool check_monitor_ddc_null_response(Display_Handle * dh) {
   assert(dh);
   assert(dh->dref);
   bool debug = false;
   DBGMSF(debug, "Starting. dh=%s", dh_repr_t(dh));

   bool result = false;

   if (dh->dref->io_mode != DDCA_IO_USB) {

      DDCA_Single_Vcp_Value * pvalrec;

      // // verbose output is distracting since this function is called when querying for other things
      // DDCA_Output_Level olev = get_output_level();
      // if (olev == DDCA_OL_VERBOSE)
      //    set_output_level(DDCA_OL_NORMAL);

      Public_Status_Code psc = get_vcp_value(dh, 0x00, DDCA_NON_TABLE_VCP_VALUE, &pvalrec);

      // if (olev == DDCA_OL_VERBOSE)
      //    set_output_level(olev);

      if (psc == DDCRC_NULL_RESPONSE) {
         result = true;
      }
      else if (psc != 0 && psc != DDCRC_REPORTED_UNSUPPORTED && psc != DDCRC_DETERMINED_UNSUPPORTED) {
         DBGMSF(debug, "Unexpected status getting value for non-existent VCP feature 0x00. gsc=%s\n", psc_desc(psc) );
      }

   }

   DBGMSF(debug, "Returning: %s", bool_repr(result));
   return result;
}


/** Collects most initial monitor checks to perform them on a single open of the
 *  monitor device, and to avoid repeating them.
 *
 *  Performs the followign tests:
 *  - Checks that DDC communication is working.
 *  - Checks if the monitor uses DDC Mull Response to indicate invalid VCP code
 *  - Queries the VCP (MCCS) version.
 *
 *  \param dh  pointer to #Display_Handle for open monitor device
 *  \return **true** if DDC communication with the display succeeded, **false** otherwise.
 */
bool initial_checks_by_dh(Display_Handle * dh) {
   bool debug = false;
   DBGMSF(debug, "Starting. dh=%s", dh_repr_t(dh));

   if (!(dh->dref->flags & DREF_DDC_COMMUNICATION_CHECKED)) {
      if (check_ddc_communication(dh))
         dh->dref->flags |= DREF_DDC_COMMUNICATION_WORKING;
      dh->dref->flags |= DREF_DDC_COMMUNICATION_CHECKED;
   }
   bool communication_working = dh->dref->flags & DREF_DDC_COMMUNICATION_WORKING;

   if (communication_working) {
      if (!(dh->dref->flags & DREF_DDC_NULL_RESPONSE_CHECKED)) {
         if (check_monitor_ddc_null_response(dh) )
            dh->dref->flags |= DREF_DDC_USES_NULL_RESPONSE_FOR_UNSUPPORTED;
         dh->dref->flags |= DREF_DDC_NULL_RESPONSE_CHECKED;
      }
      if ( vcp_version_is_unqueried(dh->dref->vcp_version)) {
         dh->dref->vcp_version = get_vcp_version_by_display_handle(dh);
         // dh->vcp_version = dh->dref->vcp_version;
      }
   }

   DBGMSF(debug, "Returning: %s", bool_repr(communication_working));
   return communication_working;
}


/** Given a #Display_Ref, opens the monitor device and calls #initial_checks_by_dh()
 *  to perform initial monitor checks.
 *
 *  \param dref pointer to #Display_Ref for monitor
 *  \return **true** if DDC communication with the display succeeded, **false** otherwise.
 */
bool initial_checks_by_dref(Display_Ref * dref) {
   bool result = false;
   Display_Handle * dh = NULL;
   Public_Status_Code psc = 0;

   psc = ddc_open_display(dref, CALLOPT_ERR_MSG | CALLOPT_ERR_ABORT, &dh);
   if (psc == 0)  {
      result = initial_checks_by_dh(dh);
      ddc_close_display(dh);
   }

   return result;
}


// function to be run in thread
void * threaded_initial_checks_by_dref(gpointer data) {
   Display_Ref * dref = data;
   assert(memcmp(dref->marker, DISPLAY_REF_MARKER, 4) == 0 );
   initial_checks_by_dref(dref);
   // g_thread_exit(NULL);
   return NULL;
}




//
// Functions to get display information
//


/** Gets a list of all detected displays, whether they support DDC or not.
 *
 *  Initializes the list of detected monitors if necessary.
 *
 *  \return **GPtrArray of #Display_Ref instances
 */
GPtrArray * ddc_get_all_displays() {
   // ddc_ensure_displays_detected();
   assert(all_displays);

   return all_displays;
}




/** Gets the controller firmware version as a string
 *
 * \param dh  pointer to display handle
 * \return pointer to character string, which is valid until the next
 * call to this function.
 *
 * \remark
 * Consider caching the value in dh->dref
 */
char * get_firmware_version_string(Display_Handle * dh) {
   bool debug = false;

   static char version[40];

   DDCA_Single_Vcp_Value * valrec;

   Public_Status_Code psc = get_vcp_value(
                               dh,
                               0xc9,                     // firmware detection
                               DDCA_NON_TABLE_VCP_VALUE,
                               &valrec);
   if (psc != 0) {
      strcpy(version, "Unspecified");
      if (psc != DDCRC_REPORTED_UNSUPPORTED && psc != DDCRC_DETERMINED_UNSUPPORTED) {
         DBGMSF(debug, "get_vcp_value(0xc9) returned %s", psc_desc(psc));
         strcpy(version, "DDC communication failed");
      }
   }
   else {
      snprintf(version, sizeof(version), "%d.%d",
         valrec->val.nc.sh, valrec->val.nc.sl);
   }
   return version;
}


/** Gets the controller manufacturer name for an open display.
 *
 * \param dh  pointer to display handle
 * \return pointer to character string, which is valid until the next
 * call to this function.
 *
 * \remark
 * Consider caching the value in dh->dref
 */
char * get_controller_mfg_string(Display_Handle * dh) {
   bool debug = false;

   static char mfg_name_buf[100] = "";
   char * mfg_name = NULL;
   DDCA_Single_Vcp_Value *   valrec;
   Public_Status_Code psc = get_vcp_value(dh, 0xc8, DDCA_NON_TABLE_VCP_VALUE, &valrec);

   if (psc == 0) {
      DDCA_Feature_Value_Entry * vals = pxc8_display_controller_type_values;
      mfg_name =  get_feature_value_name(
                            vals,
                            valrec->val.nc.sl);
      if (!mfg_name) {
         snprintf(mfg_name_buf, sizeof(mfg_name_buf), "Unrecognized manufacturer code 0x%02x", valrec->val.nc.sl);
         mfg_name = mfg_name_buf;
      }
   }
   else if (psc == DDCRC_REPORTED_UNSUPPORTED || psc == DDCRC_DETERMINED_UNSUPPORTED) {
      mfg_name = "Unspecified";
   }
   else {
      DBGMSF(debug, "get_nontable_vcp_value(0xc8) returned %s", psc_desc(psc));
      mfg_name = "DDC communication failed";
    }
   return mfg_name;
}


/** Shows information about a display, specified by a #Display_Ref
 *
 * Output is written using report functions
 *
 * \param dref   pointer to display reference
 * \param depth  logical indentation depth
 */
void
ddc_report_display_by_dref(Display_Ref * dref, int depth) {
   bool debug = false;
   DBGMSF(debug, "Starting");
   assert(dref);
   assert(memcmp(dref->marker, DISPLAY_REF_MARKER, 4) == 0);
   int d1 = depth+1;

   switch(dref->dispno) {
   case -1:
      rpt_vstring(depth, "Invalid display");
      break;
   case 0:          // valid display, no assigned display number
      d1 = depth;   // adjust indent  ??
      break;
   default:         // normal case
      rpt_vstring(depth, "Display %d", dref->dispno);
   }


   switch(dref->io_mode) {
   case DDCA_IO_DEVI2C:
      // i2c_report_active_display_by_busno(dref->busno, d1);
      {
         Bus_Info * curinfo = dref->detail2;
         assert(curinfo);
         assert(memcmp(curinfo, BUS_INFO_MARKER, 4) == 0);

         i2c_report_active_display(curinfo, d1);
      }
      break;
   case DDCA_IO_ADL:
      adlshim_report_active_display_by_display_ref(dref, d1);
      break;
   case DDCA_IO_USB:
#ifdef USE_USB
      usb_show_active_display_by_display_ref(dref, d1);
#else
      PROGRAM_LOGIC_ERROR("ddcutil not built with USB support");
#endif
      break;
   }

   assert( dref->flags & DREF_DDC_COMMUNICATION_CHECKED);
   if ( dref->flags & DREF_DDC_COMMUNICATION_WORKING)
      assert( (dref->flags & DREF_DDC_NULL_RESPONSE_CHECKED) &&
              !vcp_version_is_unqueried(dref->vcp_version)    );

   DDCA_Output_Level output_level = get_output_level();

   if (output_level >= DDCA_OL_NORMAL) {
      if (!(dref->flags & DREF_DDC_COMMUNICATION_WORKING) ) {
         rpt_vstring(d1, "DDC communication failed");
         if (output_level >= DDCA_OL_VERBOSE) {
            rpt_vstring(d1, "Is DDC/CI enabled in the monitor's on-screen display?");
         }
      }
      else {
         DDCA_MCCS_Version_Spec vspec = dref->vcp_version;
         if ( vspec.major   == 0)
            rpt_vstring(d1, "VCP version:         Detection failed");
         else
            rpt_vstring(d1, "VCP version:         %d.%d", vspec.major, vspec.minor);

         if (output_level >= DDCA_OL_VERBOSE) {
            // n. requires write access since may call get_vcp_value(), which does a write
            Display_Handle * dh = NULL;
            Public_Status_Code psc = ddc_open_display(dref, CALLOPT_ERR_MSG, &dh);
            if (psc != 0) {
               rpt_vstring(d1, "Error opening display %s, error = %s",
                                  dref_short_name(dref), psc_desc(psc));
            }
            else {
               // display controller mfg, firmware version
               rpt_vstring(d1, "Controller mfg:      %s", get_controller_mfg_string(dh) );
               rpt_vstring(d1, "Firmware version:    %s", get_firmware_version_string(dh));;
               ddc_close_display(dh);
            }

            if (dref->io_mode != DDCA_IO_USB)
               rpt_vstring(d1, "Monitor returns DDC Null Response for unsupported features: %s",
                                  bool_repr(dh->dref->flags & DREF_DDC_USES_NULL_RESPONSE_FOR_UNSUPPORTED));
         }

      }

   }
   DBGMSF(debug, "Done");
}



/** Reports all displays found.
 *
 * Output is written to the current report destination using
 * report functions.
 *
 * @param   valid_displays_only  if **true**, report only valid displays\n
 *                      if **false**, report all displays
 * @param   depth       logical indentation depth
 *
 * @return total number of displays reported
 */
int
ddc_report_displays(bool valid_displays_only, int depth) {
   bool debug = false;
   DBGMSF(debug, "Starting");

   ddc_ensure_displays_detected();

   int display_ct = 0;
   for (int ndx=0; ndx<all_displays->len; ndx++) {
      Display_Ref * dref = g_ptr_array_index(all_displays, ndx);
      assert(memcmp(dref->marker, DISPLAY_REF_MARKER, 4) == 0);
      if (dref->dispno > 0 || !valid_displays_only) {
         display_ct++;
         ddc_report_display_by_dref(dref, depth);
         rpt_title("",0);
      }
   }
   if (display_ct == 0)
      rpt_vstring(depth, "No %sdisplays found", (valid_displays_only) ? "active " : "");

   DBGMSF(debug, "Done.  Returning: %d", display_ct);
   return display_ct;
}



/** Debugging function to display the contents of a #Display_Ref.
 *
 * \param dref  pointer to #Display_Ref
 * \param depth logical indentation depth
 */
void dbgreport_display_ref(Display_Ref * dref, int depth) {
   int d1 = depth+1;
   int d2 = depth+2;
   DDCA_Output_Level saved_output_level = get_output_level();
   set_output_level(DDCA_OL_VERBOSE);
   rpt_structure_loc("Display_Ref", dref, depth);
   rpt_int("dispno", NULL, dref->dispno, d1);

   // rpt_vstring(d1, "dref: %p:", dref->dref);
   report_display_ref(dref, d1);

   rpt_vstring(d1, "edid: %p (Skipping report)", dref->pedid);
   // report_parsed_edid(drec->edid, false, d1);

   rpt_vstring(d1, "io_mode: %s", mccs_io_mode_name(dref->io_mode));
   // rpt_vstring(d1, "flags:   0x%02x", drec->flags);
   switch(dref->io_mode) {
   case(DDCA_IO_DEVI2C):
         rpt_vstring(d1, "I2C bus information: ");
         Bus_Info * businfo = dref->detail2;
         assert( memcmp(businfo->marker, BUS_INFO_MARKER, 4) == 0);
         i2c_report_bus_info(businfo, d2);
         break;
   case(DDCA_IO_ADL):
#ifdef HAVE_ADL
      rpt_vstring(d1, "ADL device information: ");
      ADL_Display_Detail * adl_detail = dref->detail2;
      assert(memcmp(adl_detail->marker, ADL_DISPLAY_DETAIL_MARKER, 4) == 0);
      adlshim_report_adl_display_detail(adl_detail, d2);
#endif
      break;
   case(DDCA_IO_USB):
#ifdef USE_USB
         rpt_vstring(d1, "USB device information: ");
         Usb_Monitor_Info * moninfo = dref->detail2;
         assert(memcmp(moninfo->marker, USB_MONITOR_INFO_MARKER, 4) == 0);
         report_usb_monitor_info(moninfo, d2);
#else
         PROGRAM_LOGIC_ERROR("Built without USB support");
#endif
   break;
   }

   set_output_level(saved_output_level);
}


/** Debugging function to report a collection of #Display_Ref.
 *
 * \param recs    pointer to collection of #Display_Ref
 * \param depth   logical indentation depth
 */
void debug_report_display_refs(GPtrArray * recs, int depth) {
   assert(recs);
   rpt_vstring(depth, "Reporting %d Display_Ref instances", recs->len);
   for (int ndx = 0; ndx < recs->len; ndx++) {
      Display_Ref * drec = g_ptr_array_index(recs, ndx);
      assert( memcmp(drec->marker, DISPLAY_REF_MARKER, 4) == 0);
      rpt_nl();
      dbgreport_display_ref(drec, depth+1);
   }
}


//
// Monitor selection
//

/** Display selection criteria */
typedef struct {
   int     dispno;
   int     i2c_busno;
   int     iAdapterIndex;
   int     iDisplayIndex;
   int     hiddev;
   int     usb_busno;
   int     usb_devno;
   char *  mfg_id;
   char *  model_name;
   char *  serial_ascii;
   Byte *  edidbytes;
} Display_Criteria;


/** Allocates a new #Display_Criteria and initializes it to contain no criteria.
 *
 * \return initialized #Display_Criteria
 */
static Display_Criteria *
new_display_criteria() {
   Display_Criteria * criteria = calloc(1, sizeof(Display_Criteria));
   criteria->dispno = -1;
   criteria->i2c_busno  = -1;
   criteria->iAdapterIndex = -1;
   criteria->iDisplayIndex = -1;
   criteria->hiddev = -1;
   criteria->usb_busno = -1;
   criteria->usb_devno = -1;
   return criteria;
}


/** Checks if a given #Display_Ref satisfies all the criteria specified in a
 *  #Display_Criteria struct.
 *
 *  \param  drec     pointer to #Display_Ref to test
 *  \param  criteria pointer to criteria
 *  \retval true     all specified criteria match
 *  \retval false    at least one specified criterion does not match
 *
 *  \remark
 *  In the degenerate case that no criteria are set in **criteria**, returns true.
 */
static bool
ddc_check_display_ref(Display_Ref * dref, Display_Criteria * criteria) {
   assert(dref && criteria);
   bool result = false;

   if (criteria->dispno >= 0 && criteria->dispno != dref->dispno)
      goto bye;

   if (criteria->i2c_busno >= 0) {
      if (dref->io_mode != DDCA_IO_DEVI2C || dref->busno != criteria->i2c_busno)
         goto bye;
   }

   if (criteria->iAdapterIndex >= 0) {
      if (dref->io_mode != DDCA_IO_ADL || dref->iAdapterIndex != criteria->iAdapterIndex)
         goto bye;
   }

   if (criteria->iDisplayIndex >= 0) {
      if (dref->io_mode != DDCA_IO_ADL || dref->iDisplayIndex != criteria->iDisplayIndex)
         goto bye;
   }

   if (criteria->hiddev >= 0) {
      if (dref->io_mode != DDCA_IO_USB)
         goto bye;
      char buf[40];
      snprintf(buf, 40, "%s/hiddev%d", hiddev_directory(), criteria->hiddev);
      Usb_Monitor_Info * moninfo = dref->detail2;
      assert(memcmp(moninfo->marker, USB_MONITOR_INFO_MARKER, 4) == 0);
      if (!streq( moninfo->hiddev_device_name, buf))
         goto bye;
   }

   if (criteria->usb_busno >= 0) {
      if (dref->io_mode != DDCA_IO_USB)
         goto bye;
      // Usb_Monitor_Info * moninfo = drec->detail2;
      // assert(memcmp(moninfo->marker, USB_MONITOR_INFO_MARKER, 4) == 0);
      // if ( moninfo->hiddev_devinfo->busnum != criteria->usb_busno )
      if ( dref->usb_bus != criteria->usb_busno )
         goto bye;
   }

   if (criteria->usb_devno >= 0) {
      if (dref->io_mode != DDCA_IO_USB)
         goto bye;
      // Usb_Monitor_Info * moninfo = drec->detail2;
      // assert(memcmp(moninfo->marker, USB_MONITOR_INFO_MARKER, 4) == 0);
      // if ( moninfo->hiddev_devinfo->devnum != criteria->usb_devno )
      if ( dref->usb_device != criteria->usb_devno )
         goto bye;
   }

   if (criteria->hiddev >= 0) {
      if (dref->io_mode != DDCA_IO_USB)
         goto bye;
      if ( dref->usb_hiddev_devno != criteria->hiddev )
         goto bye;
   }

   if (criteria->mfg_id && (strlen(criteria->mfg_id) > 0) &&
         !streq(dref->pedid->mfg_id, criteria->mfg_id) )
      goto bye;

   if (criteria->model_name && (strlen(criteria->model_name) > 0) &&
         !streq(dref->pedid->model_name, criteria->model_name) )
      goto bye;

   if (criteria->serial_ascii && (strlen(criteria->serial_ascii) > 0) &&
         !streq(dref->pedid->serial_ascii, criteria->serial_ascii) )
      goto bye;

   if (criteria->edidbytes && memcmp(dref->pedid->bytes, criteria->edidbytes, 128) != 0)
      goto bye;

   result = true;

bye:
   return result;
}



void async_scan(GPtrArray * all_displays) {
   bool debug = false;
   DBGMSF(debug, "Starting. all_displays=%p, display_count=%d", all_displays, all_displays->len);

   GPtrArray * threads = g_ptr_array_new();
   for (int ndx = 0; ndx < all_displays->len; ndx++) {
      Display_Ref * dref = g_ptr_array_index(all_displays, ndx);
      assert( memcmp(dref->marker, DISPLAY_REF_MARKER, 4) == 0 );

      GThread * th =
      g_thread_new(
            dref_repr_t(dref),
            threaded_initial_checks_by_dref,
            dref);
      g_ptr_array_add(threads, th);
   }
   DBGMSF(debug, "Started %d threads", threads->len);
   for (int ndx = 0; ndx < threads->len; ndx++) {
      GThread * thread = g_ptr_array_index(threads, ndx);
      g_thread_join(thread);
   }
   DBGMSF(debug, "Threads joined");

#ifdef OLD
   for (int ndx = 0; ndx < all_displays->len; ndx++) {
      Display_Ref * dref = g_ptr_array_index(all_displays, ndx);
      assert( memcmp(dref->marker, DISPLAY_REF_MARKER, 4) == 0 );
      if (dref->flags & DREF_DDC_COMMUNICATION_WORKING) {
         dref->dispno = ++dispno_max;
      }
      else {
         dref->dispno = -1;
      }
   }
#endif
   DBGMSF(debug, "Done");
}

void non_async_scan(GPtrArray * all_displays) {
   bool debug = false;
   DBGMSF(debug, "Starting. checking %d displays", all_displays->len);

   for (int ndx = 0; ndx < all_displays->len; ndx++) {
      Display_Ref * dref = g_ptr_array_index(all_displays, ndx);
      assert( memcmp(dref->marker, DISPLAY_REF_MARKER, 4) == 0 );
      initial_checks_by_dref(dref);

#ifdef OLD
      if (dref->flags & DREF_DDC_COMMUNICATION_WORKING) {
         dref->dispno = ++dispno_max;
      }
      else {
         dref->dispno = -1;
      }
#endif
   }
   DBGMSF(debug, "Done");
}



static Display_Ref *
ddc_find_display_ref_by_criteria(Display_Criteria * criteria) {
   Display_Ref * result = NULL;
   for (int ndx = 0; ndx < all_displays->len; ndx++) {
      Display_Ref * drec = g_ptr_array_index(all_displays, ndx);
      assert(memcmp(drec->marker, DISPLAY_REF_MARKER, 4) == 0);
      if (ddc_check_display_ref(drec, criteria)) {
         result = drec;
         break;
      }
   }
   return result;
}


/** Searches the master display list for a display matching the
 * specified #Display_Identifier, returning its #Display_Ref
 *
 * \param did display identifier to search for
 * \return #Display_Ref for the display.
 *
 * \remark
 * The returned value is a pointer into an internal data structure
 * and should not be freed by the caller.
 */
Display_Ref *
ddc_find_display_ref_by_display_identifier(Display_Identifier * did) {
   bool debug = false;
   DBGMSF(debug, "Starting");
   if (debug)
      report_display_identifier(did, 1);

   Display_Ref * result = NULL;

   Display_Criteria * criteria = new_display_criteria();

   switch(did->id_type) {
   case DISP_ID_BUSNO:
         criteria->i2c_busno = did->busno;
         break;
   case DISP_ID_ADL:
      criteria->iAdapterIndex = did->iAdapterIndex;
      criteria->iDisplayIndex = did->iDisplayIndex;
      break;
   case DISP_ID_MONSER:
      criteria->mfg_id = did->mfg_id;
      criteria->model_name = did->model_name;
      criteria->serial_ascii = did->serial_ascii;
      break;
   case DISP_ID_EDID:
      criteria->edidbytes = did->edidbytes;
      break;
   case DISP_ID_DISPNO:
      criteria->dispno = did->dispno;
      break;
   case DISP_ID_USB:
      criteria->usb_busno = did->usb_bus;
      criteria->usb_devno = did->usb_device;
      break;
   case DISP_ID_HIDDEV:
      criteria->hiddev = did->hiddev_devno;

   }

   result = ddc_find_display_ref_by_criteria(criteria);

   free(criteria);   // do not free pointers in criteria, they are owned by Display_Identifier

   if (debug) {
      if (result) {
         DBGMSG("Done.  Returning: ");
         dbgreport_display_ref(result, 1);
      }
      else
         DBGMSG("Done.  Returning NULL");
   }

   return result;
}


/** Searches the detected displays for one matching the criteria in a
 *  #Display_Identifier.
 *
 *  \param pdid  pointer to a #Display_Identifier
 *  \param callopts  standard call options
 *  \return pointer to #Display_Ref for the display, NULL if not found
 *
 *  \todo
 *  If the criteria directly specify an access path
 *  (e.g. I2C bus number) and CALLOPT_FORCe specified, then create a
 *  temporary #Display_Ref, bypassing the list of detected monitors.
 */
Display_Ref *
get_display_ref_for_display_identifier(
                Display_Identifier* pdid,
                Call_Options        callopts)
{
   Display_Ref * dref = ddc_find_display_ref_by_display_identifier(pdid);
   if ( !dref && (callopts & CALLOPT_ERR_MSG) ) {
      f0printf(FOUT, "Display not found\n");
   }

   return dref;
}


/** Detects all connected displays by querying the I2C, ADL, and USB subsystems.
 *
 * \return array of #Display_Ref
 */
GPtrArray *
ddc_detect_all_displays() {
   bool debug = false;
   DBGMSF(debug, "Starting");

   // int new_busct =  i2c_detect_buses();
   // DBGMSF(debug, "i2c_detect_buses() returned: %d", new_busct);

   GPtrArray * display_list = g_ptr_array_new();

   // int busct = i2c_get_busct();
   int busct = i2c_detect_buses();
   int busndx = 0;
   for (busndx=0; busndx < busct; busndx++) {
      Bus_Info * businfo = i2c_get_bus_info_by_index(busndx);
      if ( (businfo->flags & I2C_BUS_ADDR_0X50) ) {
         Display_Ref * dref = create_bus_display_ref(businfo->busno);
         dref->dispno = -1;
         dref->pedid = businfo->edid;    // needed?
         // drec->detail.bus_detail = businfo;
         dref->detail2 = businfo;
         dref->flags |= DREF_DDC_IS_MONITOR_CHECKED;
         dref->flags |= DREF_DDC_IS_MONITOR;
         g_ptr_array_add(display_list, dref);
         // ddc_add_display_ref(display_list, dref);
      }
   }

  GPtrArray * all_adl_details = adlshim_get_valid_display_details();
  int adlct = all_adl_details->len;
  for (int ndx = 0; ndx < adlct; ndx++) {
     ADL_Display_Detail * detail = g_ptr_array_index(all_adl_details, ndx);
     Display_Ref * dref = create_adl_display_ref(detail->iAdapterIndex, detail->iDisplayIndex);
     dref->dispno = -1;
     dref->pedid = detail->pEdid;   // needed?
     // drec->detail.adl_detail = detail;
     dref->detail2 = detail;
     dref->flags |= DREF_DDC_IS_MONITOR_CHECKED;
     dref->flags |= DREF_DDC_IS_MONITOR;
     g_ptr_array_add(display_list, dref);
     // ddc_add_display_ref(display_list, dref);
  }

#ifdef USE_USB
   GPtrArray * usb_monitors = get_usb_monitor_list();
   // DBGMSF(debug, "Found %d USB displays", usb_monitors->len);
   for (int ndx=0; ndx<usb_monitors->len; ndx++) {
      Usb_Monitor_Info  * curmon = g_ptr_array_index(usb_monitors,ndx);
      assert(memcmp(curmon->marker, USB_MONITOR_INFO_MARKER, 4) == 0);
      Display_Ref * dref = create_usb_display_ref(
                                curmon->hiddev_devinfo->busnum,
                                curmon->hiddev_devinfo->devnum,
                                curmon->hiddev_device_name);
      dref->dispno = -1;
      dref->pedid = curmon->edid;
      // drec->detail.usb_detail = curmon;
      dref->detail2 = curmon;
      dref->flags |= DREF_DDC_IS_MONITOR_CHECKED;
      dref->flags |= DREF_DDC_IS_MONITOR;
      g_ptr_array_add(display_list, dref);
      // ddc_add_display_ref(display_list, dref);
   }
#endif

   // verbose output is distracting within scans
   // saved and reset here to that async threads are not adjusting output level
   DDCA_Output_Level olev = get_output_level();
   if (olev == DDCA_OL_VERBOSE)
      set_output_level(DDCA_OL_NORMAL);

   if (display_list->len >= ASYNC_THRESHOLD && all_adl_details->len == 0)
   // if (true)
      async_scan(display_list);
   else
      non_async_scan(display_list);

   if (olev == DDCA_OL_VERBOSE)
      set_output_level(olev);

   // assign display numbers
   for (int ndx = 0; ndx < display_list->len; ndx++) {
      Display_Ref * dref = g_ptr_array_index(display_list, ndx);
      assert( memcmp(dref->marker, DISPLAY_REF_MARKER, 4) == 0 );
      if (dref->flags & DREF_DDC_COMMUNICATION_WORKING) {
         dref->dispno = ++dispno_max;
      }
      else {
         dref->dispno = -1;
      }
   }

   // if (debug) {
   //    DBGMSG("Displays detected:");
   //    report_display_recs(display_list, 1);
   // }
   DBGMSF(debug, "Done. Detected %d valid displays", dispno_max);
   return display_list;
}


/** Initializes the master display list.
 *
 *  Does nothing if the list has already been initialized.
 */
void
ddc_ensure_displays_detected() {
   if (!all_displays) {
      i2c_detect_buses();
      all_displays = ddc_detect_all_displays();
   }
}

