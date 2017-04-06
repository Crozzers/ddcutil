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

#include "base/adl_errors.h"
#include "base/ddc_errno.h"
#include "base/ddc_packets.h"
#include "base/linux_errno.h"
#include "base/parms.h"

#include "vcp/vcp_feature_codes.h"

#include "i2c/i2c_bus_core.h"
#include "i2c/i2c_do_io.h"

#include "adl/adl_shim.h"

#ifdef USE_USB
#include "usb/usb_displays.h"
#endif

#include "ddc/ddc_packet_io.h"
#include "ddc/ddc_vcp.h"
#include "ddc/ddc_vcp_version.h"

#include "ddc/ddc_displays.h"



// Trace class for this file
// static Trace_Group TRACE_GROUP = TRC_DDC;   // currently unused

// forward references
Display_Ref * ddc_find_display_by_usb_busnum_devnum(int   busnum, int   devnum);


//
//  Display Specification
//

// problem:  this function is doing 2 things:
//   reading brightness as a sanity check
//   looking up and saving vcp version

static bool
verify_adl_display_ref(Display_Ref * dref) {
   bool debug = false;
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
   bool debug = false;
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


// what to return is TBD

bool
ddc_uses_null_response_to_indicate_unsupported(Display_Ref * dref) {
   bool debug = false;
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
             // snprintf(workbuf, 100, "USB connected display %d.%d does not support DDC.\n",dref->usb_bus, dref->usb_device);
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
get_display_ref_for_display_identifier(
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
ddc_get_valid_displays() {
   bool debug = false;
   DBGMSF(debug, "Starting");
   int ndx;

   Display_Info_List i2c_displays = i2c_get_displays();
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
   Display_Info_List * all_displays = ddc_get_valid_displays();
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


/* Show information about a display.
 *
 * Output is written using report functions
 *
 * Arguments:
 *    curinfo   pointer to display information
 *    depth     logical indentation depth
 */
void
ddc_report_active_display(Display_Info * curinfo, int depth) {
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
               rpt_vstring(depth, "Uses DDC Null Response to indicate unspported: %s",
                                  bool_repr(dh->dref->flags & DREF_DDC_USES_NULL_RESPONSE_FOR_UNSUPPORTED));
         }

         ddc_close_display(dh);
      }
   }
}


/** Reports all displays found.
 *
 * Output is written to the current report destination using
 * report functions.
 *
 * @param   depth       logical indentation depth
 *
 * @return number of displays
 */
int
ddc_report_active_displays(int depth) {
   // PROGRAM_LOGIC_ERROR("---> pseudo failure <-----");
   Display_Info_List * display_list = ddc_get_valid_displays();
   int ndx;
   int valid_display_ct = 0;
   for (ndx=0; ndx<display_list->ct; ndx++) {
      Display_Info * curinfo = &display_list->info_recs[ndx];
      if (curinfo->dispno == -1)
         rpt_vstring(depth, "Invalid display");
      else {
         rpt_vstring(depth, "Display %d", curinfo->dispno);
         valid_display_ct++;
      }
      ddc_report_active_display(curinfo, depth+1);
      rpt_title("",0);
   }
   if (valid_display_ct == 0)
      rpt_vstring(depth, "No active displays found");
   free_display_info_list(display_list);
   // DBGMSG("Returning %d", valid_display_ct);
   return valid_display_ct;
}

