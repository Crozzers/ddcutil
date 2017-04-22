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
   DBGMSF(debug, "Starting. dh=%s", dh_repr(dh));

   bool result = true;

   DDCA_Single_Vcp_Value * pvalrec;

   // verbose output is distracting since this function is called when querying for other things
   DDCA_Output_Level olev = get_output_level();
   if (olev == DDCA_OL_VERBOSE)
      set_output_level(DDCA_OL_NORMAL);

   Public_Status_Code psc = get_vcp_value(dh, 0x10, DDCA_NON_TABLE_VCP_VALUE, &pvalrec);

   if (olev == DDCA_OL_VERBOSE)
      set_output_level(olev);

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
   DBGMSF(debug, "Starting. dh=%s", dh_repr(dh));

   bool result = false;

   if (dh->dref->io_mode != DDCA_IO_USB) {

      DDCA_Single_Vcp_Value * pvalrec;

      // verbose output is distracting since this function is called when querying for other things
      DDCA_Output_Level olev = get_output_level();
      if (olev == DDCA_OL_VERBOSE)
         set_output_level(DDCA_OL_NORMAL);

      Public_Status_Code psc = get_vcp_value(dh, 0x00, DDCA_NON_TABLE_VCP_VALUE, &pvalrec);

      if (olev == DDCA_OL_VERBOSE)
         set_output_level(olev);

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
   DBGMSF(debug, "Starting. dh=%s", dh_repr(dh));

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
   // bool debug = false;
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


//
//  Display Specification
//

#ifdef OLD
// problem:  this function is doing 2 things:
//   reading brightness as a sanity check
//   looking up and saving vcp version

static bool
verify_adl_display_ref(Display_Ref * dref) {
   bool debug = true;
   bool result = true;
   Display_Handle * dh = NULL;
   Public_Status_Code psc = 0;

   psc = ddc_open_display(dref, CALLOPT_ERR_MSG | CALLOPT_ERR_ABORT, &dh);
   if (psc != 0)  {
      result = false;
      goto bye;
   }
   dref->vcp_version = get_vcp_version_by_display_handle(dh);
   ddc_close_display(dh);

   DDCA_Single_Vcp_Value * pvalrec;

   // Problem: ADL does not notice that a display doesn't support DDC,
   // e.g. Dell 1905FP
   // As a heuristic check, try reading the brightness.  Observationally, any monitor
   // that supports DDC allows for for brightness adjustment.

   // verbose output is distracting since this function is called when querying for other things
   DDCA_Output_Level olev = get_output_level();
   if (olev == DDCA_OL_VERBOSE)
      set_output_level(DDCA_OL_NORMAL);
   psc = get_vcp_value(dh, 0x10, DDCA_NON_TABLE_VCP_VALUE, &pvalrec);
   if (olev == DDCA_OL_VERBOSE)
      set_output_level(olev);

   if (psc != 0 && psc != DDCRC_REPORTED_UNSUPPORTED && psc != DDCRC_DETERMINED_UNSUPPORTED) {
      result = false;
      DBGMSF(debug, "Error getting value for brightness VCP feature 0x10. gsc=%s\n", psc_desc(psc) );
   }

 bye:
   return result;
}
#endif


#ifdef OLD
//  duplicative of verify_adl_display_ref()

/* Verify that a bus actually supports DDC by trying to read brightness
 *
 * SEE ALSO above function verify_adl_display_ref()
 *
 * PROBLEM: Can't Distinguish between display that really doesn't support DDC and
 * a buggy monitor like Dell P2411 that may fail because of timeout.
 *
 * Arguments:
 *    dref             display reference
 *
 * Returns:
 *    true if brightness read successful, false if not
 */
bool
ddc_verify(Display_Ref * dref) {
   bool debug = true;
   bool result = false;
   DBGMSF(debug, "Starting.  dref=%s", dref_repr(dref));
   // FAILSIM_BOOL_EXT is wrongly coded
   // FAILSIM_EXT( ( show_backtrace(1) ) );
   FAILSIM;

   Display_Handle * dh;

   Public_Status_Code psc = ddc_open_display(dref,  CALLOPT_NONE, &dh);
   if (psc == 0) {
      Parsed_Nontable_Vcp_Response * presp = NULL;
      // or could use get_vcp_value()
      psc = get_nontable_vcp_value(dh,
                             0x10,    // brightness
                        //     true,    // retry null response
                             &presp);
      DBGMSF(debug, "get_nontable_vcp_value() returned %s", psc_desc( psc));
      if (psc == 0) {
         free(presp);
         // result = true;
      }
      if (psc == 0 || psc == DDCRC_REPORTED_UNSUPPORTED || psc == DDCRC_DETERMINED_UNSUPPORTED)
         result = true;
      ddc_close_display(dh);
   }

   DBGMSF(debug, "Returning: %s", bool_repr(result));
   return result;
}

#endif

#ifdef OLD

bool
ddc_uses_null_response_to_indicate_unsupported(Display_Ref * dref) {
   bool debug = true;
   bool result = false;
   DBGMSF(debug, "Starting.  dref=%s", dref_repr(dref));

   Display_Handle * dh;
   Public_Status_Code psc = ddc_open_display(dref,  CALLOPT_NONE, &dh);
   if (psc == 0) {
      Parsed_Nontable_Vcp_Response * presp = NULL;
      // or could use get_vcp_value()
      psc = get_nontable_vcp_value(dh,
                             0x00,    // non-existent status code
                         //     false,    // retry null response
                             &presp);
      DBGMSF(debug, "get_nontable_vcp_value() returned %s", psc_desc( psc));
      if (psc == 0) {
         free(presp);
         // result = true;
      }
      if (psc == DDCRC_NULL_RESPONSE)
         result = true;
      ddc_close_display(dh);
   }

   DBGMSF(debug, "Returning: %s", bool_repr(result));
   return result;
}
#endif


#ifdef OLD

/** Tests if a Display_Ref identifies an attached display.
 *
 * @param   dref     display reference
 * @param   callopts standard call options
 *
 * @return true if **dref** identifies a valid Display_Ref, false if not
 */
// eliminate static to allow backtrace to find symbol
// static
bool
ddc_is_valid_display_ref(Display_Ref * dref, Call_Options callopts) {
   bool emit_error_msg = callopts & CALLOPT_ERR_MSG;
   bool debug = false;
   assert( dref );
   // char buf[100];
   DBGMSF(debug, "Starting. dref= %s, callopts=%s",
                 dref_short_name(dref), interpret_call_options(callopts) );
   bool result = false;
   switch(dref->io_mode) {
   case DDCA_IO_DEVI2C:
   {
      Call_Options callopts2;
      if (callopts & CALLOPT_FORCE)
         callopts2 = CALLOPT_ERR_MSG | CALLOPT_FORCE;
      else
         callopts2 = CALLOPT_NONE;
      result = i2c_is_valid_bus(dref->busno, callopts2 );

      if (result) {
         // have seen case where I2C bus for laptop display reports x37 active, but
         // in fact it doesn't support DDC

         if (!(dref->flags & DREF_DDC_COMMUNICATION_CHECKED)) {
            dref->flags |= DREF_DDC_COMMUNICATION_CHECKED;
            bool ddc_working = ddc_verify(dref);
            if (ddc_working)
               dref->flags |= DREF_DDC_COMMUNICATION_WORKING;
            dref->flags |= DREF_DDC_NULL_RESPONSE_CHECKED;
            if (ddc_uses_null_response_to_indicate_unsupported(dref))
               dref->flags |= DREF_DDC_USES_NULL_RESPONSE_FOR_UNSUPPORTED;
         }
         result = (dref->flags & DREF_DDC_COMMUNICATION_WORKING);
      }
   }
      break;
   case DDCA_IO_ADL:
      result = adlshim_is_valid_display_ref(dref, /* emit_error_msg */ false);
      if (result) {
         if (!(dref->flags & DREF_DDC_COMMUNICATION_CHECKED)) {
            dref->flags |= DREF_DDC_COMMUNICATION_CHECKED;
            bool ddc_working =  verify_adl_display_ref(dref);   // is it really a valid monitor?
            if (ddc_working)
               dref->flags |= DREF_DDC_COMMUNICATION_WORKING;
         }
         result = (dref->flags & DREF_DDC_COMMUNICATION_WORKING);

      }
      break;
   case DDCA_IO_USB:
#ifdef USE_USB
      result = usb_is_valid_display_ref(dref, /* emit_error_msg */ false);
#endif
      break;
   }

   // Turned off emit_erro_msg in lower level routines, so issue message here.
   // This is an intermediate step in removing messages from lower level functions.
   if (emit_error_msg) {
      char workbuf[100] = {'\0'};
      if (!result) {
         switch(dref->io_mode) {
         case DDCA_IO_DEVI2C:
            snprintf(workbuf, 100, "I2C display /dev/i2c-%d not found.\n",dref->busno);
            break;
         case DDCA_IO_ADL:
            snprintf(workbuf, 100, "ADL display %d.%d not found.\n",dref->iAdapterIndex, dref->iDisplayIndex);
            break;
         case DDCA_IO_USB:
             snprintf(workbuf, 100, "USB connected display %d.%d not found.\n",dref->usb_bus, dref->usb_device);
             break;
         }
      }

      else if (!(dref->flags & DREF_DDC_COMMUNICATION_WORKING) ) {
         switch(dref->io_mode) {
         case DDCA_IO_DEVI2C:
            snprintf(workbuf, 100, "I2C display /dev/i2c-%d does not support DDC.\n",dref->busno);
            break;
         case DDCA_IO_ADL:
            snprintf(workbuf, 100, "ADL display %d.%d does not support DDC.\n",dref->iAdapterIndex, dref->iDisplayIndex);
            break;
         case DDCA_IO_USB:
              // n. flags not currently used for USB IO
             // snprintf(workbuf, 100, "USB connected display %d.%d does not support DDC.\n",dref->usb_bus, dref->usb_devno);
             break;
         }
      }

      if (strlen(workbuf) > 0)
         f0printf(FERR, workbuf);
   }

   // n. flags not current used for USB_IO
   if (result && dref->io_mode != DDCA_IO_USB  && !(dref->flags & DREF_DDC_COMMUNICATION_WORKING))
      result = false;

   DBGMSF(debug, "Returning %s", bool_repr(result));
   return result;
}

#endif


#ifdef PRE_DISPLAY_REC

/**  Converts display identifier passed on the command line to a logical
 * reference to an I2C, ADL, or USB display.  If an I2C bus number, ADL adapter.display
 * number, or usb bus/device number is specified, the translation is direct.
 * Otherwise, the discovered displays are searched.
 *
 * @param   pdid      display identifiers
 * @param   callopts  standard call options
 *
 * @return #Display_Ref specifying the display using either an I2C bus number
 *    an ADL adapter/display number, or a USB bud/device number.\n
 *    NULL if display not found
 */

// OLD   validate  if searching was not necessary, validate that that bus number or
// OLD             ADL number does in fact reference an attached displ

Display_Ref*
get_display_ref_for_display_identifier_old(
                Display_Identifier* pdid,
                Call_Options        callopts)
{
   bool emit_error_msg = (callopts & CALLOPT_ERR_MSG);
   bool debug = false;
   DBGMSF(debug, "Starting. callopts=%s", interpret_call_options(callopts));
   Display_Ref* dref = NULL;
   bool validated = true;

   switch (pdid->id_type) {
   case DISP_ID_DISPNO:
      dref = ddc_find_display_by_dispno(pdid->dispno);
      if (!dref && emit_error_msg) {
         f0printf(FERR, "Invalid display number\n");
      }
      validated = false;
      break;
   case DISP_ID_BUSNO:
      dref = create_bus_display_ref(pdid->busno);
      validated = false;
      break;
   case DISP_ID_ADL:
      dref = create_adl_display_ref(pdid->iAdapterIndex, pdid->iDisplayIndex);
      validated = false;
      break;
   case DISP_ID_MONSER:
      dref = ddc_find_display_by_mfg_model_sn(
                pdid->mfg_id,
                pdid->model_name,
                pdid->serial_ascii,
                DISPSEL_VALID_ONLY);
      if (!dref && emit_error_msg) {
         f0printf(
            FERR,
            "Unable to find monitor with the specified manufacturer id/model/serial number\n");
      }
      break;
   case DISP_ID_EDID:
      dref = ddc_find_display_by_edid(pdid->edidbytes, DISPSEL_VALID_ONLY);
      if (!dref && emit_error_msg) {
         f0printf(FERR, "Unable to find monitor with the specified EDID\n" );
      }
      break;
   case DISP_ID_USB:
#ifdef USE_USB
      dref = ddc_find_display_by_usb_busnum_devnum(pdid->usb_bus, pdid->usb_device);
      if (!dref && emit_error_msg) {
         f0printf(FERR, "Unable to find monitor with the specified USB bus and device numbers\n");
      }
#else
      if (emit_error_msg)
         f0printf(FERR, "ddcutil not built with USB support\n");
      // PROGRAM_LOGIC_ERROR("ddcutil not built with USB support");
#endif
      break;
   }  // switch - no default case, switch is exhaustive

   if (dref) {
      if (!validated)      // DISP_ID_BUSNO or DISP_ID_ADL
        validated = ddc_is_valid_display_ref(dref, callopts | CALLOPT_ERR_MSG);
      if (!validated) {
         if (callopts & CALLOPT_FORCE) {
            f0printf(FERR, "Monitor validation failed.  Continuing.\n");
         }
         else {
            free(dref);
            dref = NULL;
         }
      }
   }

   if (debug) {
      if (dref)
         DBGMSG("Returning: %p  %s", dref, dref_repr(dref) );
      else
         DBGMSG("Returning: NULL");
   }
   return dref;
}
#endif


#ifdef REDUNDANT
void report_display_info(Display_Info * dinfo, int depth) {
   const int d1 = depth+1;
   const int d2 = depth+2;
   rpt_structure_loc("Display_Info", dinfo, depth);
   rpt_int("dispno", NULL, dinfo->dispno, d1);
   rpt_vstring(d1, "dref: %p",   dinfo->dref);
   if (dinfo->dref)
      report_display_ref(dinfo->dref, d2);
   rpt_vstring(d1, "edid: %p",   dinfo->edid);
   if (dinfo->edid)
      report_parsed_edid(dinfo->edid, false /* verbose */,  d2);
}
#endif



//
// Functions to get display information
//

#ifdef OLD
/** Gets a list of valid monitors in the format needed by the API.
 *
 * \return pointer to #Display_Info_List
 */
Display_Info_List *
ddc_get_valid_displays() {
   ddc_ensure_displays_detected();

   int displayct = dispno_max;

   Display_Info_List * info_list = calloc(1, sizeof(Display_Info_List));
   info_list->ct = displayct;
   if (displayct > 0) {
      info_list->info_recs = calloc(displayct, sizeof(Display_Info));
      int displayctr = 0;
      int inforecctr = 0;
      for (displayctr = 0; displayctr < all_displays->len; displayctr++) {
         Display_Ref * dref = g_ptr_array_index(all_displays, displayctr);
         assert(memcmp(dref->marker, DISPLAY_REF_MARKER, 4) == 0);
         if (dref->dispno > 0) {
            Display_Info * curinfo =  info_list->info_recs + inforecctr;
            curinfo->dispno = dref->dispno;
            curinfo->dref   = dref;
            curinfo->edid   = dref->pedid;
            memcpy(curinfo->marker, DISPLAY_INFO_MARKER, 4);
            inforecctr++;
         }
      }
   }
   return info_list;
}
#endif


/** Gets a list of all detected displays, whether they support DDC or not.
 *
 *  Initializes the list of detected monitors if necessary.
 *
 *  \return **GPtrArray of #Display_Ref instances
 */
GPtrArray * ddc_get_all_displays() {
   ddc_ensure_displays_detected();

   return all_displays;
}


#ifdef PRE_DISPLAY_REC

/** Creates a list of all displays found.  The list first contains displays
 * on /dev/i2c-n buses, then ADL displays, then USB connected displays.
 *
 * The displays are assigned a display number (starting from 1) based on the
 * above order.
 *
 * Arguments: none
 *
 * @return pointer to newly allocated #Display_Info_List struct
 */
Display_Info_List *
ddc_get_valid_displays_old() {
   bool debug = false;
   DBGMSF(debug, "Starting");
   int ndx;

   Display_Info_List i2c_displays = i2c_get_displays_old();
   if (debug) {
      DBGMSG("i2c_displays returned from i2c_get_displays():");
      report_display_info_list(&i2c_displays,1);
   }

   Display_Info_List adl_displays = adlshim_get_valid_displays();
   if (debug) {
      DBGMSG("adl_displays returned from adlshim_get_valid_displays():");
      report_display_info_list(&adl_displays,1);
   }

#ifdef USE_USB
   Display_Info_List usb_displays = usb_get_valid_displays();
   if (debug) {
      DBGMSG("usb_displays returned from usb_get_valid_displays():");
      report_display_info_list(&usb_displays,1);
   }
#endif

   // merge the lists
   int displayct = i2c_displays.ct + adl_displays.ct;
#ifdef USE_USB
   displayct += usb_displays.ct;
#endif
   Display_Info_List * all_displays = calloc(1, sizeof(Display_Info_List));
   all_displays->ct = displayct;
   if (displayct > 0) {
      all_displays->info_recs = calloc(displayct, sizeof(Display_Info));

      if (i2c_displays.ct > 0)
         memcpy(all_displays->info_recs,
                i2c_displays.info_recs,
                i2c_displays.ct * sizeof(Display_Info));

      if (all_displays->ct > 0)
         memcpy(all_displays->info_recs + i2c_displays.ct,
                adl_displays.info_recs,
                adl_displays.ct * sizeof(Display_Info));

   #ifdef USE_USB
      if (usb_displays.ct > 0)
         memcpy(all_displays->info_recs + (i2c_displays.ct+adl_displays.ct),
                usb_displays.info_recs,
                usb_displays.ct * sizeof(Display_Info));
   #endif
   }

   if (i2c_displays.info_recs)
      free(i2c_displays.info_recs);
   if (adl_displays.info_recs)
      free(adl_displays.info_recs);
#ifdef USE_USB
   if (usb_displays.info_recs)
      free(usb_displays.info_recs);
#endif
   // rpt_title("merged list:", 0);
   int displayctr = 1;
   for (ndx = 0; ndx < displayct; ndx++) {
      // report_display_info(&all_displays->info_recs[ndx],1);
      if (ddc_is_valid_display_ref(all_displays->info_recs[ndx].dref, CALLOPT_NONE)) {
         all_displays->info_recs[ndx].dispno = displayctr++;  // displays are numbered from 1, not 0
      }
      else {
         // Do not assign display number in case of I2C bus entry that isn't in fact a display
         // that supports DDC
         all_displays->info_recs[ndx].dispno = -1;
      }
   }

   if (debug) {
      DBGMSG("Returning merged list:");
      report_display_info_list(all_displays, 1);
   }
   return all_displays;
}
#endif

#ifdef PRE_DISPLAY_REC
/*8 Returns a #Display_Ref for the nth display.
 *
 * @param dispno     display number
 *
 * @return  #Display_Ref for the dispno'th display,\n
 *          NULL if dispno < 1 or dispno > number of actual displays
 */
Display_Ref*
ddc_find_display_by_dispno(int dispno) {
   bool debug = false;
   DBGMSF(debug, "Starting.  dispno=%d", dispno);

   Display_Ref * result = NULL;
   Display_Info_List * all_displays = ddc_get_valid_displays_old();
   if (dispno >= 1 && dispno <= all_displays->ct) {
      // we're not done yet.   There may be an invalid display in the list.
      int ndx;
      for (ndx=0; ndx<all_displays->ct; ndx++) {
         if (all_displays->info_recs[ndx].dispno == dispno) {
            result = clone_display_ref(all_displays->info_recs[ndx].dref);
            break;
         }
      }
   }
   free_display_info_list(all_displays);

   DBGMSF(debug, "Returning: %p  %s", result, (result)?dref_repr(result):"" );
#ifdef OLD
   if (debug) {
      DBGMSG("Returning: %p  ", result );
      if (result)
         report_display_ref(result, 0);
   }
#endif

   return result;
}


/** Returns a Display_Ref for a display identified by its manufacturer id,
 * model name and/or serial number.
 *
 * Arguments:
 *    mfg_id   manufacturer id
 *    model    model name
 *    sn       serial number (character string)
 *    findopts selection options
 *
 * Returns:
 *    Display_Ref for the specified monitor
 *    NULL if not found
 */
Display_Ref*
ddc_find_display_by_mfg_model_sn(
   const char * mfg_id,
   const char * model,
   const char * sn,
   Display_Selection_Options findopts)
{
   bool debug = false;
   DBGMSF(debug, "Starting.  model=%s, sn=%s, findopts=0x%02x", model, sn, findopts );

   Display_Ref * result = NULL;
   Bus_Info * businfo = i2c_find_bus_info_by_mfg_model_sn(mfg_id, model, sn, findopts);
   if (businfo) {
      result = create_bus_display_ref(businfo->busno);
   }

   if (!result)
      result = adlshim_find_display_by_mfg_model_sn(mfg_id, model, sn);

#ifdef USE_USB
   if (!result)
      result = usb_find_display_by_mfg_model_sn(mfg_id, model, sn);
#endif

   DBGMSF(debug, "Returning: %p  %s", result, (result)?dref_repr(result):"" );
   return result;
}


#ifdef USE_USB
Display_Ref *
ddc_find_display_by_usb_busnum_devnum(
   int   busnum,
   int   devnum)
{
   bool debug = false;
   DBGMSF(debug, "Starting.  busnum=%d, devnum=%d", busnum, devnum);
   // printf("(%s) WARNING: Support for USB devices unimplemented\n", __func__);

   Display_Ref * result = usb_find_display_by_busnum_devnum(busnum, devnum);

   DBGMSF(debug, "Returning: %p  %s", result, (result)?dref_repr(result):"" );
   return result;
}
#endif


/* Returns a Display_Ref for a display identified by its EDID
 *
 * Arguments:
 *    edid     pointer to 128 byte edid
 *
 * Returns:
 *    Display_Ref for the specified monitor
 *    NULL if not found
 */
Display_Ref*
ddc_find_display_by_edid(const Byte * pEdidBytes, Byte findopts) {
   bool debug = false;
   DBGMSF(debug, "Starting.  pEdidBytes=%p, findopts=0x%02x", pEdidBytes, findopts );
   Display_Ref * result = NULL;
   Bus_Info * businfo = i2c_find_bus_info_by_edid(pEdidBytes, findopts);
   if (businfo) {
      result = create_bus_display_ref(businfo->busno);
   }

   if (!result)
      result = adlshim_find_display_by_edid(pEdidBytes);

#ifdef USE_USB
   if (!result)
      result = usb_find_display_by_edid(pEdidBytes);
#endif

   DBGMSF(debug, "Returning: %p  %s", result, (result)?dref_repr(result):"" );
   return result;
}
#endif

#ifdef OLD

/** Shows information about a display.
 *
 * Output is written using report functions
 *
 * \param curinfo   pointer to display information
 * \param depth     logical indentation depth
 */
void
ddc_report_active_display_old(Display_Info * curinfo, int depth) {
   assert(memcmp(curinfo->marker, DISPLAY_INFO_MARKER, 4) == 0);
   switch(curinfo->dref->io_mode) {
   case DDCA_IO_DEVI2C:
      i2c_report_active_display_by_busno(curinfo->dref->busno, depth);
      break;
   case DDCA_IO_ADL:
      adlshim_report_active_display_by_display_ref(curinfo->dref, depth);
      break;
   case DDCA_IO_USB:
#ifdef USE_USB
      usb_show_active_display_by_display_ref(curinfo->dref, depth);
#else
      PROGRAM_LOGIC_ERROR("ddcutil not built with USB support");
#endif
      break;
   }

   DDCA_Output_Level output_level = get_output_level();
   if (output_level >= DDCA_OL_NORMAL) {
      if (!ddc_is_valid_display_ref(curinfo->dref, CALLOPT_NONE)) {
         rpt_vstring(depth, "DDC communication failed");
         if (output_level >= DDCA_OL_VERBOSE) {
            rpt_vstring(depth, "Is DDC/CI enabled in the monitor's on-screen display?");
         }
      }
      else {
         // n. requires write access since may call get_vcp_value(), which does a write
         Display_Handle * dh = NULL;
         Public_Status_Code psc = ddc_open_display(curinfo->dref, CALLOPT_ERR_MSG, &dh);
         if (psc != 0) {
            rpt_vstring(depth, "Error opening display %s, error = %s",
                               dref_short_name(curinfo->dref), psc_desc(psc));
         }
         else {
                // char * short_name = dref_short_name(curinfo->dref);
                // printf("Display:       %s\n", short_name);
                // works, but TMI
                // printf("Mfg:           %s\n", cur_info->edid->mfg_id);
            // don't want debugging  output if OL_VERBOSE
            if (output_level >= DDCA_OL_VERBOSE)
               set_output_level(DDCA_OL_NORMAL);

            DDCA_MCCS_Version_Spec vspec = get_vcp_version_by_display_handle(dh);

            // printf("VCP version:   %d.%d\n", vspec.major, vspec.minor);
            if (vspec.major == 0)
               rpt_vstring(depth, "VCP version:         Detection failed");
            else
               rpt_vstring(depth, "VCP version:         %d.%d", vspec.major, vspec.minor);

            if (output_level >= DDCA_OL_VERBOSE) {
               // display controller mfg, firmware version
               char mfg_name_buf[100];
               char * mfg_name         = "Unspecified";

               // char * firmware_version = "Unspecified";

               // n. get_nontable_vcp_value() does not know how to handle USB devices, but its
               // caller, get_vcp_value() does
               DDCA_Single_Vcp_Value *   valrec;
               psc = get_vcp_value(dh, 0xc8, DDCA_NON_TABLE_VCP_VALUE, &valrec);

               if (psc != 0) {
                  if (psc != DDCRC_REPORTED_UNSUPPORTED && psc != DDCRC_DETERMINED_UNSUPPORTED) {
                      DBGMSG("get_nontable_vcp_value(0xc8) returned %s", psc_desc(psc));
                      mfg_name = "DDC communication failed";
                  }
               }
               else {
                  DDCA_Feature_Value_Entry * vals = pxc8_display_controller_type_values;
                  mfg_name =  get_feature_value_name(
                                        vals,
                                        valrec->val.nc.sl);
                  if (!mfg_name) {
                     snprintf(mfg_name_buf, 100, "Unrecognized manufacturer code 0x%02x", valrec->val.nc.sl);
                     mfg_name = mfg_name_buf;
                  }
               }
               rpt_vstring(depth,    "Controller mfg:      %s", mfg_name);

               psc = get_vcp_value(dh, 0xc9, DDCA_NON_TABLE_VCP_VALUE, &valrec);  // xc9 = firmware version
               if (psc != 0) {
                  char * version = "Unspecified";
                  if (psc != DDCRC_REPORTED_UNSUPPORTED && psc != DDCRC_DETERMINED_UNSUPPORTED) {
                     DBGMSG("get_vcp_value(0xc9) returned %s", psc_desc(psc));
                     version = "DDC communication failed";
                  }
                  rpt_vstring(depth, "Firmware version:    %s", version);
               }
               else if (psc == 0) {
                  rpt_vstring(depth, "Firmware version:    %d.%d",
                        // code_info->sh, code_info->sl);
                        valrec->val.nc.sh, valrec->val.nc.sl);
               }

            }


            if (output_level >= DDCA_OL_VERBOSE)
               set_output_level(output_level);

            if (output_level >= DDCA_OL_VERBOSE)
               rpt_vstring(depth, "DDC Null Response may indicate unsupported: %s",
                                  bool_repr(dh->dref->flags & DREF_DDC_USES_NULL_RESPONSE_FOR_UNSUPPORTED));
         }

         ddc_close_display(dh);
      }
   }
}
#endif


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
   bool debug = true;

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
   bool debug = true;

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
      i2c_report_active_display_by_busno(dref->busno, d1);
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


#ifdef NO_LONGER_USED
void
ddc_report_display_by_display_rec(Display_Rec * drec, int depth) {
   bool debug = false;
   DBGMSF(debug, "Starting");
   assert(memcmp(drec->marker, DISPLAY_REC_MARKER, 4) == 0);
   Display_Ref  * dref = drec->dref;

#ifdef MOVED
   if (drec->dispno == -1)
      rpt_vstring(depth, "Invalid display");
   else
      rpt_vstring(depth, "Display %d", drec->dispno);
#endif

   ddc_report_display_by_dref(dref, depth);
   DBGMSF(debug, "Done");
}
#endif

#ifdef NO_LONGER_USED
void
ddc_report_active_display(Display_Info * curinfo, int depth) {
   bool debug = true;
   DBGMSF(debug, "Starting");
   assert(memcmp(curinfo->marker, DISPLAY_INFO_MARKER, 4) == 0);
   Display_Ref  * dref = curinfo->dref;
   ddc_report_display_by_dref(dref, depth);
   DBGMSF(debug, "Done");
}
#endif


#ifdef OLd
/** Reports valid displays found.
 *
 * Output is written to the current report destination using
 * report functions.
 *
 * @param   depth       logical indentation depth
 *
 * @return number of displays
 *
 * \remark
 * For C API
 */
int
ddc_report_active_displays(int depth) {
    bool debug = false;
    DBGMSF(debug, "Starting");
   // PROGRAM_LOGIC_ERROR("---> pseudo failure <-----");
   Display_Info_List * display_list = ddc_get_valid_displays();
   int ndx;
   int valid_display_ct = 0;
   for (ndx=0; ndx<display_list->ct; ndx++) {
      Display_Info * curinfo = &display_list->info_recs[ndx];
#ifdef MOVED
      if (curinfo->dispno == -1)
         rpt_vstring(depth, "Invalid display");
      else {
         rpt_vstring(depth, "Display %d", curinfo->dispno);
         valid_display_ct++;
      }
#endif
      if (curinfo->dispno != -1)
         valid_display_ct++;
     // ddc_report_active_display(curinfo, depth+1);
      ddc_report_display_by_dref(curinfo->dref, depth);
      rpt_title("",0);
   }
   if (valid_display_ct == 0)
      rpt_vstring(depth, "No active displays found");
   free_display_info_list(display_list);
   // DBGMSG("Returning %d", valid_display_ct);
   DBGMSF(debug, "Done.  Returning: %d", valid_display_ct);
   return valid_display_ct;
}
#endif

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



// new way

/** Debugging function to display the contents of a #Display_Ref.
 *
 * \param dref  pointer to #Display_Ref
 * \param depth logical indentation depth
 */
void debug_report_display_ref(Display_Ref * dref, int depth) {
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
         report_businfo(businfo, d2);
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
         rpt_vstring(d1, "USB device information: ");
         Usb_Monitor_Info * moninfo = dref->detail2;
         assert(memcmp(moninfo->marker, USB_MONITOR_INFO_MARKER, 4) == 0);
         report_usb_monitor_info(moninfo, d2);
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
      debug_report_display_ref(drec, depth+1);
   }
}


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


/** Adds a display to the list of detected displays.
 *
 * \param all_displays   list to add to
 * \param pointer to #Display_Ref to add
 *
 * \remark
 * Initial monitor cheks are performed.  (Does this belong here?)
 * \remark
 * This function is used during program initialization.
 * In the future, it could be used to dynamically add nely
 * connected monitors if the library is long running.
 */
static void
ddc_add_display_ref(GPtrArray * all_displays, Display_Ref * dref) {
   bool debug = true;
   DBGMSF(debug, "Starting. dref=%s", dref_repr(dref));
   if (dref->dispno < 0) {
      // check if valid display, etc.  (Does this belong here?)
      if (initial_checks_by_dref(dref)) {
         dref->dispno = ++dispno_max;
      }
      else {
         dref->dispno = -1;
      }
   }
   g_ptr_array_add(all_displays, dref);
   DBGMSF(debug, "Done. dispno = %d", dref->dispno);
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
         debug_report_display_ref(result, 1);
      }
      else
         DBGMSG("Done.  Returning NULL");
   }

   return result;
}

#ifdef OLD
/** Searches the master display list for a display matching the given #Display_Identifier,
 *  returning its #Display_Ref.
 *
 *  @param  did  pointer to #Display_Identifier
 *  @return #Display_Ref for the identifier, NULL if not found
 *
 *  * \remark
 * The returned value is a pointer into an internal data structure
 * and should not be freed by the caller.
 */
Display_Ref *
ddc_find_dref_by_did(Display_Identifier * did) {
   Display_Ref * dref = NULL;
   Display_Rec * drec = ddc_find_display_ref_by_display_identifier(did);
   if (drec)
      dref = drec->dref;

   return dref;
}
#endif


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

   GPtrArray * display_list = g_ptr_array_new();

   int busct = i2c_get_busct();
   int busndx = 0;
   for (busndx=0; busndx < busct; busndx++) {
      Bus_Info * businfo = i2c_get_bus_info_by_index(busndx);
      if ( (businfo->flags & I2C_BUS_ADDR_0X50) ) {
         Display_Ref * dref = create_bus_display_ref(businfo->busno);

         // Transition:
         // Display_Rec * drec = calloc(1,sizeof(Display_Rec));
         // Display_Rec * drec = dref;
         // memcpy(drec->marker, DISPLAY_REC_MARKER, 4);
         dref->dispno = -1;
         // dref->dref = dref;   // was drec->dref

         dref->pedid = businfo->edid;    // needed?
         // drec->detail.bus_detail = businfo;
         dref->detail2 = businfo;
         dref->flags |= DREF_DDC_IS_MONITOR_CHECKED;
         dref->flags |= DREF_DDC_IS_MONITOR;
         ddc_add_display_ref(display_list, dref);

         // DBGMSG("======= as dref:");
         // report_display_ref(dref, 1);
         // DBGMSG("========as drec: ");
         // report_display_rec(dref, 1);
      }
   }

  GPtrArray * all_details = adlshim_get_valid_display_details();
  int adlct = all_details->len;
  for (int ndx = 0; ndx < adlct; ndx++) {
     ADL_Display_Detail * detail = g_ptr_array_index(all_details, ndx);
     Display_Ref * dref = create_adl_display_ref(detail->iAdapterIndex, detail->iDisplayIndex);

     // transition
     // Display_Rec * drec = calloc(1, sizeof(Display_Rec));
     // memcpy(drec->marker, DISPLAY_REC_MARKER, 4);
     // Display_Rec * drec = dref;

     dref->dispno = -1;
     // dref->dref = dref;   // for transition

     dref->pedid = detail->pEdid;   // needed?
     // drec->detail.adl_detail = detail;
     dref->detail2 = detail;
     dref->flags |= DREF_DDC_IS_MONITOR_CHECKED;
     dref->flags |= DREF_DDC_IS_MONITOR;
     ddc_add_display_ref(display_list, dref);
  }

   GPtrArray * usb_monitors = get_usb_monitor_list();
   // DBGMSF(debug, "Found %d USB displays", usb_monitors->len);
   for (int ndx=0; ndx<usb_monitors->len; ndx++) {
      Usb_Monitor_Info  * curmon = g_ptr_array_index(usb_monitors,ndx);
      assert(memcmp(curmon->marker, USB_MONITOR_INFO_MARKER, 4) == 0);
      Display_Ref * dref = create_usb_display_ref(
                                curmon->hiddev_devinfo->busnum,
                                curmon->hiddev_devinfo->devnum,
                                curmon->hiddev_device_name);

      // Transition
      // Display_Rec * drec = calloc(1, sizeof(Display_Rec));
      // memcpy(drec->marker, DISPLAY_REC_MARKER, 4);
      // Display_Rec * drec = dref;

      dref->dispno = -1;
      // dref->dref = dref;     // was drec->dref
      dref->pedid = curmon->edid;
      // drec->detail.usb_detail = curmon;
      dref->detail2 = curmon;
      dref->flags |= DREF_DDC_IS_MONITOR_CHECKED;
      dref->flags |= DREF_DDC_IS_MONITOR;
      ddc_add_display_ref(display_list, dref);
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
      all_displays = ddc_detect_all_displays();
   }
}

