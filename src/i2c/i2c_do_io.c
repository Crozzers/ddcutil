/*  i2c_do_io.c
 *
 *  Created on: Nov 17, 2015
 *      Author: rock
 */

#include <assert.h>
#include <stdio.h>

#include <util/string_util.h>
#include <base/execution_stats.h>
#include <base/ddc_base_defs.h>
#include <base/parms.h>
#include <i2c/i2c_base_io.h>
#include <i2c/i2c_do_io.h>


I2C_IO_Strategy  i2c_file_io_strategy = {
      write_writer,
      read_reader,
      "read_writer",
      "read_reader"
};

I2C_IO_Strategy i2c_ioctl_io_strategy = {
      ioctl_writer,
      ioctl_reader,
      "ioctl_writer",
      "ioctl_reader"
};

static I2C_IO_Strategy * i2c_io_strategy = &i2c_file_io_strategy;

void i2c_set_io_strategy(I2C_IO_Strategy_Id strategy_id) {
   switch (strategy_id) {
   case (I2C_IO_STRATEGY_FILEIO):
         i2c_io_strategy = &i2c_file_io_strategy;
         break;
   case (I2C_IO_STRATEGY_IOCTL):
         i2c_io_strategy= &i2c_ioctl_io_strategy;
         break;
   }
};


/* Write to the I2C bus, using the function specified in the
 * currently active strategy.
 *
 * Arguments:
 *    fh              file handle for open /dev/i2c bus
 *    bytect          number of bytes to write
 *    bytes_to_write  pointer to bytes to be written
 *    sleep_millisec  delay after writing to bus
 */
Global_Status_Code invoke_i2c_writer(
      int    fh,
      int    bytect,
      Byte * bytes_to_write)
{
   bool debug = false;
   if (debug) {
      char * hs = hexstring(bytes_to_write, bytect);
      DBGMSG("writer=|%s|, bytes_to_write=%s", i2c_io_strategy->i2c_writer_name, hs);
      free(hs);
   }

   Base_Status_Errno_DDC rc;
   RECORD_IO_EVENT(
      IE_WRITE,
      ( rc = i2c_io_strategy->i2c_writer(fh, bytect, bytes_to_write ) )
     );
   if (debug)
      DBGMSG("writer() function returned %d", rc);
   assert (rc <= 0);

   Global_Status_Code gsc = modulate_base_errno_ddc_to_global(rc);
   if (debug)
      DBGMSG("Returning gsc=%s", gsc_desc(gsc));
   return gsc;
}


/* Read from the I2C bus, using the function specified in the
 * currently active strategy.
 *
 * Arguments:
 *    fh              file handle for open /dev/i2c bus
 *    bytect          number of bytes to read
 *    bytes_to_write  location where bytes will be read to
 *    sleep_millisec  delay after reading from bus
 */
Global_Status_Code invoke_i2c_reader(
       int        fh,
       int        bytect,
       Byte *     readbuf)
{
     bool debug = false;
     if (debug)
        DBGMSG("reader=%s, bytect=%d", i2c_io_strategy->i2c_reader_name, bytect);

     Base_Status_Errno_DDC rc;
     RECORD_IO_EVENT(
        IE_READ,
        ( rc = i2c_io_strategy->i2c_reader(fh, bytect, readbuf) )
       );
     if (debug)
        DBGMSG("reader() function returned %d", rc);
     assert (rc <= 0);

     Global_Status_Code gsc = modulate_base_errno_ddc_to_global(rc);
     if (debug )
        DBGMSG("Returning gsc=%s", gsc_desc(gsc));
     return gsc;
}



