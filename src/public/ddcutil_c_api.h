/* ddcutil_c_api.h
 *
 * Public C APi for ddcutil. .
 *
 * <copyright>
 * Copyright (C) 2014-2018 Sanford Rockowitz <rockowitz@minsoft.com>
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

/** \f
 *  Public C API for ddcutil
 */

#ifndef DDCUTIL_C_API_H_
#define DDCUTIL_C_API_H_

/** \cond */
// #include <setjmp.h>    // obsolete
#include <stdbool.h>
#include <stdio.h>
/** \endcond */

// is this the right location?
#ifdef __cplusplus
extern "C" {
#endif

#include "ddcutil_types.h"


/** @file ddcutil_c_api.h
 *  @brief ddcutil public C API
 *
 *  Function names in the public C API begin with "ddca_"\n
 *  Typedefs, constants, etc. begin with "DDCA_".
 */


/* Note on "report" functions.
 *
 * Various functions with "report" in the name, e.g. ddca_report_display_ref(),
 * ddca_report_display_info_list(), write formatted reports to (normally) the
 * terminal. Sometimes, these are intended to display data structures for
 * debugging.  Other times, they are used to format output for the ddcutil
 * command line program.
 *
 * The operation of these functions can be tweaked in two ways.
 * - The "depth" parameter is a logical indentation depth.  This allows
 *   reports that invoke other reports to indent the subreports
 *   sensibly.  At the level of the ddcutil_c_api(), one unit of
 *   logical indentation depth translates to 3 spaces.
 * - The destination of reports is normally the STDOUT device.  This can
 *   be changed by calling set_fout().
 */


//
// Library build information
//

/**
 * Returns the ddcutil version as a struct of 3 8 bit integers.
 *
 * @return version numbers
 */
DDCA_Ddcutil_Version_Spec ddca_ddcutil_version(void);       // ddcutil version

/**
 * Returns the ddcutil version as a string in the form "major.minor.micro".
 *
 * @return version string.
 */
const char * ddca_ddcutil_version_string(void);


/** Queries the options with which the **ddcutil** library was built.
 *
 * @return  flags byte
 *
 *
 * | Defined Bit  | |
 * |:-------| :--------------
 * |#DDCA_BUILT_WITH_ADL  | built with ADL support
 * |#DDCA_BUILT_WITH_USB  | built with USB support
 * |#DDCA_BUILT_WITH_FAILSIM | built with failure simulation
 *
 * Defined Bits
 * <table>
 * <tr><td>#DDCA_BUILT_WITH_ADL</td><td>built with ADL support</td>
 * <tr><td>#DDCA_BUILT_WITH_USB</td><td>built with USB support
 * <tr><td>#DDCA_BUILT_WITH_FAILSIM</td><td> built with failure simulation
 * </table>
 *
 */
uint8_t ddca_build_options(void);

// Bit ids for ddca_get_build_options() - how to make connection in doxygen?
/** Build option flags
 *
 * Bit field definitions
 */
typedef enum {
   /** @brief ddcutil was built with support for AMD Display Library connected monitors */
   DDCA_BUILT_WITH_ADL     = 0x01,
   /** @brief ddcutil was built with support for USB connected monitors */
   DDCA_BUILT_WITH_USB     = 0x02,
  /** @brief ddcutil was built with support for failure simulation */
   DDCA_BUILT_WITH_FAILSIM = 0x04
} DDCA_Build_Option_Flags;


//
// Initialization
//

#ifdef NOT_NEEDED
/**
 * Initializes the ddcutil library module.
 *
 * Must be called before most other functions.
 *
 * It is not an error if this function is called more than once.
 */
// void __attribute__ ((constructor)) _ddca_init(void);
#endif


//
// Status Codes
//

/** Returns the symbolic name for a ddcutil status code
 * @param status_code numeric status code
 * @return symbolic name, e.g. EBUSY, DDCRC_INVALID_DATA
 *  */
char * ddca_rc_name(DDCA_Status status_code);

/** Returns a description of a ddcutil status code
 * @param status_code numeric status code
 * @return explanation of status code, e.g. "device or resource busy"
 */
char * ddca_rc_desc(DDCA_Status status_code);


//
// MCCS Version Id
//

/** Returns the symbolic name of a #DDCA_MCCS_Version_Id,
 *  e.g. "DDCA_V20."
 *
 *  @param  version_id  version id value
 *  @return symbolic name
 */
char *
ddca_mccs_version_id_name(
      DDCA_MCCS_Version_Id  version_id);

/** Returns the descriptive name of a #DDCA_MCCS_Version_Id,
 *  e.g. "2.0".
 *
 *  @param  version_id  version id value
 *  @return descriptive name
 */
char *
ddca_mccs_version_id_desc(
      DDCA_MCCS_Version_Id  version_id);


//
// Global Settings
//

/***
I2C is an inherently unreliable protocol.  The application is responsible for
retry management.
The maximum number of retries can be tuned.
There are 3 retry contexts:
- An i2C write followed by a read.  Most DDC operations are of this form.
- An I2C write without a subsequent read.  DDC operations to set a VCP feature value
  are in this category.
- Some DDC operations, such as reading the capabilities string, require multiple
  write/read exchanges.  These multi -part exchanges have a separate retry count
  for the entire operation.
*/
///@{
/** Gets the upper limit on a max tries value that can be set.
 *
 * @return maximum max tries value allowed on set_max_tries()
 */
int ddca_max_max_tries(void);

/** Gets the maximum number of I2C retries for the specified operation type.
 * @param  retry_type   I2C operation type
 * @return maximum number of retries
 */
int ddca_get_max_tries(
       DDCA_Retry_Type retry_type);

/** Sets the maximum number of I2C retries for the specified operation type
 * @param retry_type    I2C operation type
 * @param max_tries     maximum count to set
 *
 *  *
 * \retval EINVAL       max_tries < 1 or > #ddca_get_max_tries()
 */
DDCA_Status
ddca_set_max_tries(
      DDCA_Retry_Type retry_type,
      int             max_tries);
///@}

/** Controls whether VCP values are read after being set.
 *
 * \param onoff true/false
 */
void ddca_enable_verify(bool onoff);

/** Query whether VCP values are read after being set.
 * \retval true values are verified after being set
 * \retval false values are not verified
 */
bool ddca_is_verify_enabled(void);


//
// Message Control
//

/** Redirects output that normally would go to STDOUT
 */
void ddca_set_fout(
        FILE * fout);   /**< where to write normal messages, if NULL, suppress  */

/** Redirects output that normally goes to STDOUT back to STDOUT */
void ddca_set_fout_to_default(void);

/** Redirects output that normally would go to STDERR
 */
void ddca_set_ferr(
        FILE * ferr);   /**< where to write error messages, If NULL, suppress */

/** Redirects output that normally goes to STDERR back to STDERR
 */
void ddca_set_ferr_to_default(void);


/** Gets the current output level */
DDCA_Output_Level                   /**< current output level */
ddca_get_output_level(void);

/** Sets the output level */
void ddca_set_output_level(
      DDCA_Output_Level newval);   /**< new output level */

/** Gets the name of an output level
 * @param  val  output level id
 * @return output level name (do not free)
 */
char * ddca_output_level_name(
          DDCA_Output_Level val);   /**< output level id */


/** Controls whether messages describing DDC protocol errors are output
 * @param onoff    if true, errors will be issued
 * */
void ddca_enable_report_ddc_errors(bool onoff);

/** Checks whether messages describing DDC protocol errors are output */
bool ddca_is_report_ddc_errors_enabled(void);


//
// Statistics
//

/** Resets all **ddcutil** statistics */
void ddca_reset_stats(void);

/** Show execution statistics.
 *
 *  \param stats  bitflags of statistics types to show
 *  \param depth  logical indentation depth
 */
void ddca_show_stats(DDCA_Stats_Type stats, int depth);


//
// Display Descriptions
//

/** Gets a list of the detected displays.
 *
 *  Displays that do not support DDC are not included.
 *
 *  @return list of display summaries
 */
DDCA_Display_Info_List *
ddca_get_display_info_list(void);


/** Frees a list of detected displays.
 *
 *  This function understands which fields in the list
 *  point to permanently allocated data structures and should
 *  not be freed.
 *
 *  \param dlist pointer to #DDCA_Display_Info_List
 */
void ddca_free_display_info_list(DDCA_Display_Info_List * dlist);


/** Presents a report on a single display.
 *  The report is written to the current FOUT device.
 *
 *  @param[in]  dinfo  pointer to a DDCA_Display_Info struct
 *  @param[in]  depth  logical indentation depth
 */
void
ddca_report_display_info(
      DDCA_Display_Info * dinfo,
      int                 depth);

/** Reports on all displays in a list of displays.
 *  The report is written to the current FOUT device
 *
 *  @param[in]  dlist  pointer to a DDCA_Display_Info_List
 *  @param[in]  depth  logical indentation depth
 */
void
ddca_report_display_info_list(
      DDCA_Display_Info_List * dlist,
      int                      depth);

/** Reports on all active displays.
 *  This function hooks into the code used by command "ddcutil detect"
 *
 *  @param[in] depth  logical indentation depth
 *  @return    number of MCCS capable displays
 */
int
ddca_report_active_displays(
      int depth);


//
// Display Identifier
//

/** Creates a display identifier using the display number assigned by ddcutil
 * @param[in]  dispno  display number
 * @param[out] pdid    where to return display identifier handle
 * @retval     0
 *
 * \ingroup api_display_spec
 * */
DDCA_Status
ddca_create_dispno_display_identifier(
      int                      dispno,
      DDCA_Display_Identifier* pdid);

/** Creates a display identifier using an I2C bus number
 * @param[in]  busno  I2C bus number
 * @param[out] pdid   where to return display identifier handle
 * @retval     0
 *
 * \ingroup api_display_spec
 */
DDCA_Status
ddca_create_busno_display_identifier(
      int                      busno,
      DDCA_Display_Identifier* pdid);

/** Creates a display identifier using an ADL (adapter index, display index) pair
 * @param[in]  iAdapterIndex ADL adapter index
 * @param[in]  iDisplayIndex ADL display index
 * @param[out] pdid          where to return display identifier handle
 * @return     status code
 *
 * \ingroup api_display_spec
 */
DDCA_Status
ddca_create_adlno_display_identifier(
      int                      iAdapterIndex,
      int                      iDisplayIndex,
      DDCA_Display_Identifier* pdid);

/** Creates a display identifier using some combination of the manufacturer id,
 * model name string and serial number string.  At least 1 of the 3 must be specified.
 * @param mfg_id  3 letter manufacturer id
 * @param model   model name string
 * @param sn     serial number string
 * @param pdid   where to return display identifier handle
 * @retval 0       success
 * @retval -EINVAL no argument specified, or argument too long
 *
 * \ingroup api_display_spec
 */
DDCA_Status
ddca_create_mfg_model_sn_display_identifier(
      const char *             mfg_id,
      const char *             model,
      const char *             sn,
      DDCA_Display_Identifier* pdid);

/** Creates a display identifier using a 128 byte EDID
 * @param   edid  pointer to 128 byte EDID
 * @param   pdid  where to return display identifier handle
 * @retval  0       success
 * @retval  -EINVAL edid==NULL
 *
 * \ingroup api_display_spec
 */
DDCA_Status
ddca_create_edid_display_identifier(
      const uint8_t*            edid,
      DDCA_Display_Identifier * pdid);      // 128 byte edid

/** Creates a display identifier using a USB bus number and device number
 * @param bus    USB bus number
 * @param device USB device number
 * @param pdid   where to return display identifier handle
 * @retval 0 success
 *
 *  \ingroup api_display_spec
 */
DDCA_Status
ddca_create_usb_display_identifier(
      int                      bus,
      int                      device,
      DDCA_Display_Identifier* pdid);

/** Creates a display identifier using a /dev/usb/hiddev device number
 * @param hiddev_devno hiddev device number
 * @param pdid   where to return display identifier handle
 * @retval 0  success
 *
 *  \ingroup api_display_spec
 */
DDCA_Status
ddca_create_usb_hiddev_display_identifier(
      int                      hiddev_devno,
      DDCA_Display_Identifier* pdid);


/** Release the memory of a display identifier */
DDCA_Status
ddca_free_display_identifier(
      DDCA_Display_Identifier did);

/** Returns a string representation of a display identifier
 *  \param[in]  did    display identifier
 *  \return     string representation of display identifier, NULL if invalid
 *
 *  \ingroup api_display_spec
 */
char *
ddca_did_repr(
      DDCA_Display_Identifier did);


//
// Display Reference
//

/** Gets a display reference for a display identifier.
 *  Normally, this is a permanently allocated #DDCA_Display_Ref
 *  created by monitor detection and does not need to be freed.
 * @param[in]  did display identifier
 * @param[out] pdref where to return display reference
 * @retval     0 success
 * @retval     -EINVAL  did is not a valid display identifier handle
 * @retval     DDCRC_INVALID_DISPLAY display not found
 *
 * \ingroup api_display_spec
 */
DDCA_Status
ddca_create_display_ref(
      DDCA_Display_Identifier did,
      DDCA_Display_Ref*       pdref);

/** Frees a display reference.
 * @param dref  display reference to free
 * @return status code
 *
 * \ingroup api_display_spec
 */
DDCA_Status
ddca_free_display_ref(
      DDCA_Display_Ref dref);

/** Returns a string representation of a display reference
 * @param[in]   dref display reference
 * @return      string representation of display reference, NULL if invalid
 * */
char *
ddca_dref_repr(
      DDCA_Display_Ref dref);

/** Writes a report on the specified display reference to the current FOUT device
 * @param dref   display reference
 * @param depth  logical indentation depth
 *
 * \ingroup api_display_spec
 */
void
ddca_report_display_ref(
      DDCA_Display_Ref dref,
      int              depth);


//
// Display Handle
//

/** Open a display
 * @param[in]  ddca_dref  display reference for display to open
 * @param[out] p_ddca_dh  where to return display handle
 * @return     status code
 *
 * \ingroup api_display_spec
 */
DDCA_Status
ddca_open_display(
      DDCA_Display_Ref      ddca_dref,
      DDCA_Display_Handle * p_ddca_dh);

/** Close an open display
 * @param[in]  ddca_dh   display handle
 * @return     DDCA status code
 *
 * \ingroup api_display_spec
 */
DDCA_Status
ddca_close_display(
      DDCA_Display_Handle   ddca_dh);

/** Returns a string representation of a display handle.
 *  The string is valid until the next call to this function.
 *
 * @param ddca_dh  display handle
 * @return string  representation of display handle, NULL if
 *                 argument is NULL or not a display handle
 *
 *  \ingroup api_display_spec
 */
char *
ddca_dh_repr(
      DDCA_Display_Handle   ddca_dh);


//
// Monitor Capabilities
//

/** Retrieves the capabilities string for a monitor.
 *
 *  @param[in]  ddca_dh     display handle
 *  @param[out] p_caps      address at which to return pointer to capabilities string.
 *  @return     status code
 *
 *  It is the responsibility of the caller to free the returned string.
 */
DDCA_Status
ddca_get_capabilities_string(
      DDCA_Display_Handle     ddca_dh,
      char**                  p_caps);

/** Parse the capabilities string.
 *
 *  @param[in] capabilities_string unparsed capabilities string
 *  @param[out] p_parsed_capabilities address at which to return pointer to newly allocated
 *              DDCA_Capabilities struct
 *  @return     status code
 *
 *  It is the responsibility of the caller to free the returned struct
 *  using ddca_free_parsed_capabilities().
 *
 *  This function currently parses the VCP feature codes and MCCS version.
 *  It could be extended to parse additional information such as cmds if necessary.
 */
DDCA_Status
ddca_parse_capabilities_string(
      char *                   capabilities_string,
      DDCA_Capabilities **     p_parsed_capabilities);

/** Frees a DDCA_Capabilities struct
 *
 *  @param[in]  pcaps  pointer to struct to free.
 *                     Does nothing if NULL.
 */
void
ddca_free_parsed_capabilities(
      DDCA_Capabilities *      pcaps);

/** Reports the contents of a DDCA_Capabilities struct.
 *
 *  The report is written to the current FOUT location.
 *
 *  This function is intended for debugging use.
 *
 *  @param[in]  pcaps  pointer to DDCA_Capabilities struct
 *  @param[in]  depth  logical indentation depth
 */
void
ddca_report_parsed_capabilities(
      DDCA_Capabilities *      pcaps,
      int                      depth);


//
// VCP Feature Information, Monitor Independent
//

/** Gets information for a VCP feature.
 *
 * VCP characteristics (C vs NC, RW vs RO, etc) can vary by MCCS version.
 *
 * @param[in]  feature_code    VCP feature code
 * @param[in]  mccs_version_id MCCS version id, may be DDCA_VCP_VANY??
 * @param[out] p_info          where to return Version_Feature_Info struct
 * @return     status code
 */
DDCA_Status
ddca_get_feature_info_by_vcp_version(
      DDCA_Vcp_Feature_Code         feature_code,
   // DDCT_MCCS_Version_Spec        vspec,
      DDCA_MCCS_Version_Id          mccs_version_id,
      DDCA_Version_Feature_Info**   p_info);


/** Gets the VCP feature name.  If different MCCS versions use different names
 * for the feature, this function makes a best guess.
 *
 * @param[in]  feature_code
 * @return     pointer to feature name(do not free), NULL if unknown feature code
 */
char *
ddca_get_feature_name(DDCA_Vcp_Feature_Code feature_code);

/** Gets the value id/name table of the allowed values for a simple NC feature.
 *
 * @param[in]  feature_code      VCP feature code
 * @param[in]  mccs_version_id   MCCS version id
 * @param[out] p_value_table     where to return pointer to array of DDCA_Feature_Value_Entry
 * @return     status code
 * @retval     0                success
 * @retval     DDCRC_NOT_FOUND  unrecognized feature code
 * @retval     -EINVAL          feature not simple NC
 */
DDCA_Status
ddca_get_simple_sl_value_table(
      DDCA_Vcp_Feature_Code       feature_code,
      DDCA_MCCS_Version_Id        mccs_version_id,
      DDCA_Feature_Value_Table *  p_value_table);   // DDCA_Feature_Value_Entry **


DDCA_Status
ddca_get_simple_nc_feature_value_name(
      DDCA_Display_Handle    ddca_dh,    // needed because value lookup mccs version dependent
      DDCA_Vcp_Feature_Code  feature_code,
      uint8_t                feature_value,
      char**                 p_feature_name);


DDCA_Status
ddca_free_feature_info(
      DDCA_Version_Feature_Info * info);


//
// VCP Feature Information, Monitor Dependent
//

#ifdef UNIMPLEMENTED

// Unimplemented
// alt: can check status code for ddca_get_feature_info_by_display()
DDCA_Status ddct_is_feature_supported(
      DDCA_Display_Handle    dh,
      DDCA_Vcp_Feature_Code  feature_code,
      bool *                 answer);

#endif


// This is a convenience function. Keep?
DDCA_Status
ddca_get_feature_info_by_display(
      DDCA_Display_Handle           ddca_dh,
      DDCA_Vcp_Feature_Code         feature_code,
      DDCA_Version_Feature_Info **  p_info);


//
//  Miscellaneous Monitor Specific Functions
//

// TODO: keep only 1 of the 2 get_mccs_version() variants

DDCA_Status
ddca_get_mccs_version(
      DDCA_Display_Handle     ddca_dh,
      DDCA_MCCS_Version_Spec* pspec);

DDCA_Status
ddca_get_mccs_version_id(
      DDCA_Display_Handle     ddca_dh,
      DDCA_MCCS_Version_Id*   p_id);

// DDCA_Status ddca_get_edid(DDCA_Display_Handle * dh, uint8_t* edid_buffer);    // edid_buffer must be >= 128 bytes
// Keep?   Can get from ddca_get_edid_by_display_ref()

DDCA_Status
ddca_get_edid_by_display_ref(
      DDCA_Display_Ref ddca_dref,
      uint8_t **       pbytes);   // pointer into ddcutil data structures, do not free


//
// Get VCP Feature Value
//

void
ddca_free_table_value_response(
      DDCA_Table_Value * table_value_response);


// TODO: Choose between ddca_get_nontable_vcp_value()/ddca_get_table_vcp_value() vs ddca_get_vcp_value()

/** Gets the value of a non-table VCP feature.
 *
 * @param ddca_dh       display handle
 * @param feature_code  VCP feature code
 * @param valrec        pointer to response buffer provided by the caller,
 *                      which will be filled in
 *
 * @return external status code
 */
DDCA_Status
ddca_get_nontable_vcp_value(
       DDCA_Display_Handle        ddca_dh,
       DDCA_Vcp_Feature_Code      feature_code,
       DDCA_Non_Table_Value *     valrec);


/** Gets the value of a table VCP feature.
 *
 * @param ddca_dh       display handle
 * @param feature_code  VCP feature code
 * @param value_len     address at which to return the value length
 * @param value_bytes   address at which to return a pointer to the value bytes
 *
 * @return external status code
 *
 * @note
 * Implemented, but untested
 */
DDCA_Status
ddca_get_table_vcp_value(
       DDCA_Display_Handle     ddca_dh,
       DDCA_Vcp_Feature_Code   feature_code,
       int *                   value_len,
       uint8_t**               value_bytes);


// ddca_get_vcp_value() is deprecated, use ddca_get_any_vcp_value()


/** Frees a #DDCA_Any_Vcp_Value instance
 *
 *  @param valrec  pointer to #DDCA_Any_Vcp_Value instance
 */
void
ddca_free_any_vcp_value(
      DDCA_Any_Vcp_Value * valrec);


/** Gets the value of a VCP feature.
 *
 * @param ddca_dh       display handle
 * @param feature_code  VCP feature code
 * @param value_type    value type
 * @param pvalrec       address at which to return a pointer to a newly
 *                      allocated Single_Vcp_Value
 *
 * @return external status code
 */
DDCA_Status
ddca_get_any_vcp_value(
       DDCA_Display_Handle         ddca_dh,
       DDCA_Vcp_Feature_Code       feature_code,
       DDCA_Vcp_Value_Type_Parm    value_type,
       DDCA_Any_Vcp_Value **       pvalrec);


/** Returns a string containing a formatted representation of the VCP value
 *  of a feature.  It is the responsibility of the caller to free this value.
 *  @param[in] ddca_dh            Display handle
 *  @param[in] feature_code       VCP feature code
 *  @param[out] p_formatted_value Address at which to return the formatted value
 *  @return                       status code, 0 if success
 */
DDCA_Status
ddca_get_formatted_vcp_value(
       DDCA_Display_Handle *   ddca_dh,
       DDCA_Vcp_Feature_Code   feature_code,
       char**                  p_formatted_value);


//
// Set VCP value
//

/** Sets a continuous VCP value.
 *
 *  @param ddca_dh        display_handle
 *  @param feature_code   VCP feature code
 *  @param new_value      value to set (sign?)
 *
 *  @return status code
 */
DDCA_Status
ddca_set_continuous_vcp_value(
      DDCA_Display_Handle      ddca_dh,
      DDCA_Vcp_Feature_Code    feature_code,
      int                      new_value);

/** Sets a simple NC value, which is a single byte.
 *
 *  @param ddca_dh        display_handle
 *  @param feature_code   VCP feature code
 *  @param new_value      value to set
 *
 *  @return status code
 * */
DDCA_Status
ddca_set_simple_nc_vcp_value(
      DDCA_Display_Handle      ddca_dh,
      DDCA_Vcp_Feature_Code    feature_code,
      uint8_t                  new_value);

/** Sets a non-table VCP value by directly specifying its bytes. */
DDCA_Status
ddca_set_raw_vcp_value(
      DDCA_Display_Handle      ddca_dh,         /**< Display handle     */
      DDCA_Vcp_Feature_Code    feature_code,    /**< VCP feature code   */
      uint8_t                  hi_byte,         /**< High byte of value */
      uint8_t                  lo_byte          /**< Low byte of value  */
     );

#ifdef UNIMPLEMENTED
DDCA_Status
ddct_set_table_vcp_value(
      DDCA_Display_Handle     ddca_dh,
      DDCA_Vcp_Feature_Code   feature_code,
      int                     value_len,
      uint8_t*                value_bytes);
#endif


//
// Get or set multiple values
//

DDCA_Status
ddca_get_profile_related_values(
      DDCA_Display_Handle  ddca_dh,
      char**               pprofile_values_string);

DDCA_Status
ddca_set_profile_related_values(char *
      profile_values_string);


//
// Experimental - Not for public use
//

DDCA_Status
ddca_start_get_any_vcp_value(
      DDCA_Display_Handle         ddca_dh,
      DDCA_Vcp_Feature_Code       feature_code,
      DDCA_Vcp_Value_Type_Parm    call_type,
      DDCA_Notification_Func      callback_func);


/** Registers a callback function to call when a VCP value changes */
DDCA_Status
ddca_register_callback(
      DDCA_Notification_Func func,
      uint8_t                callback_options);   // type is a placeholder

DDCA_Status
ddca_pass_callback(
      Simple_Callback_Func  func,
      int                   parm
      );


// future:
DDCA_Status
ddca_queue_get_non_table_vcp_value(
      DDCA_Display_Handle      ddca_dh,         /**< Display handle     */
      DDCA_Vcp_Feature_Code    feature_code    /**< VCP feature code   */
);


#ifdef __cplusplus
}
#endif
#endif /* DDCUTIL_C_API_H_ */
