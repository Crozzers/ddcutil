/*
 * i2c_edid_tests.c
 *
 *  Created on: Jul 30, 2014
 *      Author: rock
 */

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>        // usleep
#include <stdbool.h>
#include <i2c-dev.h>
#include <fcntl.h>

#include <util/debug_util.h>
#include <util/string_util.h>

#include <base/util.h>
#include <base/msg_control.h>
#include <base/parms.h>


#include <test/i2c/i2c_io_old.h>
#include <i2c/i2c_bus_core.h>

#include <test/i2c/i2c_edid_tests.h>


// Test reading EDID using essentially the code in libxcm.

void read_edid_ala_libxcm(int busno) {
   printf("\nReading EDID for bus %d using XcmDDC method\n", busno);

   int    fd;
   char   command[128] = {0};
   int    rc;
   unsigned char* edidbuf;

   fd = open_i2c_bus(busno,EXIT_IF_FAILURE);
   set_addr(fd, 0x50);
   // usleep(TIMEOUT);
   sleep_millis_with_trace(100, __func__, "before write()");

   rc = write(fd, &command, 1);
   if (rc != 1) {
      printf("(%s) write returned %d\n", __func__, rc);  puts(" ");
   }
   else {
      // usleep(TIMEOUT);
      sleep_millis_with_trace(DDC_TIMEOUT_MILLIS_DEFAULT, __func__, NULL);
      edidbuf = (Byte *)call_calloc(sizeof(Byte),256, "read_edid_ala_libxcm");
      rc = read(fd, edidbuf, 128);
      printf("(%s) read() returned %d\n", __func__, rc);
      if (rc >= 0) {
         hex_dump(edidbuf, rc);
      }
      call_free(edidbuf, "read_edid_ala_libxcm");
   }
   close(fd);
}


// Test reading EDID using various methods.

void probe_read_edid(int busno, char * write_mode, char * read_mode) {
   printf("\n(%s) Reading EDID for bus %d, write_mode=%s, read_mode=%s\n", __func__, busno, write_mode, read_mode);

   int   rc;
   Byte* edidbuf;
   int   errsv;
   int   fd;
   // bool  debug = true;
   Byte  cmd_byte = 0xFF;  // for cases where cmd byte must be passed

   fd = open_i2c_bus(busno,EXIT_IF_FAILURE);
   set_addr(fd, 0x50);
   // usleep(TIMEOUT);
   sleep_millis_with_trace(DDC_TIMEOUT_MILLIS_DEFAULT, __func__, NULL);

   Byte byte_to_write = 0x00;
   set_i2c_write_mode(write_mode);
   rc = perform_i2c_write2(fd, 1, &byte_to_write, DDC_TIMEOUT_USE_DEFAULT);
   // rc = perform_i2c_write(fd, write_mode, 1, &byte_to_write);

   if (rc == 0) {
      edidbuf = (Byte *)call_calloc(sizeof(Byte),256, "probe_read_edid, edidbuf");

      if ( streq(read_mode,"read") ) {
         rc = do_i2c_file_read(fd, 128, edidbuf, DDC_TIMEOUT_USE_DEFAULT);
         // Byte cmd_byte = 0x00;           // ignored for call_read
         // rc = perform_read(file, "read", 128, edidbuf, cmd_byte);
      }

      else if ( streq(read_mode, "i2c_smbus_read_block_data") ) {
         printf("Reading edid using i2c_smbus_read_block_data\n");
         errno = 0;
         rc = i2c_smbus_read_block_data(fd, (unsigned char) 0x00, edidbuf);
         errsv = errno;
         printf("i2c_smbus_read_block_data returned %d, errno=%d\n", rc, errsv);
      }

      else if ( streq(read_mode,"i2c_smbus_read_byte") ) {
         int ndx;
         char byte;

         printf("Reading edid using i2c_smbus_read_byte()\n");
         for(ndx=0; ndx<128;ndx++){
            errno = 0;
            rc = i2c_smbus_read_byte(fd);
            errsv = errno;
            if (errno != 0 || rc == -1)
               printf("i2c_smbus_read_byte returned %d (%x), errno=%d\n", rc, rc, errsv);
            if (rc == -1) break;
            byte = rc & 0xff;
            edidbuf[ndx] = byte;
         }
         printf("Reading edid using i2c_smbus_read_byte() returning buffer of length %d\n", ndx);
         rc = ndx;
      }

      else if ( streq(read_mode, "i2c_smbus_read_byte_data") ) {
         int ndx;
         char byte;

         printf("Reading edid using i2c_smbus_read_byte_data(), cmd=0x%02x\n", cmd_byte);
         for (ndx=0; ndx<128;ndx++){
            errno = 0;
            rc = i2c_smbus_read_byte_data(fd, cmd_byte);
            errsv = errno;
            if (errno != 0 || rc == -1)
               printf("i2c_smbus_read_byte_data returned %d (0x%x), errno=%d\n", rc, rc, errsv);
               if (rc == -1) break;
               byte = rc & 0xff;
               edidbuf[ndx] = byte;
            }
            printf("Reading edid using i2c_smbus_read_byte_data() returning buffer of length %d\n", ndx);
            rc = ndx;
         }

      else if ( streq(read_mode, "i2c_smbus_read_i2c_block_data") ) {
         rc = do_i2c_smbus_read_i2c_block_data(fd, 32, edidbuf, DDC_TIMEOUT_USE_DEFAULT);
      }

      else {
         printf("Invalid read_mode: %s", write_mode);
         rc = -1;
      }

      if (rc > 0) {
         hex_dump(edidbuf,rc);
      }
      call_free(edidbuf, "probe_read_edid, edidbuf");
   }
   close(fd);
}


void test_read_edid_ala_libxcm() {
   read_edid_ala_libxcm(0);
   read_edid_ala_libxcm(1);
   read_edid_ala_libxcm(2);
   read_edid_ala_libxcm(3);
   read_edid_ala_libxcm(4);
   read_edid_ala_libxcm(5);
   read_edid_ala_libxcm(6);
}


void test_read_edid_for_bus(int busno) {
   // read_edid_ala_libxcm(0);
   // read_edid_ala_libxcm(3);
   //        busno, write_mode, read_mode
   probe_read_edid(busno,     "write",                   "read");                       // works
   probe_read_edid(busno,     "write",                   "i2c_smbus_read_block_data");  // fails
   probe_read_edid(busno,     "i2c_smbus_write_byte",    "read");                       // works
   probe_read_edid(busno,     "i2c_smbus_write_byte",    "read");                       // works
   probe_read_edid(busno,     "i2c_smbus_write_byte",    "i2c_smbus_read_block_data");  // fails: i2c_smbus_read_block_data unsupported
   probe_read_edid(busno,     "i2c_smbus_write_byte",    "i2c_smbus_read_byte");        // works
   probe_read_edid(busno,     "i2c_smbus_write_byte",    "i2c_smbus_read_byte");        // works
   probe_read_edid(busno,     "i2c_smbus_write_byte",    "i2c_smbus_read_byte_data");   // fails, all 0
   probe_read_edid(busno,     "None",                    "read");                       // works
   probe_read_edid(busno,     "None",                    "read");                       // fails, all FF => write reqd before read
   probe_read_edid(busno,     "None",                    "i2c_smbus_read_byte");        // works
   probe_read_edid(busno,     "None",                    "i2c_smbus_read_byte");        // fails => initializer necessary when reading with i2c_smbus_read_byte
   probe_read_edid(busno,     "None",                    "i2c_smbus_read_byte_data");   // fails all 0
   probe_read_edid(busno,     "i2c_smbus_write_byte",    "i2c_smbus_read_i2c_block_data"); // fails: i2c_smbus_read_i2c_block_data() unsupported
   probe_read_edid(busno,     "None",                    "i2c_smbus_read_i2c_block_data"); // rails: i2c_smbus_read_i2c_block_data() unsupported
}
