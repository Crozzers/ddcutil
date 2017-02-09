/* query_sysenv.c
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

// #include <base/parms.h>    // put first for USE_LIBEXPLAIN

#include <config.h>

#define _GNU_SOURCE 1       // for function group_member

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <grp.h>
#include <limits.h>
// #include <libosinfo-1.0/osinfo/osinfo.h>
// #include <libudev.h>        // not yet used
#include <linux/hiddev.h>
#include <linux/limits.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>

#include "util/device_id_util.h"
#include "util/edid.h"
#include "util/file_util.h"
#include "util/report_util.h"
#include "util/string_util.h"
#include "util/subprocess_util.h"
#include "util/x11_util.h"
#ifdef USE_USB
#include "util/udev_util.h"
#endif


#include "base/ddc_errno.h"
#include "base/core.h"
#include "base/linux_errno.h"

#include "i2c/i2c_bus_core.h"

#include "ddc/ddc_packet_io.h"

#include "adl/adl_shim.h"
#ifdef USE_USB
#include "usb/usb_displays.h"
#endif

#include "query_drm_sysenv.h"
#include "app_ddcutil/query_sysenv.h"



static char * known_video_driver_modules[] = {
      "amdgpu",
      "fglrx",
      "i915",
      "nvidia",
      "nouveau",
      "radeon",
      "vboxvideo",
      NULL
};

static char * prefix_matches[] = {
      "amdgpu",
      "drm",
      "i2c",
      "video",
      NULL
};

static char * other_driver_modules[] = {
      "drm",
      "eeprom",
      "i2c_algo_bit",
      "i2c_dev",
      "i2c_piix4",
      NULL
};


struct driver_name_node;

struct driver_name_node {
   char * driver_name;
   struct driver_name_node * next;
};


// Utilities

char * read_sysfs_attr(char * dirname, char * attrname, bool verbose) {
   char fn[PATH_MAX];
   sprintf(fn, "%s/%s", dirname, attrname);
   return file_get_first_line(fn, verbose);
}


ushort h2ushort(char * hval) {
   bool debug = false;
   int ct;
   ushort ival;
   ct = sscanf(hval, "%hx", &ival);
   assert(ct == 1);
   if (debug)
      DBGMSG("hhhh = |%s|, returning 0x%04x", hval, ival);
   return ival;
}


static void wrap_get_first_line(char * fn, char * title, int depth) {
   int d1 = depth+1;
   if (title)
      rpt_title(title, depth);
   else
      rpt_vstring(depth, "%s:", fn);

   char * s = file_get_first_line(fn, true /* verbose */);
   if (s)
      rpt_title(s, d1);
   else
      rpt_vstring(d1, "Unable to read %s", fn);
}


/* Reports basic system information
 */
static void query_base_env() {
   wrap_get_first_line("/proc/version", NULL, 0);

   rpt_nl();
   rpt_vstring(0,"/etc/os-release...");
   bool ok = execute_shell_cmd_rpt("grep PRETTY_NAME /etc/os-release", 1 /* depth */);
   if (!ok)
      rpt_vstring(1,"Unable to read PRETTY_NAME from /etc/os-release");

   rpt_nl();
   wrap_get_first_line("/proc/cmdline", NULL, 0);

}


static int query_proc_modules_for_video() {
   int rc = 0;

   GPtrArray * garray = g_ptr_array_sized_new(300);

   rpt_vstring(0,"Scanning /proc/modules for driver environment...");
   int ct = file_getlines("/proc/modules", garray, true);
   if (ct < 0)
      rc = ct;
   else {
      int ndx = 0;
      for (ndx=0; ndx<garray->len; ndx++) {
         char * curline = g_ptr_array_index(garray, ndx);
         char mod_name[32];
         int  mod_size;
         int  mod_instance_ct;
         char mod_dependencies[500];
         char mod_load_state[10];     // one of: Live Loading Unloading
         char mod_addr[30];
         int piece_ct = sscanf(curline, "%s %d %d %s %s %s",
                               mod_name,
                               &mod_size,
                               &mod_instance_ct,
                               mod_dependencies,
                               mod_load_state,
                               mod_addr);
         if (piece_ct != 6) {
            DBGMSG("Unexpected error parsing /proc/modules.  sscanf returned %d", piece_ct);
         }
         if (streq(mod_name, "drm") ) {
            rpt_vstring(0,"   Loaded drm module depends on: %s", mod_dependencies);
         }
         else if (streq(mod_name, "video") ) {
            rpt_vstring(0,"   Loaded video module depends on: %s", mod_dependencies);
         }
         else if (exactly_matches_any(mod_name, known_video_driver_modules) >= 0 ) {
            rpt_vstring(0,"   Found video driver module: %s", mod_name);
         }
         else if ( starts_with_any(mod_name, prefix_matches) >= 0 ) {
            rpt_vstring(0,"   Found other loaded module: %s", mod_name);
         }
      }
   }

   return rc;
}


static bool show_one_file(char * dir_name, char * simple_fn, bool verbose, int depth) {
   bool result = false;
   char fqfn[PATH_MAX+2];
   strcpy(fqfn,dir_name);
   if (!str_ends_with(dir_name, "/"))
      strcat(fqfn,"/");
   assert(strlen(fqfn) + strlen(simple_fn) <= PATH_MAX);   // for Coverity
   strncat(fqfn,simple_fn, sizeof(fqfn)-(strlen(fqfn)+1));  // use strncat to make Coverity happy
   if (regular_file_exists(fqfn)) {
      rpt_vstring(depth, "%s:", fqfn);
      rpt_file_contents(fqfn, depth+1);
      result = true;
   }
   else if (verbose)
      rpt_vstring(depth, "File not found: %s", fqfn);
   return result;
}


static bool query_proc_driver_nvidia() {
   bool debug = false;
   bool result = false;
   char * dn = "/proc/driver/nvidia/";
   if ( directory_exists(dn) ) {
      rpt_vstring(0,"Examining /proc/driver/nvidia:");
      result = true;
      show_one_file(dn, "version",  debug, 1);
      show_one_file(dn, "registry", debug, 1);
      show_one_file(dn, "params",   debug, 1);
      char * dn_gpus = "/proc/driver/nvidia/gpus/";
      if (directory_exists(dn_gpus)) {
         DIR * dp = opendir(dn_gpus);
         struct dirent * ep;

         while ( (ep = readdir(dp)) ) {
            if ( !streq(ep->d_name,".") && !streq(ep->d_name, "..") ) {
               // puts(ep->d_name);
               char dirbuf[400];
               strcpy(dirbuf, dn_gpus);
               strcat(dirbuf, ep->d_name);
               // printf("Reading directory: %s\n", dirbuf);
               // DIR * dp2 = opendir(dirbuf);
               // if (dp2) {
               //    struct dirent * ep2;
               //    printf("GPU: %s\n", ep->d_name);
               //    while ( (ep2 = readdir(dp2)) ) {
               //       if ( !streq(ep2->d_name,".") && !streq(ep2->d_name, "..") ) {
               //          puts(ep2->d_name);
               //       }
               //    }
               //    closedir(dp2);
               // }
               if ( directory_exists(dirbuf)) {
                  show_one_file(dirbuf, "information", debug, 1);
                  show_one_file(dirbuf, "registry",    debug, 1);
               }
            }
         }

         closedir(dp);
      }
   }
   else {
       DBGMSF(debug, "Nvidia driver directory %s not found\n", dn);
   }
   return result;
}


bool only_fglrx(struct driver_name_node * driver_list) {
   int driverct = 0;
   bool fglrx_seen = false;
   struct driver_name_node * curnode = driver_list;
   while (curnode) {
      driverct++;
      if (str_starts_with(curnode->driver_name, "fglrx"))
         fglrx_seen = true;
      curnode = curnode->next;
   }
   bool result = (driverct == 1 && fglrx_seen);
   // DBGMSG("driverct = %d, returning %d", driverct, result);
   return result;
}


static bool only_nvidia_or_fglrx(struct driver_name_node * driver_list) {
   int driverct = 0;
   bool other_driver_seen = false;
   struct driver_name_node * curnode = driver_list;
   while (curnode) {
      driverct++;
      if (!str_starts_with(curnode->driver_name, "fglrx") &&
          !streq(curnode->driver_name, "nvidia")
         )
      {
         other_driver_seen = true;
      }
      curnode = curnode->next;
   }
   bool result = (!other_driver_seen && driverct > 0);
   // DBGMSG("driverct = %d, returning %d", driverct, result);
   return result;
}


static bool found_driver(struct driver_name_node * driver_list, char * driver_name) {
   bool found = false;
   struct driver_name_node * curnode = driver_list;
   while (curnode) {
      if ( str_starts_with(curnode->driver_name, driver_name) )
         found = true;
      curnode = curnode->next;
   }
   // DBGMSG("driver_name=%s, returning %d", driver_name, found);
   return found;
}


bool is_i2c_device_rw(int busno) {
   bool debug = false;
   DBGMSF(debug, "Starting. busno=%d", busno);

   bool result = true;

   char fnbuf[PATH_MAX];
   snprintf(fnbuf, sizeof(fnbuf), "/dev/i2c-%d", busno);

   int rc;
   int errsv;
   DBGMSF(debug, "Calling access() for %s", fnbuf);
   rc = access(fnbuf, R_OK|W_OK);
   if (rc < 0) {
      errsv = errno;
      rpt_vstring(0,"Device %s is not readable and writable.  Error = %s",
             fnbuf, linux_errno_desc(errsv) );
      result = false;
   }

   DBGMSF(debug, "Returning: %s", bool_repr(result));
   return result;
}



// adapted from ddc_vcp_tests

Global_Status_Code try_single_getvcp_call(int fh, unsigned char vcp_feature_code) {
   bool debug = false;
   DBGMSF(debug, "Starting. vcp_feature_code=0x%02x", vcp_feature_code );

   int ndx;
   unsigned char checksum;
   int rc;
   Global_Status_Errno gsc = 0;

#ifdef NO
   // write seems to be necessary to reset monitor state
   unsigned char zeroByte = 0x00;  // 0x00;
   rc = write(fh, &zeroByte, 1);
   if (rc < 0) {
      rpt_vstring(0,"(%s) Bus reset failed. rc=%d, errno=%d. ", __func__, rc, errno );
      return -1;
   }
#endif
   // without this or 0 byte write, read() sometimes returns all 0 on P2411H
   usleep(50000);

   unsigned char ddc_cmd_bytes[] = {
      0x6e,              // address 0x37, shifted left 1 bit
      0x51,              // source address
      0x02 | 0x80,       // number of DDC data bytes, with high bit set
      0x01,              // DDC Get Feature Command
      vcp_feature_code,  //
      0x00,              // checksum, to be set
   };
   // unsigned char checksum0 = xor_bytes(ddc_cmd_bytes,5);
   checksum = ddc_checksum(ddc_cmd_bytes, 5, false);    // calculate DDC checksum on all bytes
   // assert(checksum==checksum0);
   ddc_cmd_bytes[5] = ddc_cmd_bytes[0];
   for (ndx=1; ndx < 5; ndx++) ddc_cmd_bytes[5] ^= ddc_cmd_bytes[ndx];    // calculate checksum
   // printf("(%s) ddc_cmd_bytes = %s   \n", __func__ , hexstring(ddc_cmd_bytes,6) );
   // printf("(%s) checksum=0x%02x, ddc_cmd_bytes[5]=0x%02x   \n", __func__, checksum, ddc_cmd_bytes[5] );
   // assert(ddc_cmd_bytes[5] == 0xac);
   assert(checksum == ddc_cmd_bytes[5]);

   int writect = sizeof(ddc_cmd_bytes)-1;
   rc = write(fh, ddc_cmd_bytes+1, writect);
   if (rc < 0) {
      int errsv = errno;
      // printf("(%s) write() returned %d, errno=%d. \n", __func__, rc, errno);
      DBGMSF(debug, "write() failed, errno=%s", linux_errno_desc(errsv));
      gsc = modulate_rc(-errsv, RR_ERRNO);
      goto bye;
   }
   if (rc != writect) {
      rpt_vstring(0,"(%s) write() returned %d, expected %d   ", __func__, rc, writect );
      gsc = DDCRC_BAD_BYTECT;
      goto bye;
   }
   usleep(50000);

   unsigned char ddc_response_bytes[12];
   int readct = sizeof(ddc_response_bytes)-1;

   rc = read(fh, ddc_response_bytes+1, readct);
   if (rc < 0) {
      // printf("(%s) read() returned %d, errno=%d.\n", __func__, rc, errno );
      int errsv = errno;
      DBGMSG("read() failed, errno=%s", linux_errno_desc(errsv));
      gsc = modulate_rc( -errsv, RR_ERRNO);
      goto bye;
   }

   if (rc != readct) {
      rpt_vstring(0,"(%s) read() returned %d, should be %d  ", __func__, rc, readct );
      gsc = DDCRC_BAD_BYTECT;
      goto bye;
   }

   // printf("(%s) read() returned %s\n", __func__, hexstring(ddc_response_bytes+1, readct) );
   if (debug) {
      char * hs = hexstring(ddc_response_bytes+1, readct);
      DBGMSG("read() returned %s", hs );
      free(hs);
      // hex_dump(ddc_response_bytes,1+rc);
   }

   if ( all_zero( ddc_response_bytes+1, readct) ) {
      DBGMSF(debug, "All bytes zero");
      gsc = DDCRC_READ_ALL_ZERO;
      goto bye;
   }


   int ddc_data_length = ddc_response_bytes[2] & 0x7f;
   // some monitors return a DDC null response to indicate an invalid request:
   if (ddc_response_bytes[1] == 0x6e && ddc_data_length == 0 && ddc_response_bytes[3] == 0xbe) {    // 0xbe == checksum
      DBGMSF(debug, "Received DDC null response");
      gsc = DDCRC_NULL_RESPONSE;
      goto bye;
   }

   if (ddc_response_bytes[1] != 0x6e) {
      // assert(ddc_response_bytes[1] == 0x6e);
      rpt_vstring(0,"(%s) Invalid address byte in response, expected 06e, actual 0x%02x", __func__, ddc_response_bytes[1] );
      gsc = DDCRC_INVALID_DATA;
      goto bye;
   }

   if (ddc_data_length != 8) {
      rpt_vstring(0,"(%s) Invalid query VCP response length: %d", __func__, ddc_data_length );
      gsc = DDCRC_BAD_BYTECT;
      goto bye;
   }

   if (ddc_response_bytes[3] != 0x02) {       // get feature response
      rpt_vstring(0,"(%s) Expected 0x02 in feature response field, actual value 0x%02x", __func__, ddc_response_bytes[3] );
      gsc = DDCRC_INVALID_DATA;
      goto bye;
   }

   ddc_response_bytes[0] = 0x50;   // for calculating DDC checksum
   // checksum0 = xor_bytes(ddc_response_bytes, sizeof(ddc_response_bytes)-1);
   unsigned char calculated_checksum = ddc_response_bytes[0];
   for (ndx=1; ndx < 11; ndx++) calculated_checksum ^= ddc_response_bytes[ndx];
   // printf("(%s) checksum0=0x%02x, calculated_checksum=0x%02x\n", __func__, checksum0, calculated_checksum );
   if (ddc_response_bytes[11] != calculated_checksum) {
      rpt_vstring(0,"(%s) Unexpected checksum.  actual=0x%02x, calculated=0x%02x  ", __func__,
             ddc_response_bytes[11], calculated_checksum );
      gsc = DDCRC_CHECKSUM;
      goto bye;
   }

      if (ddc_response_bytes[4] == 0x00) {         // valid VCP code
         // The interpretation for most VCP codes:
         int max_val = (ddc_response_bytes[7] << 8) + ddc_response_bytes[8];
         int cur_val = (ddc_response_bytes[9] << 8) + ddc_response_bytes[10];
         DBGMSF(debug, "cur_val = %d, max_val = %d", cur_val, max_val );
      }
      else if (ddc_response_bytes[4] == 0x01) {    // unsupported VCP code
         rpt_vstring(0,"(%s) Unsupported VCP code: 0x%02x", __func__ , vcp_feature_code);
         gsc = DDCRC_REPORTED_UNSUPPORTED;
      }
      else {
         rpt_vstring(0,"(%s) Unexpected value in supported VCP code field: 0x%02x  ", __func__, ddc_response_bytes[4] );
         gsc = DDCRC_INVALID_DATA;
      }

bye:
   DBGMSF(debug, "Returning: %s",  gsc_desc(gsc));
   return gsc;
}



/* Check each I2C device.
 *
 * This function largely uses direct coding to probe the I2C buses.
 * Allows for trying to read x37 even if X50 fails, and provides
 * clearer diagnostic messages than relying entirely on normal code
 * path.
 */
void raw_scan_i2c_devices() {
   rpt_nl();
   rpt_title("Performing basic scan of I2C devices",0);
   bool debug = false;
   DBGMSF(debug, "Starting");

   Buffer * buf0 = buffer_new(1000, __func__);
   int  busct = 0;
   Global_Status_Code gsc;
   Base_Status_Errno rc;
   bool saved_i2c_force_slave_addr_flag = i2c_force_slave_addr_flag;

   for (int busno=0; busno < I2C_BUS_MAX; busno++) {
      if (i2c_bus_exists(busno)) {
         busct++;
         rpt_nl();
         rpt_vstring(0, "Examining device /dev/i2c-%d...", busno);

         if (!is_i2c_device_rw(busno))
            continue;

         int fd = i2c_open_bus(busno, CALLOPT_ERR_MSG);
         if (fd < 0)
            continue;

         unsigned long functionality = i2c_get_functionality_flags_by_fd(fd);
         i2c_report_functionality_flags(functionality, 90, 1);

         //  Base_Status_Errno rc = i2c_set_addr(fd, 0x50, CALLOPT_ERR_MSG);
         // TODO save force slave addr setting, set it for duration of call - do it outside loop
         gsc = i2c_get_raw_edid_by_fd(fd, buf0);
         if (gsc != 0) {
            rpt_vstring(1, "Unable to read EDID, gsc=%s", gsc_desc(gsc));
         }
         else {
            rpt_vstring(1, "Raw EDID:");
            rpt_hex_dump(buf0->bytes, buf0->len, 1);
            Parsed_Edid *  edid = create_parsed_edid(buf0->bytes);
            if (edid)
              report_parsed_edid(edid, false /* dump hex */, 1);
            else
               rpt_vstring(1, "Unable to parse EDID");
         }

         rpt_nl();
         rpt_vstring(1, "Trying simple VCP read of feature 0x10...");
         rc = i2c_set_addr(fd, 0x37, CALLOPT_ERR_MSG);
         if (rc == 0) {
            int maxtries = 3;
            gsc = -1;
            for (int tryctr=0; tryctr<maxtries && gsc < 0; tryctr++) {
               gsc = try_single_getvcp_call(fd, 0x10);
               if (gsc == 0 || gsc == DDCRC_NULL_RESPONSE || gsc == DDCRC_REPORTED_UNSUPPORTED) {
                  rpt_vstring(1, "Attempt %d to read feature succeeded.", tryctr+1);
                  gsc = 0;
                  break;
               }
               rpt_vstring(1, "Attempt %d to read feature failed. status = %s.  %s",
                             tryctr+1, gsc_desc(gsc), (tryctr < maxtries-1) ? "Retrying..." : "");
            }
            if (gsc == 0)
               rpt_vstring(1, "DDC communication succeeded");
            else {
               rpt_vstring(1, "DDC communication failed.");
            }
         }

         i2c_close_bus(fd, busno, CALLOPT_ERR_MSG);
      }
   }

   if (busct == 0) {
      rpt_vstring(1, "No /dev/i2c-* devices found\n");
   }

   i2c_force_slave_addr_flag = saved_i2c_force_slave_addr_flag;
   buffer_free(buf0, __func__);

   DBGMSF(debug, "Done" );
}


static void check_i2c_devices(struct driver_name_node * driver_list) {
   bool debug = false;
   // int rc;
   // char username[32+1];       // per man useradd, max username length is 32
   char *uname = NULL;
   // bool have_i2c_devices = false;

   rpt_vstring(0,"Checking /dev/i2c-* devices...");
   DDCA_Output_Level output_level = get_output_level();

   bool just_fglrx = only_fglrx(driver_list);
   if (just_fglrx){
      rpt_nl();
      rpt_vstring(0,"Apparently using only the AMD proprietary driver fglrx.");
      rpt_vstring(0,"Devices /dev/i2c-* are not required.");
      if (output_level < OL_VERBOSE)
         return;
      rpt_vstring(0, "/dev/i2c device detail is purely informational.");
   }

   rpt_nl();
   rpt_multiline(0,
          "Unless the system is using the AMD proprietary driver fglrx, devices /dev/i2c-*",
          "must exist and the logged on user must have read/write permission for those",
          "devices (or at least those devices associated with monitors).",
          "Typically, this access is enabled by:",
          "  - setting the group for /dev/i2c-* to i2c",
          "  - setting group RW permissions for /dev/i2c-*",
          "  - making the current user a member of group i2c",
          "Alternatively, this could be enabled by just giving everyone RW permission",
          "The following tests probe for these conditions.",
          NULL
         );

   rpt_nl();
   rpt_vstring(0,"Checking for /dev/i2c-* devices...");
   execute_shell_cmd_rpt("ls -l /dev/i2c-*", 1);

#ifdef OLD
   rc = getlogin_r(username, sizeof(username));
   printf("(%s) getlogin_r() returned %d, strlen(username)=%zd\n", __func__,
          rc, strlen(username));
   if (rc == 0)
      printf("(%s) username = |%s|\n", __func__, username);
   // printf("\nLogged on user:  %s\n", username);
   printf("(%s) getlogin() returned |%s|\n", __func__, getlogin());
   char * cmd = "echo $LOGNAME";
   printf("(%s) executing command: %s\n", __func__, cmd);
   bool ok = execute_shell_cmd_rpt(cmd, 0);
   printf("(%s) execute_shell_cmd() returned %s\n", __func__, bool_repr(ok));

#endif
   uid_t uid = getuid();
   // uid_t euid = geteuid();
   // printf("(%s) uid=%u, euid=%u\n", __func__, uid, euid);
   struct passwd *  pwd = getpwuid(uid);
   rpt_nl();
   rpt_vstring(0,"Current user: %s (%u)\n", pwd->pw_name, uid);
   uname = strdup(pwd->pw_name);

   bool all_i2c_rw = false;
   int busct = i2c_get_busct();   // Consider replacing with local code
   if (busct == 0 && !just_fglrx) {
      rpt_vstring(0,"WARNING: No /dev/i2c-* devices found");
   }
   else {
      all_i2c_rw = true;
      int  busno;
      char fnbuf[20];

      for (busno=0; busno < 32; busno++) {
         if (i2c_bus_exists(busno)) {
            snprintf(fnbuf, sizeof(fnbuf), "/dev/i2c-%d", busno);
            int rc;
            int errsv;
            DBGMSF(debug, "Calling access() for %s", fnbuf);
            rc = access(fnbuf, R_OK|W_OK);
            if (rc < 0) {
               errsv = errno;
               rpt_vstring(0,"Device %s is not readable and writable.  Error = %s",
                      fnbuf, linux_errno_desc(errsv) );
               all_i2c_rw = false;
            }
         }
      }

      if (!all_i2c_rw) {
         rpt_vstring(0,"WARNING: Current user (%s) does not have RW access to all /dev/i2c-* devices.",
 //               username);
                uname);
      }
      else
         rpt_vstring(0,"Current user (%s) has RW access to all /dev/i2c-* devices.",
               // username);
               uname);
   }

   if (!all_i2c_rw || output_level >= OL_VERBOSE) {
      rpt_nl();
      rpt_vstring(0,"Checking for group i2c...");
      // replaced by C code
      // execute_shell_cmd("grep i2c /etc/group", 1);

      bool group_i2c_exists = false;   // avoid special value in gid_i2c
      // gid_t gid_i2c;
      struct group * pgi2c = getgrnam("i2c");
      if (pgi2c) {
         rpt_vstring(0,"   Group i2c exists");
         group_i2c_exists = true;
         // gid_i2c = pgi2c->gr_gid;
         // DBGMSG("getgrnam returned gid=%d for group i2c", gid_i2c);
         // DBGMSG("getgrnam() reports members for group i2c: %s", *pgi2c->gr_mem);
         int ndx=0;
         char * curname;
         bool found_curuser = false;
         while ( (curname = pgi2c->gr_mem[ndx]) ) {
            rtrim_in_place(curname);
            // DBGMSG("member_names[%d] = |%s|", ndx, curname);
            if (streq(curname, uname /* username */)) {
               found_curuser = true;
            }
            ndx++;
         }
         if (found_curuser) {
            rpt_vstring(0,"   Current user %s is a member of group i2c", uname  /* username */);
         }
         else {
            rpt_vstring(0,"   WARNING: Current user %s is NOT a member of group i2c", uname /*username*/);

         }
      }
      if (!group_i2c_exists) {
         rpt_vstring(0,"   Group i2c does not exist");
      }
      free(uname);
   #ifdef BAD
      // getgroups, getgrouplist returning nonsense
      else {
         uid_t uid = geteuid();
         gid_t gid = getegid();
         struct passwd * pw = getpwuid(uid);
         printf("Effective uid %d: %s\n", uid, pw->pw_name);
         char * uname = strdup(pw->pw_name);
         struct group * pguser = getgrgid(gid);
         printf("Effective gid %d: %s\n", gid, pguser->gr_name);
         if (group_member(gid_i2c)) {
            printf("User %s (%d) is a member of group i2c (%d)\n", uname, uid, gid_i2c);
         }
         else {
            printf("WARNING: User %s (%d) is a not member of group i2c (%d)\n", uname, uid, gid_i2c);
         }

         size_t supp_group_ct = getgroups(0,NULL);
         gid_t * glist = calloc(supp_group_ct, sizeof(gid_t));
         int rc = getgroups(supp_group_ct, glist);
         int errsv = errno;
         DBGMSF(debug, "getgroups() returned %d", rc);
         if (rc < 0) {
            DBGMSF(debug, "getgroups() returned %d", rc);

         }
         else {
            DBGMSG("Found %d supplementary group ids", rc);
            int ndx;
            for (ndx=0; ndx<rc; ndx++) {
               DBGMSG("Supplementary group id: %d", *glist+ndx);
            }

         }

         int supp_group_ct2 = 100;
         glist = calloc(supp_group_ct2, sizeof(gid_t));
         DBGMSG("Calling getgrouplist for user %s", uname);
         rc = getgrouplist(uname, gid, glist, &supp_group_ct2);
         errsv = errno;
         DBGMSG("getgrouplist returned %d, supp_group_ct=%d", rc, supp_group_ct2);
         if (rc < 0) {
            DBGMSF(debug, "getgrouplist() returned %d", rc);
         }
         else {
            DBGMSG("getgrouplist found %d supplementary group ids", rc);
            int ndx;
            for (ndx=0; ndx<rc; ndx++) {
               DBGMSG("Supplementary group id: %d", *glist+ndx);
            }
         }
      }
   #endif

      rpt_nl();
      rpt_vstring(0,"Looking for udev nodes files that reference i2c:");
      execute_shell_cmd_rpt("grep -H i2c /etc/udev/makedev.d/*", 1);
      rpt_nl();
      rpt_vstring(0,"Looking for udev rules files that reference i2c:");
      execute_shell_cmd_rpt("grep -H i2c "
                        "/lib/udev/rules.d/*rules "
                        "/run/udev/rules.d/*rules "
                        "/etc/udev/rules.d/*rules", 1 );
   }
}


/* Looks in the /sys file system to check if a module is loaded.
 *
 * Arguments:
 *   module_name    module name
 *
 * Returns:         true if the module is loaded, false if not
 */
bool is_module_loaded_using_sysfs(char * module_name) {
   bool debug = false;
   struct stat statbuf;
   char   module_fn[100];
   bool found = false;
   snprintf(module_fn, sizeof(module_fn), "/sys/module/%s", module_name);
   int rc = stat(module_fn, &statbuf);
   if (rc < 0) {
      // int errsv = errno;
      // will be ENOENT (2) if file not found
      // DBGMSF(debug, "stat(%s) returned %d, errno = %s",
      //       module_fn, rc, linux_errno_desc(errsv));
      found = false;
   }
   else {
      // if (S_ISDIR(statbuf.st_mode))   // pointless
         found = true;
   }
   DBGMSF(debug, "module_name = %s, returning %d", module_name, found);
   return found;
}


/* Checks if a module is built in to the kernel.
 *
 * Arguments:
 *   module_name    simple module name, as it appears in the file system, e.g. i2c-dev
 *
 * Returns:         true/false
 */
static bool is_module_builtin(char * module_name) {
   bool debug = false;
   bool result = false;

   struct utsname utsbuf;
   int rc = uname(&utsbuf);
   assert(rc == 0);
   // DBGMSG("uname() returned release: %s", &utsbuf.release);

   // works, but simpler to use uname() that doesn't require free(osrelease)
   // char * osrelease = file_get_first_line("/proc/sys/kernel/osrelease", true /* verbose */);
   // assert(streq(utsbuf.release, osrelease));

   char modules_builtin_fn[100];
   snprintf(modules_builtin_fn, 100, "/lib/modules/%s/modules.builtin", utsbuf.release);
   // free(osrelease);

   char cmdbuf[200];

   snprintf(cmdbuf, 200, "grep -H %s.ko %s", module_name, modules_builtin_fn);
   // DBGMSG("cmdbuf = |%s|", cmdbuf);

   GPtrArray * response = execute_shell_cmd_collect(cmdbuf);
   // internal rc =  0 if found, 256 if not found
   // returns 0 lines if not found

   // DBGMSG("execute_shell_cmd_collect() returned %d lines", response->len);
   // for (int ndx = 0; ndx < response->len; ndx++) {
   //    puts(g_ptr_array_index(response, ndx));
   // }

   result = (response->len > 0);
   g_ptr_array_free(response, true);

   DBGMSF(debug, "module_name = %s, returning %s", module_name, bool_repr(result));
   return result;
}



/* Checks if module i2c_dev is required and if so whether it is loaded.
 * Reports the result.
 *
 * Arguments:
 *    video_driver_list  list of video drivers
 *
 * Returns:              nothing
 */
static void check_i2c_dev_module(struct driver_name_node * video_driver_list) {
   rpt_vstring(0,"Checking for module i2c_dev...");

   DDCA_Output_Level output_level = get_output_level();

   bool module_required = !only_nvidia_or_fglrx(video_driver_list);
   if (!module_required) {
      rpt_vstring(0,"Using only proprietary nvidia or fglrx driver. Module i2c_dev not required.");
      if (output_level < OL_VERBOSE)
         return;
      rpt_vstring(0,"Remaining i2c_dev detail is purely informational.");
   }

   bool is_builtin = is_module_builtin("i2c-dev");
   rpt_vstring(0,"   Module %-16s is %sbuilt into kernel", "i2c_dev", (is_builtin) ? "" : "NOT ");
   if (is_builtin) {
      if (output_level < OL_VERBOSE)
         return;
      if (module_required)  // no need for duplicate message
         rpt_vstring(0,"Remaining i2c_dev detail is purely informational.");
   }

   bool is_loaded = is_module_loaded_using_sysfs("i2c_dev");
      // DBGMSF(debug, "is_loaded=%d", is_loaded);
   if (!is_builtin)
      rpt_vstring(0,"   Module %-16s is %sloaded", "i2c_dev", (is_loaded) ? "" : "NOT ");

   if ( (!is_loaded && !is_builtin) || output_level >= OL_VERBOSE) {
      rpt_nl();
      rpt_vstring(0,"Check that kernel module i2c_dev is being loaded by examining files where this would be specified...");
      execute_shell_cmd_rpt("grep -H i2c[-_]dev "
                        "/etc/modules "
                        "/etc/modules-load.d/*conf "
                        "/run/modules-load.d/*conf "
                        "/usr/lib/modules-load.d/*conf "
                        , 1);

      rpt_nl();
      rpt_vstring(0,"Check for any references to i2c_dev in /etc/modprobe.d ...");
      execute_shell_cmd_rpt("grep -H i2c[-_]dev "
                        "/etc/modprobe.d/*conf "
                        "/run/modprobe.d/*conf "
                        , 1);
   }
}


static void query_packages() {
   rpt_multiline(0,
         "ddcutil requiries package i2c-tools.  Use both dpkg and rpm to look for it.",
          "While we're at it, check for package libi2c-dev which is used for building",
          "ddcutil.",
          NULL
         );

   bool ok;
   // n. apt show produces warning msg that format of output may change.
   // better to use dpkg
   rpt_nl();
   rpt_vstring(0,"Using dpkg to look for package i2c-tools...");
   ok = execute_shell_cmd_rpt("dpkg --status i2c-tools", 1);
   if (!ok)
      rpt_vstring(0,"dpkg command not found");
   else {
      execute_shell_cmd_rpt("dpkg --listfiles i2c-tools", 1);
   }

   rpt_nl();
   rpt_vstring(0,"Using dpkg to look for package libi2c-dev...");
   ok = execute_shell_cmd_rpt("dpkg --status libi2c-dev", 1);
   if (!ok)
      rpt_vstring(0,"dpkg command not found");
   else {
      execute_shell_cmd_rpt("dpkg --listfiles libi2c-dev", 1);
   }

   rpt_nl();
   rpt_vstring(0,"Using rpm to look for package i2c-tools...");
   ok = execute_shell_cmd_rpt("rpm -q -l --scripts i2c-tools", 1);
   if (!ok)
      rpt_vstring(0,"rpm command not found");
}


static bool query_card_and_driver_using_lspci() {
   // DBGMSG("Starting");
   bool ok = true;
   FILE * fp;

   rpt_vstring(0,"Using lspci to examine driver environment...");
   fp = popen("lspci", "r");
   if (!fp) {
      // int errsv = errno;
      rpt_vstring(0,"Unable to execute command lspci: %s", strerror(errno));

      printf("lspci command unavailable\n");       // why doesn't this print?
      printf("lspci command really unavailable\n");  // or this?
      ok = false;
   }
   else {
      char * a_line = NULL;
      size_t len = 0;
      ssize_t read;
      char pci_addr[15];
      // char device_title[100];
      char device_name[300];
      while ( (read=getline(&a_line, &len, fp)) != -1) {
         if (strlen(a_line) > 0)
            a_line[strlen(a_line)-1] = '\0';
         // UGLY UGLY - WHY DOESN'T SCANF WORK ???
         // DBGMSG("lspci line: |%s|", a_line);
#ifdef SCAN_FAILS
         // doesn't find ':'
         // char * pattern = "%s %s:%s";
         char * pattern = "%[^' '],%[^':'], %s";
         int ct = sscanf(a_line, pattern, pci_addr, device_title, device_name);

         DBGMSG("ct=%d, t_read=%ld, pci_addr=%s, device_title=%s", ct, len, pci_addr, device_title);
         if (ct == 3) {
            if ( str_starts_with("VGA", device_title) ) {
               printf("Video controller: %s\n", device_name);
            }
         }
#endif
         int ct = sscanf(a_line, "%s %s", pci_addr, device_name);
         // DBGMSG("ct=%d, t_read=%ld, pci_addr=%s, device_name=%s", ct, len, pci_addr, device_name);
         if (ct == 2) {
            if ( str_starts_with("VGA", device_name) ) {
               // printf("Video controller 0: %s\n", device_name);
               char * colonpos = strchr(a_line + strlen(pci_addr), ':');
               if (colonpos)
                  rpt_vstring(0,"Video controller: %s", colonpos+1);
               else
                  rpt_vstring(0,"colon not found");
            }
         }
      }
      pclose(fp);
   }
   return ok;
}



static struct driver_name_node * query_card_and_driver_using_sysfs() {
   // bool debug = true;
   rpt_vstring(0,"Obtaining card and driver information from /sys...");

   // also of possible interest:
   // /sys/class/i2c-dev/i2c-*/name
   //    refers to video driver or piix4_smbus
   // also accessed at:
   // /sys/bus/i2c/devices/i2c-*/name
   // /sys/bus/pci/drivers/nouveau
   // /sys/bus/pci/drivers/piix4_smbus
   // /sys/bus/pci/drivers/nouveau/0000:01:00.0
   //                                           /name
   //                                           i2c-dev
   // /sys/module/nvidia
   // /sys/module/i2c_dev ?
   // /sys/module/... etc

   // bool ok = true;
   char * driver_name = NULL;
   struct driver_name_node * driver_list = NULL;

   struct dirent *dent;
   DIR           *d;

   char * d0 = "/sys/bus/pci/devices";
   d = opendir(d0);
   if (!d) {
      rpt_vstring(0,"Unable to open directory %s: %s", d0, strerror(errno));
   }
   else {
      while ((dent = readdir(d)) != NULL) {
         // DBGMSG("%s", dent->d_name);

         char cur_fn[100];
         char cur_dir_name[100];
         if (!streq(dent->d_name, ".") && !streq(dent->d_name, "..") ) {
            sprintf(cur_dir_name, "%s/%s", d0, dent->d_name);
            sprintf(cur_fn, "%s/class", cur_dir_name);
            char * class_id = read_sysfs_attr(cur_dir_name, "class", true);
            // printf("%s: |%s|\n", cur_fn, class_id);
            // if (streq(class_id, "0x030000")) {
            if (str_starts_with(class_id, "0x03")) {
               // printf("%s = 0x030000\n", cur_fn);

#ifdef WORKS
               printf("\nReading values from individual attribute files:\n");
               printf("vendor: %s\n", read_sysfs_attr(cur_dir_name, "vendor", true));
               printf("device: %s\n", read_sysfs_attr(cur_dir_name, "device", true));
               printf("subsystem_device: %s\n", read_sysfs_attr(cur_dir_name, "subsystem_device", true));
               printf("subsystem_vendor: %s\n", read_sysfs_attr(cur_dir_name, "subsystem_vendor", true));
#endif
               char * modalias = read_sysfs_attr(cur_dir_name, "modalias", true);
               // printf("modalias: %s\n", modalias);

               rpt_nl();
               rpt_vstring(0,"Determining driver name and possibly version...");
               // DBGMSG("cur_dir_name: %s", cur_dir_name);
               char workfn[PATH_MAX];
               sprintf(workfn, "%s/%s", cur_dir_name, "driver");
               char resolved_path[PATH_MAX];
               char * rpath = realpath(workfn, resolved_path);
               if (!rpath) {
                  int errsv = errno;
                  if (errsv == ENOENT) {
                     // fail in virtual environment?
                     rpt_vstring(0,"Cannot determine driver name");
                  }
                  else {
                     DBGMSG("realpath(%s) returned NULL, errno=%d (%s)", workfn, errsv, linux_errno_name(errsv));
                  }
               }
               else {
                  // printf("realpath returned %s\n", rpath);
                  // printf("%s --> %s\n",workfn, resolved_path);
                  char * final_slash_ptr = strrchr(rpath, '/');
                  // TODO: handle case where there are more than 1 video drivers loaded,
                  // say if the system contains both an AMD and Nvidia card
                  driver_name = final_slash_ptr+1;
                  printf(    "   Driver name:    %s\n", driver_name);
                  struct driver_name_node * new_node = calloc(1, sizeof(struct driver_name_node));
                  new_node->driver_name = strdup(driver_name);
                  new_node->next = driver_list;
                  driver_list = new_node;


                  char driver_module_dir[PATH_MAX];
                  sprintf(driver_module_dir, "%s/driver/module", cur_dir_name);
                  // printf("driver_module_dir: %s\n", driver_module_dir);
                  char * driver_version = read_sysfs_attr(driver_module_dir, "version", false);
                  if (driver_version)
                      rpt_vstring(0,"   Driver version: %s", driver_version);
                  else
                     rpt_vstring(0,"   Unable to determine driver version");
               }


               // printf("\nParsing modalias for values...\n");
               char * colonpos = strchr(modalias, ':');
               assert(colonpos);                // coverity complains that strchr() might return NULL
               assert(*(colonpos+1) == 'v');    // vendor_id
               char * vendor_id = substr(colonpos, 2, 8);
               // printf("vendor_id:        %s\n", vendor_id);
               assert(*(colonpos+10) == 'd');
               char * device_id = lsub(colonpos+11,8);
               // printf("device_id:        %s\n", device_id);
               assert( *(colonpos+19) == 's');
               assert( *(colonpos+20) == 'v');
               char * subsystem_vendor = lsub(colonpos+21,8);
               // printf("subsystem_vendor: %s\n", subsystem_vendor);
               assert( *(colonpos+29) == 's');
               assert( *(colonpos+30) == 'd');
               char * subsystem_device = lsub(colonpos+31,8);
               // printf("subsystem_device: %s\n", subsystem_device);
               assert( *(colonpos+39) == 'b');
               assert( *(colonpos+40) == 'c');
               // not used
               //char * base_class = lsub(colonpos+41,2);
               // printf("base_class:       %s\n", base_class);     // bytes 0-1 of value from class
               assert( *(colonpos+43) == 's');
               assert( *(colonpos+44) == 'c');
               // not used
               // char * sub_class = lsub(colonpos+45,2);          // bytes 1-2 of value from class
               // printf("sub_class:        %s\n", sub_class);
               assert( *(colonpos+47) == 'i');
               // not used
               // char * interface_id = lsub(colonpos+48,2);
               // printf("interface_id:     %s\n", interface_id);  // bytes 4-5 of value from class?


               // printf("\nConverting modalias strings to ushort...\n");
               ushort xvendor_id    = h2ushort(vendor_id);
               ushort xdevice_id    = h2ushort(device_id);
               ushort xsubvendor_id = h2ushort(subsystem_vendor);
               ushort xsubdevice_id = h2ushort(subsystem_device);

               // printf("\nLooking up names in pci.ids...\n");
               rpt_nl();
               rpt_vstring(0,"Video card identification:");
               bool pci_ids_ok = devid_ensure_initialized();
               if (pci_ids_ok) {
                  Pci_Usb_Id_Names names = devid_get_pci_names(
                                  xvendor_id,
                                  xdevice_id,
                                  xsubvendor_id,
                                  xsubdevice_id,
                                  4);
                  if (!names.vendor_name)
                     names.vendor_name = "unknown vendor";
                  if (!names.device_name)
                     names.device_name = "unknown device";

                  rpt_vstring(0,"   Vendor:              %04x       %s", xvendor_id, names.vendor_name);
                  rpt_vstring(0,"   Device:              %04x       %s", xdevice_id, names.device_name);
                  if (names.subsys_or_interface_name)
                  rpt_vstring(0,"   Subvendor/Subdevice: %04x/%04x  %s", xsubvendor_id, xsubdevice_id, names.subsys_or_interface_name);
               }
               else {
                  rpt_vstring(0,"Unable to find pci.ids file for name lookup.");
                  rpt_vstring(0,"   Vendor:              %04x       ", xvendor_id);
                  rpt_vstring(0,"   Device:              %04x       ", xdevice_id);
                  rpt_vstring(0,"   Subvendor/Subdevice: %04x/%04x  ", xsubvendor_id, xsubdevice_id);
               }
               free(vendor_id);
               free(device_id);
               free(subsystem_vendor);
               free(subsystem_device);
            }
         }
      }
      closedir(d);
   }

   return driver_list;
}


/* Performs checks specific to the nvidia and fglrx proprietary video drivers.
 *
 * Arguments:
 *    driver list    list of loaded drivers
 *
 * Returns:          nothing
 */
static void driver_specific_tests(struct driver_name_node * driver_list) {
   rpt_vstring(0,"Performing driver specific checks...");
   bool found_driver_specific_checks = false;

   if (found_driver(driver_list, "nvidia")) {
      found_driver_specific_checks = true;
      rpt_nl();
      rpt_vstring(0,"Checking for special settings for proprietary Nvidia driver ");
      rpt_vstring(0,"(needed for some newer Nvidia cards).");
      execute_shell_cmd_rpt("grep -iH i2c /etc/X11/xorg.conf /etc/X11/xorg.conf.d/*", 1);
   }

   if (found_driver(driver_list, "fglrx")) {
      found_driver_specific_checks = true;
      rpt_nl();
      rpt_vstring(0,"Performing ADL specific checks...");
#ifdef HAVE_ADL
     if (!adlshim_is_available()) {
        set_output_level(OL_VERBOSE);  // force error msg that names missing dll
        bool ok = adlshim_initialize();
        if (!ok)
           printf("WARNING: Using AMD proprietary video driver fglrx but unable to load ADL library\n");
     }
#else
     rpt_vstring(0,"WARNING: Using AMD proprietary video driver fglrx but ddcutil built without ADL support");
#endif
   }

   if (!found_driver_specific_checks)
      rpt_vstring(0,"No driver specific checks apply.");
}


static void query_loaded_modules_using_sysfs() {
   rpt_nl();
   rpt_vstring(0,"Testing if modules are loaded using /sys...");
   // known_video_driver_modules
   // other_driver_modules

   char ** pmodule_name = known_video_driver_modules;
   char * curmodule;
   int ndx;
   for (ndx=0; (curmodule=pmodule_name[ndx]) != NULL; ndx++) {
      bool is_loaded = is_module_loaded_using_sysfs(curmodule);
      // DBGMSF(debug, "is_loaded=%d", is_loaded);
      rpt_vstring(0,"   Module %-16s is %sloaded", curmodule, (is_loaded) ? "" : "NOT ");
   }
   pmodule_name = other_driver_modules;
   for (ndx=0; (curmodule=pmodule_name[ndx]) != NULL; ndx++) {
      bool is_loaded = is_module_loaded_using_sysfs(curmodule);
      rpt_vstring(0,"   Module %-16s is %sloaded", curmodule, (is_loaded) ? "" : "NOT ");
   }
}


static void query_i2c_bus_using_sysfs() {
   struct dirent *dent;
   DIR           *d;
   char          *dname;

   rpt_nl();
   rpt_vstring(0,"Examining /sys/bus/i2c/devices...");
   dname = "/sys/bus/i2c";
   d = opendir(dname);
   if (!d) {
      rpt_vstring(1, "i2c bus not defined in sysfs. Unable to open directory %s: %s\n",
                     dname, strerror(errno));
   }
   else {
      closedir(d);
      dname = "/sys/bus/i2c/devices";
      d = opendir(dname);
      if (!d) {
         rpt_vstring(1, "Unable to open sysfs directory %s: %s\n", dname, strerror(errno));
      }
      else {
         bool i2c_seen = false;
         while ((dent = readdir(d)) != NULL) {
            // DBGMSF("%s", dent->d_name);
            // char cur_fn[100];
            char cur_dir_name[100];
            if (!streq(dent->d_name, ".") && !streq(dent->d_name, "..") ) {
               // DBGMSF(debug, "dent->dname: %s", dent->d_name);
               sprintf(cur_dir_name, "%s/%s", dname, dent->d_name);
               char * dev_name = read_sysfs_attr(cur_dir_name, "name", true);
               rpt_vstring(1, "%s/name: %s", cur_dir_name, dev_name);
               i2c_seen = true;
            }
         }
         if (!i2c_seen)
            rpt_vstring(1, "No i2c devices found in %s", dname);
         closedir(d);
      }
   }
}


#ifdef UNUSED
static bool query_card_and_driver_using_osinfo() {
   bool ok = false;

#ifdef FAILS
   printf("Trying Osinfo\n");

   OsinfoDb * info_db = osinfo_db_new();

   OsinfoDeviceList * device_list = osinfo_db_get_device_list(info_db);
   gint device_ct = osinfo_list_get_length(device_list);
   int ndx = 0;
   for (ndx=0; ndx < ct; ndx++) {
      OsinfoEntity * entity = osinfo_list_get_nth(device_list, ndx);
      char * entity_id = osinfo_entity_get_id(entity);
      DBGMSG("osinfo entity id = %s", entity_id );

   }
#endif

   return ok;
}
#endif


static void query_i2c_buses() {
   rpt_nl();
   rpt_vstring(0,"Examining i2c buses...");
   i2c_report_buses(true, 1 /* indentation depth */);
}


/* Reports EDIDs known to X11
 *
 * Arguments:    none
 *
 * Returns:      nothing
 */
void query_x11() {
   GPtrArray* edid_recs = get_x11_edids();
   rpt_nl();
   rpt_vstring(0,"EDIDs reported by X11 for connected xrandr outputs:");
   // DBGMSG("Got %d X11_Edid_Recs\n", edid_recs->len);

   for (int ndx=0; ndx < edid_recs->len; ndx++) {
      X11_Edid_Rec * prec = g_ptr_array_index(edid_recs, ndx);
      // printf(" Output name: %s -> %p\n", prec->output_name, prec->edid);
      // hex_dump(prec->edid, 128);
      rpt_vstring(1, "xrandr output: %s", prec->output_name);
      rpt_vstring(2, "Raw EDID:");
      rpt_hex_dump(prec->edidbytes, 128, 2);
      Parsed_Edid * parsed_edid = create_parsed_edid(prec->edidbytes);
      if (parsed_edid) {
         report_parsed_edid(parsed_edid, true /* verbose */, 2 /* depth */);
         free_parsed_edid(parsed_edid);
      }
      else {
         rpt_vstring(2, "Unable to parse EDID");
         // printf(" Unparsable EDID for output name: %s -> %p\n", prec->output_name, prec->edidbytes);
         // hex_dump(prec->edidbytes, 128);
      }
      rpt_nl();
   }
   free_x11_edids(edid_recs);

   // Display * x11_disp = open_default_x11_display();
   // GPtrArray *  outputs = get_x11_connected_outputs(x11_disp);
   // close_x11_display(x11_disp);
}


/* Uses i2cdetect to probe active addresses on I2C buses
 *
 * Arguments:    none
 *
 * Returns:      nothing
 */
static void query_using_i2cdetect() {
   rpt_vstring(0,"Examining I2C buses using i2cdetect: ");
   // GPtrArray * busnames = execute_shell_cmd_collect("ls /dev/i2c*");
   GPtrArray * busnames = execute_shell_cmd_collect("ls /dev/i2c* | cut -c 10- | sort -n");
   for (int ndx = 0; ndx < busnames->len; ndx++) {
      // printf("ndx=%d, value=|%s|\n", ndx, (char *) g_ptr_array_index(busnames, ndx));
      char cmd[80];
      char * busname = (char *) g_ptr_array_index(busnames, ndx);
      // busname+=9;   // strip off "/dev/i2c-"
      snprintf(cmd, 80, "i2cdetect -y %s", busname);
      rpt_nl();
      rpt_vstring(0,"Probing bus /dev/i2c-%d using command \"%s\"", ndx, cmd);
      // DBGMSG("Executing command: |%s|\n", cmd);
      int rc = execute_shell_cmd_rpt(cmd, 1 /* depth */);
      // DBGMSG("execute_shell_cmd(\"%s\") returned %d", cmd, rc);
      if (rc != 1) {
         rpt_vstring(0,"i2cdetect command unavailable");
         break;
      }
   }
   g_ptr_array_free(busnames, true);
}


#ifdef USE_USB
int udev_i2c_device_summary_busno(Udev_Device_Summary * summary) {
   int result = -1;
   if (str_starts_with(summary->sysname, "i2c-")) {
     const char * sbusno = summary->sysname+4;
     // DBGMSG("sbusno = |%s|", sbusno);

     int ibusno;
     bool ok = str_to_int(sbusno, &ibusno);
     if (ok)
        result = ibusno;
   }
   // DBGMSG("Returning: %d", result);
   return result;
}


int compare_udev_i2c_device_summary(const void * a, const void * b) {
   Udev_Device_Summary * p1 = *(Udev_Device_Summary**) a;
   Udev_Device_Summary * p2 = *(Udev_Device_Summary**) b;

   assert( p1 && (memcmp(p1->marker, UDEV_DEVICE_SUMMARY_MARKER, 4) == 0));
   assert( p2 && (memcmp(p2->marker, UDEV_DEVICE_SUMMARY_MARKER, 4) == 0));

   int v1 = udev_i2c_device_summary_busno(p1);
   int v2 = udev_i2c_device_summary_busno(p2);

   int result = (v1 == v2) ? 0 :
                    (v1 < v2) ? -1 : 1;
   // DBGMSG("v1=%d, v2=%d, returning: %d", v1, v2, result);
   return result;
}


void probe_i2c_devices_using_udev() {
   char * subsys_name = "i2c-dev";
   rpt_nl();
   rpt_vstring(0,"Probing I2C devices using udev, susbsystem %s...", subsys_name);
   // probe_udev_subsystem() is in udev_util.c, which is only linked in if USE_USB
   probe_udev_subsystem(subsys_name, /*show_usb_parent=*/ false, 1);

   GPtrArray * summaries = summarize_udev_subsystem_devices(subsys_name);
   rpt_nl();
   rpt_vstring(0,"Summary of udev I2C devices:");
   if (!summaries || summaries->len == 0)
      rpt_vstring(0,"No devices detected");
   else {
#ifdef REFERENCE
#define UDEV_DEVICE_SUMMARY_MARKER "UDSM"
typedef struct udev_device_summary {
char   marker[4];
const char * sysname;
const char * devpath;
const char * sysattr_name;
} Udev_Device_Summary;
#endif

      g_ptr_array_sort(summaries, compare_udev_i2c_device_summary);
      rpt_vstring(0,"%-15s %-35s %s", "Sysname", "Sysattr Name", "Devpath");
      for (int ndx = 0; ndx < summaries->len; ndx++) {
         Udev_Device_Summary * summary = g_ptr_array_index(summaries, ndx);
         assert( memcmp(summary->marker, UDEV_DEVICE_SUMMARY_MARKER, 4) == 0);
         udev_i2c_device_summary_busno(summary);
         rpt_vstring(0,"%-15s %-35s %s",
                summary->sysname, summary->sysattr_name, summary->devpath);
      }
   }
   free_udev_device_summaries(summaries);   // ok if summaries == NULL
}

#endif


/* Master function to query the system environment
 *
 * Arguments:    none
 *
 * Returns:      nothing
 */
void query_sysenv() {
   rpt_nl();
   rpt_vstring(0,"*** Basic System Information ***");
   rpt_nl();
   query_base_env();

   rpt_nl();
   rpt_vstring(0,"*** Primary Check 1: Identify video card and driver ***");
   rpt_nl();
   struct driver_name_node * driver_list = query_card_and_driver_using_sysfs();

   rpt_nl();
   rpt_vstring(0,"*** Primary Check 2: Check that /dev/i2c-* exist and writable ***");
   rpt_nl();
   check_i2c_devices(driver_list);

   rpt_nl();
   rpt_vstring(0,"*** Primary Check 3: Check that module i2c_dev is loaded ***");
   rpt_nl();
   check_i2c_dev_module(driver_list);

   rpt_nl();
   rpt_vstring(0,"*** Primary Check 4: Driver specific checks ***");
   rpt_nl();
   driver_specific_tests(driver_list);

   // Free the driver list
   struct driver_name_node * cur_node = driver_list;
   while (cur_node) {
      struct driver_name_node * next_node = cur_node->next;
      free(cur_node);
      cur_node = next_node;
   }

   rpt_nl();
   rpt_vstring(0,"*** Primary Check 5: Installed packages ***");
   rpt_nl();
   query_packages();
   rpt_nl();

   rpt_nl();
   rpt_vstring(0,"*** Additional probes ***");
   // printf("Gathering card and driver information...\n");
   rpt_nl();
   query_proc_modules_for_video();
   rpt_nl();
   query_card_and_driver_using_lspci();
   rpt_nl();
   query_loaded_modules_using_sysfs();
   query_i2c_bus_using_sysfs();

   DDCA_Output_Level output_level = get_output_level();
   if (output_level >= OL_VERBOSE) {
      rpt_nl();
      query_proc_driver_nvidia();
   }

   if (output_level >= OL_VERBOSE) {
      query_i2c_buses();

      rpt_nl();
      rpt_vstring(0,"xrandr connection report:");
      execute_shell_cmd_rpt("xrandr|grep connected", 1 /* depth */);
      rpt_nl();

      rpt_vstring(0,"Checking for possibly conflicting programs...");
      execute_shell_cmd_rpt("ps aux | grep ddccontrol | grep -v grep", 1);
      rpt_nl();

      query_using_i2cdetect();

      raw_scan_i2c_devices();

      query_x11();

#ifdef USE_USB
      // probe_udev_subsystem() is in udev_util.c, which is only linked in if USE_USB
      probe_i2c_devices_using_udev();
#endif

      rpt_nl();
      rpt_vstring(0,"Checking dmesg for drm related lines...");
      if ( !execute_shell_cmd_rpt("dmesg | grep drm", 1 /* depth */) )
         rpt_vstring(1,"Unable to process dmesg");
      rpt_nl();

      rpt_vstring(0,"Checking Xorg.0.log for drm related lines...");
      if ( !execute_shell_cmd_rpt("grep drm /var/log/Xorg.0.log", 1 /* depth */) )
         rpt_vstring(1,"Unable to read Xorg.0.log");
      rpt_nl();

#ifdef USE_LIBDRM
      probe_using_libdrm();
#else
      rpt_vstring(0, "Not built with libdrm support.  Skipping DRM related checks");
#endif
   }


}

