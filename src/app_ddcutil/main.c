/** @file main.c
 *
 *  ddcutil standalone application mainline
 */

// Copyright (C) 2014-2020 Sanford Rockowitz <rockowitz@minsoft.com>
// SPDX-License-Identifier: GPL-2.0-or-later

/** \cond */
#include <config.h>

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util/data_structures.h"
#include "util/error_info.h"
#include "util/failsim.h"
#include "util/linux_util.h"
#include "util/report_util.h"
#include "util/string_util.h"
#include "util/sysfs_i2c_util.h"
#include "util/sysfs_util.h"
/** \endcond */

#include "public/ddcutil_types.h"

#include "base/adl_errors.h"
#include "base/base_init.h"
#include "base/core.h"
#include "base/ddc_errno.h"
#include "base/ddc_packets.h"
#include "base/displays.h"
#include "base/dynamic_sleep.h"
#include "base/linux_errno.h"
#include "base/parms.h"
#include "base/rtti.h"
#include "base/sleep.h"
#include "base/status_code_mgt.h"
#include "base/thread_retry_data.h"
#include "base/tuned_sleep.h"
#include "base/thread_sleep_data.h"

#include "vcp/parse_capabilities.h"
#include "vcp/vcp_feature_codes.h"

#include "dynvcp/dyn_dynamic_features.h"
#include "dynvcp/dyn_parsed_capabilities.h"

#include "i2c/i2c_bus_core.h"
#include "i2c/i2c_strategy_dispatcher.h"
#include "adl/adl_shim.h"

#ifdef USE_USB
#include "usb/usb_displays.h"
#endif

#include "ddc/ddc_displays.h"
#include "ddc/ddc_multi_part_io.h"
#include "ddc/ddc_output.h"
#include "ddc/ddc_packet_io.h"
#include "ddc/ddc_read_capabilities.h"
#include "ddc/ddc_services.h"
#include "ddc/ddc_try_stats.h"
#include "ddc/ddc_vcp_version.h"
#include "ddc/ddc_vcp.h"

#include "cmdline/cmd_parser_aux.h"    // for parse_feature_id_or_subset(), should it be elsewhere?
#include "cmdline/cmd_parser.h"
#include "cmdline/parsed_cmd.h"

#include "app_ddcutil/app_capabilities.h"
#include "app_ddcutil/app_dynamic_features.h"
#include "app_ddcutil/app_dumpload.h"
#include "app_ddcutil/app_probe.h"
#include "app_ddcutil/app_getvcp.h"
#include "app_ddcutil/app_setvcp.h"
#include "app_ddcutil/app_vcpinfo.h"

#include "app_sysenv/query_sysenv.h"
#ifdef USE_USB
// #ifdef ENABLE_ENVCMDS    // unnecessary, forced by configure
#include "app_sysenv/query_sysenv_usb.h"
// #endif
#endif

#ifdef INCLUDE_TESTCASES
#include "test/testcases.h"
#endif

#ifdef USE_API
#include "public/ddcutil_c_api.h"
#endif


// Default trace class for this file
static DDCA_Trace_Group TRACE_GROUP = DDCA_TRC_TOP;


//
// Report core settings and command line options
//

static void
report_performance_options(int depth)
{
      int d1 = depth+1;
      rpt_label(depth, "Performance and Retry Options:");
      rpt_vstring(d1, "Deferred sleep enabled:                      %s", sbool( is_deferred_sleep_enabled() ) );
      rpt_vstring(d1, "Sleep suppression (reduced sleeps) enabled:  %s", sbool( is_sleep_suppression_enabled() ) );
      bool dsa_enabled =  tsd_get_dsa_enabled_default();
      rpt_vstring(d1, "Dynamic sleep adjustment enabled:            %s", sbool(dsa_enabled) );
      if ( dsa_enabled )
        rpt_vstring(d1, "Sleep multiplier factor:                %5.2f", tsd_get_sleep_multiplier_factor() );
      rpt_nl();
}


#define REPORT_FLAG_OPTION(_flagno, _action) \
      rpt_vstring(depth+1, "Utility option --f"#_flagno" %s %s",   \
                  (parsed_cmd->flags & CMD_FLAG_F##_flagno ) ? "enabled: " : "disabled:", \
                        _action)
static void
report_utility_options(Parsed_Cmd * parsed_cmd, int depth)
{
   if (parsed_cmd->output_level >= DDCA_OL_VV) {
      rpt_label(depth, "Utility Options:");
      REPORT_FLAG_OPTION(1, "EDID read uses I2C layer");
      REPORT_FLAG_OPTION(2, "Unused");
      REPORT_FLAG_OPTION(3, "Unused");
      REPORT_FLAG_OPTION(4, "Read strategy tests");
      REPORT_FLAG_OPTION(5, "Unused");
      REPORT_FLAG_OPTION(6, "Force I2c bus");

      rpt_vstring(depth+1, "Utility option --i1 = %d:     Unused", parsed_cmd->i1);
      rpt_nl();
   }
 }
#undef REPORT_FLAG_OPTION


static void
report_settings(Parsed_Cmd * parsed_cmd, int depth)
{
    show_reporting();  // uses fout()

    rpt_vstring( depth, "%.*s%-*s%s",
              0,"",
              28, "Force I2C slave address:",
              sbool(i2c_force_slave_addr_flag));
    rpt_vstring( depth, "%.*s%-*s%s",
              0,"",
              28, "User defined features:",
              (enable_dynamic_features) ? "enabled" : "disabled" );  // "Enable user defined features" is too long a title
              // sbool(enable_dynamic_features));
    rpt_nl();
    // f0puts("\n", output_dest);

    report_performance_options(depth);

    report_utility_options(parsed_cmd, depth);
}


//
// Initialize and Report Statistics
//

// static long start_time_nanos;

#ifdef ENABLE_ENVCMDS
static void reset_stats() {
   ddc_reset_stats_main();
}
#endif

#ifdef OLD
static
void report_stats(DDCA_Stats_Type stats) {
   ddc_report_stats_main(stats, get_output_level() >= DDCA_OL_VERBOSE, 0);

   // Report the elapsed time in ddc_report_stats_main().
   // The start time used there is that at the time of stats initialization,
   // which is slightly later than start_time_nanos, but the difference is
   // less than a tenth of a millisecond.  Using that start time allows for
   // elapsed time to be used from library functions.

   // puts("");
   // long elapsed_nanos = cur_realtime_nanosec() - start_time_nanos;
   // printf("Elapsed milliseconds (nanoseconds):             %10ld  (%10ld)\n",
   //       elapsed_nanos / (1000*1000),
   //       elapsed_nanos);
}
#endif


static bool
validate_environment()
{
   bool ok = true;
#ifdef TARGET_LINUX
   int depth = 0;
   if (!is_module_builtin("i2c-dev") && !is_module_loaded_using_sysfs("i2c_dev")) {
      rpt_vstring(depth, "ddcutil requires module i2c_dev.");
      ok = false;
   }
#endif
   return ok;
}

// static
void ensure_vcp_version_set(Display_Handle * dh)
{
   bool debug = false;
   DBGMSF(debug, "Starting. dh=%s", dh_repr(dh));
   DDCA_MCCS_Version_Spec vspec = get_vcp_version_by_dh(dh);
   if (vspec.major < 2 && get_output_level() >= DDCA_OL_NORMAL) {
      f0printf(stdout, "VCP (aka MCCS) version for display is undetected or less than 2.0. "
            "Output may not be accurate.\n");
   }
   DBGMSF(debug, "Done");
}


// Initialization functions called only once but factored out of main() to clarify mainline

void init_tracing(Parsed_Cmd * parsed_cmd)
{
   if (parsed_cmd->flags & CMD_FLAG_TIMESTAMP_TRACE)     // timestamps on debug and trace messages?
       dbgtrc_show_time = true;                           // extern in core.h
    if (parsed_cmd->flags & CMD_FLAG_THREAD_ID_TRACE)     // timestamps on debug and trace messages?
       dbgtrc_show_thread_id = true;                      // extern in core.h
    report_freed_exceptions = parsed_cmd->flags & CMD_FLAG_REPORT_FREED_EXCP;   // extern in core.h
    set_trace_levels(parsed_cmd->traced_groups);
    if (parsed_cmd->traced_functions) {
       for (int ndx = 0; ndx < ntsa_length(parsed_cmd->traced_functions); ndx++)
          add_traced_function(parsed_cmd->traced_functions[ndx]);
    }
    if (parsed_cmd->traced_files) {
       for (int ndx = 0; ndx < ntsa_length(parsed_cmd->traced_files); ndx++)
          add_traced_file(parsed_cmd->traced_files[ndx]);
    }
}

bool init_failsim(Parsed_Cmd * parsed_cmd) {
#ifdef ENABLE_FAILSIM
   fsim_set_name_to_number_funcs(
         status_name_to_modulated_number,
         status_name_to_unmodulated_number);
   if (parsed_cmd->failsim_control_fn) {
      bool ok = fsim_load_control_file(parsed_cmd->failsim_control_fn);
      if (!ok) {
         fprintf(stderr, "Error loading failure simulation control file %s.\n",
                         parsed_cmd->failsim_control_fn);
         return false;

      }
      fsim_report_error_table(0);
   }
#endif
   return true;
}

bool init_utility_options(Parsed_Cmd* parsed_cmd)
{
   if (parsed_cmd->flags & CMD_FLAG_F1) {
      fprintf(stdout, "EDID reads will use normal I2C calls\n");
      EDID_Read_Uses_I2C_Layer = true;
   }

   // HACK FOR TESTING
   if (parsed_cmd->flags & CMD_FLAG_F6) {
      fprintf(stdout, "Setting i2c_force_bus\n");
      if ( !(parsed_cmd->pdid) || parsed_cmd->pdid->id_type != DISP_ID_BUSNO) {
         fprintf(stdout, "bus number required, use --busno\n");
         return false;
      }
      i2c_force_bus = true;
   }
   return true;
}

void init_max_tries(Parsed_Cmd * parsed_cmd)
{
   // n. MAX_MAX_TRIES checked during command line parsing
   if (parsed_cmd->max_tries[0] > 0) {
      // ddc_set_max_write_only_exchange_tries(parsed_cmd->max_tries[0]);  // sets in Try_Data
      // try_data_set_maxtries2(WRITE_ONLY_TRIES_OP, parsed_cmd->max_tries[0]);
      try_data_init_retry_type(WRITE_ONLY_TRIES_OP, parsed_cmd->max_tries[0]);  // resets highest, lowest

      // redundant
      trd_set_default_max_tries(0, parsed_cmd->max_tries[0]);
      trd_set_initial_thread_max_tries(0, parsed_cmd->max_tries[0]);
   }

   if (parsed_cmd->max_tries[1] > 0) {
      // ddc_set_max_write_read_exchange_tries(parsed_cmd->max_tries[1]);   // sets in Try_Data
      // try_data_set_maxtries2(WRITE_READ_TRIES_OP, parsed_cmd->max_tries[1]);
      try_data_init_retry_type(WRITE_READ_TRIES_OP, parsed_cmd->max_tries[1]);

      trd_set_default_max_tries(1, parsed_cmd->max_tries[1]);
      trd_set_initial_thread_max_tries(1, parsed_cmd->max_tries[1]);
   }

   if (parsed_cmd->max_tries[2] > 0) {
      // ddc_set_max_multi_part_read_tries(parsed_cmd->max_tries[2]);       // sets in Try_Data
      // ddc_set_max_multi_part_write_tries(parsed_cmd->max_tries[2]);
      // try_data_set_maxtries2(MULTI_PART_READ_OP,  parsed_cmd->max_tries[2]);
      // try_data_set_maxtries2(MULTI_PART_WRITE_OP, parsed_cmd->max_tries[2]);
      try_data_init_retry_type(MULTI_PART_READ_OP,  parsed_cmd->max_tries[2]);
      try_data_init_retry_type(MULTI_PART_WRITE_OP, parsed_cmd->max_tries[2]);

      trd_set_default_max_tries(2, parsed_cmd->max_tries[2]);
      trd_set_initial_thread_max_tries(2, parsed_cmd->max_tries[2]);
      // impedance match
      trd_set_default_max_tries(3, parsed_cmd->max_tries[2]);
      trd_set_initial_thread_max_tries(3, parsed_cmd->max_tries[2]);
   }
}

void init_performance_options(Parsed_Cmd * parsed_cmd)
{
   enable_sleep_suppression( parsed_cmd->flags & CMD_FLAG_REDUCE_SLEEPS );
   enable_deferred_sleep( parsed_cmd->flags & CMD_FLAG_DEFER_SLEEPS);

   int threshold = DISPLAY_CHECK_ASYNC_NEVER;
   if (parsed_cmd->flags & CMD_FLAG_ASYNC)
      threshold = DISPLAY_CHECK_ASYNC_THRESHOLD;
   ddc_set_async_threshold(threshold);

   if (parsed_cmd->sleep_multiplier != 0 && parsed_cmd->sleep_multiplier != 1) {
      tsd_set_sleep_multiplier_factor(parsed_cmd->sleep_multiplier);         // for current thread
      tsd_set_default_sleep_multiplier_factor(parsed_cmd->sleep_multiplier); // for new threads
      if (parsed_cmd->sleep_multiplier > 1.0f && (parsed_cmd->flags & CMD_FLAG_DSA) )
      {
         tsd_dsa_enable_globally(true);
      }
   }

   // experimental timeout of i2c read()
   if (parsed_cmd->flags & CMD_FLAG_TIMEOUT_I2C_IO) {
      set_i2c_fileio_use_timeout(true);
   }
}


bool initialize(Parsed_Cmd * parsed_cmd) {
   bool ok = false;

   if (!validate_environment())
      goto bye;




    if (!init_failsim(parsed_cmd))
       goto bye;      // main_rc == EXIT_FAILURE

    // global variable in dyn_dynamic_features:
    enable_dynamic_features = parsed_cmd->flags & CMD_FLAG_ENABLE_UDF;


    init_ddc_services();   // n. initializes start timestamp
    // overrides setting in init_ddc_services():
    i2c_set_io_strategy(DEFAULT_I2C_IO_STRATEGY);

    ddc_set_verify_setvcp(parsed_cmd->flags & CMD_FLAG_VERIFY);

    if (!init_utility_options(parsed_cmd))
       goto bye;




    set_output_level(parsed_cmd->output_level);
    enable_report_ddc_errors( parsed_cmd->flags & CMD_FLAG_DDCDATA );
    // TMI:
    // if (show_recoverable_errors)
    //    parsed_cmd->stats = true;

    init_max_tries(parsed_cmd);

 #ifdef USE_USB
       DDCA_Status rc = ddc_enable_usb_display_detection( parsed_cmd->flags & CMD_FLAG_ENABLE_USB );
       assert (rc == DDCRC_OK);
 #endif

    init_performance_options(parsed_cmd);


    if (parsed_cmd->output_level >= DDCA_OL_VERBOSE) {
       report_settings(parsed_cmd, 0);
    }

   ok = true;

bye:
   return ok;
}


void test_display_detection_variants() {

   typedef struct {
      I2C_IO_Strategy_Id  i2c_io_strategy_id;
      bool                i2c_read_bytewise;
      bool                edid_uses_i2c_layer;
      bool                edid_read_bytewise;
   } Choice_Entry;

   Choice_Entry choices[16] =
   { {I2C_IO_STRATEGY_FILEIO,  false,    false,    false},
     {I2C_IO_STRATEGY_FILEIO,  false,    false,    true},
     {I2C_IO_STRATEGY_FILEIO,  false,    true,     false},
     {I2C_IO_STRATEGY_FILEIO,  false,    true,     true},
     {I2C_IO_STRATEGY_FILEIO,  true,     false,    false},
     {I2C_IO_STRATEGY_FILEIO,  true,     false,    true},
     {I2C_IO_STRATEGY_FILEIO,  true,     true,     false},
     {I2C_IO_STRATEGY_FILEIO,  true,     true,     true},

     {I2C_IO_STRATEGY_IOCTL,  false,     false,    false},
     {I2C_IO_STRATEGY_IOCTL,  false,     false,    true},
     {I2C_IO_STRATEGY_IOCTL,  false,     true,     false},
     {I2C_IO_STRATEGY_IOCTL,  false,     true,     true},
     {I2C_IO_STRATEGY_IOCTL,  true,      false,    false},
     {I2C_IO_STRATEGY_IOCTL,  true,      false,    true},
     {I2C_IO_STRATEGY_IOCTL,  true,      true,     false},
     {I2C_IO_STRATEGY_IOCTL,  true,      true,     true},
   };

   for (int ndx=0; ndx<16; ndx++) {
      Choice_Entry cur = choices[ndx];

      rpt_nl();
      rpt_vstring(0, "===========> IO STRATEGY %d:", ndx+1);
       char * s = (cur.i2c_io_strategy_id == I2C_IO_STRATEGY_FILEIO) ? "FILEIO" : "IOCTL";
       int d = 1;
       rpt_vstring(d, "i2c_io_strategy:          %s", s);
       rpt_vstring(d, "i2c_read_bytewise:        %s", sbool(cur.i2c_read_bytewise));
       rpt_vstring(d, "EDID read uses I2C layer: %s", sbool(cur.edid_uses_i2c_layer));
       rpt_vstring(d, "EDID read bytewise:       %s", sbool(cur.edid_read_bytewise));

       i2c_set_io_strategy(       cur.i2c_io_strategy_id);
       I2C_Read_Bytewise        = cur.i2c_read_bytewise;
       EDID_Read_Uses_I2C_Layer = cur.edid_uses_i2c_layer;
       EDID_Read_Bytewise       = cur.edid_read_bytewise;

       rpt_nl();
       // discard existing detected monitors
       ddc_discard_detected_displays();
       ddc_ensure_displays_detected();
       // will include any USB or ADL displays, but that's ok
       ddc_report_displays(/*include_invalid_displays=*/ true, 0);
   }
}

#ifdef ENABLE_ENVCMDS
void interrogate(Parsed_Cmd * parsed_cmd, bool main_debug) {
   DBGTRC(main_debug, TRACE_GROUP, "Processing command INTERROGATE...");
   dup2(1,2);   // redirect stderr to stdout
   // set_ferr(fout);    // ensure that all messages are collected - made unnecessary by dup2()
   f0printf(fout(), "Setting output level verbose...\n");
   set_output_level(DDCA_OL_VERBOSE);
   f0printf(fout(), "Setting maximum retries...\n");
   f0printf(fout(), "Forcing --stats...\n");
   parsed_cmd->stats_types = DDCA_STATS_ALL;
   f0printf(fout(), "Forcing --force-slave-address..\n");
   i2c_force_slave_addr_flag = true;
   f0printf(fout(), "This command will take a while to run...\n\n");
   try_data_set_maxtries2(MULTI_PART_READ_OP, MAX_MAX_TRIES);
   try_data_set_maxtries2(MULTI_PART_WRITE_OP, MAX_MAX_TRIES);

   ddc_ensure_displays_detected();    // *** ???
   DBGTRC(main_debug, TRACE_GROUP, "display detection complete");

   query_sysenv();
#ifdef USE_USB
   // 7/2017: disable, USB attached monitors are rare, and this just
   // clutters the output
   f0printf(fout(), "\nSkipping USB environment exploration.\n");
   f0printf(fout(), "Issue command \"ddcutil usbenvironment --verbose\" if there are any USB attached monitors.\n");
   // query_usbenv();
#endif
   f0printf(fout(), "\nStatistics for environment exploration:\n");
   ddc_report_stats_main(DDCA_STATS_ALL, parsed_cmd->flags & CMD_FLAG_PER_THREAD_STATS, 0);
   reset_stats();

   f0printf(fout(), "\n*** Detected Displays ***\n");
   /* int display_ct =  */ ddc_report_displays(
                              true,   // include_invalid_displays
                              0);      // logical depth
   // printf("Detected: %d displays\n", display_ct);   // not needed
   f0printf(fout(), "\nStatistics for display detection:\n");
   ddc_report_stats_main(DDCA_STATS_ALL, parsed_cmd->flags & CMD_FLAG_PER_THREAD_STATS, 0);
   reset_stats();

   f0printf(fout(), "Setting output level normal  Table features will be skipped...\n");
   set_output_level(DDCA_OL_NORMAL);

   tsd_dsa_enable_globally(parsed_cmd->flags & CMD_FLAG_DSA);   // should this apply to INTERROGATE?
   GPtrArray * all_displays = ddc_get_all_displays();
   for (int ndx=0; ndx < all_displays->len; ndx++) {
      Display_Ref * dref = g_ptr_array_index(all_displays, ndx);
      assert( memcmp(dref->marker, DISPLAY_REF_MARKER, 4) == 0);
      if (dref->dispno < 0) {
         f0printf(fout(), "\nSkipping invalid display on %s\n", dref_short_name_t(dref));
      }
      else {
         f0printf(fout(), "\nProbing display %d\n", dref->dispno);
         app_probe_display_by_dref(dref);
         f0printf(fout(), "\nStatistics for probe of display %d:\n", dref->dispno);
         ddc_report_stats_main(DDCA_STATS_ALL, parsed_cmd->flags & CMD_FLAG_PER_THREAD_STATS, 0);
      }
      reset_stats();
   }
   f0printf(fout(), "\nDisplay scanning complete.\n");
}
#endif


typedef enum {
   DISPLAY_ID_REQUIRED,
   DISPLAY_ID_USE_DEFAULT,
   DISPLAY_ID_OPTIONAL
} Displayid_Requirement;


const char * displayid_requirement_name(Displayid_Requirement id) {
   char * result = NULL;
   switch (id) {
   case DISPLAY_ID_REQUIRED:    result = "DISPLAY_ID_REQUIRED";     break;
   case DISPLAY_ID_USE_DEFAULT: result = "DISPLAY_ID_USE_DEFAULT";  break;
   case DISPLAY_ID_OPTIONAL:    result = "DISPLAY_ID_OPTIONAL";     break;
   }
   return result;
}


Status_Errno_DDC
find_dref(
      Parsed_Cmd * parsed_cmd,
      Displayid_Requirement displayid_required,
      Display_Ref ** dref_loc)
{
   bool debug = false;
   DBGTRC(debug, TRACE_GROUP, "did: %s, set_default_display: %s",
                                    did_repr(parsed_cmd->pdid),
                                    displayid_requirement_name(displayid_required));
   FILE * outf = fout();

   Status_Errno_DDC final_result = DDCRC_OK;

   Display_Ref * dref = NULL;
   Call_Options callopts = CALLOPT_ERR_MSG;        // emit error messages
   if (parsed_cmd->flags & CMD_FLAG_FORCE)
      callopts |= CALLOPT_FORCE;

   Display_Identifier * did_work = parsed_cmd->pdid;
   if (did_work && did_work->id_type == DISP_ID_BUSNO) {
      // special handling for --busno
      int busno = did_work->busno;
      // is this really a monitor?
      I2C_Bus_Info * businfo = i2c_detect_single_bus(busno);
      if ( businfo && (businfo->flags & I2C_BUS_ADDR_0X50) ) {
         dref = create_bus_display_ref(busno);
         dref->dispno = -1;     // should use some other value for unassigned vs invalid
         dref->pedid = businfo->edid;    // needed?
         // dref->pedid = i2c_get_parsed_edid_by_busno(did_work->busno);
         dref->detail = businfo;
         dref->flags |= DREF_DDC_IS_MONITOR_CHECKED;
         dref->flags |= DREF_DDC_IS_MONITOR;
         dref->flags |= DREF_TRANSIENT;
         if (!initial_checks_by_dref(dref)) {
            f0printf(outf, "DDC communication failed for monitor on I2C bus /dev/i2c-%d\n", busno);
            free_display_ref(dref);
            dref = NULL;
            final_result = DDCRC_INVALID_DISPLAY;
         }
         // else
               // check_dynamic_features(dref);    // the hook wrong location
         // DBGMSG("Synthetic Display_Ref");
         final_result = DDCRC_OK;
      }
      else {
         f0printf(fout(), "No monitor detected on I2C bus /dev/i2c-%d\n", busno);
         final_result = DDCRC_INVALID_DISPLAY;
      }
   }       // DISP_ID_BUSNO
   else {
      if (!did_work && displayid_required == DISPLAY_ID_OPTIONAL) {
         dref = NULL;
         final_result = DDCRC_OK;
      }
      else {
         if (!did_work)
            did_work = create_dispno_display_identifier(1);   // default monitor
         // assert(did_work);
         DBGTRC(debug, TRACE_GROUP, "Detecting displays...");
         ddc_ensure_displays_detected();
         DBGTRC(debug, TRACE_GROUP, "display detection complete");
         dref = get_display_ref_for_display_identifier(did_work, callopts);
         final_result = (dref) ? DDCRC_OK : DDCRC_INVALID_DISPLAY;
      }
   }  // !DISP_ID_BUSNO

   *dref_loc = dref;
   DBGTRC(debug, TRACE_GROUP,
                 "Done. *dref_loc = %p, returning %s", *dref_loc, psc_desc(final_result));
   return final_result;
}


int
execute_cmd_with_optional_display_handle(
      Parsed_Cmd *      parsed_cmd,
      Display_Handle * dh)
{
   bool debug = false;
   int main_rc =EXIT_SUCCESS;

   if (dh) {
      if (!vcp_version_eq(parsed_cmd->mccs_vspec, DDCA_VSPEC_UNKNOWN)) {
         DBGTRC(debug, TRACE_GROUP, "Forcing mccs_vspec=%d.%d",
                            parsed_cmd->mccs_vspec.major, parsed_cmd->mccs_vspec.minor);
         dh->dref->vcp_version_cmdline = parsed_cmd->mccs_vspec;
      }
   }

   DBGTRC(debug, TRACE_GROUP, "%s", cmdid_name(parsed_cmd->cmd_id));
   switch(parsed_cmd->cmd_id) {

   case CMDID_LOADVCP:
      {
         // check_dynamic_features();
         // ensure_vcp_version_set();

         tsd_dsa_enable(parsed_cmd->flags & CMD_FLAG_DSA);
         // loadvcp will search monitors to find the one matching the
         // identifiers in the record
         ddc_ensure_displays_detected();
         bool loadvcp_ok = loadvcp_by_file(parsed_cmd->args[0], dh);
         main_rc = (loadvcp_ok) ? EXIT_SUCCESS : EXIT_FAILURE;
         break;
      }

   case CMDID_CAPABILITIES:
      {
         check_dynamic_features(dh->dref);
         ensure_vcp_version_set(dh);

         DDCA_Status ddcrc = app_capabilities(dh);
         main_rc = (ddcrc==0) ? EXIT_SUCCESS : EXIT_FAILURE;
         break;
      }

   case CMDID_GETVCP:
      {
         check_dynamic_features(dh->dref);
         ensure_vcp_version_set(dh);

         // DBGMSG("parsed_cmd->flags: 0x%04x", parsed_cmd->flags);
         Feature_Set_Flags flags = 0x00;
         if (parsed_cmd->flags & CMD_FLAG_SHOW_UNSUPPORTED)
            flags |= FSF_SHOW_UNSUPPORTED;
         if (parsed_cmd->flags & CMD_FLAG_FORCE)
            flags |= FSF_FORCE;
         if (parsed_cmd->flags & CMD_FLAG_NOTABLE)
            flags |= FSF_NOTABLE;
         if (parsed_cmd->flags & CMD_FLAG_RW_ONLY)
            flags |= FSF_RW_ONLY;
         if (parsed_cmd->flags & CMD_FLAG_RO_ONLY)
            flags |= FSF_RO_ONLY;

         // this is nonsense, getvcp on a WO feature should be
         // caught by parser
         if (parsed_cmd->flags & CMD_FLAG_WO_ONLY) {
            flags |= FSF_WO_ONLY;
            DBGMSG("Invalid: GETVCP for WO features");
            assert(false);
         }
         // char * s0 = feature_set_flag_names(flags);
         // DBGMSG("flags: 0x%04x - %s", flags, s0);
         // free(s0);

         Public_Status_Code psc = app_show_feature_set_values_by_dh(
               dh,
               parsed_cmd->fref,
               flags
               );
         main_rc = (psc==0) ? EXIT_SUCCESS : EXIT_FAILURE;
      }
      break;

   case CMDID_SETVCP:
      {
         check_dynamic_features(dh->dref);
         ensure_vcp_version_set(dh);

         main_rc = EXIT_SUCCESS;
         Error_Info * ddc_excp = NULL;
         for (int ndx = 0; ndx < parsed_cmd->setvcp_values->len; ndx++) {
            Parsed_Setvcp_Args * cur =
                  &g_array_index(parsed_cmd->setvcp_values, Parsed_Setvcp_Args, ndx);
            ddc_excp = app_set_vcp_value(
                  dh,
                  cur->feature_code,
                  cur->feature_value_type,
                  cur->feature_value,
                  parsed_cmd->flags & CMD_FLAG_FORCE);
            if (ddc_excp) {
               f0printf(ferr(), "%s\n", ddc_excp->detail);
               if (ddc_excp->status_code == DDCRC_RETRIES)
                  f0printf(ferr(), "    Try errors: %s\n", errinfo_causes_string(ddc_excp));
               ERRINFO_FREE_WITH_REPORT(ddc_excp, report_freed_exceptions);
               main_rc = EXIT_FAILURE;
               break;
            }
         }
      }
      break;

   case CMDID_SAVE_SETTINGS:
      if (parsed_cmd->argct != 0) {
         f0printf(fout(), "SCS command takes no arguments\n");
         main_rc = EXIT_FAILURE;
      }
      else if (dh->dref->io_path.io_mode == DDCA_IO_USB) {
         f0printf(fout(), "SCS command is not supported for USB devices\n");
         main_rc = EXIT_FAILURE;
      }
      else {
         main_rc = EXIT_SUCCESS;
         Error_Info * ddc_excp = ddc_save_current_settings(dh);
         if (ddc_excp)  {
            f0printf(fout(), "Save current settings failed. rc=%s\n", psc_desc(ddc_excp->status_code));
            if (ddc_excp->status_code == DDCRC_RETRIES)
               f0printf(fout(), "    Try errors: %s", errinfo_causes_string(ddc_excp) );
            errinfo_report(ddc_excp, 0);   // ** ALTERNATIVE **/
            errinfo_free(ddc_excp);
            // ERRINFO_FREE_WITH_REPORT(ddc_excp, report_exceptions);
            main_rc = EXIT_FAILURE;
         }
      }
      break;

   case CMDID_DUMPVCP:
      {
         DBGMSF(debug, "case CMDID_DUMPVCP");
         // MCCS vspec can affect whether a feature is NC or TABLE
         check_dynamic_features(dh->dref);
         ensure_vcp_version_set(dh);

         Public_Status_Code psc =
               dumpvcp_as_file(dh, (parsed_cmd->argct > 0)
                                      ? parsed_cmd->args[0]
                                      : NULL );
         main_rc = (psc==0) ? EXIT_SUCCESS : EXIT_FAILURE;
         break;
      }

   case CMDID_READCHANGES:
      check_dynamic_features(dh->dref);
      ensure_vcp_version_set(dh);

      app_read_changes_forever(dh, parsed_cmd->flags & CMD_FLAG_X52_NO_FIFO);     // only returns if fatal error
      main_rc = EXIT_FAILURE;
      break;

   case CMDID_PROBE:
      check_dynamic_features(dh->dref);
      ensure_vcp_version_set(dh);

      app_probe_display_by_dh(dh);
      main_rc = EXIT_SUCCESS;
      break;

   default:
      main_rc = EXIT_FAILURE;
      break;
   }    // switch

   return main_rc;
}


//
// Mainline
//

/** **ddcutil** program mainline.
  *
  * @param argc   number of command line arguments
  * @param argv   pointer to array of argument strings
  *
  * @retval  EXIT_SUCCESS normal exit
  * @retval  EXIT_FAILURE an error occurred
  */
int main(int argc, char *argv[]) {
   FILE * fout = stdout;
   bool main_debug = false;
   int main_rc = EXIT_FAILURE;

   // set_trace_levels(TRC_ADL);   // uncomment to enable tracing during initialization
    init_base_services();  // so tracing related modules are initialized
    rtti_func_name_table_add(main, "main");
    Parsed_Cmd * parsed_cmd = parse_command(argc, argv);
    if (!parsed_cmd) {
       goto bye;      // main_rc == EXIT_FAILURE
    }

    init_tracing(parsed_cmd);

    time_t cur_time = time(NULL);
    char * cur_time_s = asctime(localtime(&cur_time));
    if (cur_time_s[strlen(cur_time_s)-1] == 0x0a)
         cur_time_s[strlen(cur_time_s)-1] = 0;
    DBGTRC(parsed_cmd->traced_groups || parsed_cmd->traced_functions || parsed_cmd->traced_files,
           TRACE_GROUP,   /* redundant with parsed_cmd->traced_groups */
           "Starting ddcutil execution, %s",
           cur_time_s);

   if (!initialize(parsed_cmd))
      goto bye;

   Call_Options callopts = CALLOPT_NONE;
   i2c_force_slave_addr_flag = parsed_cmd->flags & CMD_FLAG_FORCE_SLAVE_ADDR;
   if (parsed_cmd->flags & CMD_FLAG_FORCE)
      callopts |= CALLOPT_FORCE;

   main_rc = EXIT_SUCCESS;     // from now on assume success;
   DBGTRC(main_debug, TRACE_GROUP, "Initialization complete, process commands");

   if (parsed_cmd->cmd_id == CMDID_LISTVCP) {    // vestigial
      app_listvcp(stdout);
      main_rc = EXIT_SUCCESS;
   }

   else if (parsed_cmd->cmd_id == CMDID_VCPINFO) {
      Feature_Set_Flags flags = 0;
      if (parsed_cmd->flags & CMD_FLAG_RW_ONLY)
         flags |= FSF_RW_ONLY;
      if (parsed_cmd->flags & CMD_FLAG_RO_ONLY)
         flags |= FSF_RO_ONLY;
      if (parsed_cmd->flags & CMD_FLAG_WO_ONLY)
         flags |= FSF_WO_ONLY;

      bool vcpinfo_ok = app_vcpinfo(parsed_cmd->fref, parsed_cmd->mccs_vspec, flags);
      main_rc = (vcpinfo_ok) ? EXIT_SUCCESS : EXIT_FAILURE;
   }

#ifdef INCLUDE_TESTCASES
   else if (parsed_cmd->cmd_id == CMDID_LISTTESTS) {
      show_test_cases();
      main_rc = EXIT_SUCCESS;
   }
#endif

   // start of commands that actually access monitors

   else if (parsed_cmd->cmd_id == CMDID_DETECT) {
      DBGTRC(main_debug, TRACE_GROUP, "Detecting displays...");
      if ( parsed_cmd->flags & CMD_FLAG_F4) {
         test_display_detection_variants();
      }
      else {     // normal case
         ddc_ensure_displays_detected();
         ddc_report_displays(/*include_invalid_displays=*/ true, 0);
      }

      DBGTRC(main_debug, TRACE_GROUP, "Display detection complete");
      main_rc = EXIT_SUCCESS;
   }

#ifdef INCLUDE_TESTCASES
   else if (parsed_cmd->cmd_id == CMDID_TESTCASE) {
      int testnum;
      bool ok = true;
      int ct = sscanf(parsed_cmd->args[0], "%d", &testnum);
      if (ct != 1) {
         f0printf(fout, "Invalid test number: %s\n", parsed_cmd->args[0]);
         ok = false;
      }
      else {
         ddc_ensure_displays_detected();
         main_rc = EXIT_SUCCESS;
         // Why is ddc_save_current_settings() call here?
         Error_Info * ddc_excp = ddc_save_current_settings(dh);
         if (ddc_excp)  {
            f0printf(fout(), "Save current settings failed. rc=%s\n", psc_desc(ddc_excp->status_code));
            if (ddc_excp->status_code == DDCRC_RETRIES)
               f0printf(fout(), "    Try errors: %s", errinfo_causes_string(ddc_excp) );
            errinfo_report(ddc_excp, 0);   // ** ALTERNATIVE **/
            errinfo_free(ddc_excp);
            // ERRINFO_FREE_WITH_REPORT(ddc_excp, report_exceptions);
            main_rc = EXIT_FAILURE;
         }


         if (!parsed_cmd->pdid)
            parsed_cmd->pdid = create_dispno_display_identifier(1);   // default monitor
         ok = execute_testcase(testnum, parsed_cmd->pdid);
      }
      main_rc = (ok) ? EXIT_SUCCESS : EXIT_FAILURE;
   }
#endif


#ifdef ENABLE_ENVCMDS
   else if (parsed_cmd->cmd_id == CMDID_ENVIRONMENT) {
      DBGTRC(main_debug, TRACE_GROUP, "Processing command ENVIRONMENT...");
      dup2(1,2);   // redirect stderr to stdout
      // ddc_ensure_displays_detected();   // *** NEEDED HERE ??? ***
      // DBGTRC(main_debug, TRACE_GROUP, "display detection complete");
      query_sysenv();
      main_rc = EXIT_SUCCESS;
   }

   else if (parsed_cmd->cmd_id == CMDID_USBENV) {
#ifdef USE_USB
      main_rc = EXIT_SUCCESS;
      DBGTRC(main_debug, TRACE_GROUP, "Processing command USBENV...");
      dup2(1,2);   // redirect stderr to stdout
      // ddc_ensure_displays_detected();   // *** NEEDED HERE ??? ***
      // DBGTRC(main_debug, TRACE_GROUP, "display detection complete");

      query_usbenv();
      main_rc = EXIT_SUCCESS;
#else
      f0printf(fout, "ddcutil was not built with support for USB connected monitors\n");
      main_rc = EXIT_FAILURE;
#endif
   }
#endif

   else if (parsed_cmd->cmd_id == CMDID_CHKUSBMON) {
#ifdef USE_USB
      // DBGMSG("Processing command chkusbmon...\n");
      DBGTRC(main_debug, TRACE_GROUP, "Processing command CHKUSBMON...");
      bool is_monitor = check_usb_monitor( parsed_cmd->args[0] );
      main_rc = (is_monitor) ? EXIT_SUCCESS : EXIT_FAILURE;
#else
      PROGRAM_LOGIC_ERROR("ddcutil not built with USB support");
      main_rc = EXIT_FAILURE;
#endif
   }

#ifdef ENABLE_ENVCMDS
   else if (parsed_cmd->cmd_id == CMDID_INTERROGATE) {
      interrogate(parsed_cmd, main_debug);
      main_rc = EXIT_SUCCESS;
   }
#endif

   // *** Commands that may require Display Identifier ***
   else {
      Display_Ref * dref = NULL;
      Status_Errno_DDC  rc =
      find_dref(parsed_cmd,
               (parsed_cmd->cmd_id == CMDID_LOADVCP) ? DISPLAY_ID_OPTIONAL : DISPLAY_ID_REQUIRED,
               &dref);
      if (rc != DDCRC_OK) {
         main_rc = EXIT_FAILURE;
      }
      else {
         Display_Handle * dh = NULL;
         if (dref) {
            DBGMSF(main_debug,
                   "mainline - display detection complete, about to call ddc_open_display() for dref" );
            Status_Errno_DDC ddcrc = ddc_open_display(dref, callopts |CALLOPT_ERR_MSG, &dh);
            ASSERT_IFF( (ddcrc==0), dh);
            if (!dh) {
               f0printf(ferr(), "Error %s opening display ref %s", psc_desc(ddcrc), dref_repr_t(dref));
               main_rc = EXIT_FAILURE;
            }
         }  // dref

         if (main_rc == EXIT_SUCCESS) {
            // affects all current threads and new threads
            tsd_dsa_enable_globally(parsed_cmd->flags & CMD_FLAG_DSA);
            main_rc = execute_cmd_with_optional_display_handle(parsed_cmd, dh);
         }

         if (dh)
               ddc_close_display(dh);
         if (dref && (dref->flags & DREF_TRANSIENT))
            free_display_ref(dref);
      }
   }

   if (parsed_cmd->stats_types != DDCA_STATS_NONE
#ifdef ENABLE_ENVCMDS
         && parsed_cmd->cmd_id != CMDID_INTERROGATE
#endif
      )
   {
      ddc_report_stats_main(parsed_cmd->stats_types, parsed_cmd->flags & CMD_FLAG_PER_THREAD_STATS, 0);
      // report_timestamp_history();  // debugging function
   }


bye:
   DBGTRC(main_debug, TRACE_GROUP, "Done.  main_rc=%d", main_rc);

   cur_time = time(NULL);
   cur_time_s = asctime(localtime(&cur_time));
   if (cur_time_s[strlen(cur_time_s)-1] == 0x0a)
      cur_time_s[strlen(cur_time_s)-1] = 0;
   DBGTRC(parsed_cmd && (parsed_cmd->traced_groups || parsed_cmd->traced_functions || parsed_cmd->traced_files),
           TRACE_GROUP,   /* redundant with parsed_cmd->traced_groups */
           "ddcutil execution complete, %s",
           cur_time_s);
   if (parsed_cmd)
      free_parsed_cmd(parsed_cmd);
   release_base_services();
   return main_rc;
}
