/*
 * ddc_command_codes.h
 *
 *  Created on: Nov 17, 2015
 *      Author: rock
 */

#ifndef DDC_COMMAND_CODES_H_
#define DDC_COMMAND_CODES_H_


#include <util/string_util.h>

#include <base/ddc_packets.h>
#include <base/util.h>

//
// MCCS Command and Response Codes
//

// Used in 2 ways: to identify commands, and as identifiers
// in command request and response packets

#define CMD_VCP_REQUEST          0x01
#define CMD_VCP_RESPONSE         0x02
#define CMD_VCP_SET              0x03
#define CMD_TIMING_REPLY         0x06
#define CMD_TIMING_REQUEST       0x07
#define CMD_VCP_RESET            0x09
#define CMD_SAVE_SETTINGS        0x0c
#define CMD_SELF_TEST_REPLY      0xa1
#define CMD_SELF_TEST_REQUEST    0xb1
#define CMD_ID_REPLY             0xe1
#define CMD_TABLE_READ_REQUST    0xe2
#define CMD_CAPABILITIES_REPLY   0xe3
#define CMD_TABLE_READ_REPLY     0xe4
#define CMD_TABLE_WRITE          0xe7
#define CMD_ID_REQUEST           0xf1
#define CMD_CAPABILITIES_REQUEST 0xf3
#define CMD_ENABLE_APP_REPORT    0xf5

typedef
struct {
   Byte    cmd_code;
   char *  name;
} Cmd_Code_Table_Entry;

void list_cmd_codes();

Cmd_Code_Table_Entry * find_cmd_entry_by_hexid(Byte id);

Cmd_Code_Table_Entry * find_cmd_entry_by_charid(char * id);

char * get_command_name(Byte command_id);

extern int cmd_code_count;    // number of entries in VCP code table

Cmd_Code_Table_Entry * get_cmd_code_table_entry(int ndx);


#endif /* DDC_COMMAND_CODES_H_ */
