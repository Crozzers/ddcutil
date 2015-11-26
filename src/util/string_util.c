/*
 * stringutil.c
 *
 *  Created on: Nov 4, 2015
 *      Author: rock
 *
 *  Basic utility functions for basic data types,
 *  particularly strings and hex values.
 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/string_util.h"


//
// General
//

// Returns a boolean value as a string "true" or "false"
char * bool_repr(int value) {
   char * answer = (value) ? "true" : "false";
   return answer;
}


//
// String functions (other than hex)
//

// Compares 2 strings for equality, handling nulls
//
// Returns true if the strings match, false if not
bool streq(const char * s1, const char * s2) {
   bool result = false;
   if ( (s1 == NULL && s2 == NULL) ||
        (s1 != NULL && s2 != NULL && (strcmp(s1, s2) == 0) )
      )
      result = true;
   return result;
}


/* Tests if one string is a valid abbreviation of another.
 *
 * Arguments:
 *   value     is this string an abbreviation?
 *   longname  unabbreviated value
 *   minchars  minimum number of characters that must match
 *
 * Returns:
 *   true/false
 */
bool is_abbrev(char * value, const char * longname, int minchars) {
   bool result = false;
   int vlen = strlen(value);
   if ( vlen >= minchars &&
        vlen <= strlen(longname) &&
        memcmp(value, longname, vlen) == 0
      )
      result = true;

   // printf("(%s) value=|%s|, longname=|%s| returning %d\n", __func__, value, longname, result);
   return result;
}


/* Joins an array of strings into a single string, using a separator string.
 *
 * Arguments:
 *   pieces   array of strings
 *   ct       number of strings
 *   sepstr   separator string
 *
 * Returns:
 *   joined string (null terminated)
 *
 * The returned string has been malloc'd.  It is the responsibility of
 * the caller to free it.
 */
char * strjoin(const char ** pieces, const int ct, const char * sepstr) {
   // printf("(%s) ct=%d\n", __func__, ct);
   int total_length = 0;
   int ndx;
   int seplen = (sepstr) ? strlen(sepstr) : 0;  // sepstr may be null
   for (ndx=0; ndx<ct; ndx++)
      total_length += strlen(pieces[ndx]);
   total_length += (ct-1) * seplen + 1;
   // printf("(%s) total_length=%d\n", __func__, total_length);
   char * result = malloc(total_length);
   char * end = result;
   for (ndx=0; ndx<ct; ndx++) {
      if (ndx > 0 && seplen > 0) {
         strcpy(end, sepstr);
         end += strlen(sepstr);
      }
      strcpy(end, pieces[ndx]);
      end += strlen(pieces[ndx]);
   }
   // printf("(%s) result=%p, end=%p\n", __func__, result, end);
   assert(end == result + total_length -1);
   return result;
}

#ifdef FUTURE
// YAGNI: String_Array

typedef struct {
   int  max_ct;
   int  cur_ct;
   char** s;
} String_Array;


String_Array* new_string_array(int size) {
   String_Array * result = calloc(1, sizeof(String_Array));
   result->max_ct = size;
   result->cur_ct = 0;
   result->s = calloc(sizeof(char*), size);
   return result;
}
#endif


Null_Terminated_String_Array strsplit(char * str_to_split, char sepchar) {
   int max_pieces = (strlen(str_to_split)+1)/2;
   char** workstruct = calloc(sizeof(char *), max_pieces);
   int piecect = 0;

   char * rest = str_to_split;
   char * token;
   char delim = sepchar;
   // originally token assignment was in while() clause, but valgrind
   // complaining about uninitialized variable, trying to figure out why
   token = strsep(&rest, &delim);
   while (token) {
      // printf("(%s) token: |%s|\n", __func__, token);
      workstruct[piecect++] = strdup(token);
      token = strsep(&rest, &delim);
   }
   char ** result = calloc(sizeof(char *), piecect+1);
   // n. workstruct[piecect] == NULL because we used calloc()
   memcpy(result, workstruct, 4*(piecect+1));
   free(workstruct);
   return result;
}


void null_terminated_string_array_free(Null_Terminated_String_Array string_array) {
   int ndx = 0;
   while (string_array[ndx] != NULL)
      free(string_array[ndx++]);
   free(string_array);
}

int null_terminated_string_array_length(Null_Terminated_String_Array string_array) {
   int ndx = 0;
   while (string_array[ndx] != NULL) {ndx++;}
   return ndx;
}







/* Converts string to upper case.  The original string is converted in place.
 *
 * Arguments:
 *   s   string to force to upper case
 *
 * Returns:
 *   s   converted string
 */
char * strupper(char * s) {
   if (s) {     // check s not a null pointer
      char * p = s;
      while(*p) {
         *p = toupper(*p);
         p++;
      }
   }
   return s;
}


/* Converts a sequence of characters into a (null-terminated) string.
 *
 * Arguments:
 *    start   pointer to first character
 *    len     number of characters
 *
 * Returns:
 *    newly allocated string
 *    NULL if start was NULL (is this the most useful behavior?)
 */
char * chars_to_string(char * start, int len) {
   assert(len >= 0);
   char * strbuf = NULL;
   if (start) {
      strbuf = malloc(len+1);
      memcpy(strbuf, start, len);
      *(strbuf + len) = '\0';
   }
   return strbuf;
}



//
// Hex value conversion.
//


/* Converts a (null terminated) string of 2 hex characters to
 * its byte value.
 *
 * Arguments:
 *   s       pointer to hex string
 *   result  pointer to byte in which result will be returned
 *
 * Returns:
 *   true if successful conversion, false if string does not
 *   consist of hex characters, or is not 2 characters in length.
 *
 */
bool hhs_to_byte_in_buf(char * s, Byte * result) {
   // printf("(%s) Starting s=%s, strlen(s)=%d   \n", __func__, s, strlen(s) );
   // consider changing to fail if len != 2, or perhaps len != 1,2
   //assert(strlen(s) == 2);

   bool ok = true;
   if (strlen(s) != 2)
      ok = false;
   else {
   char * endptr = NULL;
   errno = 0;
   long longtemp = strtol(s, &endptr, 16 );
   int errsv = errno;
   // printf("(%s) After strtol, longtemp=%ld  \n", __func__, longtemp );
   // printf("errno=%d, s=|%s|, s=0x%02x &s=%p, longtemp = %ld, endptr=%p, *endptr=0x%02x\n",
   //        errsv, s, s, &s, longtemp, endptr,*endptr);
   // if (*endptr != '\0' || errsv != 0) {
   if (endptr != s+2 || errsv != 0) {
      ok = false;
   }
   else
      *result = (Byte) longtemp;
   }

   return ok;
}


/* Converts 2 hex characters to their corresponding byte value.
 * The characters need not be null terminated.
 *
 * Arguments:
 *   s       pointer to hex characters.
 *   result  pointer go byte in which result will be returned
 *
 * Returns:
 *   true if successful conversion, false if string does not
 *   consist of hex characters.
 */
bool hhc_to_byte_in_buf(char * hh, Byte * converted) {
   // printf("(%s) Starting hh=%.2s   \n", __func__, hh );
   char hhs[3];
   hhs[0] = hh[0];
   // hhs[1] = cc[1];   // why does compiler complain?
   hhs[1] = *(hh+1);
   hhs[2] = '\0';
   return  hhs_to_byte_in_buf(hhs, converted);
}


/* Converts a (null terminated) string of 2 hex characters to
 * its byte value.
 *
 * Arguments:
 *   s   pointer to hex string
 *
 * Returns:
 *   byte value
 *
 * Execution terminates if invalid hex value.
 */
Byte hhs_to_byte(char * s) {
   // printf("(%s) Starting s=%s, strlen(s)=%d   \n", __func__, s, strlen(s) );
   Byte converted;
   if (!hhs_to_byte_in_buf(s, &converted)) {
      // no way to properly signal failure, so terminate execution
      // don't call a function such as program_logic_error() since this
      // file should have no dependencies on any other program files.
      fprintf(stderr, "Invalid hex value: %s", s);
      exit(1);
   }
   return converted;
}


/* Converts 2 hex characters to a single byte.
 *
 * Arguments:
 *   hh    address of 2 hex characters, need not be null terminated
 *
 * Returns:
 *   byte value
 *
 * Execution terminates if invalid hex value.
 */
Byte hhc_to_byte(char * hh) {
   // printf("(%s) Starting hh=%.2s   \n", __func__, hh );
   char hhs[3];
   hhs[0] = hh[0];
   // hhs[1] = cc[1];   // why does compiler complain?
   hhs[1] = *(hh+1);
   hhs[2] = '\0';
   return hhs_to_byte(hhs);
}


/* Converts a string of hex characters (null terminated) to an array of bytes.
 *
 * Arguments:
 *    hhs     string of hex characters
 *    pBa     address at which to return pointer to byte array
 *
 * Returns:
 *    number of bytes in array
 *    -1 if string could not be converted
 *
 * If successful, the byte array whose address is returned in pBa has
 * been malloc'd.  It is the responsibility of the caller to free it.
 */
int hhs_to_byte_array(char * hhs, Byte** pBa) {
   if ( strlen(hhs) % 2)     // if odd number of characters
      return -1;
   char xlate[] = "0123456789ABCDEF";
   int bytect = strlen(hhs)/2;
   Byte * ba = malloc(bytect);
   bool ok = true;

   char * h = hhs;
   Byte * b = ba;
   for (;  *h && ok; b++) {
      char ch0 = toupper(*h++);
      char ch1 = toupper(*h++);
      char * pos0 = strchr(xlate, ch0);
      char * pos1 = strchr(xlate, ch1);
      if (pos0 && pos1) {
         *b = (pos0-xlate) * 16 + (pos1-xlate);
      }
      else {
         ok = false;
      }
   }

   if (!ok) {
      free(ba);
      bytect = -1;
   }
   else {
      *pBa = ba;
   }
   return bytect;
}


void test_one_hhs2Byte(char * hhs) {
   printf("(%s) Starting.  hhs=%s  \n", __func__, hhs );
   Byte b1 = hhs_to_byte(hhs);
   printf("(%s) %s -> 0x%02x  \n", __func__, hhs, b1 );
}


void test_hhs_to_byte() {
   printf("(%s) Startomg  \n", __func__ );
   test_one_hhs2Byte("01");
   test_one_hhs2Byte("ZZ");
   // test_one_hhs2Byte("123");
}


/* Converts a sequence of bytes to its representation as a string of hex characters.
 *
 * Arguments:
 *    bytes     pointer to bytes
 *    len       number of bytes
 *
 * Returns:
 *   pointer to hex string
 *
 * The value returned by this funtion has been malloc'd.   It is the
 * responsiblity of the caller to free the memory.
 */
char * hexstring(unsigned char * bytes, int len) {
   int alloc_size = 3*len + 1;
   char* str_buf = malloc(alloc_size);

   int i;
   for (i = 0; i < len; i++) {
      snprintf(str_buf+3*i, alloc_size-3*i, "%02x ", bytes[i]);
   }
   str_buf[3*len-1] = 0x00;
   return str_buf;
}


/* Converts a sequence of bytes to its representation as a string of hex characters.
 *
 * Arguments:
 *    bytes    pointer to bytes
 *    len      number of bytes
 *    sep      string to separate each 2 hex character pairs representing a byte,
 *             if NULL then no separators will be inserted
 *    uppercase if true, use uppercase hex characters,
 *              if false, use lower case hex characters
 *    buffer    pointer to buffer in which hex string will be returned,
 *              if NULL, then a buffer will be allocated
 *    bufsz     size of buffer
 *              if 0, then a buffer will be allocated
 *
 * Returns:
 *   pointer to hex string
 *
 * If this function allocates a buffer, it is the responsiblity of the caller
 * to free the memory.
 */
char * hexstring2(const unsigned char * bytes, int len, const char * sep, bool uppercase, char * buffer, int bufsz) {
   int sepsize = 0;
   if (sep) {
      sepsize = strlen(sep);
   }
   int required_size =   2*len             // hex rep of bytes
                       + (len-1)*sepsize   // for separators
                       + 1;                // terminating null
   if (!buffer)
      bufsz = 0;
   assert (bufsz == 0 || bufsz >= required_size);
   if (bufsz == 0) {
      buffer = malloc(required_size);
   }

   char * pattern = (uppercase) ? "%02X" : "%0sx";

   int incr1 = 2 + sepsize;
   int i;
   for (i=0; i < len; i++) {
      sprintf(buffer+i*incr1, pattern, bytes[i]);
      if (i < (len-1) && sep)
         strcat(buffer, sep);
   }
   // printf("(%s) strlen(buffer) = %ld, required_size=%d   \n", __func__, strlen(buffer), required_size );
   // printf("(%s)  buffer=|%s|\n", __func__, buffer );
   assert(strlen(buffer) == required_size-1);

   return buffer;
}


/* Dump a region of memory as hex characters and their ASCII values.
 *
 * Arguments:
 *    data     start of region to show
 *    size     length of region
 *
 * Returns:
 *    nothing
 */
void hex_dump(unsigned char *data, int size)
{
   int i; // index in data...
   int j; // index in line...
   char temp[8];
   char buffer[128];
   char *ascii;

   memset(buffer, 0, 128);

   // printf("\n");
   // Printing the ruler...
   printf("        +0          +4          +8          +c            0   4   8   c   \n");
   ascii = buffer + 58;
   memset(buffer, ' ', 58 + 16);
   buffer[58 + 16] = '\n';
   buffer[58 + 17] = '\0';
   buffer[0] = '+';
   buffer[1] = '0';
   buffer[2] = '0';
   buffer[3] = '0';
   buffer[4] = '0';
   for (i = 0, j = 0; i < size; i++, j++) {
      if (j == 16) {
         printf("%s", buffer);
         memset(buffer, ' ', 58 + 16);
         sprintf(temp, "+%04x", i);
         memcpy(buffer, temp, 5);
         j = 0;
      }

      sprintf(temp, "%02x", 0xff & data[i]);
      memcpy(buffer + 8 + (j * 3), temp, 2);
      if ((data[i] > 31) && (data[i] < 127))
         ascii[j] = data[i];
      else
         ascii[j] = '.';
   }

   if (j != 0)
      printf("%s", buffer);
}

