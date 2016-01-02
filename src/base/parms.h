/* parms.h
 *
 * Created on: Oct 23, 2015
 *     Author: rock
 *
 * Tunable parameters.
 *
 * <copyright>
 * Copyright (C) 2014-2015 Sanford Rockowitz <rockowitz@minsoft.com>
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

#ifndef PARMS_H_
#define PARMS_H_

//not very informative
// #define USE_LIBEXPLAIN

// Should this really be in parms?   These values are obtained from the DDC spec.

// Timeout values in microseconds
// n. the DDC spec lists timeout values in milliseconds
#define DDC_TIMEOUT_MILLIS_DEFAULT                 50    // per spec
// #define DDC_TIMEOUT_MILLIS_DEFAULT                 20
#ifdef UNUSED
#define DDC_TIMEOUT_MILLIS_POST_GETVCP_WRITE       40  // per spec
// #define DDC_TIMEOUT_MILLIS_POST_GETVCP_WRITE       20
#endif
#define DDC_TIMEOUT_MILLIS_POST_SETVCP_WRITE       50
#ifdef UNUSED
#define DDC_TIMEOUT_MILLIS_POST_CAPABILITIES_READ  50
#endif
// not part of spec
#define DDC_TIMEOUT_MILLIS_RETRY                  200
#define DDC_TIMEOUT_USE_DEFAULT                    -1
#define DDC_TIMEOUT_NONE                            0


// TODO: move comments re smbus problems to low level smbus functions (currently in i2c_base_io.c)

// DEFAULT_I2C_WRITE_MODE, DEFAULT_I2C_READ_MODE currently used only within tests

// Default settings in i2c_io.c
// valid write modes: "write", "ioctl_write", "i2c_smbus_write_i2c_block_data"
// valid read modes:  "read",  "ioctl_read",  "i2c_smbus_read_i2c_block_data"
// 11/2015: write modes "write" and "ioctl_write" both work
//          "i2c_smbus_write_i2c_block_data" returns ERRNO EINVAL, invalid argument
//          "read" and "ioctl_read" both work, appear comparable
//          fails: "i2c_smb_read_i2c_block_data"
#define DEFAULT_I2C_WRITE_MODE "write"
// #define DEFAULT_I2C_WRITE_MODE "ioctl_write"
//#define DEFAULT_I2C_WRITE_MODE  "i2c_smbus_write_i2c_block_data"
#define DEFAULT_I2C_READ_MODE  "read"
// #define DEFAULT_I2C_READ_MODE  "ioctl_read"
// i2c_smbus_read_i2c_block_data can't handle capabilities fragments 32 bytes in size, since with
// "envelope" the packet exceeds the i2c_smbus_read_i2c_block_data 32 byte limit

// New way:
#define DEFAULT_I2C_IO_STRATEGY  I2C_IO_STRATEGY_FILEIO
//#define DEFAULT_I2C_IO_STRATEGY  I2C_IO_STRATEGY_IOCTL




// Affects memory allocation in try_stats:
#define MAX_MAX_TRIES         15

// All MAX_..._TRIES values must be <= MAX_MAX_TRIES
#define MAX_WRITE_ONLY_EXCHANGE_TRIES     4
#define MAX_WRITE_READ_EXCHANGE_TRIES    10
#define MAX_MULTI_EXCHANGE_TRIES          8

// Maximum command arguments
// #define MAX_ARGS (MAX_SETVCP_VALUES*2)   // causes CMDID_* undefined
#define MAX_ARGS 100        // hack

//
// Miscellaneous

// Maximum numbers of values on command ddc setvcp
#define MAX_SETVCP_VALUES    50



#endif /* PARMS_H_ */
