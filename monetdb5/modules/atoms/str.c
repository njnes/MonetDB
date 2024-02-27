/*
 * SPDX-License-Identifier: MPL-2.0
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0.  If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright 2024 MonetDB Foundation;
 * Copyright August 2008 - 2023 MonetDB B.V.;
 * Copyright 1997 - July 2008 CWI.
 */

/*
 *  N.J. Nes, M.L. Kersten
 * The String Module
 * Strings can be created in many ways. Already in the built-in
 * operations each atom can be cast to a string using the str(atom)
 * mil command.  The string module gives the possibility of
 * construction string as a substring of the a given string (s). There
 * are two such construction functions.  The first is the substring
 * from some position (offset) until the end of the string. The second
 * start again on the given offset position but only copies count
 * number of bytes. The functions fail when the position and count
 * fall out of bounds. A negative position indicates that the position
 * is computed from the end of the source string.
 *
 * The strings can be compared using the "=" and "!=" operators.
 *
 * The operator "+" concatenates a string and an atom. The atom will
 * be converted to a string using the atom to string c function. The
 * string and the result of the conversion are concatenated to form a
 * new string. This string is returned.
 *
 * The length function returns the length of the string. The length is
 * the number of characters in the string. The maximum string length
 * handled by the kernel is 32-bits long.
 *
 * chrAt() returns the character at position index in the string
 * s. The function will fail when the index is out of range. The range
 * is from 0 to length(s)-1.
 *
 * The startsWith and endsWith functions test if the string s starts
 * with or ends with the given prefix or suffix.
 *
 * The toLower and toUpper functions cast the string to lower or upper
 * case characters.
 *
 * The search(str,chr) function searches for the first occurrence of a
 * character from the begining of the string. The search(chr,str)
 * searches for the last occurrence (or first from the end of the
 * string). The last search function locates the position of first
 * occurrence of the string s2 in string s. All search functions
 * return -1 when the search failed.  Otherwise the position is
 * returned.
 *
 * All string functions fail when an incorrect string (NULL pointer)
 * is given.  In the current implementation, a fail is signaled by
 * returning nil, since this facilitates the use of the string module
 * in bulk operations.
 *
 * All functions in the module have now been converted to
 * Unicode. Internally, we use UTF-8 to store strings as Unicode in
 * zero-terminated byte-sequences.
 */
#include "monetdb_config.h"
#include "str.h"
#include <string.h>
#ifdef HAVE_ICONV
#include <iconv.h>
#include <locale.h>
#endif
#include "mal_interpreter.h"

#include "utf8.h"

/*
 * UTF-8 Handling
 * UTF-8 is a way to store Unicode strings in zero-terminated byte
 * sequences, which you can e.g. strcmp() with old 8-bit Latin-1
 * strcmp() functions and which then gives the same results as doing
 * the strcmp() on equivalent Latin-1 and ASCII character strings
 * stored in simple one-byte sequences.  These characteristics make
 * UTF-8 an attractive format for upgrading an ASCII-oriented computer
 * program towards one that supports Unicode. That is why we use UTF-8
 * in MonetDB.
 *
 * For MonetDB, UTF-8 mostly has no consequences, as strings stored in
 * BATs are regarded as data, and it does not matter for the database
 * kernel whether the zero-terminated byte sequence it is processing
 * has UTF-8 or Latin-1 semantics. This module is the only place where
 * explicit string functionality is located. We {\bf do} have to adapt
 * the behavior of the length(), search(), substring() and the
 * like commands to the fact that one (Unicode) character is now
 * stored in a variable number of bytes (possibly > 1).
 *
 * One of the things that become more complex in Unicode are
 * uppercase/lowercase conversions. The below tables are the simple
 * one-to-one Unicode case mappings. We do not support the special
 * casing mappings (e.g. from one to two letters).
 *
 * References:
 * simple casing:	http://www.unicode.org/Public/UNIDATA/UnicodeData.txt
 * complex casing: http://www.unicode.org/Public/UNIDATA/SpecialCasing.txt
 *
 * The Unicode case conversion implementation in MonetDB fills a
 * mapping BAT of int,int combinations, in which we perform
 * high-performance hash-lookup (all code inlined).
 */

/* These tables were generated from the Unicode 13.0.0 spec. */
const struct UTF8_lower_upper {
	const unsigned int from, to;
} UTF8_toUpper[] = {			/* code points with non-null uppercase conversion */
	{0x0061, 0x0041,},
	{0x0062, 0x0042,},
	{0x0063, 0x0043,},
	{0x0064, 0x0044,},
	{0x0065, 0x0045,},
	{0x0066, 0x0046,},
	{0x0067, 0x0047,},
	{0x0068, 0x0048,},
	{0x0069, 0x0049,},
	{0x006A, 0x004A,},
	{0x006B, 0x004B,},
	{0x006C, 0x004C,},
	{0x006D, 0x004D,},
	{0x006E, 0x004E,},
	{0x006F, 0x004F,},
	{0x0070, 0x0050,},
	{0x0071, 0x0051,},
	{0x0072, 0x0052,},
	{0x0073, 0x0053,},
	{0x0074, 0x0054,},
	{0x0075, 0x0055,},
	{0x0076, 0x0056,},
	{0x0077, 0x0057,},
	{0x0078, 0x0058,},
	{0x0079, 0x0059,},
	{0x007A, 0x005A,},
	{0x00B5, 0x039C,},
	{0x00E0, 0x00C0,},
	{0x00E1, 0x00C1,},
	{0x00E2, 0x00C2,},
	{0x00E3, 0x00C3,},
	{0x00E4, 0x00C4,},
	{0x00E5, 0x00C5,},
	{0x00E6, 0x00C6,},
	{0x00E7, 0x00C7,},
	{0x00E8, 0x00C8,},
	{0x00E9, 0x00C9,},
	{0x00EA, 0x00CA,},
	{0x00EB, 0x00CB,},
	{0x00EC, 0x00CC,},
	{0x00ED, 0x00CD,},
	{0x00EE, 0x00CE,},
	{0x00EF, 0x00CF,},
	{0x00F0, 0x00D0,},
	{0x00F1, 0x00D1,},
	{0x00F2, 0x00D2,},
	{0x00F3, 0x00D3,},
	{0x00F4, 0x00D4,},
	{0x00F5, 0x00D5,},
	{0x00F6, 0x00D6,},
	{0x00F8, 0x00D8,},
	{0x00F9, 0x00D9,},
	{0x00FA, 0x00DA,},
	{0x00FB, 0x00DB,},
	{0x00FC, 0x00DC,},
	{0x00FD, 0x00DD,},
	{0x00FE, 0x00DE,},
	{0x00FF, 0x0178,},
	{0x0101, 0x0100,},
	{0x0103, 0x0102,},
	{0x0105, 0x0104,},
	{0x0107, 0x0106,},
	{0x0109, 0x0108,},
	{0x010B, 0x010A,},
	{0x010D, 0x010C,},
	{0x010F, 0x010E,},
	{0x0111, 0x0110,},
	{0x0113, 0x0112,},
	{0x0115, 0x0114,},
	{0x0117, 0x0116,},
	{0x0119, 0x0118,},
	{0x011B, 0x011A,},
	{0x011D, 0x011C,},
	{0x011F, 0x011E,},
	{0x0121, 0x0120,},
	{0x0123, 0x0122,},
	{0x0125, 0x0124,},
	{0x0127, 0x0126,},
	{0x0129, 0x0128,},
	{0x012B, 0x012A,},
	{0x012D, 0x012C,},
	{0x012F, 0x012E,},
	{0x0131, 0x0049,},
	{0x0133, 0x0132,},
	{0x0135, 0x0134,},
	{0x0137, 0x0136,},
	{0x013A, 0x0139,},
	{0x013C, 0x013B,},
	{0x013E, 0x013D,},
	{0x0140, 0x013F,},
	{0x0142, 0x0141,},
	{0x0144, 0x0143,},
	{0x0146, 0x0145,},
	{0x0148, 0x0147,},
	{0x014B, 0x014A,},
	{0x014D, 0x014C,},
	{0x014F, 0x014E,},
	{0x0151, 0x0150,},
	{0x0153, 0x0152,},
	{0x0155, 0x0154,},
	{0x0157, 0x0156,},
	{0x0159, 0x0158,},
	{0x015B, 0x015A,},
	{0x015D, 0x015C,},
	{0x015F, 0x015E,},
	{0x0161, 0x0160,},
	{0x0163, 0x0162,},
	{0x0165, 0x0164,},
	{0x0167, 0x0166,},
	{0x0169, 0x0168,},
	{0x016B, 0x016A,},
	{0x016D, 0x016C,},
	{0x016F, 0x016E,},
	{0x0171, 0x0170,},
	{0x0173, 0x0172,},
	{0x0175, 0x0174,},
	{0x0177, 0x0176,},
	{0x017A, 0x0179,},
	{0x017C, 0x017B,},
	{0x017E, 0x017D,},
	{0x017F, 0x0053,},
	{0x0180, 0x0243,},
	{0x0183, 0x0182,},
	{0x0185, 0x0184,},
	{0x0188, 0x0187,},
	{0x018C, 0x018B,},
	{0x0192, 0x0191,},
	{0x0195, 0x01F6,},
	{0x0199, 0x0198,},
	{0x019A, 0x023D,},
	{0x019E, 0x0220,},
	{0x01A1, 0x01A0,},
	{0x01A3, 0x01A2,},
	{0x01A5, 0x01A4,},
	{0x01A8, 0x01A7,},
	{0x01AD, 0x01AC,},
	{0x01B0, 0x01AF,},
	{0x01B4, 0x01B3,},
	{0x01B6, 0x01B5,},
	{0x01B9, 0x01B8,},
	{0x01BD, 0x01BC,},
	{0x01BF, 0x01F7,},
	{0x01C5, 0x01C4,},
	{0x01C6, 0x01C4,},
	{0x01C8, 0x01C7,},
	{0x01C9, 0x01C7,},
	{0x01CB, 0x01CA,},
	{0x01CC, 0x01CA,},
	{0x01CE, 0x01CD,},
	{0x01D0, 0x01CF,},
	{0x01D2, 0x01D1,},
	{0x01D4, 0x01D3,},
	{0x01D6, 0x01D5,},
	{0x01D8, 0x01D7,},
	{0x01DA, 0x01D9,},
	{0x01DC, 0x01DB,},
	{0x01DD, 0x018E,},
	{0x01DF, 0x01DE,},
	{0x01E1, 0x01E0,},
	{0x01E3, 0x01E2,},
	{0x01E5, 0x01E4,},
	{0x01E7, 0x01E6,},
	{0x01E9, 0x01E8,},
	{0x01EB, 0x01EA,},
	{0x01ED, 0x01EC,},
	{0x01EF, 0x01EE,},
	{0x01F2, 0x01F1,},
	{0x01F3, 0x01F1,},
	{0x01F5, 0x01F4,},
	{0x01F9, 0x01F8,},
	{0x01FB, 0x01FA,},
	{0x01FD, 0x01FC,},
	{0x01FF, 0x01FE,},
	{0x0201, 0x0200,},
	{0x0203, 0x0202,},
	{0x0205, 0x0204,},
	{0x0207, 0x0206,},
	{0x0209, 0x0208,},
	{0x020B, 0x020A,},
	{0x020D, 0x020C,},
	{0x020F, 0x020E,},
	{0x0211, 0x0210,},
	{0x0213, 0x0212,},
	{0x0215, 0x0214,},
	{0x0217, 0x0216,},
	{0x0219, 0x0218,},
	{0x021B, 0x021A,},
	{0x021D, 0x021C,},
	{0x021F, 0x021E,},
	{0x0223, 0x0222,},
	{0x0225, 0x0224,},
	{0x0227, 0x0226,},
	{0x0229, 0x0228,},
	{0x022B, 0x022A,},
	{0x022D, 0x022C,},
	{0x022F, 0x022E,},
	{0x0231, 0x0230,},
	{0x0233, 0x0232,},
	{0x023C, 0x023B,},
	{0x023F, 0x2C7E,},
	{0x0240, 0x2C7F,},
	{0x0242, 0x0241,},
	{0x0247, 0x0246,},
	{0x0249, 0x0248,},
	{0x024B, 0x024A,},
	{0x024D, 0x024C,},
	{0x024F, 0x024E,},
	{0x0250, 0x2C6F,},
	{0x0251, 0x2C6D,},
	{0x0252, 0x2C70,},
	{0x0253, 0x0181,},
	{0x0254, 0x0186,},
	{0x0256, 0x0189,},
	{0x0257, 0x018A,},
	{0x0259, 0x018F,},
	{0x025B, 0x0190,},
	{0x025C, 0xA7AB,},
	{0x0260, 0x0193,},
	{0x0261, 0xA7AC,},
	{0x0263, 0x0194,},
	{0x0265, 0xA78D,},
	{0x0266, 0xA7AA,},
	{0x0268, 0x0197,},
	{0x0269, 0x0196,},
	{0x026A, 0xA7AE,},
	{0x026B, 0x2C62,},
	{0x026C, 0xA7AD,},
	{0x026F, 0x019C,},
	{0x0271, 0x2C6E,},
	{0x0272, 0x019D,},
	{0x0275, 0x019F,},
	{0x027D, 0x2C64,},
	{0x0280, 0x01A6,},
	{0x0282, 0xA7C5,},
	{0x0283, 0x01A9,},
	{0x0287, 0xA7B1,},
	{0x0288, 0x01AE,},
	{0x0289, 0x0244,},
	{0x028A, 0x01B1,},
	{0x028B, 0x01B2,},
	{0x028C, 0x0245,},
	{0x0292, 0x01B7,},
	{0x029D, 0xA7B2,},
	{0x029E, 0xA7B0,},
	{0x0345, 0x0399,},
	{0x0371, 0x0370,},
	{0x0373, 0x0372,},
	{0x0377, 0x0376,},
	{0x037B, 0x03FD,},
	{0x037C, 0x03FE,},
	{0x037D, 0x03FF,},
	{0x03AC, 0x0386,},
	{0x03AD, 0x0388,},
	{0x03AE, 0x0389,},
	{0x03AF, 0x038A,},
	{0x03B1, 0x0391,},
	{0x03B2, 0x0392,},
	{0x03B3, 0x0393,},
	{0x03B4, 0x0394,},
	{0x03B5, 0x0395,},
	{0x03B6, 0x0396,},
	{0x03B7, 0x0397,},
	{0x03B8, 0x0398,},
	{0x03B9, 0x0399,},
	{0x03BA, 0x039A,},
	{0x03BB, 0x039B,},
	{0x03BC, 0x039C,},
	{0x03BD, 0x039D,},
	{0x03BE, 0x039E,},
	{0x03BF, 0x039F,},
	{0x03C0, 0x03A0,},
	{0x03C1, 0x03A1,},
	{0x03C2, 0x03A3,},
	{0x03C3, 0x03A3,},
	{0x03C4, 0x03A4,},
	{0x03C5, 0x03A5,},
	{0x03C6, 0x03A6,},
	{0x03C7, 0x03A7,},
	{0x03C8, 0x03A8,},
	{0x03C9, 0x03A9,},
	{0x03CA, 0x03AA,},
	{0x03CB, 0x03AB,},
	{0x03CC, 0x038C,},
	{0x03CD, 0x038E,},
	{0x03CE, 0x038F,},
	{0x03D0, 0x0392,},
	{0x03D1, 0x0398,},
	{0x03D5, 0x03A6,},
	{0x03D6, 0x03A0,},
	{0x03D7, 0x03CF,},
	{0x03D9, 0x03D8,},
	{0x03DB, 0x03DA,},
	{0x03DD, 0x03DC,},
	{0x03DF, 0x03DE,},
	{0x03E1, 0x03E0,},
	{0x03E3, 0x03E2,},
	{0x03E5, 0x03E4,},
	{0x03E7, 0x03E6,},
	{0x03E9, 0x03E8,},
	{0x03EB, 0x03EA,},
	{0x03ED, 0x03EC,},
	{0x03EF, 0x03EE,},
	{0x03F0, 0x039A,},
	{0x03F1, 0x03A1,},
	{0x03F2, 0x03F9,},
	{0x03F3, 0x037F,},
	{0x03F5, 0x0395,},
	{0x03F8, 0x03F7,},
	{0x03FB, 0x03FA,},
	{0x0430, 0x0410,},
	{0x0431, 0x0411,},
	{0x0432, 0x0412,},
	{0x0433, 0x0413,},
	{0x0434, 0x0414,},
	{0x0435, 0x0415,},
	{0x0436, 0x0416,},
	{0x0437, 0x0417,},
	{0x0438, 0x0418,},
	{0x0439, 0x0419,},
	{0x043A, 0x041A,},
	{0x043B, 0x041B,},
	{0x043C, 0x041C,},
	{0x043D, 0x041D,},
	{0x043E, 0x041E,},
	{0x043F, 0x041F,},
	{0x0440, 0x0420,},
	{0x0441, 0x0421,},
	{0x0442, 0x0422,},
	{0x0443, 0x0423,},
	{0x0444, 0x0424,},
	{0x0445, 0x0425,},
	{0x0446, 0x0426,},
	{0x0447, 0x0427,},
	{0x0448, 0x0428,},
	{0x0449, 0x0429,},
	{0x044A, 0x042A,},
	{0x044B, 0x042B,},
	{0x044C, 0x042C,},
	{0x044D, 0x042D,},
	{0x044E, 0x042E,},
	{0x044F, 0x042F,},
	{0x0450, 0x0400,},
	{0x0451, 0x0401,},
	{0x0452, 0x0402,},
	{0x0453, 0x0403,},
	{0x0454, 0x0404,},
	{0x0455, 0x0405,},
	{0x0456, 0x0406,},
	{0x0457, 0x0407,},
	{0x0458, 0x0408,},
	{0x0459, 0x0409,},
	{0x045A, 0x040A,},
	{0x045B, 0x040B,},
	{0x045C, 0x040C,},
	{0x045D, 0x040D,},
	{0x045E, 0x040E,},
	{0x045F, 0x040F,},
	{0x0461, 0x0460,},
	{0x0463, 0x0462,},
	{0x0465, 0x0464,},
	{0x0467, 0x0466,},
	{0x0469, 0x0468,},
	{0x046B, 0x046A,},
	{0x046D, 0x046C,},
	{0x046F, 0x046E,},
	{0x0471, 0x0470,},
	{0x0473, 0x0472,},
	{0x0475, 0x0474,},
	{0x0477, 0x0476,},
	{0x0479, 0x0478,},
	{0x047B, 0x047A,},
	{0x047D, 0x047C,},
	{0x047F, 0x047E,},
	{0x0481, 0x0480,},
	{0x048B, 0x048A,},
	{0x048D, 0x048C,},
	{0x048F, 0x048E,},
	{0x0491, 0x0490,},
	{0x0493, 0x0492,},
	{0x0495, 0x0494,},
	{0x0497, 0x0496,},
	{0x0499, 0x0498,},
	{0x049B, 0x049A,},
	{0x049D, 0x049C,},
	{0x049F, 0x049E,},
	{0x04A1, 0x04A0,},
	{0x04A3, 0x04A2,},
	{0x04A5, 0x04A4,},
	{0x04A7, 0x04A6,},
	{0x04A9, 0x04A8,},
	{0x04AB, 0x04AA,},
	{0x04AD, 0x04AC,},
	{0x04AF, 0x04AE,},
	{0x04B1, 0x04B0,},
	{0x04B3, 0x04B2,},
	{0x04B5, 0x04B4,},
	{0x04B7, 0x04B6,},
	{0x04B9, 0x04B8,},
	{0x04BB, 0x04BA,},
	{0x04BD, 0x04BC,},
	{0x04BF, 0x04BE,},
	{0x04C2, 0x04C1,},
	{0x04C4, 0x04C3,},
	{0x04C6, 0x04C5,},
	{0x04C8, 0x04C7,},
	{0x04CA, 0x04C9,},
	{0x04CC, 0x04CB,},
	{0x04CE, 0x04CD,},
	{0x04CF, 0x04C0,},
	{0x04D1, 0x04D0,},
	{0x04D3, 0x04D2,},
	{0x04D5, 0x04D4,},
	{0x04D7, 0x04D6,},
	{0x04D9, 0x04D8,},
	{0x04DB, 0x04DA,},
	{0x04DD, 0x04DC,},
	{0x04DF, 0x04DE,},
	{0x04E1, 0x04E0,},
	{0x04E3, 0x04E2,},
	{0x04E5, 0x04E4,},
	{0x04E7, 0x04E6,},
	{0x04E9, 0x04E8,},
	{0x04EB, 0x04EA,},
	{0x04ED, 0x04EC,},
	{0x04EF, 0x04EE,},
	{0x04F1, 0x04F0,},
	{0x04F3, 0x04F2,},
	{0x04F5, 0x04F4,},
	{0x04F7, 0x04F6,},
	{0x04F9, 0x04F8,},
	{0x04FB, 0x04FA,},
	{0x04FD, 0x04FC,},
	{0x04FF, 0x04FE,},
	{0x0501, 0x0500,},
	{0x0503, 0x0502,},
	{0x0505, 0x0504,},
	{0x0507, 0x0506,},
	{0x0509, 0x0508,},
	{0x050B, 0x050A,},
	{0x050D, 0x050C,},
	{0x050F, 0x050E,},
	{0x0511, 0x0510,},
	{0x0513, 0x0512,},
	{0x0515, 0x0514,},
	{0x0517, 0x0516,},
	{0x0519, 0x0518,},
	{0x051B, 0x051A,},
	{0x051D, 0x051C,},
	{0x051F, 0x051E,},
	{0x0521, 0x0520,},
	{0x0523, 0x0522,},
	{0x0525, 0x0524,},
	{0x0527, 0x0526,},
	{0x0529, 0x0528,},
	{0x052B, 0x052A,},
	{0x052D, 0x052C,},
	{0x052F, 0x052E,},
	{0x0561, 0x0531,},
	{0x0562, 0x0532,},
	{0x0563, 0x0533,},
	{0x0564, 0x0534,},
	{0x0565, 0x0535,},
	{0x0566, 0x0536,},
	{0x0567, 0x0537,},
	{0x0568, 0x0538,},
	{0x0569, 0x0539,},
	{0x056A, 0x053A,},
	{0x056B, 0x053B,},
	{0x056C, 0x053C,},
	{0x056D, 0x053D,},
	{0x056E, 0x053E,},
	{0x056F, 0x053F,},
	{0x0570, 0x0540,},
	{0x0571, 0x0541,},
	{0x0572, 0x0542,},
	{0x0573, 0x0543,},
	{0x0574, 0x0544,},
	{0x0575, 0x0545,},
	{0x0576, 0x0546,},
	{0x0577, 0x0547,},
	{0x0578, 0x0548,},
	{0x0579, 0x0549,},
	{0x057A, 0x054A,},
	{0x057B, 0x054B,},
	{0x057C, 0x054C,},
	{0x057D, 0x054D,},
	{0x057E, 0x054E,},
	{0x057F, 0x054F,},
	{0x0580, 0x0550,},
	{0x0581, 0x0551,},
	{0x0582, 0x0552,},
	{0x0583, 0x0553,},
	{0x0584, 0x0554,},
	{0x0585, 0x0555,},
	{0x0586, 0x0556,},
	{0x10D0, 0x1C90,},
	{0x10D1, 0x1C91,},
	{0x10D2, 0x1C92,},
	{0x10D3, 0x1C93,},
	{0x10D4, 0x1C94,},
	{0x10D5, 0x1C95,},
	{0x10D6, 0x1C96,},
	{0x10D7, 0x1C97,},
	{0x10D8, 0x1C98,},
	{0x10D9, 0x1C99,},
	{0x10DA, 0x1C9A,},
	{0x10DB, 0x1C9B,},
	{0x10DC, 0x1C9C,},
	{0x10DD, 0x1C9D,},
	{0x10DE, 0x1C9E,},
	{0x10DF, 0x1C9F,},
	{0x10E0, 0x1CA0,},
	{0x10E1, 0x1CA1,},
	{0x10E2, 0x1CA2,},
	{0x10E3, 0x1CA3,},
	{0x10E4, 0x1CA4,},
	{0x10E5, 0x1CA5,},
	{0x10E6, 0x1CA6,},
	{0x10E7, 0x1CA7,},
	{0x10E8, 0x1CA8,},
	{0x10E9, 0x1CA9,},
	{0x10EA, 0x1CAA,},
	{0x10EB, 0x1CAB,},
	{0x10EC, 0x1CAC,},
	{0x10ED, 0x1CAD,},
	{0x10EE, 0x1CAE,},
	{0x10EF, 0x1CAF,},
	{0x10F0, 0x1CB0,},
	{0x10F1, 0x1CB1,},
	{0x10F2, 0x1CB2,},
	{0x10F3, 0x1CB3,},
	{0x10F4, 0x1CB4,},
	{0x10F5, 0x1CB5,},
	{0x10F6, 0x1CB6,},
	{0x10F7, 0x1CB7,},
	{0x10F8, 0x1CB8,},
	{0x10F9, 0x1CB9,},
	{0x10FA, 0x1CBA,},
	{0x10FD, 0x1CBD,},
	{0x10FE, 0x1CBE,},
	{0x10FF, 0x1CBF,},
	{0x13F8, 0x13F0,},
	{0x13F9, 0x13F1,},
	{0x13FA, 0x13F2,},
	{0x13FB, 0x13F3,},
	{0x13FC, 0x13F4,},
	{0x13FD, 0x13F5,},
	{0x1C80, 0x0412,},
	{0x1C81, 0x0414,},
	{0x1C82, 0x041E,},
	{0x1C83, 0x0421,},
	{0x1C84, 0x0422,},
	{0x1C85, 0x0422,},
	{0x1C86, 0x042A,},
	{0x1C87, 0x0462,},
	{0x1C88, 0xA64A,},
	{0x1D79, 0xA77D,},
	{0x1D7D, 0x2C63,},
	{0x1D8E, 0xA7C6,},
	{0x1E01, 0x1E00,},
	{0x1E03, 0x1E02,},
	{0x1E05, 0x1E04,},
	{0x1E07, 0x1E06,},
	{0x1E09, 0x1E08,},
	{0x1E0B, 0x1E0A,},
	{0x1E0D, 0x1E0C,},
	{0x1E0F, 0x1E0E,},
	{0x1E11, 0x1E10,},
	{0x1E13, 0x1E12,},
	{0x1E15, 0x1E14,},
	{0x1E17, 0x1E16,},
	{0x1E19, 0x1E18,},
	{0x1E1B, 0x1E1A,},
	{0x1E1D, 0x1E1C,},
	{0x1E1F, 0x1E1E,},
	{0x1E21, 0x1E20,},
	{0x1E23, 0x1E22,},
	{0x1E25, 0x1E24,},
	{0x1E27, 0x1E26,},
	{0x1E29, 0x1E28,},
	{0x1E2B, 0x1E2A,},
	{0x1E2D, 0x1E2C,},
	{0x1E2F, 0x1E2E,},
	{0x1E31, 0x1E30,},
	{0x1E33, 0x1E32,},
	{0x1E35, 0x1E34,},
	{0x1E37, 0x1E36,},
	{0x1E39, 0x1E38,},
	{0x1E3B, 0x1E3A,},
	{0x1E3D, 0x1E3C,},
	{0x1E3F, 0x1E3E,},
	{0x1E41, 0x1E40,},
	{0x1E43, 0x1E42,},
	{0x1E45, 0x1E44,},
	{0x1E47, 0x1E46,},
	{0x1E49, 0x1E48,},
	{0x1E4B, 0x1E4A,},
	{0x1E4D, 0x1E4C,},
	{0x1E4F, 0x1E4E,},
	{0x1E51, 0x1E50,},
	{0x1E53, 0x1E52,},
	{0x1E55, 0x1E54,},
	{0x1E57, 0x1E56,},
	{0x1E59, 0x1E58,},
	{0x1E5B, 0x1E5A,},
	{0x1E5D, 0x1E5C,},
	{0x1E5F, 0x1E5E,},
	{0x1E61, 0x1E60,},
	{0x1E63, 0x1E62,},
	{0x1E65, 0x1E64,},
	{0x1E67, 0x1E66,},
	{0x1E69, 0x1E68,},
	{0x1E6B, 0x1E6A,},
	{0x1E6D, 0x1E6C,},
	{0x1E6F, 0x1E6E,},
	{0x1E71, 0x1E70,},
	{0x1E73, 0x1E72,},
	{0x1E75, 0x1E74,},
	{0x1E77, 0x1E76,},
	{0x1E79, 0x1E78,},
	{0x1E7B, 0x1E7A,},
	{0x1E7D, 0x1E7C,},
	{0x1E7F, 0x1E7E,},
	{0x1E81, 0x1E80,},
	{0x1E83, 0x1E82,},
	{0x1E85, 0x1E84,},
	{0x1E87, 0x1E86,},
	{0x1E89, 0x1E88,},
	{0x1E8B, 0x1E8A,},
	{0x1E8D, 0x1E8C,},
	{0x1E8F, 0x1E8E,},
	{0x1E91, 0x1E90,},
	{0x1E93, 0x1E92,},
	{0x1E95, 0x1E94,},
	{0x1E9B, 0x1E60,},
	{0x1EA1, 0x1EA0,},
	{0x1EA3, 0x1EA2,},
	{0x1EA5, 0x1EA4,},
	{0x1EA7, 0x1EA6,},
	{0x1EA9, 0x1EA8,},
	{0x1EAB, 0x1EAA,},
	{0x1EAD, 0x1EAC,},
	{0x1EAF, 0x1EAE,},
	{0x1EB1, 0x1EB0,},
	{0x1EB3, 0x1EB2,},
	{0x1EB5, 0x1EB4,},
	{0x1EB7, 0x1EB6,},
	{0x1EB9, 0x1EB8,},
	{0x1EBB, 0x1EBA,},
	{0x1EBD, 0x1EBC,},
	{0x1EBF, 0x1EBE,},
	{0x1EC1, 0x1EC0,},
	{0x1EC3, 0x1EC2,},
	{0x1EC5, 0x1EC4,},
	{0x1EC7, 0x1EC6,},
	{0x1EC9, 0x1EC8,},
	{0x1ECB, 0x1ECA,},
	{0x1ECD, 0x1ECC,},
	{0x1ECF, 0x1ECE,},
	{0x1ED1, 0x1ED0,},
	{0x1ED3, 0x1ED2,},
	{0x1ED5, 0x1ED4,},
	{0x1ED7, 0x1ED6,},
	{0x1ED9, 0x1ED8,},
	{0x1EDB, 0x1EDA,},
	{0x1EDD, 0x1EDC,},
	{0x1EDF, 0x1EDE,},
	{0x1EE1, 0x1EE0,},
	{0x1EE3, 0x1EE2,},
	{0x1EE5, 0x1EE4,},
	{0x1EE7, 0x1EE6,},
	{0x1EE9, 0x1EE8,},
	{0x1EEB, 0x1EEA,},
	{0x1EED, 0x1EEC,},
	{0x1EEF, 0x1EEE,},
	{0x1EF1, 0x1EF0,},
	{0x1EF3, 0x1EF2,},
	{0x1EF5, 0x1EF4,},
	{0x1EF7, 0x1EF6,},
	{0x1EF9, 0x1EF8,},
	{0x1EFB, 0x1EFA,},
	{0x1EFD, 0x1EFC,},
	{0x1EFF, 0x1EFE,},
	{0x1F00, 0x1F08,},
	{0x1F01, 0x1F09,},
	{0x1F02, 0x1F0A,},
	{0x1F03, 0x1F0B,},
	{0x1F04, 0x1F0C,},
	{0x1F05, 0x1F0D,},
	{0x1F06, 0x1F0E,},
	{0x1F07, 0x1F0F,},
	{0x1F10, 0x1F18,},
	{0x1F11, 0x1F19,},
	{0x1F12, 0x1F1A,},
	{0x1F13, 0x1F1B,},
	{0x1F14, 0x1F1C,},
	{0x1F15, 0x1F1D,},
	{0x1F20, 0x1F28,},
	{0x1F21, 0x1F29,},
	{0x1F22, 0x1F2A,},
	{0x1F23, 0x1F2B,},
	{0x1F24, 0x1F2C,},
	{0x1F25, 0x1F2D,},
	{0x1F26, 0x1F2E,},
	{0x1F27, 0x1F2F,},
	{0x1F30, 0x1F38,},
	{0x1F31, 0x1F39,},
	{0x1F32, 0x1F3A,},
	{0x1F33, 0x1F3B,},
	{0x1F34, 0x1F3C,},
	{0x1F35, 0x1F3D,},
	{0x1F36, 0x1F3E,},
	{0x1F37, 0x1F3F,},
	{0x1F40, 0x1F48,},
	{0x1F41, 0x1F49,},
	{0x1F42, 0x1F4A,},
	{0x1F43, 0x1F4B,},
	{0x1F44, 0x1F4C,},
	{0x1F45, 0x1F4D,},
	{0x1F51, 0x1F59,},
	{0x1F53, 0x1F5B,},
	{0x1F55, 0x1F5D,},
	{0x1F57, 0x1F5F,},
	{0x1F60, 0x1F68,},
	{0x1F61, 0x1F69,},
	{0x1F62, 0x1F6A,},
	{0x1F63, 0x1F6B,},
	{0x1F64, 0x1F6C,},
	{0x1F65, 0x1F6D,},
	{0x1F66, 0x1F6E,},
	{0x1F67, 0x1F6F,},
	{0x1F70, 0x1FBA,},
	{0x1F71, 0x1FBB,},
	{0x1F72, 0x1FC8,},
	{0x1F73, 0x1FC9,},
	{0x1F74, 0x1FCA,},
	{0x1F75, 0x1FCB,},
	{0x1F76, 0x1FDA,},
	{0x1F77, 0x1FDB,},
	{0x1F78, 0x1FF8,},
	{0x1F79, 0x1FF9,},
	{0x1F7A, 0x1FEA,},
	{0x1F7B, 0x1FEB,},
	{0x1F7C, 0x1FFA,},
	{0x1F7D, 0x1FFB,},
	{0x1F80, 0x1F88,},
	{0x1F81, 0x1F89,},
	{0x1F82, 0x1F8A,},
	{0x1F83, 0x1F8B,},
	{0x1F84, 0x1F8C,},
	{0x1F85, 0x1F8D,},
	{0x1F86, 0x1F8E,},
	{0x1F87, 0x1F8F,},
	{0x1F90, 0x1F98,},
	{0x1F91, 0x1F99,},
	{0x1F92, 0x1F9A,},
	{0x1F93, 0x1F9B,},
	{0x1F94, 0x1F9C,},
	{0x1F95, 0x1F9D,},
	{0x1F96, 0x1F9E,},
	{0x1F97, 0x1F9F,},
	{0x1FA0, 0x1FA8,},
	{0x1FA1, 0x1FA9,},
	{0x1FA2, 0x1FAA,},
	{0x1FA3, 0x1FAB,},
	{0x1FA4, 0x1FAC,},
	{0x1FA5, 0x1FAD,},
	{0x1FA6, 0x1FAE,},
	{0x1FA7, 0x1FAF,},
	{0x1FB0, 0x1FB8,},
	{0x1FB1, 0x1FB9,},
	{0x1FB3, 0x1FBC,},
	{0x1FBE, 0x0399,},
	{0x1FC3, 0x1FCC,},
	{0x1FD0, 0x1FD8,},
	{0x1FD1, 0x1FD9,},
	{0x1FE0, 0x1FE8,},
	{0x1FE1, 0x1FE9,},
	{0x1FE5, 0x1FEC,},
	{0x1FF3, 0x1FFC,},
	{0x214E, 0x2132,},
	{0x2170, 0x2160,},
	{0x2171, 0x2161,},
	{0x2172, 0x2162,},
	{0x2173, 0x2163,},
	{0x2174, 0x2164,},
	{0x2175, 0x2165,},
	{0x2176, 0x2166,},
	{0x2177, 0x2167,},
	{0x2178, 0x2168,},
	{0x2179, 0x2169,},
	{0x217A, 0x216A,},
	{0x217B, 0x216B,},
	{0x217C, 0x216C,},
	{0x217D, 0x216D,},
	{0x217E, 0x216E,},
	{0x217F, 0x216F,},
	{0x2184, 0x2183,},
	{0x24D0, 0x24B6,},
	{0x24D1, 0x24B7,},
	{0x24D2, 0x24B8,},
	{0x24D3, 0x24B9,},
	{0x24D4, 0x24BA,},
	{0x24D5, 0x24BB,},
	{0x24D6, 0x24BC,},
	{0x24D7, 0x24BD,},
	{0x24D8, 0x24BE,},
	{0x24D9, 0x24BF,},
	{0x24DA, 0x24C0,},
	{0x24DB, 0x24C1,},
	{0x24DC, 0x24C2,},
	{0x24DD, 0x24C3,},
	{0x24DE, 0x24C4,},
	{0x24DF, 0x24C5,},
	{0x24E0, 0x24C6,},
	{0x24E1, 0x24C7,},
	{0x24E2, 0x24C8,},
	{0x24E3, 0x24C9,},
	{0x24E4, 0x24CA,},
	{0x24E5, 0x24CB,},
	{0x24E6, 0x24CC,},
	{0x24E7, 0x24CD,},
	{0x24E8, 0x24CE,},
	{0x24E9, 0x24CF,},
	{0x2C30, 0x2C00,},
	{0x2C31, 0x2C01,},
	{0x2C32, 0x2C02,},
	{0x2C33, 0x2C03,},
	{0x2C34, 0x2C04,},
	{0x2C35, 0x2C05,},
	{0x2C36, 0x2C06,},
	{0x2C37, 0x2C07,},
	{0x2C38, 0x2C08,},
	{0x2C39, 0x2C09,},
	{0x2C3A, 0x2C0A,},
	{0x2C3B, 0x2C0B,},
	{0x2C3C, 0x2C0C,},
	{0x2C3D, 0x2C0D,},
	{0x2C3E, 0x2C0E,},
	{0x2C3F, 0x2C0F,},
	{0x2C40, 0x2C10,},
	{0x2C41, 0x2C11,},
	{0x2C42, 0x2C12,},
	{0x2C43, 0x2C13,},
	{0x2C44, 0x2C14,},
	{0x2C45, 0x2C15,},
	{0x2C46, 0x2C16,},
	{0x2C47, 0x2C17,},
	{0x2C48, 0x2C18,},
	{0x2C49, 0x2C19,},
	{0x2C4A, 0x2C1A,},
	{0x2C4B, 0x2C1B,},
	{0x2C4C, 0x2C1C,},
	{0x2C4D, 0x2C1D,},
	{0x2C4E, 0x2C1E,},
	{0x2C4F, 0x2C1F,},
	{0x2C50, 0x2C20,},
	{0x2C51, 0x2C21,},
	{0x2C52, 0x2C22,},
	{0x2C53, 0x2C23,},
	{0x2C54, 0x2C24,},
	{0x2C55, 0x2C25,},
	{0x2C56, 0x2C26,},
	{0x2C57, 0x2C27,},
	{0x2C58, 0x2C28,},
	{0x2C59, 0x2C29,},
	{0x2C5A, 0x2C2A,},
	{0x2C5B, 0x2C2B,},
	{0x2C5C, 0x2C2C,},
	{0x2C5D, 0x2C2D,},
	{0x2C5E, 0x2C2E,},
	{0x2C5F, 0x2C2F,},
	{0x2C61, 0x2C60,},
	{0x2C65, 0x023A,},
	{0x2C66, 0x023E,},
	{0x2C68, 0x2C67,},
	{0x2C6A, 0x2C69,},
	{0x2C6C, 0x2C6B,},
	{0x2C73, 0x2C72,},
	{0x2C76, 0x2C75,},
	{0x2C81, 0x2C80,},
	{0x2C83, 0x2C82,},
	{0x2C85, 0x2C84,},
	{0x2C87, 0x2C86,},
	{0x2C89, 0x2C88,},
	{0x2C8B, 0x2C8A,},
	{0x2C8D, 0x2C8C,},
	{0x2C8F, 0x2C8E,},
	{0x2C91, 0x2C90,},
	{0x2C93, 0x2C92,},
	{0x2C95, 0x2C94,},
	{0x2C97, 0x2C96,},
	{0x2C99, 0x2C98,},
	{0x2C9B, 0x2C9A,},
	{0x2C9D, 0x2C9C,},
	{0x2C9F, 0x2C9E,},
	{0x2CA1, 0x2CA0,},
	{0x2CA3, 0x2CA2,},
	{0x2CA5, 0x2CA4,},
	{0x2CA7, 0x2CA6,},
	{0x2CA9, 0x2CA8,},
	{0x2CAB, 0x2CAA,},
	{0x2CAD, 0x2CAC,},
	{0x2CAF, 0x2CAE,},
	{0x2CB1, 0x2CB0,},
	{0x2CB3, 0x2CB2,},
	{0x2CB5, 0x2CB4,},
	{0x2CB7, 0x2CB6,},
	{0x2CB9, 0x2CB8,},
	{0x2CBB, 0x2CBA,},
	{0x2CBD, 0x2CBC,},
	{0x2CBF, 0x2CBE,},
	{0x2CC1, 0x2CC0,},
	{0x2CC3, 0x2CC2,},
	{0x2CC5, 0x2CC4,},
	{0x2CC7, 0x2CC6,},
	{0x2CC9, 0x2CC8,},
	{0x2CCB, 0x2CCA,},
	{0x2CCD, 0x2CCC,},
	{0x2CCF, 0x2CCE,},
	{0x2CD1, 0x2CD0,},
	{0x2CD3, 0x2CD2,},
	{0x2CD5, 0x2CD4,},
	{0x2CD7, 0x2CD6,},
	{0x2CD9, 0x2CD8,},
	{0x2CDB, 0x2CDA,},
	{0x2CDD, 0x2CDC,},
	{0x2CDF, 0x2CDE,},
	{0x2CE1, 0x2CE0,},
	{0x2CE3, 0x2CE2,},
	{0x2CEC, 0x2CEB,},
	{0x2CEE, 0x2CED,},
	{0x2CF3, 0x2CF2,},
	{0x2D00, 0x10A0,},
	{0x2D01, 0x10A1,},
	{0x2D02, 0x10A2,},
	{0x2D03, 0x10A3,},
	{0x2D04, 0x10A4,},
	{0x2D05, 0x10A5,},
	{0x2D06, 0x10A6,},
	{0x2D07, 0x10A7,},
	{0x2D08, 0x10A8,},
	{0x2D09, 0x10A9,},
	{0x2D0A, 0x10AA,},
	{0x2D0B, 0x10AB,},
	{0x2D0C, 0x10AC,},
	{0x2D0D, 0x10AD,},
	{0x2D0E, 0x10AE,},
	{0x2D0F, 0x10AF,},
	{0x2D10, 0x10B0,},
	{0x2D11, 0x10B1,},
	{0x2D12, 0x10B2,},
	{0x2D13, 0x10B3,},
	{0x2D14, 0x10B4,},
	{0x2D15, 0x10B5,},
	{0x2D16, 0x10B6,},
	{0x2D17, 0x10B7,},
	{0x2D18, 0x10B8,},
	{0x2D19, 0x10B9,},
	{0x2D1A, 0x10BA,},
	{0x2D1B, 0x10BB,},
	{0x2D1C, 0x10BC,},
	{0x2D1D, 0x10BD,},
	{0x2D1E, 0x10BE,},
	{0x2D1F, 0x10BF,},
	{0x2D20, 0x10C0,},
	{0x2D21, 0x10C1,},
	{0x2D22, 0x10C2,},
	{0x2D23, 0x10C3,},
	{0x2D24, 0x10C4,},
	{0x2D25, 0x10C5,},
	{0x2D27, 0x10C7,},
	{0x2D2D, 0x10CD,},
	{0xA641, 0xA640,},
	{0xA643, 0xA642,},
	{0xA645, 0xA644,},
	{0xA647, 0xA646,},
	{0xA649, 0xA648,},
	{0xA64B, 0xA64A,},
	{0xA64D, 0xA64C,},
	{0xA64F, 0xA64E,},
	{0xA651, 0xA650,},
	{0xA653, 0xA652,},
	{0xA655, 0xA654,},
	{0xA657, 0xA656,},
	{0xA659, 0xA658,},
	{0xA65B, 0xA65A,},
	{0xA65D, 0xA65C,},
	{0xA65F, 0xA65E,},
	{0xA661, 0xA660,},
	{0xA663, 0xA662,},
	{0xA665, 0xA664,},
	{0xA667, 0xA666,},
	{0xA669, 0xA668,},
	{0xA66B, 0xA66A,},
	{0xA66D, 0xA66C,},
	{0xA681, 0xA680,},
	{0xA683, 0xA682,},
	{0xA685, 0xA684,},
	{0xA687, 0xA686,},
	{0xA689, 0xA688,},
	{0xA68B, 0xA68A,},
	{0xA68D, 0xA68C,},
	{0xA68F, 0xA68E,},
	{0xA691, 0xA690,},
	{0xA693, 0xA692,},
	{0xA695, 0xA694,},
	{0xA697, 0xA696,},
	{0xA699, 0xA698,},
	{0xA69B, 0xA69A,},
	{0xA723, 0xA722,},
	{0xA725, 0xA724,},
	{0xA727, 0xA726,},
	{0xA729, 0xA728,},
	{0xA72B, 0xA72A,},
	{0xA72D, 0xA72C,},
	{0xA72F, 0xA72E,},
	{0xA733, 0xA732,},
	{0xA735, 0xA734,},
	{0xA737, 0xA736,},
	{0xA739, 0xA738,},
	{0xA73B, 0xA73A,},
	{0xA73D, 0xA73C,},
	{0xA73F, 0xA73E,},
	{0xA741, 0xA740,},
	{0xA743, 0xA742,},
	{0xA745, 0xA744,},
	{0xA747, 0xA746,},
	{0xA749, 0xA748,},
	{0xA74B, 0xA74A,},
	{0xA74D, 0xA74C,},
	{0xA74F, 0xA74E,},
	{0xA751, 0xA750,},
	{0xA753, 0xA752,},
	{0xA755, 0xA754,},
	{0xA757, 0xA756,},
	{0xA759, 0xA758,},
	{0xA75B, 0xA75A,},
	{0xA75D, 0xA75C,},
	{0xA75F, 0xA75E,},
	{0xA761, 0xA760,},
	{0xA763, 0xA762,},
	{0xA765, 0xA764,},
	{0xA767, 0xA766,},
	{0xA769, 0xA768,},
	{0xA76B, 0xA76A,},
	{0xA76D, 0xA76C,},
	{0xA76F, 0xA76E,},
	{0xA77A, 0xA779,},
	{0xA77C, 0xA77B,},
	{0xA77F, 0xA77E,},
	{0xA781, 0xA780,},
	{0xA783, 0xA782,},
	{0xA785, 0xA784,},
	{0xA787, 0xA786,},
	{0xA78C, 0xA78B,},
	{0xA791, 0xA790,},
	{0xA793, 0xA792,},
	{0xA794, 0xA7C4,},
	{0xA797, 0xA796,},
	{0xA799, 0xA798,},
	{0xA79B, 0xA79A,},
	{0xA79D, 0xA79C,},
	{0xA79F, 0xA79E,},
	{0xA7A1, 0xA7A0,},
	{0xA7A3, 0xA7A2,},
	{0xA7A5, 0xA7A4,},
	{0xA7A7, 0xA7A6,},
	{0xA7A9, 0xA7A8,},
	{0xA7B5, 0xA7B4,},
	{0xA7B7, 0xA7B6,},
	{0xA7B9, 0xA7B8,},
	{0xA7BB, 0xA7BA,},
	{0xA7BD, 0xA7BC,},
	{0xA7BF, 0xA7BE,},
	{0xA7C1, 0xA7C0,},
	{0xA7C3, 0xA7C2,},
	{0xA7C8, 0xA7C7,},
	{0xA7CA, 0xA7C9,},
	{0xA7D1, 0xA7D0,},
	{0xA7D7, 0xA7D6,},
	{0xA7D9, 0xA7D8,},
	{0xA7F6, 0xA7F5,},
	{0xAB53, 0xA7B3,},
	{0xAB70, 0x13A0,},
	{0xAB71, 0x13A1,},
	{0xAB72, 0x13A2,},
	{0xAB73, 0x13A3,},
	{0xAB74, 0x13A4,},
	{0xAB75, 0x13A5,},
	{0xAB76, 0x13A6,},
	{0xAB77, 0x13A7,},
	{0xAB78, 0x13A8,},
	{0xAB79, 0x13A9,},
	{0xAB7A, 0x13AA,},
	{0xAB7B, 0x13AB,},
	{0xAB7C, 0x13AC,},
	{0xAB7D, 0x13AD,},
	{0xAB7E, 0x13AE,},
	{0xAB7F, 0x13AF,},
	{0xAB80, 0x13B0,},
	{0xAB81, 0x13B1,},
	{0xAB82, 0x13B2,},
	{0xAB83, 0x13B3,},
	{0xAB84, 0x13B4,},
	{0xAB85, 0x13B5,},
	{0xAB86, 0x13B6,},
	{0xAB87, 0x13B7,},
	{0xAB88, 0x13B8,},
	{0xAB89, 0x13B9,},
	{0xAB8A, 0x13BA,},
	{0xAB8B, 0x13BB,},
	{0xAB8C, 0x13BC,},
	{0xAB8D, 0x13BD,},
	{0xAB8E, 0x13BE,},
	{0xAB8F, 0x13BF,},
	{0xAB90, 0x13C0,},
	{0xAB91, 0x13C1,},
	{0xAB92, 0x13C2,},
	{0xAB93, 0x13C3,},
	{0xAB94, 0x13C4,},
	{0xAB95, 0x13C5,},
	{0xAB96, 0x13C6,},
	{0xAB97, 0x13C7,},
	{0xAB98, 0x13C8,},
	{0xAB99, 0x13C9,},
	{0xAB9A, 0x13CA,},
	{0xAB9B, 0x13CB,},
	{0xAB9C, 0x13CC,},
	{0xAB9D, 0x13CD,},
	{0xAB9E, 0x13CE,},
	{0xAB9F, 0x13CF,},
	{0xABA0, 0x13D0,},
	{0xABA1, 0x13D1,},
	{0xABA2, 0x13D2,},
	{0xABA3, 0x13D3,},
	{0xABA4, 0x13D4,},
	{0xABA5, 0x13D5,},
	{0xABA6, 0x13D6,},
	{0xABA7, 0x13D7,},
	{0xABA8, 0x13D8,},
	{0xABA9, 0x13D9,},
	{0xABAA, 0x13DA,},
	{0xABAB, 0x13DB,},
	{0xABAC, 0x13DC,},
	{0xABAD, 0x13DD,},
	{0xABAE, 0x13DE,},
	{0xABAF, 0x13DF,},
	{0xABB0, 0x13E0,},
	{0xABB1, 0x13E1,},
	{0xABB2, 0x13E2,},
	{0xABB3, 0x13E3,},
	{0xABB4, 0x13E4,},
	{0xABB5, 0x13E5,},
	{0xABB6, 0x13E6,},
	{0xABB7, 0x13E7,},
	{0xABB8, 0x13E8,},
	{0xABB9, 0x13E9,},
	{0xABBA, 0x13EA,},
	{0xABBB, 0x13EB,},
	{0xABBC, 0x13EC,},
	{0xABBD, 0x13ED,},
	{0xABBE, 0x13EE,},
	{0xABBF, 0x13EF,},
	{0xFF41, 0xFF21,},
	{0xFF42, 0xFF22,},
	{0xFF43, 0xFF23,},
	{0xFF44, 0xFF24,},
	{0xFF45, 0xFF25,},
	{0xFF46, 0xFF26,},
	{0xFF47, 0xFF27,},
	{0xFF48, 0xFF28,},
	{0xFF49, 0xFF29,},
	{0xFF4A, 0xFF2A,},
	{0xFF4B, 0xFF2B,},
	{0xFF4C, 0xFF2C,},
	{0xFF4D, 0xFF2D,},
	{0xFF4E, 0xFF2E,},
	{0xFF4F, 0xFF2F,},
	{0xFF50, 0xFF30,},
	{0xFF51, 0xFF31,},
	{0xFF52, 0xFF32,},
	{0xFF53, 0xFF33,},
	{0xFF54, 0xFF34,},
	{0xFF55, 0xFF35,},
	{0xFF56, 0xFF36,},
	{0xFF57, 0xFF37,},
	{0xFF58, 0xFF38,},
	{0xFF59, 0xFF39,},
	{0xFF5A, 0xFF3A,},
	{0x10428, 0x10400,},
	{0x10429, 0x10401,},
	{0x1042A, 0x10402,},
	{0x1042B, 0x10403,},
	{0x1042C, 0x10404,},
	{0x1042D, 0x10405,},
	{0x1042E, 0x10406,},
	{0x1042F, 0x10407,},
	{0x10430, 0x10408,},
	{0x10431, 0x10409,},
	{0x10432, 0x1040A,},
	{0x10433, 0x1040B,},
	{0x10434, 0x1040C,},
	{0x10435, 0x1040D,},
	{0x10436, 0x1040E,},
	{0x10437, 0x1040F,},
	{0x10438, 0x10410,},
	{0x10439, 0x10411,},
	{0x1043A, 0x10412,},
	{0x1043B, 0x10413,},
	{0x1043C, 0x10414,},
	{0x1043D, 0x10415,},
	{0x1043E, 0x10416,},
	{0x1043F, 0x10417,},
	{0x10440, 0x10418,},
	{0x10441, 0x10419,},
	{0x10442, 0x1041A,},
	{0x10443, 0x1041B,},
	{0x10444, 0x1041C,},
	{0x10445, 0x1041D,},
	{0x10446, 0x1041E,},
	{0x10447, 0x1041F,},
	{0x10448, 0x10420,},
	{0x10449, 0x10421,},
	{0x1044A, 0x10422,},
	{0x1044B, 0x10423,},
	{0x1044C, 0x10424,},
	{0x1044D, 0x10425,},
	{0x1044E, 0x10426,},
	{0x1044F, 0x10427,},
	{0x104D8, 0x104B0,},
	{0x104D9, 0x104B1,},
	{0x104DA, 0x104B2,},
	{0x104DB, 0x104B3,},
	{0x104DC, 0x104B4,},
	{0x104DD, 0x104B5,},
	{0x104DE, 0x104B6,},
	{0x104DF, 0x104B7,},
	{0x104E0, 0x104B8,},
	{0x104E1, 0x104B9,},
	{0x104E2, 0x104BA,},
	{0x104E3, 0x104BB,},
	{0x104E4, 0x104BC,},
	{0x104E5, 0x104BD,},
	{0x104E6, 0x104BE,},
	{0x104E7, 0x104BF,},
	{0x104E8, 0x104C0,},
	{0x104E9, 0x104C1,},
	{0x104EA, 0x104C2,},
	{0x104EB, 0x104C3,},
	{0x104EC, 0x104C4,},
	{0x104ED, 0x104C5,},
	{0x104EE, 0x104C6,},
	{0x104EF, 0x104C7,},
	{0x104F0, 0x104C8,},
	{0x104F1, 0x104C9,},
	{0x104F2, 0x104CA,},
	{0x104F3, 0x104CB,},
	{0x104F4, 0x104CC,},
	{0x104F5, 0x104CD,},
	{0x104F6, 0x104CE,},
	{0x104F7, 0x104CF,},
	{0x104F8, 0x104D0,},
	{0x104F9, 0x104D1,},
	{0x104FA, 0x104D2,},
	{0x104FB, 0x104D3,},
	{0x10597, 0x10570,},
	{0x10598, 0x10571,},
	{0x10599, 0x10572,},
	{0x1059A, 0x10573,},
	{0x1059B, 0x10574,},
	{0x1059C, 0x10575,},
	{0x1059D, 0x10576,},
	{0x1059E, 0x10577,},
	{0x1059F, 0x10578,},
	{0x105A0, 0x10579,},
	{0x105A1, 0x1057A,},
	{0x105A3, 0x1057C,},
	{0x105A4, 0x1057D,},
	{0x105A5, 0x1057E,},
	{0x105A6, 0x1057F,},
	{0x105A7, 0x10580,},
	{0x105A8, 0x10581,},
	{0x105A9, 0x10582,},
	{0x105AA, 0x10583,},
	{0x105AB, 0x10584,},
	{0x105AC, 0x10585,},
	{0x105AD, 0x10586,},
	{0x105AE, 0x10587,},
	{0x105AF, 0x10588,},
	{0x105B0, 0x10589,},
	{0x105B1, 0x1058A,},
	{0x105B3, 0x1058C,},
	{0x105B4, 0x1058D,},
	{0x105B5, 0x1058E,},
	{0x105B6, 0x1058F,},
	{0x105B7, 0x10590,},
	{0x105B8, 0x10591,},
	{0x105B9, 0x10592,},
	{0x105BB, 0x10594,},
	{0x105BC, 0x10595,},
	{0x10CC0, 0x10C80,},
	{0x10CC1, 0x10C81,},
	{0x10CC2, 0x10C82,},
	{0x10CC3, 0x10C83,},
	{0x10CC4, 0x10C84,},
	{0x10CC5, 0x10C85,},
	{0x10CC6, 0x10C86,},
	{0x10CC7, 0x10C87,},
	{0x10CC8, 0x10C88,},
	{0x10CC9, 0x10C89,},
	{0x10CCA, 0x10C8A,},
	{0x10CCB, 0x10C8B,},
	{0x10CCC, 0x10C8C,},
	{0x10CCD, 0x10C8D,},
	{0x10CCE, 0x10C8E,},
	{0x10CCF, 0x10C8F,},
	{0x10CD0, 0x10C90,},
	{0x10CD1, 0x10C91,},
	{0x10CD2, 0x10C92,},
	{0x10CD3, 0x10C93,},
	{0x10CD4, 0x10C94,},
	{0x10CD5, 0x10C95,},
	{0x10CD6, 0x10C96,},
	{0x10CD7, 0x10C97,},
	{0x10CD8, 0x10C98,},
	{0x10CD9, 0x10C99,},
	{0x10CDA, 0x10C9A,},
	{0x10CDB, 0x10C9B,},
	{0x10CDC, 0x10C9C,},
	{0x10CDD, 0x10C9D,},
	{0x10CDE, 0x10C9E,},
	{0x10CDF, 0x10C9F,},
	{0x10CE0, 0x10CA0,},
	{0x10CE1, 0x10CA1,},
	{0x10CE2, 0x10CA2,},
	{0x10CE3, 0x10CA3,},
	{0x10CE4, 0x10CA4,},
	{0x10CE5, 0x10CA5,},
	{0x10CE6, 0x10CA6,},
	{0x10CE7, 0x10CA7,},
	{0x10CE8, 0x10CA8,},
	{0x10CE9, 0x10CA9,},
	{0x10CEA, 0x10CAA,},
	{0x10CEB, 0x10CAB,},
	{0x10CEC, 0x10CAC,},
	{0x10CED, 0x10CAD,},
	{0x10CEE, 0x10CAE,},
	{0x10CEF, 0x10CAF,},
	{0x10CF0, 0x10CB0,},
	{0x10CF1, 0x10CB1,},
	{0x10CF2, 0x10CB2,},
	{0x118C0, 0x118A0,},
	{0x118C1, 0x118A1,},
	{0x118C2, 0x118A2,},
	{0x118C3, 0x118A3,},
	{0x118C4, 0x118A4,},
	{0x118C5, 0x118A5,},
	{0x118C6, 0x118A6,},
	{0x118C7, 0x118A7,},
	{0x118C8, 0x118A8,},
	{0x118C9, 0x118A9,},
	{0x118CA, 0x118AA,},
	{0x118CB, 0x118AB,},
	{0x118CC, 0x118AC,},
	{0x118CD, 0x118AD,},
	{0x118CE, 0x118AE,},
	{0x118CF, 0x118AF,},
	{0x118D0, 0x118B0,},
	{0x118D1, 0x118B1,},
	{0x118D2, 0x118B2,},
	{0x118D3, 0x118B3,},
	{0x118D4, 0x118B4,},
	{0x118D5, 0x118B5,},
	{0x118D6, 0x118B6,},
	{0x118D7, 0x118B7,},
	{0x118D8, 0x118B8,},
	{0x118D9, 0x118B9,},
	{0x118DA, 0x118BA,},
	{0x118DB, 0x118BB,},
	{0x118DC, 0x118BC,},
	{0x118DD, 0x118BD,},
	{0x118DE, 0x118BE,},
	{0x118DF, 0x118BF,},
	{0x16E60, 0x16E40,},
	{0x16E61, 0x16E41,},
	{0x16E62, 0x16E42,},
	{0x16E63, 0x16E43,},
	{0x16E64, 0x16E44,},
	{0x16E65, 0x16E45,},
	{0x16E66, 0x16E46,},
	{0x16E67, 0x16E47,},
	{0x16E68, 0x16E48,},
	{0x16E69, 0x16E49,},
	{0x16E6A, 0x16E4A,},
	{0x16E6B, 0x16E4B,},
	{0x16E6C, 0x16E4C,},
	{0x16E6D, 0x16E4D,},
	{0x16E6E, 0x16E4E,},
	{0x16E6F, 0x16E4F,},
	{0x16E70, 0x16E50,},
	{0x16E71, 0x16E51,},
	{0x16E72, 0x16E52,},
	{0x16E73, 0x16E53,},
	{0x16E74, 0x16E54,},
	{0x16E75, 0x16E55,},
	{0x16E76, 0x16E56,},
	{0x16E77, 0x16E57,},
	{0x16E78, 0x16E58,},
	{0x16E79, 0x16E59,},
	{0x16E7A, 0x16E5A,},
	{0x16E7B, 0x16E5B,},
	{0x16E7C, 0x16E5C,},
	{0x16E7D, 0x16E5D,},
	{0x16E7E, 0x16E5E,},
	{0x16E7F, 0x16E5F,},
	{0x1E922, 0x1E900,},
	{0x1E923, 0x1E901,},
	{0x1E924, 0x1E902,},
	{0x1E925, 0x1E903,},
	{0x1E926, 0x1E904,},
	{0x1E927, 0x1E905,},
	{0x1E928, 0x1E906,},
	{0x1E929, 0x1E907,},
	{0x1E92A, 0x1E908,},
	{0x1E92B, 0x1E909,},
	{0x1E92C, 0x1E90A,},
	{0x1E92D, 0x1E90B,},
	{0x1E92E, 0x1E90C,},
	{0x1E92F, 0x1E90D,},
	{0x1E930, 0x1E90E,},
	{0x1E931, 0x1E90F,},
	{0x1E932, 0x1E910,},
	{0x1E933, 0x1E911,},
	{0x1E934, 0x1E912,},
	{0x1E935, 0x1E913,},
	{0x1E936, 0x1E914,},
	{0x1E937, 0x1E915,},
	{0x1E938, 0x1E916,},
	{0x1E939, 0x1E917,},
	{0x1E93A, 0x1E918,},
	{0x1E93B, 0x1E919,},
	{0x1E93C, 0x1E91A,},
	{0x1E93D, 0x1E91B,},
	{0x1E93E, 0x1E91C,},
	{0x1E93F, 0x1E91D,},
	{0x1E940, 0x1E91E,},
	{0x1E941, 0x1E91F,},
	{0x1E942, 0x1E920,},
	{0x1E943, 0x1E921,},
}, UTF8_toLower[] = {			/* code points with non-null lowercase conversion */
	{0x0041, 0x0061,},
	{0x0042, 0x0062,},
	{0x0043, 0x0063,},
	{0x0044, 0x0064,},
	{0x0045, 0x0065,},
	{0x0046, 0x0066,},
	{0x0047, 0x0067,},
	{0x0048, 0x0068,},
	{0x0049, 0x0069,},
	{0x004A, 0x006A,},
	{0x004B, 0x006B,},
	{0x004C, 0x006C,},
	{0x004D, 0x006D,},
	{0x004E, 0x006E,},
	{0x004F, 0x006F,},
	{0x0050, 0x0070,},
	{0x0051, 0x0071,},
	{0x0052, 0x0072,},
	{0x0053, 0x0073,},
	{0x0054, 0x0074,},
	{0x0055, 0x0075,},
	{0x0056, 0x0076,},
	{0x0057, 0x0077,},
	{0x0058, 0x0078,},
	{0x0059, 0x0079,},
	{0x005A, 0x007A,},
	{0x00C0, 0x00E0,},
	{0x00C1, 0x00E1,},
	{0x00C2, 0x00E2,},
	{0x00C3, 0x00E3,},
	{0x00C4, 0x00E4,},
	{0x00C5, 0x00E5,},
	{0x00C6, 0x00E6,},
	{0x00C7, 0x00E7,},
	{0x00C8, 0x00E8,},
	{0x00C9, 0x00E9,},
	{0x00CA, 0x00EA,},
	{0x00CB, 0x00EB,},
	{0x00CC, 0x00EC,},
	{0x00CD, 0x00ED,},
	{0x00CE, 0x00EE,},
	{0x00CF, 0x00EF,},
	{0x00D0, 0x00F0,},
	{0x00D1, 0x00F1,},
	{0x00D2, 0x00F2,},
	{0x00D3, 0x00F3,},
	{0x00D4, 0x00F4,},
	{0x00D5, 0x00F5,},
	{0x00D6, 0x00F6,},
	{0x00D8, 0x00F8,},
	{0x00D9, 0x00F9,},
	{0x00DA, 0x00FA,},
	{0x00DB, 0x00FB,},
	{0x00DC, 0x00FC,},
	{0x00DD, 0x00FD,},
	{0x00DE, 0x00FE,},
	{0x0100, 0x0101,},
	{0x0102, 0x0103,},
	{0x0104, 0x0105,},
	{0x0106, 0x0107,},
	{0x0108, 0x0109,},
	{0x010A, 0x010B,},
	{0x010C, 0x010D,},
	{0x010E, 0x010F,},
	{0x0110, 0x0111,},
	{0x0112, 0x0113,},
	{0x0114, 0x0115,},
	{0x0116, 0x0117,},
	{0x0118, 0x0119,},
	{0x011A, 0x011B,},
	{0x011C, 0x011D,},
	{0x011E, 0x011F,},
	{0x0120, 0x0121,},
	{0x0122, 0x0123,},
	{0x0124, 0x0125,},
	{0x0126, 0x0127,},
	{0x0128, 0x0129,},
	{0x012A, 0x012B,},
	{0x012C, 0x012D,},
	{0x012E, 0x012F,},
	{0x0130, 0x0069,},
	{0x0132, 0x0133,},
	{0x0134, 0x0135,},
	{0x0136, 0x0137,},
	{0x0139, 0x013A,},
	{0x013B, 0x013C,},
	{0x013D, 0x013E,},
	{0x013F, 0x0140,},
	{0x0141, 0x0142,},
	{0x0143, 0x0144,},
	{0x0145, 0x0146,},
	{0x0147, 0x0148,},
	{0x014A, 0x014B,},
	{0x014C, 0x014D,},
	{0x014E, 0x014F,},
	{0x0150, 0x0151,},
	{0x0152, 0x0153,},
	{0x0154, 0x0155,},
	{0x0156, 0x0157,},
	{0x0158, 0x0159,},
	{0x015A, 0x015B,},
	{0x015C, 0x015D,},
	{0x015E, 0x015F,},
	{0x0160, 0x0161,},
	{0x0162, 0x0163,},
	{0x0164, 0x0165,},
	{0x0166, 0x0167,},
	{0x0168, 0x0169,},
	{0x016A, 0x016B,},
	{0x016C, 0x016D,},
	{0x016E, 0x016F,},
	{0x0170, 0x0171,},
	{0x0172, 0x0173,},
	{0x0174, 0x0175,},
	{0x0176, 0x0177,},
	{0x0178, 0x00FF,},
	{0x0179, 0x017A,},
	{0x017B, 0x017C,},
	{0x017D, 0x017E,},
	{0x0181, 0x0253,},
	{0x0182, 0x0183,},
	{0x0184, 0x0185,},
	{0x0186, 0x0254,},
	{0x0187, 0x0188,},
	{0x0189, 0x0256,},
	{0x018A, 0x0257,},
	{0x018B, 0x018C,},
	{0x018E, 0x01DD,},
	{0x018F, 0x0259,},
	{0x0190, 0x025B,},
	{0x0191, 0x0192,},
	{0x0193, 0x0260,},
	{0x0194, 0x0263,},
	{0x0196, 0x0269,},
	{0x0197, 0x0268,},
	{0x0198, 0x0199,},
	{0x019C, 0x026F,},
	{0x019D, 0x0272,},
	{0x019F, 0x0275,},
	{0x01A0, 0x01A1,},
	{0x01A2, 0x01A3,},
	{0x01A4, 0x01A5,},
	{0x01A6, 0x0280,},
	{0x01A7, 0x01A8,},
	{0x01A9, 0x0283,},
	{0x01AC, 0x01AD,},
	{0x01AE, 0x0288,},
	{0x01AF, 0x01B0,},
	{0x01B1, 0x028A,},
	{0x01B2, 0x028B,},
	{0x01B3, 0x01B4,},
	{0x01B5, 0x01B6,},
	{0x01B7, 0x0292,},
	{0x01B8, 0x01B9,},
	{0x01BC, 0x01BD,},
	{0x01C4, 0x01C6,},
	{0x01C5, 0x01C6,},
	{0x01C7, 0x01C9,},
	{0x01C8, 0x01C9,},
	{0x01CA, 0x01CC,},
	{0x01CB, 0x01CC,},
	{0x01CD, 0x01CE,},
	{0x01CF, 0x01D0,},
	{0x01D1, 0x01D2,},
	{0x01D3, 0x01D4,},
	{0x01D5, 0x01D6,},
	{0x01D7, 0x01D8,},
	{0x01D9, 0x01DA,},
	{0x01DB, 0x01DC,},
	{0x01DE, 0x01DF,},
	{0x01E0, 0x01E1,},
	{0x01E2, 0x01E3,},
	{0x01E4, 0x01E5,},
	{0x01E6, 0x01E7,},
	{0x01E8, 0x01E9,},
	{0x01EA, 0x01EB,},
	{0x01EC, 0x01ED,},
	{0x01EE, 0x01EF,},
	{0x01F1, 0x01F3,},
	{0x01F2, 0x01F3,},
	{0x01F4, 0x01F5,},
	{0x01F6, 0x0195,},
	{0x01F7, 0x01BF,},
	{0x01F8, 0x01F9,},
	{0x01FA, 0x01FB,},
	{0x01FC, 0x01FD,},
	{0x01FE, 0x01FF,},
	{0x0200, 0x0201,},
	{0x0202, 0x0203,},
	{0x0204, 0x0205,},
	{0x0206, 0x0207,},
	{0x0208, 0x0209,},
	{0x020A, 0x020B,},
	{0x020C, 0x020D,},
	{0x020E, 0x020F,},
	{0x0210, 0x0211,},
	{0x0212, 0x0213,},
	{0x0214, 0x0215,},
	{0x0216, 0x0217,},
	{0x0218, 0x0219,},
	{0x021A, 0x021B,},
	{0x021C, 0x021D,},
	{0x021E, 0x021F,},
	{0x0220, 0x019E,},
	{0x0222, 0x0223,},
	{0x0224, 0x0225,},
	{0x0226, 0x0227,},
	{0x0228, 0x0229,},
	{0x022A, 0x022B,},
	{0x022C, 0x022D,},
	{0x022E, 0x022F,},
	{0x0230, 0x0231,},
	{0x0232, 0x0233,},
	{0x023A, 0x2C65,},
	{0x023B, 0x023C,},
	{0x023D, 0x019A,},
	{0x023E, 0x2C66,},
	{0x0241, 0x0242,},
	{0x0243, 0x0180,},
	{0x0244, 0x0289,},
	{0x0245, 0x028C,},
	{0x0246, 0x0247,},
	{0x0248, 0x0249,},
	{0x024A, 0x024B,},
	{0x024C, 0x024D,},
	{0x024E, 0x024F,},
	{0x0370, 0x0371,},
	{0x0372, 0x0373,},
	{0x0376, 0x0377,},
	{0x037F, 0x03F3,},
	{0x0386, 0x03AC,},
	{0x0388, 0x03AD,},
	{0x0389, 0x03AE,},
	{0x038A, 0x03AF,},
	{0x038C, 0x03CC,},
	{0x038E, 0x03CD,},
	{0x038F, 0x03CE,},
	{0x0391, 0x03B1,},
	{0x0392, 0x03B2,},
	{0x0393, 0x03B3,},
	{0x0394, 0x03B4,},
	{0x0395, 0x03B5,},
	{0x0396, 0x03B6,},
	{0x0397, 0x03B7,},
	{0x0398, 0x03B8,},
	{0x0399, 0x03B9,},
	{0x039A, 0x03BA,},
	{0x039B, 0x03BB,},
	{0x039C, 0x03BC,},
	{0x039D, 0x03BD,},
	{0x039E, 0x03BE,},
	{0x039F, 0x03BF,},
	{0x03A0, 0x03C0,},
	{0x03A1, 0x03C1,},
	{0x03A3, 0x03C3,},
	{0x03A4, 0x03C4,},
	{0x03A5, 0x03C5,},
	{0x03A6, 0x03C6,},
	{0x03A7, 0x03C7,},
	{0x03A8, 0x03C8,},
	{0x03A9, 0x03C9,},
	{0x03AA, 0x03CA,},
	{0x03AB, 0x03CB,},
	{0x03CF, 0x03D7,},
	{0x03D8, 0x03D9,},
	{0x03DA, 0x03DB,},
	{0x03DC, 0x03DD,},
	{0x03DE, 0x03DF,},
	{0x03E0, 0x03E1,},
	{0x03E2, 0x03E3,},
	{0x03E4, 0x03E5,},
	{0x03E6, 0x03E7,},
	{0x03E8, 0x03E9,},
	{0x03EA, 0x03EB,},
	{0x03EC, 0x03ED,},
	{0x03EE, 0x03EF,},
	{0x03F4, 0x03B8,},
	{0x03F7, 0x03F8,},
	{0x03F9, 0x03F2,},
	{0x03FA, 0x03FB,},
	{0x03FD, 0x037B,},
	{0x03FE, 0x037C,},
	{0x03FF, 0x037D,},
	{0x0400, 0x0450,},
	{0x0401, 0x0451,},
	{0x0402, 0x0452,},
	{0x0403, 0x0453,},
	{0x0404, 0x0454,},
	{0x0405, 0x0455,},
	{0x0406, 0x0456,},
	{0x0407, 0x0457,},
	{0x0408, 0x0458,},
	{0x0409, 0x0459,},
	{0x040A, 0x045A,},
	{0x040B, 0x045B,},
	{0x040C, 0x045C,},
	{0x040D, 0x045D,},
	{0x040E, 0x045E,},
	{0x040F, 0x045F,},
	{0x0410, 0x0430,},
	{0x0411, 0x0431,},
	{0x0412, 0x0432,},
	{0x0413, 0x0433,},
	{0x0414, 0x0434,},
	{0x0415, 0x0435,},
	{0x0416, 0x0436,},
	{0x0417, 0x0437,},
	{0x0418, 0x0438,},
	{0x0419, 0x0439,},
	{0x041A, 0x043A,},
	{0x041B, 0x043B,},
	{0x041C, 0x043C,},
	{0x041D, 0x043D,},
	{0x041E, 0x043E,},
	{0x041F, 0x043F,},
	{0x0420, 0x0440,},
	{0x0421, 0x0441,},
	{0x0422, 0x0442,},
	{0x0423, 0x0443,},
	{0x0424, 0x0444,},
	{0x0425, 0x0445,},
	{0x0426, 0x0446,},
	{0x0427, 0x0447,},
	{0x0428, 0x0448,},
	{0x0429, 0x0449,},
	{0x042A, 0x044A,},
	{0x042B, 0x044B,},
	{0x042C, 0x044C,},
	{0x042D, 0x044D,},
	{0x042E, 0x044E,},
	{0x042F, 0x044F,},
	{0x0460, 0x0461,},
	{0x0462, 0x0463,},
	{0x0464, 0x0465,},
	{0x0466, 0x0467,},
	{0x0468, 0x0469,},
	{0x046A, 0x046B,},
	{0x046C, 0x046D,},
	{0x046E, 0x046F,},
	{0x0470, 0x0471,},
	{0x0472, 0x0473,},
	{0x0474, 0x0475,},
	{0x0476, 0x0477,},
	{0x0478, 0x0479,},
	{0x047A, 0x047B,},
	{0x047C, 0x047D,},
	{0x047E, 0x047F,},
	{0x0480, 0x0481,},
	{0x048A, 0x048B,},
	{0x048C, 0x048D,},
	{0x048E, 0x048F,},
	{0x0490, 0x0491,},
	{0x0492, 0x0493,},
	{0x0494, 0x0495,},
	{0x0496, 0x0497,},
	{0x0498, 0x0499,},
	{0x049A, 0x049B,},
	{0x049C, 0x049D,},
	{0x049E, 0x049F,},
	{0x04A0, 0x04A1,},
	{0x04A2, 0x04A3,},
	{0x04A4, 0x04A5,},
	{0x04A6, 0x04A7,},
	{0x04A8, 0x04A9,},
	{0x04AA, 0x04AB,},
	{0x04AC, 0x04AD,},
	{0x04AE, 0x04AF,},
	{0x04B0, 0x04B1,},
	{0x04B2, 0x04B3,},
	{0x04B4, 0x04B5,},
	{0x04B6, 0x04B7,},
	{0x04B8, 0x04B9,},
	{0x04BA, 0x04BB,},
	{0x04BC, 0x04BD,},
	{0x04BE, 0x04BF,},
	{0x04C0, 0x04CF,},
	{0x04C1, 0x04C2,},
	{0x04C3, 0x04C4,},
	{0x04C5, 0x04C6,},
	{0x04C7, 0x04C8,},
	{0x04C9, 0x04CA,},
	{0x04CB, 0x04CC,},
	{0x04CD, 0x04CE,},
	{0x04D0, 0x04D1,},
	{0x04D2, 0x04D3,},
	{0x04D4, 0x04D5,},
	{0x04D6, 0x04D7,},
	{0x04D8, 0x04D9,},
	{0x04DA, 0x04DB,},
	{0x04DC, 0x04DD,},
	{0x04DE, 0x04DF,},
	{0x04E0, 0x04E1,},
	{0x04E2, 0x04E3,},
	{0x04E4, 0x04E5,},
	{0x04E6, 0x04E7,},
	{0x04E8, 0x04E9,},
	{0x04EA, 0x04EB,},
	{0x04EC, 0x04ED,},
	{0x04EE, 0x04EF,},
	{0x04F0, 0x04F1,},
	{0x04F2, 0x04F3,},
	{0x04F4, 0x04F5,},
	{0x04F6, 0x04F7,},
	{0x04F8, 0x04F9,},
	{0x04FA, 0x04FB,},
	{0x04FC, 0x04FD,},
	{0x04FE, 0x04FF,},
	{0x0500, 0x0501,},
	{0x0502, 0x0503,},
	{0x0504, 0x0505,},
	{0x0506, 0x0507,},
	{0x0508, 0x0509,},
	{0x050A, 0x050B,},
	{0x050C, 0x050D,},
	{0x050E, 0x050F,},
	{0x0510, 0x0511,},
	{0x0512, 0x0513,},
	{0x0514, 0x0515,},
	{0x0516, 0x0517,},
	{0x0518, 0x0519,},
	{0x051A, 0x051B,},
	{0x051C, 0x051D,},
	{0x051E, 0x051F,},
	{0x0520, 0x0521,},
	{0x0522, 0x0523,},
	{0x0524, 0x0525,},
	{0x0526, 0x0527,},
	{0x0528, 0x0529,},
	{0x052A, 0x052B,},
	{0x052C, 0x052D,},
	{0x052E, 0x052F,},
	{0x0531, 0x0561,},
	{0x0532, 0x0562,},
	{0x0533, 0x0563,},
	{0x0534, 0x0564,},
	{0x0535, 0x0565,},
	{0x0536, 0x0566,},
	{0x0537, 0x0567,},
	{0x0538, 0x0568,},
	{0x0539, 0x0569,},
	{0x053A, 0x056A,},
	{0x053B, 0x056B,},
	{0x053C, 0x056C,},
	{0x053D, 0x056D,},
	{0x053E, 0x056E,},
	{0x053F, 0x056F,},
	{0x0540, 0x0570,},
	{0x0541, 0x0571,},
	{0x0542, 0x0572,},
	{0x0543, 0x0573,},
	{0x0544, 0x0574,},
	{0x0545, 0x0575,},
	{0x0546, 0x0576,},
	{0x0547, 0x0577,},
	{0x0548, 0x0578,},
	{0x0549, 0x0579,},
	{0x054A, 0x057A,},
	{0x054B, 0x057B,},
	{0x054C, 0x057C,},
	{0x054D, 0x057D,},
	{0x054E, 0x057E,},
	{0x054F, 0x057F,},
	{0x0550, 0x0580,},
	{0x0551, 0x0581,},
	{0x0552, 0x0582,},
	{0x0553, 0x0583,},
	{0x0554, 0x0584,},
	{0x0555, 0x0585,},
	{0x0556, 0x0586,},
	{0x10A0, 0x2D00,},
	{0x10A1, 0x2D01,},
	{0x10A2, 0x2D02,},
	{0x10A3, 0x2D03,},
	{0x10A4, 0x2D04,},
	{0x10A5, 0x2D05,},
	{0x10A6, 0x2D06,},
	{0x10A7, 0x2D07,},
	{0x10A8, 0x2D08,},
	{0x10A9, 0x2D09,},
	{0x10AA, 0x2D0A,},
	{0x10AB, 0x2D0B,},
	{0x10AC, 0x2D0C,},
	{0x10AD, 0x2D0D,},
	{0x10AE, 0x2D0E,},
	{0x10AF, 0x2D0F,},
	{0x10B0, 0x2D10,},
	{0x10B1, 0x2D11,},
	{0x10B2, 0x2D12,},
	{0x10B3, 0x2D13,},
	{0x10B4, 0x2D14,},
	{0x10B5, 0x2D15,},
	{0x10B6, 0x2D16,},
	{0x10B7, 0x2D17,},
	{0x10B8, 0x2D18,},
	{0x10B9, 0x2D19,},
	{0x10BA, 0x2D1A,},
	{0x10BB, 0x2D1B,},
	{0x10BC, 0x2D1C,},
	{0x10BD, 0x2D1D,},
	{0x10BE, 0x2D1E,},
	{0x10BF, 0x2D1F,},
	{0x10C0, 0x2D20,},
	{0x10C1, 0x2D21,},
	{0x10C2, 0x2D22,},
	{0x10C3, 0x2D23,},
	{0x10C4, 0x2D24,},
	{0x10C5, 0x2D25,},
	{0x10C7, 0x2D27,},
	{0x10CD, 0x2D2D,},
	{0x13A0, 0xAB70,},
	{0x13A1, 0xAB71,},
	{0x13A2, 0xAB72,},
	{0x13A3, 0xAB73,},
	{0x13A4, 0xAB74,},
	{0x13A5, 0xAB75,},
	{0x13A6, 0xAB76,},
	{0x13A7, 0xAB77,},
	{0x13A8, 0xAB78,},
	{0x13A9, 0xAB79,},
	{0x13AA, 0xAB7A,},
	{0x13AB, 0xAB7B,},
	{0x13AC, 0xAB7C,},
	{0x13AD, 0xAB7D,},
	{0x13AE, 0xAB7E,},
	{0x13AF, 0xAB7F,},
	{0x13B0, 0xAB80,},
	{0x13B1, 0xAB81,},
	{0x13B2, 0xAB82,},
	{0x13B3, 0xAB83,},
	{0x13B4, 0xAB84,},
	{0x13B5, 0xAB85,},
	{0x13B6, 0xAB86,},
	{0x13B7, 0xAB87,},
	{0x13B8, 0xAB88,},
	{0x13B9, 0xAB89,},
	{0x13BA, 0xAB8A,},
	{0x13BB, 0xAB8B,},
	{0x13BC, 0xAB8C,},
	{0x13BD, 0xAB8D,},
	{0x13BE, 0xAB8E,},
	{0x13BF, 0xAB8F,},
	{0x13C0, 0xAB90,},
	{0x13C1, 0xAB91,},
	{0x13C2, 0xAB92,},
	{0x13C3, 0xAB93,},
	{0x13C4, 0xAB94,},
	{0x13C5, 0xAB95,},
	{0x13C6, 0xAB96,},
	{0x13C7, 0xAB97,},
	{0x13C8, 0xAB98,},
	{0x13C9, 0xAB99,},
	{0x13CA, 0xAB9A,},
	{0x13CB, 0xAB9B,},
	{0x13CC, 0xAB9C,},
	{0x13CD, 0xAB9D,},
	{0x13CE, 0xAB9E,},
	{0x13CF, 0xAB9F,},
	{0x13D0, 0xABA0,},
	{0x13D1, 0xABA1,},
	{0x13D2, 0xABA2,},
	{0x13D3, 0xABA3,},
	{0x13D4, 0xABA4,},
	{0x13D5, 0xABA5,},
	{0x13D6, 0xABA6,},
	{0x13D7, 0xABA7,},
	{0x13D8, 0xABA8,},
	{0x13D9, 0xABA9,},
	{0x13DA, 0xABAA,},
	{0x13DB, 0xABAB,},
	{0x13DC, 0xABAC,},
	{0x13DD, 0xABAD,},
	{0x13DE, 0xABAE,},
	{0x13DF, 0xABAF,},
	{0x13E0, 0xABB0,},
	{0x13E1, 0xABB1,},
	{0x13E2, 0xABB2,},
	{0x13E3, 0xABB3,},
	{0x13E4, 0xABB4,},
	{0x13E5, 0xABB5,},
	{0x13E6, 0xABB6,},
	{0x13E7, 0xABB7,},
	{0x13E8, 0xABB8,},
	{0x13E9, 0xABB9,},
	{0x13EA, 0xABBA,},
	{0x13EB, 0xABBB,},
	{0x13EC, 0xABBC,},
	{0x13ED, 0xABBD,},
	{0x13EE, 0xABBE,},
	{0x13EF, 0xABBF,},
	{0x13F0, 0x13F8,},
	{0x13F1, 0x13F9,},
	{0x13F2, 0x13FA,},
	{0x13F3, 0x13FB,},
	{0x13F4, 0x13FC,},
	{0x13F5, 0x13FD,},
	{0x1C90, 0x10D0,},
	{0x1C91, 0x10D1,},
	{0x1C92, 0x10D2,},
	{0x1C93, 0x10D3,},
	{0x1C94, 0x10D4,},
	{0x1C95, 0x10D5,},
	{0x1C96, 0x10D6,},
	{0x1C97, 0x10D7,},
	{0x1C98, 0x10D8,},
	{0x1C99, 0x10D9,},
	{0x1C9A, 0x10DA,},
	{0x1C9B, 0x10DB,},
	{0x1C9C, 0x10DC,},
	{0x1C9D, 0x10DD,},
	{0x1C9E, 0x10DE,},
	{0x1C9F, 0x10DF,},
	{0x1CA0, 0x10E0,},
	{0x1CA1, 0x10E1,},
	{0x1CA2, 0x10E2,},
	{0x1CA3, 0x10E3,},
	{0x1CA4, 0x10E4,},
	{0x1CA5, 0x10E5,},
	{0x1CA6, 0x10E6,},
	{0x1CA7, 0x10E7,},
	{0x1CA8, 0x10E8,},
	{0x1CA9, 0x10E9,},
	{0x1CAA, 0x10EA,},
	{0x1CAB, 0x10EB,},
	{0x1CAC, 0x10EC,},
	{0x1CAD, 0x10ED,},
	{0x1CAE, 0x10EE,},
	{0x1CAF, 0x10EF,},
	{0x1CB0, 0x10F0,},
	{0x1CB1, 0x10F1,},
	{0x1CB2, 0x10F2,},
	{0x1CB3, 0x10F3,},
	{0x1CB4, 0x10F4,},
	{0x1CB5, 0x10F5,},
	{0x1CB6, 0x10F6,},
	{0x1CB7, 0x10F7,},
	{0x1CB8, 0x10F8,},
	{0x1CB9, 0x10F9,},
	{0x1CBA, 0x10FA,},
	{0x1CBD, 0x10FD,},
	{0x1CBE, 0x10FE,},
	{0x1CBF, 0x10FF,},
	{0x1E00, 0x1E01,},
	{0x1E02, 0x1E03,},
	{0x1E04, 0x1E05,},
	{0x1E06, 0x1E07,},
	{0x1E08, 0x1E09,},
	{0x1E0A, 0x1E0B,},
	{0x1E0C, 0x1E0D,},
	{0x1E0E, 0x1E0F,},
	{0x1E10, 0x1E11,},
	{0x1E12, 0x1E13,},
	{0x1E14, 0x1E15,},
	{0x1E16, 0x1E17,},
	{0x1E18, 0x1E19,},
	{0x1E1A, 0x1E1B,},
	{0x1E1C, 0x1E1D,},
	{0x1E1E, 0x1E1F,},
	{0x1E20, 0x1E21,},
	{0x1E22, 0x1E23,},
	{0x1E24, 0x1E25,},
	{0x1E26, 0x1E27,},
	{0x1E28, 0x1E29,},
	{0x1E2A, 0x1E2B,},
	{0x1E2C, 0x1E2D,},
	{0x1E2E, 0x1E2F,},
	{0x1E30, 0x1E31,},
	{0x1E32, 0x1E33,},
	{0x1E34, 0x1E35,},
	{0x1E36, 0x1E37,},
	{0x1E38, 0x1E39,},
	{0x1E3A, 0x1E3B,},
	{0x1E3C, 0x1E3D,},
	{0x1E3E, 0x1E3F,},
	{0x1E40, 0x1E41,},
	{0x1E42, 0x1E43,},
	{0x1E44, 0x1E45,},
	{0x1E46, 0x1E47,},
	{0x1E48, 0x1E49,},
	{0x1E4A, 0x1E4B,},
	{0x1E4C, 0x1E4D,},
	{0x1E4E, 0x1E4F,},
	{0x1E50, 0x1E51,},
	{0x1E52, 0x1E53,},
	{0x1E54, 0x1E55,},
	{0x1E56, 0x1E57,},
	{0x1E58, 0x1E59,},
	{0x1E5A, 0x1E5B,},
	{0x1E5C, 0x1E5D,},
	{0x1E5E, 0x1E5F,},
	{0x1E60, 0x1E61,},
	{0x1E62, 0x1E63,},
	{0x1E64, 0x1E65,},
	{0x1E66, 0x1E67,},
	{0x1E68, 0x1E69,},
	{0x1E6A, 0x1E6B,},
	{0x1E6C, 0x1E6D,},
	{0x1E6E, 0x1E6F,},
	{0x1E70, 0x1E71,},
	{0x1E72, 0x1E73,},
	{0x1E74, 0x1E75,},
	{0x1E76, 0x1E77,},
	{0x1E78, 0x1E79,},
	{0x1E7A, 0x1E7B,},
	{0x1E7C, 0x1E7D,},
	{0x1E7E, 0x1E7F,},
	{0x1E80, 0x1E81,},
	{0x1E82, 0x1E83,},
	{0x1E84, 0x1E85,},
	{0x1E86, 0x1E87,},
	{0x1E88, 0x1E89,},
	{0x1E8A, 0x1E8B,},
	{0x1E8C, 0x1E8D,},
	{0x1E8E, 0x1E8F,},
	{0x1E90, 0x1E91,},
	{0x1E92, 0x1E93,},
	{0x1E94, 0x1E95,},
	{0x1E9E, 0x00DF,},
	{0x1EA0, 0x1EA1,},
	{0x1EA2, 0x1EA3,},
	{0x1EA4, 0x1EA5,},
	{0x1EA6, 0x1EA7,},
	{0x1EA8, 0x1EA9,},
	{0x1EAA, 0x1EAB,},
	{0x1EAC, 0x1EAD,},
	{0x1EAE, 0x1EAF,},
	{0x1EB0, 0x1EB1,},
	{0x1EB2, 0x1EB3,},
	{0x1EB4, 0x1EB5,},
	{0x1EB6, 0x1EB7,},
	{0x1EB8, 0x1EB9,},
	{0x1EBA, 0x1EBB,},
	{0x1EBC, 0x1EBD,},
	{0x1EBE, 0x1EBF,},
	{0x1EC0, 0x1EC1,},
	{0x1EC2, 0x1EC3,},
	{0x1EC4, 0x1EC5,},
	{0x1EC6, 0x1EC7,},
	{0x1EC8, 0x1EC9,},
	{0x1ECA, 0x1ECB,},
	{0x1ECC, 0x1ECD,},
	{0x1ECE, 0x1ECF,},
	{0x1ED0, 0x1ED1,},
	{0x1ED2, 0x1ED3,},
	{0x1ED4, 0x1ED5,},
	{0x1ED6, 0x1ED7,},
	{0x1ED8, 0x1ED9,},
	{0x1EDA, 0x1EDB,},
	{0x1EDC, 0x1EDD,},
	{0x1EDE, 0x1EDF,},
	{0x1EE0, 0x1EE1,},
	{0x1EE2, 0x1EE3,},
	{0x1EE4, 0x1EE5,},
	{0x1EE6, 0x1EE7,},
	{0x1EE8, 0x1EE9,},
	{0x1EEA, 0x1EEB,},
	{0x1EEC, 0x1EED,},
	{0x1EEE, 0x1EEF,},
	{0x1EF0, 0x1EF1,},
	{0x1EF2, 0x1EF3,},
	{0x1EF4, 0x1EF5,},
	{0x1EF6, 0x1EF7,},
	{0x1EF8, 0x1EF9,},
	{0x1EFA, 0x1EFB,},
	{0x1EFC, 0x1EFD,},
	{0x1EFE, 0x1EFF,},
	{0x1F08, 0x1F00,},
	{0x1F09, 0x1F01,},
	{0x1F0A, 0x1F02,},
	{0x1F0B, 0x1F03,},
	{0x1F0C, 0x1F04,},
	{0x1F0D, 0x1F05,},
	{0x1F0E, 0x1F06,},
	{0x1F0F, 0x1F07,},
	{0x1F18, 0x1F10,},
	{0x1F19, 0x1F11,},
	{0x1F1A, 0x1F12,},
	{0x1F1B, 0x1F13,},
	{0x1F1C, 0x1F14,},
	{0x1F1D, 0x1F15,},
	{0x1F28, 0x1F20,},
	{0x1F29, 0x1F21,},
	{0x1F2A, 0x1F22,},
	{0x1F2B, 0x1F23,},
	{0x1F2C, 0x1F24,},
	{0x1F2D, 0x1F25,},
	{0x1F2E, 0x1F26,},
	{0x1F2F, 0x1F27,},
	{0x1F38, 0x1F30,},
	{0x1F39, 0x1F31,},
	{0x1F3A, 0x1F32,},
	{0x1F3B, 0x1F33,},
	{0x1F3C, 0x1F34,},
	{0x1F3D, 0x1F35,},
	{0x1F3E, 0x1F36,},
	{0x1F3F, 0x1F37,},
	{0x1F48, 0x1F40,},
	{0x1F49, 0x1F41,},
	{0x1F4A, 0x1F42,},
	{0x1F4B, 0x1F43,},
	{0x1F4C, 0x1F44,},
	{0x1F4D, 0x1F45,},
	{0x1F59, 0x1F51,},
	{0x1F5B, 0x1F53,},
	{0x1F5D, 0x1F55,},
	{0x1F5F, 0x1F57,},
	{0x1F68, 0x1F60,},
	{0x1F69, 0x1F61,},
	{0x1F6A, 0x1F62,},
	{0x1F6B, 0x1F63,},
	{0x1F6C, 0x1F64,},
	{0x1F6D, 0x1F65,},
	{0x1F6E, 0x1F66,},
	{0x1F6F, 0x1F67,},
	{0x1F88, 0x1F80,},
	{0x1F89, 0x1F81,},
	{0x1F8A, 0x1F82,},
	{0x1F8B, 0x1F83,},
	{0x1F8C, 0x1F84,},
	{0x1F8D, 0x1F85,},
	{0x1F8E, 0x1F86,},
	{0x1F8F, 0x1F87,},
	{0x1F98, 0x1F90,},
	{0x1F99, 0x1F91,},
	{0x1F9A, 0x1F92,},
	{0x1F9B, 0x1F93,},
	{0x1F9C, 0x1F94,},
	{0x1F9D, 0x1F95,},
	{0x1F9E, 0x1F96,},
	{0x1F9F, 0x1F97,},
	{0x1FA8, 0x1FA0,},
	{0x1FA9, 0x1FA1,},
	{0x1FAA, 0x1FA2,},
	{0x1FAB, 0x1FA3,},
	{0x1FAC, 0x1FA4,},
	{0x1FAD, 0x1FA5,},
	{0x1FAE, 0x1FA6,},
	{0x1FAF, 0x1FA7,},
	{0x1FB8, 0x1FB0,},
	{0x1FB9, 0x1FB1,},
	{0x1FBA, 0x1F70,},
	{0x1FBB, 0x1F71,},
	{0x1FBC, 0x1FB3,},
	{0x1FC8, 0x1F72,},
	{0x1FC9, 0x1F73,},
	{0x1FCA, 0x1F74,},
	{0x1FCB, 0x1F75,},
	{0x1FCC, 0x1FC3,},
	{0x1FD8, 0x1FD0,},
	{0x1FD9, 0x1FD1,},
	{0x1FDA, 0x1F76,},
	{0x1FDB, 0x1F77,},
	{0x1FE8, 0x1FE0,},
	{0x1FE9, 0x1FE1,},
	{0x1FEA, 0x1F7A,},
	{0x1FEB, 0x1F7B,},
	{0x1FEC, 0x1FE5,},
	{0x1FF8, 0x1F78,},
	{0x1FF9, 0x1F79,},
	{0x1FFA, 0x1F7C,},
	{0x1FFB, 0x1F7D,},
	{0x1FFC, 0x1FF3,},
	{0x2126, 0x03C9,},
	{0x212A, 0x006B,},
	{0x212B, 0x00E5,},
	{0x2132, 0x214E,},
	{0x2160, 0x2170,},
	{0x2161, 0x2171,},
	{0x2162, 0x2172,},
	{0x2163, 0x2173,},
	{0x2164, 0x2174,},
	{0x2165, 0x2175,},
	{0x2166, 0x2176,},
	{0x2167, 0x2177,},
	{0x2168, 0x2178,},
	{0x2169, 0x2179,},
	{0x216A, 0x217A,},
	{0x216B, 0x217B,},
	{0x216C, 0x217C,},
	{0x216D, 0x217D,},
	{0x216E, 0x217E,},
	{0x216F, 0x217F,},
	{0x2183, 0x2184,},
	{0x24B6, 0x24D0,},
	{0x24B7, 0x24D1,},
	{0x24B8, 0x24D2,},
	{0x24B9, 0x24D3,},
	{0x24BA, 0x24D4,},
	{0x24BB, 0x24D5,},
	{0x24BC, 0x24D6,},
	{0x24BD, 0x24D7,},
	{0x24BE, 0x24D8,},
	{0x24BF, 0x24D9,},
	{0x24C0, 0x24DA,},
	{0x24C1, 0x24DB,},
	{0x24C2, 0x24DC,},
	{0x24C3, 0x24DD,},
	{0x24C4, 0x24DE,},
	{0x24C5, 0x24DF,},
	{0x24C6, 0x24E0,},
	{0x24C7, 0x24E1,},
	{0x24C8, 0x24E2,},
	{0x24C9, 0x24E3,},
	{0x24CA, 0x24E4,},
	{0x24CB, 0x24E5,},
	{0x24CC, 0x24E6,},
	{0x24CD, 0x24E7,},
	{0x24CE, 0x24E8,},
	{0x24CF, 0x24E9,},
	{0x2C00, 0x2C30,},
	{0x2C01, 0x2C31,},
	{0x2C02, 0x2C32,},
	{0x2C03, 0x2C33,},
	{0x2C04, 0x2C34,},
	{0x2C05, 0x2C35,},
	{0x2C06, 0x2C36,},
	{0x2C07, 0x2C37,},
	{0x2C08, 0x2C38,},
	{0x2C09, 0x2C39,},
	{0x2C0A, 0x2C3A,},
	{0x2C0B, 0x2C3B,},
	{0x2C0C, 0x2C3C,},
	{0x2C0D, 0x2C3D,},
	{0x2C0E, 0x2C3E,},
	{0x2C0F, 0x2C3F,},
	{0x2C10, 0x2C40,},
	{0x2C11, 0x2C41,},
	{0x2C12, 0x2C42,},
	{0x2C13, 0x2C43,},
	{0x2C14, 0x2C44,},
	{0x2C15, 0x2C45,},
	{0x2C16, 0x2C46,},
	{0x2C17, 0x2C47,},
	{0x2C18, 0x2C48,},
	{0x2C19, 0x2C49,},
	{0x2C1A, 0x2C4A,},
	{0x2C1B, 0x2C4B,},
	{0x2C1C, 0x2C4C,},
	{0x2C1D, 0x2C4D,},
	{0x2C1E, 0x2C4E,},
	{0x2C1F, 0x2C4F,},
	{0x2C20, 0x2C50,},
	{0x2C21, 0x2C51,},
	{0x2C22, 0x2C52,},
	{0x2C23, 0x2C53,},
	{0x2C24, 0x2C54,},
	{0x2C25, 0x2C55,},
	{0x2C26, 0x2C56,},
	{0x2C27, 0x2C57,},
	{0x2C28, 0x2C58,},
	{0x2C29, 0x2C59,},
	{0x2C2A, 0x2C5A,},
	{0x2C2B, 0x2C5B,},
	{0x2C2C, 0x2C5C,},
	{0x2C2D, 0x2C5D,},
	{0x2C2E, 0x2C5E,},
	{0x2C2F, 0x2C5F,},
	{0x2C60, 0x2C61,},
	{0x2C62, 0x026B,},
	{0x2C63, 0x1D7D,},
	{0x2C64, 0x027D,},
	{0x2C67, 0x2C68,},
	{0x2C69, 0x2C6A,},
	{0x2C6B, 0x2C6C,},
	{0x2C6D, 0x0251,},
	{0x2C6E, 0x0271,},
	{0x2C6F, 0x0250,},
	{0x2C70, 0x0252,},
	{0x2C72, 0x2C73,},
	{0x2C75, 0x2C76,},
	{0x2C7E, 0x023F,},
	{0x2C7F, 0x0240,},
	{0x2C80, 0x2C81,},
	{0x2C82, 0x2C83,},
	{0x2C84, 0x2C85,},
	{0x2C86, 0x2C87,},
	{0x2C88, 0x2C89,},
	{0x2C8A, 0x2C8B,},
	{0x2C8C, 0x2C8D,},
	{0x2C8E, 0x2C8F,},
	{0x2C90, 0x2C91,},
	{0x2C92, 0x2C93,},
	{0x2C94, 0x2C95,},
	{0x2C96, 0x2C97,},
	{0x2C98, 0x2C99,},
	{0x2C9A, 0x2C9B,},
	{0x2C9C, 0x2C9D,},
	{0x2C9E, 0x2C9F,},
	{0x2CA0, 0x2CA1,},
	{0x2CA2, 0x2CA3,},
	{0x2CA4, 0x2CA5,},
	{0x2CA6, 0x2CA7,},
	{0x2CA8, 0x2CA9,},
	{0x2CAA, 0x2CAB,},
	{0x2CAC, 0x2CAD,},
	{0x2CAE, 0x2CAF,},
	{0x2CB0, 0x2CB1,},
	{0x2CB2, 0x2CB3,},
	{0x2CB4, 0x2CB5,},
	{0x2CB6, 0x2CB7,},
	{0x2CB8, 0x2CB9,},
	{0x2CBA, 0x2CBB,},
	{0x2CBC, 0x2CBD,},
	{0x2CBE, 0x2CBF,},
	{0x2CC0, 0x2CC1,},
	{0x2CC2, 0x2CC3,},
	{0x2CC4, 0x2CC5,},
	{0x2CC6, 0x2CC7,},
	{0x2CC8, 0x2CC9,},
	{0x2CCA, 0x2CCB,},
	{0x2CCC, 0x2CCD,},
	{0x2CCE, 0x2CCF,},
	{0x2CD0, 0x2CD1,},
	{0x2CD2, 0x2CD3,},
	{0x2CD4, 0x2CD5,},
	{0x2CD6, 0x2CD7,},
	{0x2CD8, 0x2CD9,},
	{0x2CDA, 0x2CDB,},
	{0x2CDC, 0x2CDD,},
	{0x2CDE, 0x2CDF,},
	{0x2CE0, 0x2CE1,},
	{0x2CE2, 0x2CE3,},
	{0x2CEB, 0x2CEC,},
	{0x2CED, 0x2CEE,},
	{0x2CF2, 0x2CF3,},
	{0xA640, 0xA641,},
	{0xA642, 0xA643,},
	{0xA644, 0xA645,},
	{0xA646, 0xA647,},
	{0xA648, 0xA649,},
	{0xA64A, 0xA64B,},
	{0xA64C, 0xA64D,},
	{0xA64E, 0xA64F,},
	{0xA650, 0xA651,},
	{0xA652, 0xA653,},
	{0xA654, 0xA655,},
	{0xA656, 0xA657,},
	{0xA658, 0xA659,},
	{0xA65A, 0xA65B,},
	{0xA65C, 0xA65D,},
	{0xA65E, 0xA65F,},
	{0xA660, 0xA661,},
	{0xA662, 0xA663,},
	{0xA664, 0xA665,},
	{0xA666, 0xA667,},
	{0xA668, 0xA669,},
	{0xA66A, 0xA66B,},
	{0xA66C, 0xA66D,},
	{0xA680, 0xA681,},
	{0xA682, 0xA683,},
	{0xA684, 0xA685,},
	{0xA686, 0xA687,},
	{0xA688, 0xA689,},
	{0xA68A, 0xA68B,},
	{0xA68C, 0xA68D,},
	{0xA68E, 0xA68F,},
	{0xA690, 0xA691,},
	{0xA692, 0xA693,},
	{0xA694, 0xA695,},
	{0xA696, 0xA697,},
	{0xA698, 0xA699,},
	{0xA69A, 0xA69B,},
	{0xA722, 0xA723,},
	{0xA724, 0xA725,},
	{0xA726, 0xA727,},
	{0xA728, 0xA729,},
	{0xA72A, 0xA72B,},
	{0xA72C, 0xA72D,},
	{0xA72E, 0xA72F,},
	{0xA732, 0xA733,},
	{0xA734, 0xA735,},
	{0xA736, 0xA737,},
	{0xA738, 0xA739,},
	{0xA73A, 0xA73B,},
	{0xA73C, 0xA73D,},
	{0xA73E, 0xA73F,},
	{0xA740, 0xA741,},
	{0xA742, 0xA743,},
	{0xA744, 0xA745,},
	{0xA746, 0xA747,},
	{0xA748, 0xA749,},
	{0xA74A, 0xA74B,},
	{0xA74C, 0xA74D,},
	{0xA74E, 0xA74F,},
	{0xA750, 0xA751,},
	{0xA752, 0xA753,},
	{0xA754, 0xA755,},
	{0xA756, 0xA757,},
	{0xA758, 0xA759,},
	{0xA75A, 0xA75B,},
	{0xA75C, 0xA75D,},
	{0xA75E, 0xA75F,},
	{0xA760, 0xA761,},
	{0xA762, 0xA763,},
	{0xA764, 0xA765,},
	{0xA766, 0xA767,},
	{0xA768, 0xA769,},
	{0xA76A, 0xA76B,},
	{0xA76C, 0xA76D,},
	{0xA76E, 0xA76F,},
	{0xA779, 0xA77A,},
	{0xA77B, 0xA77C,},
	{0xA77D, 0x1D79,},
	{0xA77E, 0xA77F,},
	{0xA780, 0xA781,},
	{0xA782, 0xA783,},
	{0xA784, 0xA785,},
	{0xA786, 0xA787,},
	{0xA78B, 0xA78C,},
	{0xA78D, 0x0265,},
	{0xA790, 0xA791,},
	{0xA792, 0xA793,},
	{0xA796, 0xA797,},
	{0xA798, 0xA799,},
	{0xA79A, 0xA79B,},
	{0xA79C, 0xA79D,},
	{0xA79E, 0xA79F,},
	{0xA7A0, 0xA7A1,},
	{0xA7A2, 0xA7A3,},
	{0xA7A4, 0xA7A5,},
	{0xA7A6, 0xA7A7,},
	{0xA7A8, 0xA7A9,},
	{0xA7AA, 0x0266,},
	{0xA7AB, 0x025C,},
	{0xA7AC, 0x0261,},
	{0xA7AD, 0x026C,},
	{0xA7AE, 0x026A,},
	{0xA7B0, 0x029E,},
	{0xA7B1, 0x0287,},
	{0xA7B2, 0x029D,},
	{0xA7B3, 0xAB53,},
	{0xA7B4, 0xA7B5,},
	{0xA7B6, 0xA7B7,},
	{0xA7B8, 0xA7B9,},
	{0xA7BA, 0xA7BB,},
	{0xA7BC, 0xA7BD,},
	{0xA7BE, 0xA7BF,},
	{0xA7C0, 0xA7C1,},
	{0xA7C2, 0xA7C3,},
	{0xA7C4, 0xA794,},
	{0xA7C5, 0x0282,},
	{0xA7C6, 0x1D8E,},
	{0xA7C7, 0xA7C8,},
	{0xA7C9, 0xA7CA,},
	{0xA7D0, 0xA7D1,},
	{0xA7D6, 0xA7D7,},
	{0xA7D8, 0xA7D9,},
	{0xA7F5, 0xA7F6,},
	{0xFF21, 0xFF41,},
	{0xFF22, 0xFF42,},
	{0xFF23, 0xFF43,},
	{0xFF24, 0xFF44,},
	{0xFF25, 0xFF45,},
	{0xFF26, 0xFF46,},
	{0xFF27, 0xFF47,},
	{0xFF28, 0xFF48,},
	{0xFF29, 0xFF49,},
	{0xFF2A, 0xFF4A,},
	{0xFF2B, 0xFF4B,},
	{0xFF2C, 0xFF4C,},
	{0xFF2D, 0xFF4D,},
	{0xFF2E, 0xFF4E,},
	{0xFF2F, 0xFF4F,},
	{0xFF30, 0xFF50,},
	{0xFF31, 0xFF51,},
	{0xFF32, 0xFF52,},
	{0xFF33, 0xFF53,},
	{0xFF34, 0xFF54,},
	{0xFF35, 0xFF55,},
	{0xFF36, 0xFF56,},
	{0xFF37, 0xFF57,},
	{0xFF38, 0xFF58,},
	{0xFF39, 0xFF59,},
	{0xFF3A, 0xFF5A,},
	{0x10400, 0x10428,},
	{0x10401, 0x10429,},
	{0x10402, 0x1042A,},
	{0x10403, 0x1042B,},
	{0x10404, 0x1042C,},
	{0x10405, 0x1042D,},
	{0x10406, 0x1042E,},
	{0x10407, 0x1042F,},
	{0x10408, 0x10430,},
	{0x10409, 0x10431,},
	{0x1040A, 0x10432,},
	{0x1040B, 0x10433,},
	{0x1040C, 0x10434,},
	{0x1040D, 0x10435,},
	{0x1040E, 0x10436,},
	{0x1040F, 0x10437,},
	{0x10410, 0x10438,},
	{0x10411, 0x10439,},
	{0x10412, 0x1043A,},
	{0x10413, 0x1043B,},
	{0x10414, 0x1043C,},
	{0x10415, 0x1043D,},
	{0x10416, 0x1043E,},
	{0x10417, 0x1043F,},
	{0x10418, 0x10440,},
	{0x10419, 0x10441,},
	{0x1041A, 0x10442,},
	{0x1041B, 0x10443,},
	{0x1041C, 0x10444,},
	{0x1041D, 0x10445,},
	{0x1041E, 0x10446,},
	{0x1041F, 0x10447,},
	{0x10420, 0x10448,},
	{0x10421, 0x10449,},
	{0x10422, 0x1044A,},
	{0x10423, 0x1044B,},
	{0x10424, 0x1044C,},
	{0x10425, 0x1044D,},
	{0x10426, 0x1044E,},
	{0x10427, 0x1044F,},
	{0x104B0, 0x104D8,},
	{0x104B1, 0x104D9,},
	{0x104B2, 0x104DA,},
	{0x104B3, 0x104DB,},
	{0x104B4, 0x104DC,},
	{0x104B5, 0x104DD,},
	{0x104B6, 0x104DE,},
	{0x104B7, 0x104DF,},
	{0x104B8, 0x104E0,},
	{0x104B9, 0x104E1,},
	{0x104BA, 0x104E2,},
	{0x104BB, 0x104E3,},
	{0x104BC, 0x104E4,},
	{0x104BD, 0x104E5,},
	{0x104BE, 0x104E6,},
	{0x104BF, 0x104E7,},
	{0x104C0, 0x104E8,},
	{0x104C1, 0x104E9,},
	{0x104C2, 0x104EA,},
	{0x104C3, 0x104EB,},
	{0x104C4, 0x104EC,},
	{0x104C5, 0x104ED,},
	{0x104C6, 0x104EE,},
	{0x104C7, 0x104EF,},
	{0x104C8, 0x104F0,},
	{0x104C9, 0x104F1,},
	{0x104CA, 0x104F2,},
	{0x104CB, 0x104F3,},
	{0x104CC, 0x104F4,},
	{0x104CD, 0x104F5,},
	{0x104CE, 0x104F6,},
	{0x104CF, 0x104F7,},
	{0x104D0, 0x104F8,},
	{0x104D1, 0x104F9,},
	{0x104D2, 0x104FA,},
	{0x104D3, 0x104FB,},
	{0x10570, 0x10597,},
	{0x10571, 0x10598,},
	{0x10572, 0x10599,},
	{0x10573, 0x1059A,},
	{0x10574, 0x1059B,},
	{0x10575, 0x1059C,},
	{0x10576, 0x1059D,},
	{0x10577, 0x1059E,},
	{0x10578, 0x1059F,},
	{0x10579, 0x105A0,},
	{0x1057A, 0x105A1,},
	{0x1057C, 0x105A3,},
	{0x1057D, 0x105A4,},
	{0x1057E, 0x105A5,},
	{0x1057F, 0x105A6,},
	{0x10580, 0x105A7,},
	{0x10581, 0x105A8,},
	{0x10582, 0x105A9,},
	{0x10583, 0x105AA,},
	{0x10584, 0x105AB,},
	{0x10585, 0x105AC,},
	{0x10586, 0x105AD,},
	{0x10587, 0x105AE,},
	{0x10588, 0x105AF,},
	{0x10589, 0x105B0,},
	{0x1058A, 0x105B1,},
	{0x1058C, 0x105B3,},
	{0x1058D, 0x105B4,},
	{0x1058E, 0x105B5,},
	{0x1058F, 0x105B6,},
	{0x10590, 0x105B7,},
	{0x10591, 0x105B8,},
	{0x10592, 0x105B9,},
	{0x10594, 0x105BB,},
	{0x10595, 0x105BC,},
	{0x10C80, 0x10CC0,},
	{0x10C81, 0x10CC1,},
	{0x10C82, 0x10CC2,},
	{0x10C83, 0x10CC3,},
	{0x10C84, 0x10CC4,},
	{0x10C85, 0x10CC5,},
	{0x10C86, 0x10CC6,},
	{0x10C87, 0x10CC7,},
	{0x10C88, 0x10CC8,},
	{0x10C89, 0x10CC9,},
	{0x10C8A, 0x10CCA,},
	{0x10C8B, 0x10CCB,},
	{0x10C8C, 0x10CCC,},
	{0x10C8D, 0x10CCD,},
	{0x10C8E, 0x10CCE,},
	{0x10C8F, 0x10CCF,},
	{0x10C90, 0x10CD0,},
	{0x10C91, 0x10CD1,},
	{0x10C92, 0x10CD2,},
	{0x10C93, 0x10CD3,},
	{0x10C94, 0x10CD4,},
	{0x10C95, 0x10CD5,},
	{0x10C96, 0x10CD6,},
	{0x10C97, 0x10CD7,},
	{0x10C98, 0x10CD8,},
	{0x10C99, 0x10CD9,},
	{0x10C9A, 0x10CDA,},
	{0x10C9B, 0x10CDB,},
	{0x10C9C, 0x10CDC,},
	{0x10C9D, 0x10CDD,},
	{0x10C9E, 0x10CDE,},
	{0x10C9F, 0x10CDF,},
	{0x10CA0, 0x10CE0,},
	{0x10CA1, 0x10CE1,},
	{0x10CA2, 0x10CE2,},
	{0x10CA3, 0x10CE3,},
	{0x10CA4, 0x10CE4,},
	{0x10CA5, 0x10CE5,},
	{0x10CA6, 0x10CE6,},
	{0x10CA7, 0x10CE7,},
	{0x10CA8, 0x10CE8,},
	{0x10CA9, 0x10CE9,},
	{0x10CAA, 0x10CEA,},
	{0x10CAB, 0x10CEB,},
	{0x10CAC, 0x10CEC,},
	{0x10CAD, 0x10CED,},
	{0x10CAE, 0x10CEE,},
	{0x10CAF, 0x10CEF,},
	{0x10CB0, 0x10CF0,},
	{0x10CB1, 0x10CF1,},
	{0x10CB2, 0x10CF2,},
	{0x118A0, 0x118C0,},
	{0x118A1, 0x118C1,},
	{0x118A2, 0x118C2,},
	{0x118A3, 0x118C3,},
	{0x118A4, 0x118C4,},
	{0x118A5, 0x118C5,},
	{0x118A6, 0x118C6,},
	{0x118A7, 0x118C7,},
	{0x118A8, 0x118C8,},
	{0x118A9, 0x118C9,},
	{0x118AA, 0x118CA,},
	{0x118AB, 0x118CB,},
	{0x118AC, 0x118CC,},
	{0x118AD, 0x118CD,},
	{0x118AE, 0x118CE,},
	{0x118AF, 0x118CF,},
	{0x118B0, 0x118D0,},
	{0x118B1, 0x118D1,},
	{0x118B2, 0x118D2,},
	{0x118B3, 0x118D3,},
	{0x118B4, 0x118D4,},
	{0x118B5, 0x118D5,},
	{0x118B6, 0x118D6,},
	{0x118B7, 0x118D7,},
	{0x118B8, 0x118D8,},
	{0x118B9, 0x118D9,},
	{0x118BA, 0x118DA,},
	{0x118BB, 0x118DB,},
	{0x118BC, 0x118DC,},
	{0x118BD, 0x118DD,},
	{0x118BE, 0x118DE,},
	{0x118BF, 0x118DF,},
	{0x16E40, 0x16E60,},
	{0x16E41, 0x16E61,},
	{0x16E42, 0x16E62,},
	{0x16E43, 0x16E63,},
	{0x16E44, 0x16E64,},
	{0x16E45, 0x16E65,},
	{0x16E46, 0x16E66,},
	{0x16E47, 0x16E67,},
	{0x16E48, 0x16E68,},
	{0x16E49, 0x16E69,},
	{0x16E4A, 0x16E6A,},
	{0x16E4B, 0x16E6B,},
	{0x16E4C, 0x16E6C,},
	{0x16E4D, 0x16E6D,},
	{0x16E4E, 0x16E6E,},
	{0x16E4F, 0x16E6F,},
	{0x16E50, 0x16E70,},
	{0x16E51, 0x16E71,},
	{0x16E52, 0x16E72,},
	{0x16E53, 0x16E73,},
	{0x16E54, 0x16E74,},
	{0x16E55, 0x16E75,},
	{0x16E56, 0x16E76,},
	{0x16E57, 0x16E77,},
	{0x16E58, 0x16E78,},
	{0x16E59, 0x16E79,},
	{0x16E5A, 0x16E7A,},
	{0x16E5B, 0x16E7B,},
	{0x16E5C, 0x16E7C,},
	{0x16E5D, 0x16E7D,},
	{0x16E5E, 0x16E7E,},
	{0x16E5F, 0x16E7F,},
	{0x1E900, 0x1E922,},
	{0x1E901, 0x1E923,},
	{0x1E902, 0x1E924,},
	{0x1E903, 0x1E925,},
	{0x1E904, 0x1E926,},
	{0x1E905, 0x1E927,},
	{0x1E906, 0x1E928,},
	{0x1E907, 0x1E929,},
	{0x1E908, 0x1E92A,},
	{0x1E909, 0x1E92B,},
	{0x1E90A, 0x1E92C,},
	{0x1E90B, 0x1E92D,},
	{0x1E90C, 0x1E92E,},
	{0x1E90D, 0x1E92F,},
	{0x1E90E, 0x1E930,},
	{0x1E90F, 0x1E931,},
	{0x1E910, 0x1E932,},
	{0x1E911, 0x1E933,},
	{0x1E912, 0x1E934,},
	{0x1E913, 0x1E935,},
	{0x1E914, 0x1E936,},
	{0x1E915, 0x1E937,},
	{0x1E916, 0x1E938,},
	{0x1E917, 0x1E939,},
	{0x1E918, 0x1E93A,},
	{0x1E919, 0x1E93B,},
	{0x1E91A, 0x1E93C,},
	{0x1E91B, 0x1E93D,},
	{0x1E91C, 0x1E93E,},
	{0x1E91D, 0x1E93F,},
	{0x1E91E, 0x1E940,},
	{0x1E91F, 0x1E941,},
	{0x1E920, 0x1E942,},
	{0x1E921, 0x1E943,},
};

static BAT *UTF8_toUpperFrom = NULL, *UTF8_toUpperTo = NULL,
		*UTF8_toLowerFrom = NULL, *UTF8_toLowerTo = NULL;

static str
STRprelude(void)
{
	if (UTF8_toUpperFrom == NULL) {
		size_t i;

		UTF8_toUpperFrom = COLnew(0, TYPE_int,
								  sizeof(UTF8_toUpper) / sizeof(UTF8_toUpper[0]),
								  SYSTRANS);
		UTF8_toUpperTo = COLnew(0, TYPE_int,
								sizeof(UTF8_toUpper) / sizeof(UTF8_toUpper[0]),
								SYSTRANS);
		UTF8_toLowerFrom = COLnew(0, TYPE_int,
								  sizeof(UTF8_toLower) / sizeof(UTF8_toLower[0]),
								  SYSTRANS);
		UTF8_toLowerTo = COLnew(0, TYPE_int,
								sizeof(UTF8_toLower) / sizeof(UTF8_toLower[0]),
								SYSTRANS);
		if (UTF8_toUpperFrom == NULL || UTF8_toUpperTo == NULL
			|| UTF8_toLowerFrom == NULL || UTF8_toLowerTo == NULL) {
			goto bailout;
		}

		int *fp = (int *) Tloc(UTF8_toUpperFrom, 0);
		int *tp = (int *) Tloc(UTF8_toUpperTo, 0);
		for (i = 0; i < sizeof(UTF8_toUpper) / sizeof(UTF8_toUpper[0]); i++) {
			fp[i] = UTF8_toUpper[i].from;
			tp[i] = UTF8_toUpper[i].to;
		}
		BATsetcount(UTF8_toUpperFrom, i);
		UTF8_toUpperFrom->tkey = true;
		UTF8_toUpperFrom->tsorted = true;
		UTF8_toUpperFrom->trevsorted = false;
		UTF8_toUpperFrom->tnil = false;
		UTF8_toUpperFrom->tnonil = true;
		BATsetcount(UTF8_toUpperTo, i);
		UTF8_toUpperTo->tkey = false;
		UTF8_toUpperTo->tsorted = false;
		UTF8_toUpperTo->trevsorted = false;
		UTF8_toUpperTo->tnil = false;
		UTF8_toUpperTo->tnonil = true;

		fp = (int *) Tloc(UTF8_toLowerFrom, 0);
		tp = (int *) Tloc(UTF8_toLowerTo, 0);
		for (i = 0; i < sizeof(UTF8_toLower) / sizeof(UTF8_toLower[0]); i++) {
			fp[i] = UTF8_toLower[i].from;
			tp[i] = UTF8_toLower[i].to;
		}
		BATsetcount(UTF8_toLowerFrom, i);
		UTF8_toLowerFrom->tkey = true;
		UTF8_toLowerFrom->tsorted = true;
		UTF8_toLowerFrom->trevsorted = false;
		UTF8_toLowerFrom->tnil = false;
		UTF8_toLowerFrom->tnonil = true;
		BATsetcount(UTF8_toLowerTo, i);
		UTF8_toLowerTo->tkey = false;
		UTF8_toLowerTo->tsorted = false;
		UTF8_toLowerTo->trevsorted = false;
		UTF8_toLowerTo->tnil = false;
		UTF8_toLowerTo->tnonil = true;

		if (BBPrename(UTF8_toUpperFrom, "monet_unicode_upper_from") != 0 ||
			BBPrename(UTF8_toUpperTo, "monet_unicode_upper_to") != 0 ||
			BBPrename(UTF8_toLowerFrom, "monet_unicode_lower_from") != 0 ||
			BBPrename(UTF8_toLowerTo, "monet_unicode_lower_to") != 0) {
			goto bailout;
		}
		BBP_pid(UTF8_toUpperFrom->batCacheid) = 0;
		BBP_pid(UTF8_toUpperTo->batCacheid) = 0;
		BBP_pid(UTF8_toLowerFrom->batCacheid) = 0;
		BBP_pid(UTF8_toLowerTo->batCacheid) = 0;
	}
	return MAL_SUCCEED;

  bailout:
	BBPreclaim(UTF8_toUpperFrom);
	BBPreclaim(UTF8_toUpperTo);
	BBPreclaim(UTF8_toLowerFrom);
	BBPreclaim(UTF8_toLowerTo);
	UTF8_toUpperFrom = NULL;
	UTF8_toUpperTo = NULL;
	UTF8_toLowerFrom = NULL;
	UTF8_toLowerTo = NULL;
	throw(MAL, "str.prelude", GDK_EXCEPTION);
}

static str
STRepilogue(void *ret)
{
	(void) ret;
	BBPreclaim(UTF8_toUpperFrom);
	BBPreclaim(UTF8_toUpperTo);
	BBPreclaim(UTF8_toLowerFrom);
	BBPreclaim(UTF8_toLowerTo);
	UTF8_toUpperFrom = NULL;
	UTF8_toUpperTo = NULL;
	UTF8_toLowerFrom = NULL;
	UTF8_toLowerTo = NULL;
	return MAL_SUCCEED;
}

#ifndef NDEBUG
static void
UTF8_assert(const char *restrict s)
{
	int c;

	if (s == NULL)
		return;
	if (*s == '\200' && s[1] == '\0')
		return;					/* str_nil */
	while ((c = *s++) != '\0') {
		if ((c & 0x80) == 0)
			continue;
		if ((*s++ & 0xC0) != 0x80)
			assert(0);
		if ((c & 0xE0) == 0xC0)
			continue;
		if ((*s++ & 0xC0) != 0x80)
			assert(0);
		if ((c & 0xF0) == 0xE0)
			continue;
		if ((*s++ & 0xC0) != 0x80)
			assert(0);
		if ((c & 0xF8) == 0xF0)
			continue;
		assert(0);
	}
}
#else
#define UTF8_assert(s)		((void) 0)
#endif

static inline int
UTF8_strpos(const char *s, const char *end)
{
	int pos = 0;

	UTF8_assert(s);

	if (s > end) {
		return -1;
	}
	while (s < end) {
		/* just count leading bytes of encoded code points; only works
		 * for correctly encoded UTF-8 */
		pos += (*s++ & 0xC0) != 0x80;
	}
	return pos;
}

static inline str
UTF8_strtail(const char *s, int pos)
{
	UTF8_assert(s);
	while (*s) {
		if ((*s & 0xC0) != 0x80) {
			if (pos <= 0)
				break;
			pos--;
		}
		s++;
	}
	return (str) s;
}

static inline str
UTF8_strncpy(char *restrict dst, const char *restrict s, int n)
{
	UTF8_assert(s);
	while (*s && n) {
		if ((*s & 0xF8) == 0xF0) {
			/* 4 byte UTF-8 sequence */
			*dst++ = *s++;
			*dst++ = *s++;
			*dst++ = *s++;
			*dst++ = *s++;
		} else if ((*s & 0xF0) == 0xE0) {
			/* 3 byte UTF-8 sequence */
			*dst++ = *s++;
			*dst++ = *s++;
			*dst++ = *s++;
		} else if ((*s & 0xE0) == 0xC0) {
			/* 2 byte UTF-8 sequence */
			*dst++ = *s++;
			*dst++ = *s++;
		} else {
			/* 1 byte UTF-8 "sequence" */
			*dst++ = *s++;
		}
		n--;
	}
	*dst = '\0';
	return dst;
}

static inline str
UTF8_offset(char *restrict s, int n)
{
	UTF8_assert(s);
	while (*s && n) {
		if ((*s & 0xF8) == 0xF0) {
			/* 4 byte UTF-8 sequence */
			s += 4;
		} else if ((*s & 0xF0) == 0xE0) {
			/* 3 byte UTF-8 sequence */
			s += 3;
		} else if ((*s & 0xE0) == 0xC0) {
			/* 2 byte UTF-8 sequence */
			s += 2;
		} else {
			/* 1 byte UTF-8 "sequence" */
			s++;
		}
		n--;
	}
	return s;
}

int
UTF8_strlen(const char *restrict s)
{								/* This function assumes, s is never nil */
	size_t pos = 0;

	UTF8_assert(s);
	assert(!strNil(s));

	while (*s) {
		/* just count leading bytes of encoded code points; only works
		 * for correctly encoded UTF-8 */
		pos += (*s++ & 0xC0) != 0x80;
	}
	assert(pos < INT_MAX);
	return (int) pos;
}

int
str_strlen(const char *restrict s)
{								/* This function assumes, s is never nil */
	size_t pos = strlen(s);
	assert(pos < INT_MAX);
	return (int) pos;
}

int
UTF8_strwidth(const char *restrict s)
{
	int len = 0;
	int c;
	int n;

	if (strNil(s))
		return int_nil;
	c = 0;
	n = 0;
	while (*s != 0) {
		if ((*s & 0x80) == 0) {
			assert(n == 0);
			len++;
			n = 0;
		} else if ((*s & 0xC0) == 0x80) {
			c = (c << 6) | (*s & 0x3F);
			if (--n == 0) {
				/* last byte of a multi-byte character */
				len++;
				/* this list was created by combining
				 * the code points marked as
				 * Emoji_Presentation in
				 * /usr/share/unicode/emoji/emoji-data.txt
				 * and code points marked either F or
				 * W in EastAsianWidth.txt; this list
				 * is up-to-date with Unicode 9.0 */
				if ((0x1100 <= c && c <= 0x115F) ||
					(0x231A <= c && c <= 0x231B) ||
					(0x2329 <= c && c <= 0x232A) ||
					(0x23E9 <= c && c <= 0x23EC) ||
					c == 0x23F0 ||
					c == 0x23F3 ||
					(0x25FD <= c && c <= 0x25FE) ||
					(0x2614 <= c && c <= 0x2615) ||
					(0x2648 <= c && c <= 0x2653) ||
					c == 0x267F ||
					c == 0x2693 ||
					c == 0x26A1 ||
					(0x26AA <= c && c <= 0x26AB) ||
					(0x26BD <= c && c <= 0x26BE) ||
					(0x26C4 <= c && c <= 0x26C5) ||
					c == 0x26CE ||
					c == 0x26D4 ||
					c == 0x26EA ||
					(0x26F2 <= c && c <= 0x26F3) ||
					c == 0x26F5 ||
					c == 0x26FA ||
					c == 0x26FD ||
					c == 0x2705 ||
					(0x270A <= c && c <= 0x270B) ||
					c == 0x2728 ||
					c == 0x274C ||
					c == 0x274E ||
					(0x2753 <= c && c <= 0x2755) ||
					c == 0x2757 ||
					(0x2795 <= c && c <= 0x2797) ||
					c == 0x27B0 ||
					c == 0x27BF ||
					(0x2B1B <= c && c <= 0x2B1C) ||
					c == 0x2B50 ||
					c == 0x2B55 ||
					(0x2E80 <= c && c <= 0x2E99) ||
					(0x2E9B <= c && c <= 0x2EF3) ||
					(0x2F00 <= c && c <= 0x2FD5) ||
					(0x2FF0 <= c && c <= 0x2FFB) ||
					(0x3000 <= c && c <= 0x303E) ||
					(0x3041 <= c && c <= 0x3096) ||
					(0x3099 <= c && c <= 0x30FF) ||
					(0x3105 <= c && c <= 0x312D) ||
					(0x3131 <= c && c <= 0x318E) ||
					(0x3190 <= c && c <= 0x31BA) ||
					(0x31C0 <= c && c <= 0x31E3) ||
					(0x31F0 <= c && c <= 0x321E) ||
					(0x3220 <= c && c <= 0x3247) ||
					(0x3250 <= c && c <= 0x32FE) ||
					(0x3300 <= c && c <= 0x4DBF) ||
					(0x4E00 <= c && c <= 0xA48C) ||
					(0xA490 <= c && c <= 0xA4C6) ||
					(0xA960 <= c && c <= 0xA97C) ||
					(0xAC00 <= c && c <= 0xD7A3) ||
					(0xF900 <= c && c <= 0xFAFF) ||
					(0xFE10 <= c && c <= 0xFE19) ||
					(0xFE30 <= c && c <= 0xFE52) ||
					(0xFE54 <= c && c <= 0xFE66) ||
					(0xFE68 <= c && c <= 0xFE6B) ||
					(0xFF01 <= c && c <= 0xFF60) ||
					(0xFFE0 <= c && c <= 0xFFE6) ||
					c == 0x16FE0 ||
					(0x17000 <= c && c <= 0x187EC) ||
					(0x18800 <= c && c <= 0x18AF2) ||
					(0x1B000 <= c && c <= 0x1B001) ||
					c == 0x1F004 ||
					c == 0x1F0CF ||
					c == 0x1F18E || (0x1F191 <= c && c <= 0x1F19A) ||
					/* removed 0x1F1E6..0x1F1FF */
					(0x1F200 <= c && c <= 0x1F202) ||
					(0x1F210 <= c && c <= 0x1F23B) ||
					(0x1F240 <= c && c <= 0x1F248) ||
					(0x1F250 <= c && c <= 0x1F251) ||
					(0x1F300 <= c && c <= 0x1F320) ||
					(0x1F32D <= c && c <= 0x1F335) ||
					(0x1F337 <= c && c <= 0x1F37C) ||
					(0x1F37E <= c && c <= 0x1F393) ||
					(0x1F3A0 <= c && c <= 0x1F3CA) ||
					(0x1F3CF <= c && c <= 0x1F3D3) ||
					(0x1F3E0 <= c && c <= 0x1F3F0) ||
					c == 0x1F3F4 ||
					(0x1F3F8 <= c && c <= 0x1F43E) ||
					c == 0x1F440 ||
					(0x1F442 <= c && c <= 0x1F4FC) ||
					(0x1F4FF <= c && c <= 0x1F53D) ||
					(0x1F54B <= c && c <= 0x1F54E) ||
					(0x1F550 <= c && c <= 0x1F567) ||
					c == 0x1F57A ||
					(0x1F595 <= c && c <= 0x1F596) ||
					c == 0x1F5A4 ||
					(0x1F5FB <= c && c <= 0x1F64F) ||
					(0x1F680 <= c && c <= 0x1F6C5) ||
					c == 0x1F6CC ||
					(0x1F6D0 <= c && c <= 0x1F6D2) ||
					(0x1F6EB <= c && c <= 0x1F6EC) ||
					(0x1F6F4 <= c && c <= 0x1F6F6) ||
					(0x1F910 <= c && c <= 0x1F91E) ||
					(0x1F920 <= c && c <= 0x1F927) ||
					c == 0x1F930 ||
					(0x1F933 <= c && c <= 0x1F93E) ||
					(0x1F940 <= c && c <= 0x1F94B) ||
					(0x1F950 <= c && c <= 0x1F95E) ||
					(0x1F980 <= c && c <= 0x1F991) ||
					c == 0x1F9C0 ||
					(0x20000 <= c && c <= 0x2FFFD) ||
					(0x30000 <= c && c <= 0x3FFFD))
					len++;
			}
		} else if ((*s & 0xE0) == 0xC0) {
			assert(n == 0);
			n = 1;
			c = *s & 0x1F;
		} else if ((*s & 0xF0) == 0xE0) {
			assert(n == 0);
			n = 2;
			c = *s & 0x0F;
		} else if ((*s & 0xF8) == 0xF0) {
			assert(n == 0);
			n = 3;
			c = *s & 0x07;
		} else if ((*s & 0xFC) == 0xF8) {
			assert(n == 0);
			n = 4;
			c = *s & 0x03;
		} else {
			assert(0);
			n = 0;
		}
		s++;
	}
	return len;
}

str
str_case_hash_lock(bool upper)
{
	BAT *b = upper ? UTF8_toUpperFrom : UTF8_toLowerFrom;

	if (BAThash(b) != GDK_SUCCEED)
		throw(MAL, "str.str_case_hash_lock", GDK_EXCEPTION);
	MT_rwlock_rdlock(&b->thashlock);
	if (b->thash)
		return MAL_SUCCEED;
	MT_rwlock_rdunlock(&b->thashlock);
	throw(MAL, "str.str_case_hash_lock", "Lost hash");
}

void
str_case_hash_unlock(bool upper)
{
	BAT *b = upper ? UTF8_toUpperFrom : UTF8_toLowerFrom;
	MT_rwlock_rdunlock(&b->thashlock);
}

static inline str
convertCase(BAT *from, BAT *to, str *buf, size_t *buflen, const char *src,
			const char *malfunc)
{
	size_t len = strlen(src);
	char *dst;
	const char *end = src + len;
	bool lower_to_upper = from == UTF8_toUpperFrom;
	const Hash *h = from->thash;
	const int *restrict fromb = (const int *restrict) from->theap->base;
	const int *restrict tob = (const int *restrict) to->theap->base;

	/* the from and to bats are not views */
	assert(from->tbaseoff == 0);
	assert(to->tbaseoff == 0);
	CHECK_STR_BUFFER_LENGTH(buf, buflen, len + 1, malfunc);
	dst = *buf;
	while (src < end) {
		int c;

		UTF8_GETCHAR(c, src);
		if (c < 192) {			/* the first 191 characters in unicode are trivial to convert */
			/* for ASCII characters we don't need to do a hash lookup */
			if (lower_to_upper) {
				if ('a' <= c && c <= 'z')
					c += 'A' - 'a';
			} else {
				if ('A' <= c && c <= 'Z')
					c += 'a' - 'A';
			}
		} else {
			/* use hash, even though BAT is sorted */
			for (BUN hb = HASHget(h, hash_int(h, &c));
				 hb != BUN_NONE; hb = HASHgetlink(h, hb)) {
				if (c == fromb[hb]) {
					c = tob[hb];
					break;
				}
			}
		}
		if (dst + UTF8_CHARLEN(c) > *buf + len) {
			/* doesn't fit, so allocate more space;
			 * also allocate enough for the rest of the
			 * source */
			size_t off = dst - *buf;
			size_t nextlen = (len += 4 + (end - src)) + 1;

			/* Don't use CHECK_STR_BUFFER_LENGTH here, because it
			 * does GDKmalloc instead of GDKrealloc and data could be lost */
			if (nextlen > *buflen) {
				size_t newlen = ((nextlen + 1023) & ~1023);	/* align to a multiple of 1024 bytes */
				str newbuf = GDKrealloc(*buf, newlen);
				if (!newbuf)
					throw(MAL, malfunc, SQLSTATE(HY013) MAL_MALLOC_FAIL);
				*buf = newbuf;
				*buflen = newlen;
			}
			dst = *buf + off;
		}
		UTF8_PUTCHAR(c, dst);
	}
	*dst = 0;
	return MAL_SUCCEED;
  illegal:
	throw(MAL, malfunc, SQLSTATE(42000) "Illegal Unicode code point");
}

/*
 * Here you find the wrappers around the version 4 library code
 * It also contains the direct implementation of the string
 * matching support routines.
 */
#include "mal_exception.h"

/*
 * The SQL like function return a boolean
 */
static bool
STRlike(const char *s, const char *pat, const char *esc)
{
	const char *t, *p;

	t = s;
	for (p = pat; *p && *t; p++) {
		if (esc && *p == *esc) {
			p++;
			if (*p != *t)
				return false;
			t++;
		} else if (*p == '_')
			t++;
		else if (*p == '%') {
			p++;
			while (*p == '%')
				p++;
			if (*p == 0)
				return true;	/* tail is acceptable */
			for (; *p && *t; t++)
				if (STRlike(t, p, esc))
					return true;
			if (*p == 0 && *t == 0)
				return true;
			return false;
		} else if (*p == *t)
			t++;
		else
			return false;
	}
	if (*p == '%' && *(p + 1) == 0)
		return true;
	return *t == 0 && *p == 0;
}

static str
STRlikewrap3(bit *ret, const str *s, const str *pat, const str *esc)
{
	if (strNil(*s) || strNil(*pat) || strNil(*esc))
		*ret = bit_nil;
	else
		*ret = (bit) STRlike(*s, *pat, *esc);
	return MAL_SUCCEED;
}

static str
STRlikewrap(bit *ret, const str *s, const str *pat)
{
	if (strNil(*s) || strNil(*pat))
		*ret = bit_nil;
	else
		*ret = (bit) STRlike(*s, *pat, NULL);
	return MAL_SUCCEED;
}

static str
STRtostr(str *res, const str *src)
{
	if (*src == 0)
		*res = GDKstrdup(str_nil);
	else
		*res = GDKstrdup(*src);
	if (*res == NULL)
		throw(MAL, "str.str", SQLSTATE(HY013) MAL_MALLOC_FAIL);
	return MAL_SUCCEED;
}

static str
STRLength(int *res, const str *arg1)
{
	const char *s = *arg1;

	*res = strNil(s) ? int_nil : UTF8_strlen(s);
	return MAL_SUCCEED;
}

static str
STRBytes(int *res, const str *arg1)
{
	const char *s = *arg1;

	*res = strNil(s) ? int_nil : str_strlen(s);
	return MAL_SUCCEED;
}

str
str_tail(str *buf, size_t *buflen, const char *s, int off)
{
	if (off < 0) {
		off += UTF8_strlen(s);
		if (off < 0)
			off = 0;
	}
	char *tail = UTF8_strtail(s, off);
	size_t nextlen = strlen(tail) + 1;
	CHECK_STR_BUFFER_LENGTH(buf, buflen, nextlen, "str.tail");
	strcpy(*buf, tail);
	return MAL_SUCCEED;
}

static str
STRTail(str *res, const str *arg1, const int *offset)
{
	str buf = NULL, msg = MAL_SUCCEED;
	const char *s = *arg1;
	int off = *offset;

	if (strNil(s) || is_int_nil(off)) {
		*res = GDKstrdup(str_nil);
	} else {
		size_t buflen = INITIAL_STR_BUFFER_LENGTH;

		*res = NULL;
		if (!(buf = GDKmalloc(buflen)))
			throw(MAL, "str.tail", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		if ((msg = str_tail(&buf, &buflen, s, off)) != MAL_SUCCEED) {
			GDKfree(buf);
			return msg;
		}
		*res = GDKstrdup(buf);
	}

	GDKfree(buf);
	if (!*res)
		msg = createException(MAL, "str.tail", SQLSTATE(HY013) MAL_MALLOC_FAIL);
	return msg;
}

str
str_Sub_String(str *buf, size_t *buflen, const char *s, int off, int l)
{
	size_t len;

	if (off < 0) {
		off += UTF8_strlen(s);
		if (off < 0) {
			l += off;
			off = 0;
		}
	}
	/* here, off >= 0 */
	if (l < 0) {
		strcpy(*buf, "");
		return MAL_SUCCEED;
	}
	s = UTF8_strtail(s, off);
	len = (size_t) (UTF8_strtail(s, l) - s + 1);
	CHECK_STR_BUFFER_LENGTH(buf, buflen, len, "str.substring");
	strcpy_len(*buf, s, len);
	return MAL_SUCCEED;
}

static str
STRSubString(str *res, const str *arg1, const int *offset, const int *length)
{
	str buf = NULL, msg = MAL_SUCCEED;
	const char *s = *arg1;
	int off = *offset, len = *length;

	if (strNil(s) || is_int_nil(off) || is_int_nil(len)) {
		*res = GDKstrdup(str_nil);
	} else {
		size_t buflen = INITIAL_STR_BUFFER_LENGTH;

		*res = NULL;
		if (!(buf = GDKmalloc(buflen)))
			throw(MAL, "str.substring", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		if ((msg = str_Sub_String(&buf, &buflen, s, off, len)) != MAL_SUCCEED) {
			GDKfree(buf);
			return msg;
		}
		*res = GDKstrdup(buf);
	}

	GDKfree(buf);
	if (!*res)
		msg = createException(MAL, "str.substring",
							  SQLSTATE(HY013) MAL_MALLOC_FAIL);
	return msg;
}

str
str_from_wchr(str *buf, size_t *buflen, int c)
{
	CHECK_STR_BUFFER_LENGTH(buf, buflen, 5, "str.unicode");
	str s = *buf;
	UTF8_PUTCHAR(c, s);
	*s = 0;
	return MAL_SUCCEED;
  illegal:
	throw(MAL, "str.unicode", SQLSTATE(42000) "Illegal Unicode code point");
}

static str
STRFromWChr(str *res, const int *c)
{
	str buf = NULL, msg = MAL_SUCCEED;
	int cc = *c;

	if (is_int_nil(cc)) {
		*res = GDKstrdup(str_nil);
	} else {
		size_t buflen = MAX(strlen(str_nil) + 1, 8);

		*res = NULL;
		if (!(buf = GDKmalloc(buflen)))
			throw(MAL, "str.unicode", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		if ((msg = str_from_wchr(&buf, &buflen, cc)) != MAL_SUCCEED) {
			GDKfree(buf);
			return msg;
		}
		*res = GDKstrdup(buf);
	}

	GDKfree(buf);
	if (!*res)
		msg = createException(MAL, "str.unicode",
							  SQLSTATE(HY013) MAL_MALLOC_FAIL);
	return msg;
}

/* return the Unicode code point of arg1 at position at */
str
str_wchr_at(int *res, const char *s, int at)
{
	/* 64bit: should have lng arg */
	if (strNil(s) || is_int_nil(at) || at < 0) {
		*res = int_nil;
		return MAL_SUCCEED;
	}
	s = UTF8_strtail(s, at);
	if (s == NULL || *s == 0) {
		*res = int_nil;
		return MAL_SUCCEED;
	}
	UTF8_GETCHAR(*res, s);
	return MAL_SUCCEED;
  illegal:
	throw(MAL, "str.unicodeAt", SQLSTATE(42000) "Illegal Unicode code point");
}

static str
STRWChrAt(int *res, const str *arg1, const int *at)
{
	return str_wchr_at(res, *arg1, *at);
}

str
str_lower(str *buf, size_t *buflen, const char *s)
{
	return convertCase(UTF8_toLowerFrom, UTF8_toLowerTo, buf, buflen, s,
					   "str.lower");
}

static inline str
STRlower(str *res, const str *arg1)
{
	str buf = NULL, msg = MAL_SUCCEED;
	const char *s = *arg1;

	if (strNil(s)) {
		*res = GDKstrdup(str_nil);
	} else {
		size_t buflen = INITIAL_STR_BUFFER_LENGTH;

		*res = NULL;
		if (!(buf = GDKmalloc(buflen)))
			throw(MAL, "str.lower", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		if ((msg = str_case_hash_lock(false))) {
			GDKfree(buf);
			return msg;
		}
		msg = str_lower(&buf, &buflen, s);
		str_case_hash_unlock(false);
		if (msg != MAL_SUCCEED) {
			GDKfree(buf);
			return msg;
		}
		*res = GDKstrdup(buf);
	}

	GDKfree(buf);
	if (!*res)
		msg = createException(MAL, "str.lower",
							  SQLSTATE(HY013) MAL_MALLOC_FAIL);
	return msg;
}

str
str_upper(str *buf, size_t *buflen, const char *s)
{
	return convertCase(UTF8_toUpperFrom, UTF8_toUpperTo, buf, buflen, s,
					   "str.upper");
}

static str
STRupper(str *res, const str *arg1)
{
	str buf = NULL, msg = MAL_SUCCEED;
	const char *s = *arg1;

	if (strNil(s)) {
		*res = GDKstrdup(str_nil);
	} else {
		size_t buflen = INITIAL_STR_BUFFER_LENGTH;

		*res = NULL;
		if (!(buf = GDKmalloc(buflen)))
			throw(MAL, "str.upper", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		if ((msg = str_case_hash_lock(true))) {
			GDKfree(buf);
			return msg;
		}
		msg = str_upper(&buf, &buflen, s);
		str_case_hash_unlock(true);
		if (msg != MAL_SUCCEED) {
			GDKfree(buf);
			return msg;
		}
		*res = GDKstrdup(buf);
	}

	GDKfree(buf);
	if (!*res)
		msg = createException(MAL, "str.upper",
							  SQLSTATE(HY013) MAL_MALLOC_FAIL);
	return msg;
}

/* returns whether arg1 starts with arg2 */
int
str_is_prefix(const char *s, const char *prefix, int plen)
{
	return strncmp(s, prefix, plen);
}

int
str_is_iprefix(const char *s, const char *prefix, int plen)
{
	return utf8ncasecmp(s, prefix, plen);
}

int
str_is_suffix(const char *s, const char *suffix, int sul)
{
	int sl = str_strlen(s);

	if (sl < sul)
		return -1;
	else
		return strcmp(s + sl - sul, suffix);
}

int
str_is_isuffix(const char *s, const char *suffix, int sul)
{
	int sl = str_strlen(s);

	if (sl < sul)
		return -1;
	else
		return utf8casecmp(s + sl - sul, suffix);
}

int
str_contains(const char *h, const char *n, int nlen)
{
	(void) nlen;
	/* 64bit: should return lng */
	return strstr(h, n) ? 0 : 1;
}

int
str_icontains(const char *h, const char *n, int nlen)
{
	(void) nlen;
	/* 64bit: should return lng */
	return utf8casestr(h, n) ? 0 : 1;
}

#define STR_MAPARGS(STK, PCI, R, S1, S2, ICASE)				\
	do{														\
		R = getArgReference(STK, PCI, 0);						\
		S1 = *getArgReference_str(STK, PCI, 1);				\
		S2 = *getArgReference_str(STK, PCI, 2);				\
		icase = PCI->argc == 4 &&								\
			*getArgReference_bit(STK, PCI, 3) ? true : false;	\
																\
	} while(0)

static str
STRstartswith(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	(void) cntxt;
	(void) mb;

	str s1, s2;
	bit *r, icase;

	STR_MAPARGS(stk, pci, r, s1, s2, icase);

	int s2_len = str_strlen(s2);
	*r = (strNil(s1) || strNil(s2)) ? bit_nil :
		icase ? str_is_iprefix(s1, s2, s2_len) == 0 :
		        str_is_prefix(s1, s2, s2_len) == 0;
	return MAL_SUCCEED;
}

static str
STRendswith(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	(void) cntxt;
	(void) mb;

	str s1, s2;
	bit *r, icase;

	STR_MAPARGS(stk, pci, r, s1, s2, icase);

	int s2_len = str_strlen(s2);
	*r = (strNil(s1) || strNil(s2)) ? bit_nil :
		icase ? str_is_isuffix(s1, s2, s2_len) == 0 :
		        str_is_suffix(s1, s2, s2_len) == 0;
	return MAL_SUCCEED;
}

/* returns whether haystack contains needle */
static str
STRcontains(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	(void) cntxt;
	(void) mb;

	str s1, s2;
	bit *r, icase;

	STR_MAPARGS(stk, pci, r, s1, s2, icase);

	int s2_len = str_strlen(s2);
	*r = (strNil(s1) || strNil(s2)) ? bit_nil :
		icase ? str_icontains(s1, s2, s2_len) == 0 :
		        str_contains(s1, s2, s2_len) == 0;
	return MAL_SUCCEED;
}

int
str_search(const char *s, const char *s2, int slen)
{
	(void) slen;
	/* 64bit: should return lng */
	if ((s2 = strstr(s, s2)) != NULL)
		return UTF8_strpos(s, s2);
	else
		return -1;
}

int
str_isearch(const char *s, const char *s2, int slen)
{
	(void) slen;
	/* 64bit: should return lng */
	if ((s2 = utf8casestr(s, s2)) != NULL)
		return UTF8_strpos(s, s2);
	else
		return -1;
}

/* find first occurrence of needle in haystack */
static str
STRstr_search(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	(void) cntxt;
	(void) mb;
	bit *res = getArgReference(stk, pci, 0);
	const str *haystack = getArgReference(stk, pci, 1),
		*needle = getArgReference(stk, pci, 2);
	bit icase = pci->argc == 4
			&& *getArgReference_bit(stk, pci, 3) ? true : false;
	str s = *haystack, h = *needle, msg = MAL_SUCCEED;
	int needle_len = str_strlen(h);

	*res = (strNil(s) || strNil(h)) ? bit_nil :
			icase ? str_isearch(s, h, needle_len) : str_search(s, h,
															   needle_len);
	return msg;
}

int
str_reverse_str_search(const char *s, const char *s2, int slen)
{
	/* 64bit: should return lng */
	int len = str_strlen(s);
	int res = -1;				/* changed if found */

	if (len >= slen) {
		const char *p = s + len - slen;
		do {
			if (strncmp(p, s2, slen) == 0) {
				res = UTF8_strpos(s, p);
				break;
			}
		} while (p-- > s);
	}
	return res;
}

int
str_reverse_str_isearch(const char *s, const char *s2, int slen)
{
	/* 64bit: should return lng */
	int len = str_strlen(s);
	int res = -1;				/* changed if found */

	if (len >= slen) {
		const char *p = s + len - slen;
		do {
			if (utf8ncasecmp(p, s2, slen) == 0) {
				res = UTF8_strpos(s, p);
				break;
			}
		} while (p-- > s);
	}
	return res;
}

/* find last occurrence of arg2 in arg1 */
static str
STRrevstr_search(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	(void) cntxt;
	(void) mb;
	bit *res = getArgReference(stk, pci, 0);
	const str *haystack = getArgReference(stk, pci, 1);
	const str *needle = getArgReference(stk, pci, 2);
	bit icase = pci->argc == 4
			&& *getArgReference_bit(stk, pci, 3) ? true : false;
	str s = *haystack, h = *needle, msg = MAL_SUCCEED;
	int needle_len = str_strlen(h);

	*res = (strNil(s) || strNil(h)) ? bit_nil :
			icase ? str_reverse_str_isearch(s, h,
											needle_len) :
			str_reverse_str_search(s, h, needle_len);
	return msg;
}

str
str_splitpart(str *buf, size_t *buflen, const char *s, const char *s2, int f)
{
	size_t len;
	char *p = NULL;

	if (f <= 0)
		throw(MAL, "str.splitpart",
			  SQLSTATE(42000) "field position must be greater than zero");

	len = strlen(s2);
	if (len) {
		while ((p = strstr(s, s2)) != NULL && f > 1) {
			s = p + len;
			f--;
		}
	}

	if (f != 1) {
		strcpy(*buf, "");
		return MAL_SUCCEED;
	}

	if (p == NULL) {
		len = strlen(s);
	} else {
		len = (size_t) (p - s);
	}

	len++;
	CHECK_STR_BUFFER_LENGTH(buf, buflen, len, "str.splitpart");
	strcpy_len(*buf, s, len);
	return MAL_SUCCEED;
}

static str
STRsplitpart(str *res, str *haystack, str *needle, int *field)
{
	str buf = NULL, msg = MAL_SUCCEED;
	const char *s = *haystack, *s2 = *needle;
	int f = *field;

	if (strNil(s) || strNil(s2) || is_int_nil(f)) {
		*res = GDKstrdup(str_nil);
	} else {
		size_t buflen = INITIAL_STR_BUFFER_LENGTH;

		*res = NULL;
		if (!(buf = GDKmalloc(buflen)))
			throw(MAL, "str.splitpart", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		if ((msg = str_splitpart(&buf, &buflen, s, s2, f)) != MAL_SUCCEED) {
			GDKfree(buf);
			return msg;
		}
		*res = GDKstrdup(buf);
	}

	GDKfree(buf);
	if (!*res)
		msg = createException(MAL, "str.splitpart",
							  SQLSTATE(HY013) MAL_MALLOC_FAIL);
	return msg;
}

/* returns number of bytes to remove from left to strip the codepoints in rm */
static size_t
lstrip(const char *s, size_t len, const int *rm, size_t nrm)
{
	int c;
	size_t i, n, skip = 0;

	while (len > 0) {
		UTF8_NEXTCHAR(c, n, s);
		assert(n > 0 && n <= len);
		for (i = 0; i < nrm; i++) {
			if (rm[i] == c) {
				s += n;
				skip += n;
				len -= n;
				break;
			}
		}
		if (i == nrm)
			break;
	}
	return skip;
}

/* returns the resulting length of s after stripping codepoints in rm
 * from the right */
static size_t
rstrip(const char *s, size_t len, const int *rm, size_t nrm)
{
	int c;
	size_t i, n;

	while (len > 0) {
		UTF8_LASTCHAR(c, n, s, len);
		assert(n > 0 && n <= len);
		for (i = 0; i < nrm; i++) {
			if (rm[i] == c) {
				len -= n;
				break;
			}
		}
		if (i == nrm)
			break;
	}
	return len;
}

const int whitespace[] = {
	' ',						/* space */
	'\t',						/* tab (character tabulation) */
	'\n',						/* line feed */
	'\r',						/* carriage return */
	'\f',						/* form feed */
	'\v',						/* vertical tab (line tabulation) */
/* below the code points that have the Unicode Zs (space separator) property */
	0x00A0,						/* no-break space */
	0x1680,						/* ogham space mark */
	0x2000,						/* en quad */
	0x2001,						/* em quad */
	0x2002,						/* en space */
	0x2003,						/* em space */
	0x2004,						/* three-per-em space */
	0x2005,						/* four-per-em space */
	0x2006,						/* six-per-em space */
	0x2007,						/* figure space */
	0x2008,						/* punctuation space */
	0x2009,						/* thin space */
	0x200A,						/* hair space */
	0x202F,						/* narrow no-break space */
	0x205F,						/* medium mathematical space */
	0x3000,						/* ideographic space */
};

#define NSPACES		(sizeof(whitespace) / sizeof(whitespace[0]))

str
str_strip(str *buf, size_t *buflen, const char *s)
{
	size_t len = strlen(s);
	size_t n = lstrip(s, len, whitespace, NSPACES);
	s += n;
	len -= n;
	n = rstrip(s, len, whitespace, NSPACES);

	n++;
	CHECK_STR_BUFFER_LENGTH(buf, buflen, n, "str.strip");
	strcpy_len(*buf, s, n);
	return MAL_SUCCEED;
}

/* remove all whitespace from either side of arg1 */
static str
STRStrip(str *res, const str *arg1)
{
	str buf = NULL, msg = MAL_SUCCEED;
	const char *s = *arg1;

	if (strNil(s)) {
		*res = GDKstrdup(str_nil);
	} else {
		size_t buflen = INITIAL_STR_BUFFER_LENGTH;

		*res = NULL;
		if (!(buf = GDKmalloc(buflen)))
			throw(MAL, "str.strip", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		if ((msg = str_strip(&buf, &buflen, s)) != MAL_SUCCEED) {
			GDKfree(buf);
			return msg;
		}
		*res = GDKstrdup(buf);
	}

	GDKfree(buf);
	if (!*res)
		msg = createException(MAL, "str.strip",
							  SQLSTATE(HY013) MAL_MALLOC_FAIL);
	return msg;
}

str
str_ltrim(str *buf, size_t *buflen, const char *s)
{
	size_t len = strlen(s);
	size_t n = lstrip(s, len, whitespace, NSPACES);
	size_t nallocate = len - n + 1;

	CHECK_STR_BUFFER_LENGTH(buf, buflen, nallocate, "str.ltrim");
	strcpy_len(*buf, s + n, nallocate);
	return MAL_SUCCEED;
}

/* remove all whitespace from the start (left) of arg1 */
static str
STRLtrim(str *res, const str *arg1)
{
	str buf = NULL, msg = MAL_SUCCEED;
	const char *s = *arg1;

	if (strNil(s)) {
		*res = GDKstrdup(str_nil);
	} else {
		size_t buflen = INITIAL_STR_BUFFER_LENGTH;

		*res = NULL;
		if (!(buf = GDKmalloc(buflen)))
			throw(MAL, "str.ltrim", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		if ((msg = str_ltrim(&buf, &buflen, s)) != MAL_SUCCEED) {
			GDKfree(buf);
			return msg;
		}
		*res = GDKstrdup(buf);
	}

	GDKfree(buf);
	if (!*res)
		msg = createException(MAL, "str.ltrim",
							  SQLSTATE(HY013) MAL_MALLOC_FAIL);
	return msg;
}

str
str_rtrim(str *buf, size_t *buflen, const char *s)
{
	size_t len = strlen(s);
	size_t n = rstrip(s, len, whitespace, NSPACES);

	n++;
	CHECK_STR_BUFFER_LENGTH(buf, buflen, n, "str.rtrim");
	strcpy_len(*buf, s, n);
	return MAL_SUCCEED;
}

/* remove all whitespace from the end (right) of arg1 */
static str
STRRtrim(str *res, const str *arg1)
{
	str buf = NULL, msg = MAL_SUCCEED;
	const char *s = *arg1;

	if (strNil(s)) {
		*res = GDKstrdup(str_nil);
	} else {
		size_t buflen = INITIAL_STR_BUFFER_LENGTH;

		*res = NULL;
		if (!(buf = GDKmalloc(buflen)))
			throw(MAL, "str.rtrim", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		if ((msg = str_rtrim(&buf, &buflen, s)) != MAL_SUCCEED) {
			GDKfree(buf);
			return msg;
		}
		*res = GDKstrdup(buf);
	}

	GDKfree(buf);
	if (!*res)
		msg = createException(MAL, "str.rtrim",
							  SQLSTATE(HY013) MAL_MALLOC_FAIL);
	return msg;
}

/* return a list of codepoints in s */
static str
trimchars(str *buf, size_t *buflen, size_t *n, const char *s, size_t len_s,
		  const char *malfunc)
{
	size_t len = 0, nlen = len_s * sizeof(int);
	int c, *cbuf;

	assert(s);
	CHECK_STR_BUFFER_LENGTH(buf, buflen, nlen, malfunc);
	cbuf = *(int **) buf;

	while (*s) {
		UTF8_GETCHAR(c, s);
		assert(!is_int_nil(c));
		cbuf[len++] = c;
	}
	*n = len;
	return MAL_SUCCEED;
  illegal:
	throw(MAL, malfunc, SQLSTATE(42000) "Illegal Unicode code point");
}

str
str_strip2(str *buf, size_t *buflen, const char *s, const char *s2)
{
	str msg = MAL_SUCCEED;
	size_t len, n, n2, n3;

	if ((n2 = strlen(s2)) == 0) {
		len = strlen(s) + 1;
		CHECK_STR_BUFFER_LENGTH(buf, buflen, len, "str.strip2");
		strcpy(*buf, s);
		return MAL_SUCCEED;
	} else {
		if ((msg = trimchars(buf, buflen, &n3, s2, n2, "str.strip2")) != MAL_SUCCEED)
			return msg;
		len = strlen(s);
		n = lstrip(s, len, *(int **) buf, n3);
		s += n;
		len -= n;
		n = rstrip(s, len, *(int **) buf, n3);

		n++;
		CHECK_STR_BUFFER_LENGTH(buf, buflen, n, "str.strip2");
		strcpy_len(*buf, s, n);
		return MAL_SUCCEED;
	}
}

/* remove the longest string containing only characters from arg2 from
 * either side of arg1 */
static str
STRStrip2(str *res, const str *arg1, const str *arg2)
{
	str buf = NULL, msg = MAL_SUCCEED;
	const char *s = *arg1, *s2 = *arg2;

	if (strNil(s) || strNil(s2)) {
		*res = GDKstrdup(str_nil);
	} else {
		size_t buflen = INITIAL_STR_BUFFER_LENGTH * sizeof(int);

		*res = NULL;
		if (!(buf = GDKmalloc(buflen)))
			throw(MAL, "str.strip2", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		if ((msg = str_strip2(&buf, &buflen, s, s2)) != MAL_SUCCEED) {
			GDKfree(buf);
			return msg;
		}
		*res = GDKstrdup(buf);
	}

	GDKfree(buf);
	if (!*res)
		msg = createException(MAL, "str.strip2",
							  SQLSTATE(HY013) MAL_MALLOC_FAIL);
	return msg;
}

str
str_ltrim2(str *buf, size_t *buflen, const char *s, const char *s2)
{
	str msg = MAL_SUCCEED;
	size_t len, n, n2, n3, nallocate;

	if ((n2 = strlen(s2)) == 0) {
		len = strlen(s) + 1;
		CHECK_STR_BUFFER_LENGTH(buf, buflen, len, "str.ltrim2");
		strcpy(*buf, s);
		return MAL_SUCCEED;
	} else {
		if ((msg = trimchars(buf, buflen, &n3, s2, n2, "str.ltrim2")) != MAL_SUCCEED)
			return msg;
		len = strlen(s);
		n = lstrip(s, len, *(int **) buf, n3);
		nallocate = len - n + 1;

		CHECK_STR_BUFFER_LENGTH(buf, buflen, nallocate, "str.ltrim2");
		strcpy_len(*buf, s + n, nallocate);
		return MAL_SUCCEED;
	}
}

/* remove the longest string containing only characters from arg2 from
 * the start (left) of arg1 */
static str
STRLtrim2(str *res, const str *arg1, const str *arg2)
{
	str buf = NULL, msg = MAL_SUCCEED;
	const char *s = *arg1, *s2 = *arg2;

	if (strNil(s) || strNil(s2)) {
		*res = GDKstrdup(str_nil);
	} else {
		size_t buflen = INITIAL_STR_BUFFER_LENGTH * sizeof(int);

		*res = NULL;
		if (!(buf = GDKmalloc(buflen)))
			throw(MAL, "str.ltrim2", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		if ((msg = str_ltrim2(&buf, &buflen, s, s2)) != MAL_SUCCEED) {
			GDKfree(buf);
			return msg;
		}
		*res = GDKstrdup(buf);
	}

	GDKfree(buf);
	if (!*res)
		msg = createException(MAL, "str.ltrim2",
							  SQLSTATE(HY013) MAL_MALLOC_FAIL);
	return msg;
}

str
str_rtrim2(str *buf, size_t *buflen, const char *s, const char *s2)
{
	str msg = MAL_SUCCEED;
	size_t len, n, n2, n3;

	if ((n2 = strlen(s2)) == 0) {
		len = strlen(s) + 1;
		CHECK_STR_BUFFER_LENGTH(buf, buflen, len, "str.rtrim2");
		strcpy(*buf, s);
		return MAL_SUCCEED;
	} else {
		if ((msg = trimchars(buf, buflen, &n3, s2, n2, "str.ltrim2")) != MAL_SUCCEED)
			return msg;
		len = strlen(s);
		n = rstrip(s, len, *(int **) buf, n3);
		n++;

		CHECK_STR_BUFFER_LENGTH(buf, buflen, n, "str.rtrim2");
		strcpy_len(*buf, s, n);
		return MAL_SUCCEED;
	}
}

/* remove the longest string containing only characters from arg2 from
 * the end (right) of arg1 */
static str
STRRtrim2(str *res, const str *arg1, const str *arg2)
{
	str buf = NULL, msg = MAL_SUCCEED;
	const char *s = *arg1, *s2 = *arg2;

	if (strNil(s) || strNil(s2)) {
		*res = GDKstrdup(str_nil);
	} else {
		size_t buflen = INITIAL_STR_BUFFER_LENGTH * sizeof(int);

		*res = NULL;
		if (!(buf = GDKmalloc(buflen)))
			throw(MAL, "str.rtrim2", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		if ((msg = str_rtrim2(&buf, &buflen, s, s2)) != MAL_SUCCEED) {
			GDKfree(buf);
			return msg;
		}
		*res = GDKstrdup(buf);
	}

	GDKfree(buf);
	if (!*res)
		msg = createException(MAL, "str.rtrim2",
							  SQLSTATE(HY013) MAL_MALLOC_FAIL);
	return msg;
}

static str
pad(str *buf, size_t *buflen, const char *s, const char *pad, int len, int left,
	const char *malfunc)
{
	size_t slen, padlen, repeats, residual, i, nlen;
	char *res;

	if (len < 0)
		len = 0;

	slen = (size_t) UTF8_strlen(s);
	if (slen > (size_t) len) {
		/* truncate */
		pad = UTF8_strtail(s, len);
		slen = pad - s + 1;

		CHECK_STR_BUFFER_LENGTH(buf, buflen, slen, malfunc);
		strcpy_len(*buf, s, slen);
		return MAL_SUCCEED;
	}

	padlen = (size_t) UTF8_strlen(pad);
	if (slen == (size_t) len || padlen == 0) {
		/* nothing to do (no padding if there is no pad string) */
		slen = strlen(s) + 1;
		CHECK_STR_BUFFER_LENGTH(buf, buflen, slen, malfunc);
		strcpy(*buf, s);
		return MAL_SUCCEED;
	}

	repeats = ((size_t) len - slen) / padlen;
	residual = ((size_t) len - slen) % padlen;
	if (residual > 0)
		residual = (size_t) (UTF8_strtail(pad, (int) residual) - pad);
	padlen = strlen(pad);
	slen = strlen(s);

	nlen = slen + repeats * padlen + residual + 1;
	CHECK_STR_BUFFER_LENGTH(buf, buflen, nlen, malfunc);
	res = *buf;
	if (left) {
		for (i = 0; i < repeats; i++)
			memcpy(res + i * padlen, pad, padlen);
		if (residual > 0)
			memcpy(res + repeats * padlen, pad, residual);
		if (slen > 0)
			memcpy(res + repeats * padlen + residual, s, slen);
	} else {
		if (slen > 0)
			memcpy(res, s, slen);
		for (i = 0; i < repeats; i++)
			memcpy(res + slen + i * padlen, pad, padlen);
		if (residual > 0)
			memcpy(res + slen + repeats * padlen, pad, residual);
	}
	res[repeats * padlen + residual + slen] = 0;
	return MAL_SUCCEED;
}

str
str_lpad(str *buf, size_t *buflen, const char *s, int len)
{
	return pad(buf, buflen, s, " ", len, 1, "str.lpad");
}

/* Fill up 'arg1' to length 'len' by prepending whitespaces.
 * If 'arg1' is already longer than 'len', then it's truncated on the right
 * (NB: this is the PostgreSQL definition).
 *
 * Example: lpad('hi', 5)
 * Result: '   hi'
 */
static str
STRLpad(str *res, const str *arg1, const int *len)
{
	str buf = NULL, msg = MAL_SUCCEED;
	const char *s = *arg1;
	int l = *len;

	if (strNil(s) || is_int_nil(l)) {
		*res = GDKstrdup(str_nil);
	} else {
		size_t buflen = INITIAL_STR_BUFFER_LENGTH;

		*res = NULL;
		if (!(buf = GDKmalloc(buflen)))
			throw(MAL, "str.lpad", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		if ((msg = str_lpad(&buf, &buflen, s, l)) != MAL_SUCCEED) {
			GDKfree(buf);
			return msg;
		}
		*res = GDKstrdup(buf);
	}

	GDKfree(buf);
	if (!*res)
		msg = createException(MAL, "str.lpad", SQLSTATE(HY013) MAL_MALLOC_FAIL);
	return msg;
}

str
str_rpad(str *buf, size_t *buflen, const char *s, int len)
{
	return pad(buf, buflen, s, " ", len, 0, "str.lpad");
}

/* Fill up 'arg1' to length 'len' by appending whitespaces.
 * If 'arg1' is already longer than 'len', then it's truncated (on the right)
 * (NB: this is the PostgreSQL definition).
 *
 * Example: rpad('hi', 5)
 * Result: 'hi   '
 */
static str
STRRpad(str *res, const str *arg1, const int *len)
{
	str buf = NULL, msg = MAL_SUCCEED;
	const char *s = *arg1;
	int l = *len;

	if (strNil(s) || is_int_nil(l)) {
		*res = GDKstrdup(str_nil);
	} else {
		size_t buflen = INITIAL_STR_BUFFER_LENGTH;

		*res = NULL;
		if (!(buf = GDKmalloc(buflen)))
			throw(MAL, "str.rpad", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		if ((msg = str_rpad(&buf, &buflen, s, l)) != MAL_SUCCEED) {
			GDKfree(buf);
			return msg;
		}
		*res = GDKstrdup(buf);
	}

	GDKfree(buf);
	if (!*res)
		msg = createException(MAL, "str.rpad", SQLSTATE(HY013) MAL_MALLOC_FAIL);
	return msg;
}

str
str_lpad3(str *buf, size_t *buflen, const char *s, int len, const char *s2)
{
	return pad(buf, buflen, s, s2, len, 1, "str.lpad2");
}

/* Fill up 'arg1' to length 'len' by prepending characters from 'arg2'
 * If 'arg1' is already longer than 'len', then it's truncated on the right
 * (NB: this is the PostgreSQL definition).
 *
 * Example: lpad('hi', 5, 'xy')
 * Result: xyxhi
 */
static str
STRLpad3(str *res, const str *arg1, const int *len, const str *arg2)
{
	str buf = NULL, msg = MAL_SUCCEED;
	const char *s = *arg1, *s2 = *arg2;
	int l = *len;

	if (strNil(s) || strNil(s2) || is_int_nil(l)) {
		*res = GDKstrdup(str_nil);
	} else {
		size_t buflen = INITIAL_STR_BUFFER_LENGTH;

		*res = NULL;
		if (!(buf = GDKmalloc(buflen)))
			throw(MAL, "str.lpad2", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		if ((msg = str_lpad3(&buf, &buflen, s, l, s2)) != MAL_SUCCEED) {
			GDKfree(buf);
			return msg;
		}
		*res = GDKstrdup(buf);
	}

	GDKfree(buf);
	if (!*res)
		msg = createException(MAL, "str.lpad2",
							  SQLSTATE(HY013) MAL_MALLOC_FAIL);
	return msg;
}

str
str_rpad3(str *buf, size_t *buflen, const char *s, int len, const char *s2)
{
	return pad(buf, buflen, s, s2, len, 0, "str.rpad2");
}

/* Fill up 'arg1' to length 'len' by appending characters from 'arg2'
 * If 'arg1' is already longer than 'len', then it's truncated (on the right)
 * (NB: this is the PostgreSQL definition).
 *
 * Example: rpad('hi', 5, 'xy')
 * Result: hixyx
 */
static str
STRRpad3(str *res, const str *arg1, const int *len, const str *arg2)
{
	str buf = NULL, msg = MAL_SUCCEED;
	const char *s = *arg1, *s2 = *arg2;
	int l = *len;

	if (strNil(s) || strNil(s2) || is_int_nil(l)) {
		*res = GDKstrdup(str_nil);
	} else {
		size_t buflen = INITIAL_STR_BUFFER_LENGTH;

		*res = NULL;
		if (!(buf = GDKmalloc(buflen)))
			throw(MAL, "str.rpad2", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		if ((msg = str_rpad3(&buf, &buflen, s, l, s2)) != MAL_SUCCEED) {
			GDKfree(buf);
			return msg;
		}
		*res = GDKstrdup(buf);
	}

	GDKfree(buf);
	if (!*res)
		msg = createException(MAL, "str.rpad2",
							  SQLSTATE(HY013) MAL_MALLOC_FAIL);
	return msg;
}

str
str_substitute(str *buf, size_t *buflen, const char *s, const char *src,
			   const char *dst, bit repeat)
{
	size_t lsrc = strlen(src), ldst = strlen(dst), n, l = strlen(s);
	char *b, *fnd;
	const char *pfnd;

	if (!lsrc || !l) {			/* s/src is an empty string, there's nothing to substitute */
		l++;
		CHECK_STR_BUFFER_LENGTH(buf, buflen, l, "str.substitute");
		strcpy(*buf, s);
		return MAL_SUCCEED;
	}

	n = l + ldst;
	if (repeat && ldst > lsrc)
		n = (ldst * l) / lsrc;	/* max length */

	n++;
	CHECK_STR_BUFFER_LENGTH(buf, buflen, n, "str.substitute");
	b = *buf;
	pfnd = s;
	do {
		fnd = strstr(pfnd, src);
		if (fnd == NULL)
			break;
		n = fnd - pfnd;
		if (n > 0) {
			strcpy_len(b, pfnd, n + 1);
			b += n;
		}
		if (ldst > 0) {
			strcpy_len(b, dst, ldst + 1);
			b += ldst;
		}
		if (*fnd == 0)
			break;
		pfnd = fnd + lsrc;
	} while (repeat);
	strcpy(b, pfnd);
	return MAL_SUCCEED;
}

static str
STRSubstitute(str *res, const str *arg1, const str *arg2, const str *arg3,
			  const bit *g)
{
	str buf = NULL, msg = MAL_SUCCEED;
	const char *s = *arg1, *s2 = *arg2, *s3 = *arg3;

	if (strNil(s) || strNil(s2) || strNil(s3)) {
		*res = GDKstrdup(str_nil);
	} else {
		size_t buflen = INITIAL_STR_BUFFER_LENGTH;

		*res = NULL;
		if (!(buf = GDKmalloc(buflen)))
			throw(MAL, "str.substitute", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		if ((msg = str_substitute(&buf, &buflen, s, s2, s3, *g)) != MAL_SUCCEED) {
			GDKfree(buf);
			return msg;
		}
		*res = GDKstrdup(buf);
	}

	GDKfree(buf);
	if (!*res)
		msg = createException(MAL, "str.substitute",
							  SQLSTATE(HY013) MAL_MALLOC_FAIL);
	return msg;
}

static str
STRascii(int *ret, const str *s)
{
	return str_wchr_at(ret, *s, 0);
}

str
str_substring_tail(str *buf, size_t *buflen, const char *s, int start)
{
	if (start < 1)
		start = 1;
	start--;
	return str_tail(buf, buflen, s, start);
}

static str
STRsubstringTail(str *res, const str *arg1, const int *start)
{
	str buf = NULL, msg = MAL_SUCCEED;
	const char *s = *arg1;
	int st = *start;

	if (strNil(s) || is_int_nil(st)) {
		*res = GDKstrdup(str_nil);
	} else {
		size_t buflen = INITIAL_STR_BUFFER_LENGTH;

		*res = NULL;
		if (!(buf = GDKmalloc(buflen)))
			throw(MAL, "str.substringTail", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		if ((msg = str_substring_tail(&buf, &buflen, s, st)) != MAL_SUCCEED) {
			GDKfree(buf);
			return msg;
		}
		*res = GDKstrdup(buf);
	}

	GDKfree(buf);
	if (!*res)
		msg = createException(MAL, "str.substringTail",
							  SQLSTATE(HY013) MAL_MALLOC_FAIL);
	return msg;
}

str
str_sub_string(str *buf, size_t *buflen, const char *s, int start, int l)
{
	if (start < 1)
		start = 1;
	start--;
	return str_Sub_String(buf, buflen, s, start, l);
}

static str
STRsubstring(str *res, const str *arg1, const int *start, const int *ll)
{
	str buf = NULL, msg = MAL_SUCCEED;
	const char *s = *arg1;
	int st = *start, l = *ll;

	if (strNil(s) || is_int_nil(st) || is_int_nil(l)) {
		*res = GDKstrdup(str_nil);
	} else {
		size_t buflen = INITIAL_STR_BUFFER_LENGTH;

		*res = NULL;
		if (!(buf = GDKmalloc(buflen)))
			throw(MAL, "str.substring", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		if ((msg = str_sub_string(&buf, &buflen, s, st, l)) != MAL_SUCCEED) {
			GDKfree(buf);
			return msg;
		}
		*res = GDKstrdup(buf);
	}

	GDKfree(buf);
	if (!*res)
		msg = createException(MAL, "str.substring",
							  SQLSTATE(HY013) MAL_MALLOC_FAIL);
	return msg;
}

static str
STRprefix(str *res, const str *arg1, const int *ll)
{
	str buf = NULL, msg = MAL_SUCCEED;
	const char *s = *arg1;
	int l = *ll;

	if (strNil(s) || is_int_nil(l)) {
		*res = GDKstrdup(str_nil);
	} else {
		size_t buflen = INITIAL_STR_BUFFER_LENGTH;

		*res = NULL;
		if (!(buf = GDKmalloc(buflen)))
			throw(MAL, "str.prefix", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		if ((msg = str_Sub_String(&buf, &buflen, s, 0, l)) != MAL_SUCCEED) {
			GDKfree(buf);
			return msg;
		}
		*res = GDKstrdup(buf);
	}

	GDKfree(buf);
	if (!*res)
		msg = createException(MAL, "str.prefix",
							  SQLSTATE(HY013) MAL_MALLOC_FAIL);
	return msg;
}

str
str_suffix(str *buf, size_t *buflen, const char *s, int l)
{
	int start = (int) (strlen(s) - l);
	return str_Sub_String(buf, buflen, s, start, l);
}

static str
STRsuffix(str *res, const str *arg1, const int *ll)
{
	str buf = NULL, msg = MAL_SUCCEED;
	const char *s = *arg1;
	int l = *ll;

	if (strNil(s) || is_int_nil(l)) {
		*res = GDKstrdup(str_nil);
	} else {
		size_t buflen = INITIAL_STR_BUFFER_LENGTH;

		*res = NULL;
		if (!(buf = GDKmalloc(buflen)))
			throw(MAL, "str.suffix", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		if ((msg = str_suffix(&buf, &buflen, s, l)) != MAL_SUCCEED) {
			GDKfree(buf);
			return msg;
		}
		*res = GDKstrdup(buf);
	}

	GDKfree(buf);
	if (!*res)
		msg = createException(MAL, "str.suffix",
							  SQLSTATE(HY013) MAL_MALLOC_FAIL);
	return msg;
}

int
str_locate2(const char *needle, const char *haystack, int start)
{
	int off, res;
	char *s;

	off = start <= 0 ? 1 : start;
	s = UTF8_strtail(haystack, off - 1);
	res = str_search(s, needle, str_strlen(needle));
	return res >= 0 ? res + off : 0;
}

static str
STRlocate3(int *ret, const str *needle, const str *haystack, const int *start)
{
	const char *s = *needle, *s2 = *haystack;
	int st = *start;

	*ret = (strNil(s) || strNil(s2)
			|| is_int_nil(st)) ? int_nil : str_locate2(s, s2, st);
	return MAL_SUCCEED;
}

static str
STRlocate(int *ret, const str *needle, const str *haystack)
{
	const char *s = *needle, *s2 = *haystack;

	*ret = (strNil(s) || strNil(s2)) ? int_nil : str_locate2(s, s2, 1);
	return MAL_SUCCEED;
}

str
str_insert(str *buf, size_t *buflen, const char *s, int strt, int l,
		   const char *s2)
{
	str v;
	int l1 = UTF8_strlen(s);
	size_t nextlen;

	if (l < 0)
		throw(MAL, "str.insert",
			  SQLSTATE(42000)
			  "The number of characters for insert function must be non negative");
	if (strt < 0) {
		if (-strt <= l1)
			strt = l1 + strt;
		else
			strt = 0;
	}
	if (strt > l1)
		strt = l1;

	nextlen = strlen(s) + strlen(s2) + 1;
	CHECK_STR_BUFFER_LENGTH(buf, buflen, nextlen, "str.insert");
	v = *buf;
	if (strt > 0)
		v = UTF8_strncpy(v, s, strt);
	strcpy(v, s2);
	if (strt + l < l1)
		strcat(v, UTF8_offset((char *) s, strt + l));
	return MAL_SUCCEED;
}

static str
STRinsert(str *res, const str *input, const int *start, const int *nchars,
		  const str *input2)
{
	str buf = NULL, msg = MAL_SUCCEED;
	const char *s = *input, *s2 = *input2;
	int st = *start, n = *nchars;

	if (strNil(s) || is_int_nil(st) || is_int_nil(n) || strNil(s2)) {
		*res = GDKstrdup(str_nil);
	} else {
		size_t buflen = INITIAL_STR_BUFFER_LENGTH;

		*res = NULL;
		if (!(buf = GDKmalloc(buflen)))
			throw(MAL, "str.insert", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		if ((msg = str_insert(&buf, &buflen, s, st, n, s2)) != MAL_SUCCEED) {
			GDKfree(buf);
			return msg;
		}
		*res = GDKstrdup(buf);
	}

	GDKfree(buf);
	if (!*res)
		msg = createException(MAL, "str.insert",
							  SQLSTATE(HY013) MAL_MALLOC_FAIL);
	return msg;
}

static str
STRreplace(str *ret, const str *s1, const str *s2, const str *s3)
{
	bit flag = TRUE;
	return STRSubstitute(ret, s1, s2, s3, &flag);
}

str
str_repeat(str *buf, size_t *buflen, const char *s, int c)
{
	size_t l = strlen(s), nextlen;

	if (l >= INT_MAX)
		throw(MAL, "str.repeat", SQLSTATE(HY013) MAL_MALLOC_FAIL);
	nextlen = (size_t) c *l + 1;

	CHECK_STR_BUFFER_LENGTH(buf, buflen, nextlen, "str.repeat");
	str t = *buf;
	*t = 0;
	for (int i = c; i > 0; i--, t += l)
		strcpy(t, s);
	return MAL_SUCCEED;
}

static str
STRrepeat(str *res, const str *arg1, const int *c)
{
	str buf = NULL, msg = MAL_SUCCEED;
	const char *s = *arg1;
	int cc = *c;

	if (strNil(s) || is_int_nil(cc) || cc < 0) {
		*res = GDKstrdup(str_nil);
	} else {
		size_t buflen = INITIAL_STR_BUFFER_LENGTH;

		*res = NULL;
		if (!(buf = GDKmalloc(buflen)))
			throw(MAL, "str.repeat", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		if ((msg = str_repeat(&buf, &buflen, s, cc)) != MAL_SUCCEED) {
			GDKfree(buf);
			return msg;
		}
		*res = GDKstrdup(buf);
	}

	GDKfree(buf);
	if (!*res)
		msg = createException(MAL, "str.repeat",
							  SQLSTATE(HY013) MAL_MALLOC_FAIL);
	return msg;
}

static str
STRspace(str *res, const int *ll)
{
	str buf = NULL, msg = MAL_SUCCEED;
	int l = *ll;

	if (is_int_nil(l) || l < 0) {
		*res = GDKstrdup(str_nil);
	} else {
		const char space[] = " ", *s = space;
		size_t buflen = INITIAL_STR_BUFFER_LENGTH;

		*res = NULL;
		if (!(buf = GDKmalloc(buflen)))
			throw(MAL, "str.space", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		if ((msg = str_repeat(&buf, &buflen, s, l)) != MAL_SUCCEED) {
			GDKfree(buf);
			return msg;
		}
		*res = GDKstrdup(buf);
	}

	GDKfree(buf);
	if (!*res)
		msg = createException(MAL, "str.space",
							  SQLSTATE(HY013) MAL_MALLOC_FAIL);
	return msg;
}

static str
STRasciify(str *r, const str *s)
{
#ifdef HAVE_ICONV

	if (strNil(*s)) {
		if ((*r = GDKstrdup(str_nil)) == NULL)
			throw(MAL, "str.asciify", SQLSTATE(HY013) MAL_MALLOC_FAIL);
		else
			return MAL_SUCCEED;
	}

	iconv_t cd;
	const str f = "UTF-8", t = "ASCII//TRANSLIT";
	str in = *s, out;
	size_t in_len = strlen(in), out_len = in_len * 4; /* oversized as a single utf8 char could change into multiple ascii char */

	if ((cd = iconv_open(t, f)) == (iconv_t) (-1))
		throw(MAL, "str.asciify", "ICONV: cannot convert from (%s) to (%s).", f, t);

	if ((*r = out = GDKmalloc(out_len)) == NULL) {
		iconv_close(cd);
		throw(MAL, "str.asciify", SQLSTATE(HY013) MAL_MALLOC_FAIL);
	}

	str o = out;

	if (iconv(cd, &in, &in_len, &o, &out_len) == (size_t) -1) {
		GDKfree(out);
		*r = NULL;
		iconv_close(cd);
		throw(MAL, "str.asciify", "Conversion failed, possibly due to system locale %s.", setlocale(0, NULL));
	}

	*o = '\0';
	iconv_close(cd);
	return MAL_SUCCEED;

#else
	throw(MAL, "str.asciify", "ICONV library not available.");
#endif
}

static inline void
BBPnreclaim(int nargs, ...)
{
	va_list valist;
	va_start(valist, nargs);
	for (int i = 0; i < nargs; i++) {
		BAT *b = va_arg(valist, BAT *);
		BBPreclaim(b);
	}
	va_end(valist);
}

/* scan select loop with or without candidates */
#define scanloop(TEST, KEEP_NULLS)									    \
	do {																\
		TRC_DEBUG(ALGO,												\
				  "scanselect(b=%s#"BUNFMT",anti=%d): "				\
				  "scanselect %s\n", BATgetId(b), BATcount(b),			\
				  anti, #TEST);										\
		if (!s || BATtdense(s)) {										\
			for (; p < q; p++) {										\
				GDK_CHECK_TIMEOUT(timeoffset, counter,					\
								  GOTO_LABEL_TIMEOUT_HANDLER(bailout)); \
				const char *restrict v = BUNtvar(bi, p - off);			\
				if ((TEST) || ((KEEP_NULLS) && *v == '\200'))			\
					vals[cnt++] = p;									\
			}															\
		} else {														\
			for (; p < ncands; p++) {									\
				GDK_CHECK_TIMEOUT(timeoffset, counter,					\
								  GOTO_LABEL_TIMEOUT_HANDLER(bailout)); \
				oid o = canditer_next(ci);								\
				const char *restrict v = BUNtvar(bi, o - off);			\
				if ((TEST) || ((KEEP_NULLS) && *v == '\200'))			\
					vals[cnt++] = o;									\
			}															\
		}																\
	} while (0)

/* scan select loop with or without candidates */
#define scanloop_anti(TEST, KEEP_NULLS)							    \
	do {																\
		TRC_DEBUG(ALGO,												\
				  "scanselect(b=%s#"BUNFMT",anti=%d): "				\
				  "scanselect %s\n", BATgetId(b), BATcount(b),			\
				  anti, #TEST);										\
		if (!s || BATtdense(s)) {										\
			for (; p < q; p++) {										\
				GDK_CHECK_TIMEOUT(timeoffset, counter,					\
								  GOTO_LABEL_TIMEOUT_HANDLER(bailout)); \
				const char *restrict v = BUNtvar(bi, p - off);			\
				if ((TEST) || ((KEEP_NULLS) && *v == '\200'))			\
					vals[cnt++] = p;									\
			}															\
		} else {														\
			for (; p < ncands; p++) {									\
				GDK_CHECK_TIMEOUT(timeoffset, counter,					\
								  GOTO_LABEL_TIMEOUT_HANDLER(bailout)); \
				oid o = canditer_next(ci);								\
				const char *restrict v = BUNtvar(bi, o - off);			\
				if ((TEST) || ((KEEP_NULLS) && *v == '\200'))			\
					vals[cnt++] = o;									\
			}															\
		}																\
	} while (0)

static str
str_select(BAT *bn, BAT *b, BAT *s, struct canditer *ci, BUN p, BUN q,
				 BUN *rcnt, const char *key, bool anti,
				 int (*str_cmp)(const char *, const char *, int),
				 bool keep_nulls)
{
	BATiter bi = bat_iterator(b);
	BUN cnt = 0, ncands = ci->ncand;
	oid off = b->hseqbase, *restrict vals = Tloc(bn, 0);
	str msg = MAL_SUCCEED;
	int klen = str_strlen(key);

	size_t counter = 0;
	lng timeoffset = 0;
	QryCtx *qry_ctx = MT_thread_get_qry_ctx();
	if (qry_ctx != NULL)
		timeoffset = (qry_ctx->starttime
					  && qry_ctx->querytimeout) ? (qry_ctx->starttime +
												   qry_ctx->querytimeout) : 0;

	if (anti)					/* keep nulls ? (use false for now) */
		scanloop_anti(v && *v != '\200' && str_cmp(v, key, klen) != 0, keep_nulls);
	else
		scanloop(v && *v != '\200' && str_cmp(v, key, klen) == 0, keep_nulls);

  bailout:
	bat_iterator_end(&bi);
	*rcnt = cnt;
	return msg;
}

static str
STRselect(bat *r_id, const bat *b_id, const bat *cb_id, const char *key,
			  const bit anti, int (*str_cmp)(const char *, const char *, int),
			  const str fname)
{
	str msg = MAL_SUCCEED;

	BAT *b, *cb = NULL, *r = NULL, *old_s = NULL;;
	BUN p = 0, q = 0, rcnt = 0;
	struct canditer ci;
	bool with_strimps = false,
		with_strimps_anti = false;

	if (!(b = BATdescriptor(*b_id)))
		throw(MAL, fname, RUNTIME_OBJECT_MISSING);

	if (cb_id && !is_bat_nil(*cb_id) && !(cb = BATdescriptor(*cb_id))) {
		BBPreclaim(b);
		throw(MAL, fname, RUNTIME_OBJECT_MISSING);
	}

	assert(ATOMstorage(b->ttype) == TYPE_str);

	if (BAThasstrimps(b)) {
		if (STRMPcreate(b, NULL) == GDK_SUCCEED) {
			BAT *tmp_s = STRMPfilter(b, cb, key, anti);
			if (tmp_s) {
				old_s = cb;
				cb = tmp_s;
				if (!anti)
					with_strimps = true;
				else
					with_strimps_anti = true;
			}
		} else {
			GDKclrerr();
		}
	}

	MT_thread_setalgorithm(with_strimps ?
						   "string_select: strcmp function using strimps" :
						   (with_strimps_anti ?
							"string_select: strcmp function using strimps anti"
							: "string_select: strcmp function with no accelerator"));

	canditer_init(&ci, b, cb);
	if (!(r = COLnew(0, TYPE_oid, ci.ncand, TRANSIENT))) {
		BBPnreclaim(2, b, cb);
		throw(MAL, fname, SQLSTATE(HY013) MAL_MALLOC_FAIL);
	}

	if (!cb || BATtdense(cb)) {
		if (cb) {
			assert(BATtdense(cb));
			p = (BUN) cb->tseqbase;
			q = p + BATcount(cb);
			if ((oid) p < b->hseqbase)
				p = b->hseqbase;
			if ((oid) q > b->hseqbase + BATcount(b))
				q = b->hseqbase + BATcount(b);
		} else {
			p = b->hseqbase;
			q = BATcount(b) + b->hseqbase;
		}
	}

	msg = str_select(r, b, cb, &ci, p, q, &rcnt, key, anti
					 && !with_strimps_anti, str_cmp, with_strimps_anti);

	if (!msg) {
		BATsetcount(r, rcnt);
		r->tsorted = r->batCount <= 1;
		r->trevsorted = r->batCount <= 1;
		r->tkey = false;
		r->tnil = false;
		r->tnonil = true;
		r->tseqbase = rcnt == 0 ?
			0 : rcnt == 1 ?
			*(const oid *) Tloc(r, 0) : rcnt == b->batCount ? b->hseqbase : oid_nil;

		if (with_strimps_anti) {
			BAT *rev;
			if (old_s) {
				rev = BATdiffcand(old_s, r);
#ifndef NDEBUG
				BAT *is = BATintersectcand(old_s, r);
				if (is) {
					assert(is->batCount == r->batCount);
					BBPreclaim(is);
				}
				assert(rev->batCount == old_s->batCount - r->batCount);
#endif
			} else
				rev = BATnegcands(b->batCount, r);

			BBPreclaim(r);
			r = rev;
			if (r == NULL)
				msg = createException(MAL, fname, SQLSTATE(HY013) MAL_MALLOC_FAIL);
		}
	}

	if (r && !msg) {
		*r_id = r->batCacheid;
		BBPkeepref(r);
	} else {
		BBPreclaim(r);
	}

	BBPnreclaim(3, b, cb, old_s);
	return msg;
}

#define STRSELECT_MAPARGS(STK, PCI, R_ID, B_ID, CB_ID, KEY, ICASE, ANTI) \
	do {																\
		R_ID = getArgReference(STK, PCI, 0);							\
		B_ID = getArgReference(STK, PCI, 1);							\
		CB_ID = getArgReference(STK, PCI, 2);							\
		KEY = *getArgReference_str(STK, PCI, 3);						\
		ICASE = PCI->argc == 5 ? false : true;							\
		ANTI = PCI->argc == 5 ? *getArgReference_bit(STK, PCI, 4) :	\
			*getArgReference_bit(STK, PCI, 5);							\
	} while (0)

/**
 * @r_id: result oid
 * @b_id: input bat oid
 * @cb_id: input bat candidates oid
 * @key: input string
 * @icase: ignore case
 * @anti: anti join
 */
static str
STRstartswithselect(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	(void) cntxt;
	(void) mb;

	bat *r_id = NULL, *b_id = NULL, *cb_id = NULL;
	char *key = NULL;
	bit icase = 0, anti = 0;

	STRSELECT_MAPARGS(stk, pci, r_id, b_id, cb_id, key, icase, anti);
	return STRselect(r_id, b_id, cb_id, key, anti,
					 icase ? str_is_iprefix : str_is_prefix, "str.startswithselect");
}

/**
 * @r_id: result oid
 * @b_id: input bat oid
 * @cb_id: input bat candidates oid
 * @key: input string
 * @icase: ignore case
 * @anti: anti join
 */
static str
STRendswithselect(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	(void) cntxt;
	(void) mb;

	bat *r_id = NULL, *b_id = NULL, *cb_id = NULL;
	char *key = NULL;
	bit icase = 0, anti = 0;

	STRSELECT_MAPARGS(stk, pci, r_id, b_id, cb_id, key, icase, anti);
	return STRselect(r_id, b_id, cb_id, key, anti,
					 icase ? str_is_isuffix : str_is_suffix, "str.endswithselect");
}

/**
 * @r_id: result oid
 * @b_id: input bat oid
 * @cb_id: input bat candidates oid
 * @key: input string
 * @icase: ignore case
 * @anti: anti join
 */
static str
STRcontainsselect(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	(void) cntxt;
	(void) mb;

	bat *r_id = NULL, *b_id = NULL, *cb_id = NULL;
	char *key = NULL;
	bit icase = 0, anti = 0;

	STRSELECT_MAPARGS(stk, pci, r_id, b_id, cb_id, key, icase, anti);
	return STRselect(r_id, b_id, cb_id, key, anti,
					 icase ? str_icontains : str_contains, "str.containsselect");
}

#define APPEND(b, o) (((oid *) b->theap->base)[b->batCount++] = (o))
#define VALUE(s, x)  (s##vars + VarHeapVal(s##vals, (x), s##i.width))

#define set_empty_bat_props(B)					\
	do {										\
		B->tnil = false;						\
		B->tnonil = true;						\
		B->tkey = true;						\
		B->tsorted = true;						\
		B->trevsorted = true;					\
		B->tseqbase = 0;						\
	} while (0)

#define CONTAINS_JOIN_LOOP(STR_CMP, STR_LEN)							\
	do {																\
		canditer_init(&rci, r, cr);									\
		for (BUN ridx = 0; ridx < rci.ncand; ridx++) {					\
			BAT *filtered_sl = NULL;									\
			GDK_CHECK_TIMEOUT(timeoffset, counter, GOTO_LABEL_TIMEOUT_HANDLER(exit)); \
			ro = canditer_next(&rci);									\
			vr = VALUE(r, ro - rbase);									\
			vr_len = STR_LEN;											\
			matches = 0;												\
			if (with_strimps)											\
				filtered_sl = STRMPfilter(l, cl, vr, anti);			\
			if (filtered_sl)											\
				canditer_init(&lci, l, filtered_sl);					\
			else														\
				canditer_init(&lci, l, cl);							\
			for (BUN lidx = 0; lidx < lci.ncand; lidx++) {				\
				lo = canditer_next(&lci);								\
				vl = VALUE(l, lo - lbase);								\
				if (strNil(vl))										\
					continue;											\
				if (STR_CMP)											\
					continue;											\
				if (BATcount(rl) == BATcapacity(rl)) {					\
					newcap = BATgrows(rl);								\
					BATsetcount(rl, BATcount(rl));						\
					if (rr)											\
						BATsetcount(rr, BATcount(rr));					\
					if (BATextend(rl, newcap) != GDK_SUCCEED ||		\
						(rr && BATextend(rr, newcap) != GDK_SUCCEED)) { \
						msg = createException(MAL, fname, SQLSTATE(HY013) MAL_MALLOC_FAIL);	\
						goto exit;										\
					}													\
					assert(!rr || BATcapacity(rl) == BATcapacity(rr));	\
				}														\
				if (BATcount(rl) > 0) {								\
					if (lastl + 1 != lo)								\
						rl->tseqbase = oid_nil;						\
					if (matches == 0) {								\
						if (rr)										\
							rr->trevsorted = false;					\
						if (lastl > lo) {								\
							rl->tsorted = false;						\
							rl->tkey = false;							\
						} else if (lastl < lo) {						\
							rl->trevsorted = false;					\
						} else {										\
							rl->tkey = false;							\
						}												\
					}													\
				}														\
				APPEND(rl, lo);										\
				if (rr)												\
					APPEND(rr, ro);									\
				lastl = lo;											\
				matches++;												\
			}															\
			BBPreclaim(filtered_sl);									\
			if (rr) {													\
				if (matches > 1) {										\
					rr->tkey = false;									\
					rr->tseqbase = oid_nil;							\
					rl->trevsorted = false;							\
				} else if (matches == 0) {								\
					rskipped = BATcount(rr) > 0;						\
				} else if (rskipped) {									\
					rr->tseqbase = oid_nil;							\
				}														\
			} else if (matches > 1) {									\
				rl->trevsorted = false;								\
			}															\
		}																\
	} while (0)

#define STR_JOIN_NESTED_LOOP(STR_CMP, STR_LEN, FNAME)					\
	do {																\
		canditer_init(&rci, r, cr);									\
		for (BUN ridx = 0; ridx < rci.ncand; ridx++) {					\
			GDK_CHECK_TIMEOUT(timeoffset, counter, GOTO_LABEL_TIMEOUT_HANDLER(exit)); \
			ro = canditer_next(&rci);									\
			vr = VALUE(r, ro - rbase);									\
			if (strNil(vr))											\
				continue;												\
			vr_len = STR_LEN;											\
			matches = 0;												\
			canditer_init(&lci, l, cl);								\
			for (BUN lidx = 0; lidx < lci.ncand; lidx++) {				\
				lo = canditer_next(&lci);								\
				vl = VALUE(l, lo - lbase);								\
				if (strNil(vl))										\
					continue;											\
				if (STR_CMP)											\
					continue;											\
				if (BATcount(rl) == BATcapacity(rl)) {					\
					newcap = BATgrows(rl);								\
					BATsetcount(rl, BATcount(rl));						\
					if (rr)											\
						BATsetcount(rr, BATcount(rr));					\
					if (BATextend(rl, newcap) != GDK_SUCCEED ||		\
						(rr && BATextend(rr, newcap) != GDK_SUCCEED)) { \
						msg = createException(MAL, FNAME, SQLSTATE(HY013) MAL_MALLOC_FAIL); \
						goto exit;										\
					}													\
					assert(!rr || BATcapacity(rl) == BATcapacity(rr));	\
				}														\
				if (BATcount(rl) > 0) {								\
					if (last_lo + 1 != lo)								\
						rl->tseqbase = oid_nil;						\
					if (matches == 0) {								\
						if (rr)										\
							rr->trevsorted = false;					\
						if (last_lo > lo) {							\
							rl->tsorted = false;						\
							rl->tkey = false;							\
						} else if (last_lo < lo) {						\
							rl->trevsorted = false;					\
						} else {										\
							rl->tkey = false;							\
						}												\
					}													\
				}														\
				APPEND(rl, lo);										\
				if (rr)												\
					APPEND(rr, ro);									\
				last_lo = lo;											\
				matches++;												\
			}															\
			if (rr) {													\
				if (matches > 1) {										\
					rr->tkey = false;									\
					rr->tseqbase = oid_nil;							\
					rl->trevsorted = false;							\
				} else if (matches == 0) {								\
					rskipped = BATcount(rr) > 0;						\
				} else if (rskipped) {									\
					rr->tseqbase = oid_nil;							\
				}														\
			} else if (matches > 1) {									\
				rl->trevsorted = false;								\
			}															\
		}																\
	} while (0)

#define STARTSWITH_SORTED_LOOP(STR_CMP, STR_LEN, FNAME)				\
	do {																\
		canditer_init(&rci, sorted_r, sorted_cr);						\
		canditer_init(&lci, sorted_l, sorted_cl);						\
		for (lx = 0; lx < lci.ncand; lx++) {							\
			lo = canditer_next(&lci);									\
			vl = VALUE(l, lo - lbase);									\
			if (!strNil(vl))											\
				break;													\
		}																\
		for (rx = 0; rx < rci.ncand; rx++) {							\
			ro = canditer_next(&rci);									\
			vr = VALUE(r, ro - rbase);									\
			if (!strNil(vr)) {											\
				canditer_setidx(&rci, rx);								\
				break;													\
			}															\
		}																\
		for (; rx < rci.ncand; rx++) {									\
			GDK_CHECK_TIMEOUT(timeoffset, counter, GOTO_LABEL_TIMEOUT_HANDLER(exit)); \
			ro = canditer_next(&rci);									\
			vr = VALUE(r, ro - rbase);									\
			vr_len = STR_LEN;									\
			matches = 0;												\
			for (canditer_setidx(&lci, lx), n = lx; n < lci.ncand; n++) { \
				lo = canditer_next(&lci);								\
				vl = VALUE(l, lo - lbase);								\
				cmp = STR_CMP;											\
				if (cmp < 0) {											\
					lx++;												\
					continue;											\
				}														\
				else if (cmp > 0)										\
					break;												\
				if (BATcount(rl) == BATcapacity(rl)) {					\
					newcap = BATgrows(rl);								\
					BATsetcount(rl, BATcount(rl));						\
					if (rr)											\
						BATsetcount(rr, BATcount(rr));					\
					if (BATextend(rl, newcap) != GDK_SUCCEED ||		\
						(rr && BATextend(rr, newcap) != GDK_SUCCEED)) { \
						msg = createException(MAL, FNAME, SQLSTATE(HY013) MAL_MALLOC_FAIL); \
						goto exit;										\
					}													\
					assert(!rr || BATcapacity(rl) == BATcapacity(rr));	\
				}														\
				if (BATcount(rl) > 0) {								\
					if (last_lo + 1 != lo)								\
						rl->tseqbase = oid_nil;						\
					if (matches == 0) {								\
						if (rr)										\
							rr->trevsorted = false;					\
						if (last_lo > lo) {							\
							rl->tsorted = false;						\
							rl->tkey = false;							\
						} else if (last_lo < lo) {						\
							rl->trevsorted = false;					\
						} else {										\
							rl->tkey = false;							\
						}												\
					}													\
				}														\
				APPEND(rl, lo);										\
				if (rr)												\
					APPEND(rr, ro);									\
				last_lo = lo;											\
				matches++;												\
			}															\
			if (rr) {													\
				if (matches > 1) {										\
					rr->tkey = false;									\
					rr->tseqbase = oid_nil;							\
					rl->trevsorted = false;							\
				} else if (matches == 0) {								\
					rskipped = BATcount(rr) > 0;						\
				} else if (rskipped) {									\
					rr->tseqbase = oid_nil;							\
				}														\
			} else if (matches > 1) {									\
				rl->trevsorted = false;								\
			}															\
		}																\
	} while (0)

static void
do_strrev(char *dst, const char *src, size_t len)
{
	dst[len] = 0;
	if (strNil(src)) {
		assert(len == strlen(str_nil));
		strcpy(dst, str_nil);
		return;
	}
	while (*src) {
		if ((*src & 0xF8) == 0xF0) {
			assert(len >= 4);
			dst[len - 4] = *src++;
			assert((*src & 0xC0) == 0x80);
			dst[len - 3] = *src++;
			assert((*src & 0xC0) == 0x80);
			dst[len - 2] = *src++;
			assert((*src & 0xC0) == 0x80);
			dst[len - 1] = *src++;
			len -= 4;
		} else if ((*src & 0xF0) == 0xE0) {
			assert(len >= 3);
			dst[len - 3] = *src++;
			assert((*src & 0xC0) == 0x80);
			dst[len - 2] = *src++;
			assert((*src & 0xC0) == 0x80);
			dst[len - 1] = *src++;
			len -= 3;
		} else if ((*src & 0xE0) == 0xC0) {
			assert(len >= 2);
			dst[len - 2] = *src++;
			assert((*src & 0xC0) == 0x80);
			dst[len - 1] = *src++;
			len -= 2;
		} else {
			assert(len >= 1);
			assert((*src & 0x80) == 0);
			dst[--len] = *src++;
		}
	}
	assert(len == 0);
}

static BAT *
batstr_strrev(BAT *b)
{
	BAT *bn = NULL;
	BATiter bi;
	BUN p, q;
	const char *src;
	size_t len;
	char *dst;
	size_t dstlen;

	dstlen = 1024;
	dst = GDKmalloc(dstlen);
	if (dst == NULL)
		return NULL;

	assert(b->ttype == TYPE_str);

	bn = COLnew(b->hseqbase, TYPE_str, BATcount(b), TRANSIENT);
	if (bn == NULL) {
		GDKfree(dst);
		return NULL;
	}

	bi = bat_iterator(b);
	BATloop(b, p, q) {
		src = (const char *) BUNtail(bi, p);
		len = strlen(src);
		if (len >= dstlen) {
			char *ndst;
			dstlen = len + 1024;
			ndst = GDKrealloc(dst, dstlen);
			if (ndst == NULL) {
				bat_iterator_end(&bi);
				BBPreclaim(bn);
				GDKfree(dst);
				return NULL;
			}
			dst = ndst;
		}
		do_strrev(dst, src, len);
		if (BUNappend(bn, dst, false) != GDK_SUCCEED) {
			bat_iterator_end(&bi);
			BBPreclaim(bn);
			GDKfree(dst);
			return NULL;
		}
	}

	bat_iterator_end(&bi);
	GDKfree(dst);
	return bn;
}

static BAT *
batstr_strlower(BAT *b)
{
	BAT *bn = NULL;
	BATiter bi;
	BUN p, q;

	assert(b->ttype == TYPE_str);

	bn = COLnew(b->hseqbase, TYPE_str, BATcount(b), TRANSIENT);
	if (bn == NULL)
		return NULL;

	bi = bat_iterator(b);
	BATloop(b, p, q) {
		str vb = BUNtail(bi, p), vb_low;
		if (STRlower(&vb_low, &vb)) {
			bat_iterator_end(&bi);
			BBPreclaim(bn);
			return NULL;
		}
		if (BUNappend(bn, vb, false) != GDK_SUCCEED) {
			bat_iterator_end(&bi);
			BBPreclaim(bn);
			return NULL;
		}
	}
	bat_iterator_end(&bi);
	return bn;
}

static str
str_join_nested(BAT *rl, BAT *rr, BAT *l, BAT *r, BAT *cl, BAT *cr,
				bit anti, int (*str_cmp)(const char *, const char *, int), str fname)
{
	str msg = MAL_SUCCEED;

	lng timeoffset = 0;
	QryCtx *qry_ctx = MT_thread_get_qry_ctx();
	if (qry_ctx != NULL)
		timeoffset = (qry_ctx->starttime && qry_ctx->querytimeout) ?
				(qry_ctx->starttime + qry_ctx->querytimeout) : 0;

	TRC_DEBUG(ALGO,
			  "(%s, %s, l=%s#" BUNFMT "[%s]%s%s,"
			  "r=%s#" BUNFMT "[%s]%s%s,sl=%s#" BUNFMT "%s%s,"
			  "sr=%s#" BUNFMT "%s%s)\n",
			  fname, "nested loop",
			  BATgetId(l), BATcount(l), ATOMname(l->ttype),
			  l->tsorted ? "-sorted" : "",
			  l->trevsorted ? "-revsorted" : "",
			  BATgetId(r), BATcount(r), ATOMname(r->ttype),
			  r->tsorted ? "-sorted" : "",
			  r->trevsorted ? "-revsorted" : "",
			  cl ? BATgetId(cl) : "NULL", cl ? BATcount(cl) : 0,
			  cl && cl->tsorted ? "-sorted" : "",
			  cl && cl->trevsorted ? "-revsorted" : "",
			  cr ? BATgetId(cr) : "NULL", cr ? BATcount(cr) : 0,
			  cr && cr->tsorted ? "-sorted" : "",
			  cr && cr->trevsorted ? "-revsorted" : "");

	assert(ATOMtype(l->ttype) == ATOMtype(r->ttype));
	assert(ATOMtype(l->ttype) == TYPE_str);

	BATiter li = bat_iterator(l);
	BATiter ri = bat_iterator(r);
	assert(ri.vh && r->ttype);

	struct canditer lci, rci;
	oid lbase = l->hseqbase,
		rbase = r->hseqbase,
		lo, ro, last_lo = 0;
	const char *lvals = (const char *) li.base,
		*rvals = (const char *) ri.base,
		*lvars = li.vh->base,
		*rvars = ri.vh->base,
		*vl, *vr;
	BUN matches, newcap;
	int rskipped = 0, vr_len = 0;
	size_t counter = 0;

	if (anti)
		STR_JOIN_NESTED_LOOP((str_cmp(vl, vr, vr_len) == 0), str_strlen(vr), fname);
	else
		STR_JOIN_NESTED_LOOP((str_cmp(vl, vr, vr_len) != 0), str_strlen(vr), fname);

	assert(!rr || BATcount(rl) == BATcount(rr));
	BATsetcount(rl, BATcount(rl));
	if (rr)
		BATsetcount(rr, BATcount(rr));

	if (BATcount(rl) > 0) {
		if (BATtdense(rl))
			rl->tseqbase = ((oid *) rl->theap->base)[0];
		if (rr && BATtdense(rr))
			rr->tseqbase = ((oid *) rr->theap->base)[0];
	} else {
		rl->tseqbase = 0;
		if (rr)
			rr->tseqbase = 0;
	}

	TRC_DEBUG(ALGO,
			  "(%s, l=%s,r=%s)=(%s#" BUNFMT "%s%s,%s#" BUNFMT "%s%s\n",
			  fname,
			  BATgetId(l), BATgetId(r), BATgetId(rl), BATcount(rl),
			  rl->tsorted ? "-sorted" : "",
			  rl->trevsorted ? "-revsorted" : "",
			  rr ? BATgetId(rr) : NULL, rr ? BATcount(rr) : 0,
			  rr && rr->tsorted ? "-sorted" : "",
			  rr && rr->trevsorted ? "-revsorted" : "");

exit:
	bat_iterator_end(&li);
	bat_iterator_end(&ri);
	return msg;
}

static str
contains_join(BAT *rl, BAT *rr, BAT *l, BAT *r, BAT *cl, BAT *cr, bit anti,
			  int (*str_cmp)(const char *, const char *, int), const str fname)
{
	str msg = MAL_SUCCEED;

	lng timeoffset = 0;
	QryCtx *qry_ctx = MT_thread_get_qry_ctx();
	if (qry_ctx != NULL)
		timeoffset = (qry_ctx->starttime && qry_ctx->querytimeout) ?
			(qry_ctx->starttime + qry_ctx->querytimeout) : 0;

	TRC_DEBUG(ALGO,
			  "(%s, l=%s#" BUNFMT "[%s]%s%s,"
			  "r=%s#" BUNFMT "[%s]%s%s,sl=%s#" BUNFMT "%s%s,"
			  "sr=%s#" BUNFMT "%s%s)\n",
			  fname,
			  BATgetId(l), BATcount(l), ATOMname(l->ttype),
			  l->tsorted ? "-sorted" : "",
			  l->trevsorted ? "-revsorted" : "",
			  BATgetId(r), BATcount(r), ATOMname(r->ttype),
			  r->tsorted ? "-sorted" : "",
			  r->trevsorted ? "-revsorted" : "",
			  cl ? BATgetId(cl) : "NULL", cl ? BATcount(cl) : 0,
			  cl && cl->tsorted ? "-sorted" : "",
			  cl && cl->trevsorted ? "-revsorted" : "",
			  cr ? BATgetId(cr) : "NULL", cr ? BATcount(cr) : 0,
			  cr && cr->tsorted ? "-sorted" : "",
			  cr && cr->trevsorted ? "-revsorted" : "");

	bool with_strimps = false;

	if (BAThasstrimps(l)) {
		with_strimps = true;
		if (STRMPcreate(l, NULL) != GDK_SUCCEED) {
			GDKclrerr();
			with_strimps = false;
		}
	}

	assert(ATOMtype(l->ttype) == ATOMtype(r->ttype));
	assert(ATOMtype(l->ttype) == TYPE_str);

	BATiter li = bat_iterator(l);
	BATiter ri = bat_iterator(r);
	assert(ri.vh && r->ttype);

	struct canditer lci, rci;
	oid lbase = l->hseqbase,
		rbase = r->hseqbase,
		lo, ro, lastl = 0;
	const char *lvals = (const char *) li.base,
		*rvals = (const char *) ri.base,
		*lvars = li.vh->base,
		*rvars = ri.vh->base,
		*vl, *vr;
	int rskipped = 0, vr_len = 0;
	BUN matches, newcap;
	size_t counter = 0;

	if (anti)
		CONTAINS_JOIN_LOOP(str_cmp(vl, vr, vr_len) == 0, str_strlen(vr));
	else
		CONTAINS_JOIN_LOOP(str_cmp(vl, vr, vr_len) != 0, str_strlen(vr));

	assert(!rr || BATcount(rl) == BATcount(rr));
	BATsetcount(rl, BATcount(rl));
	if (rr)
		BATsetcount(rr, BATcount(rr));
	if (BATcount(rl) > 0) {
		if (BATtdense(rl))
			rl->tseqbase = ((oid *) rl->theap->base)[0];
		if (rr && BATtdense(rr))
			rr->tseqbase = ((oid *) rr->theap->base)[0];
	} else {
		rl->tseqbase = 0;
		if (rr)
			rr->tseqbase = 0;
	}

	TRC_DEBUG(ALGO,
			  "(%s, l=%s,r=%s)=(%s#" BUNFMT "%s%s,%s#" BUNFMT "%s%s\n",
			  fname,
			  BATgetId(l), BATgetId(r), BATgetId(rl), BATcount(rl),
			  rl->tsorted ? "-sorted" : "",
			  rl->trevsorted ? "-revsorted" : "",
			  rr ? BATgetId(rr) : NULL, rr ? BATcount(rr) : 0,
			  rr && rr->tsorted ? "-sorted" : "",
			  rr && rr->trevsorted ? "-revsorted" : "");
exit:
	bat_iterator_end(&li);
	bat_iterator_end(&ri);
	return msg;
}

static str
startswith_join(BAT **rl_ptr, BAT **rr_ptr, BAT *l, BAT *r, BAT *cl, BAT *cr,
				bit anti, int (*str_cmp)(const char *, const char *, int), str fname)
{
	str msg = MAL_SUCCEED;
	gdk_return rc;

	lng timeoffset = 0;
	QryCtx *qry_ctx = MT_thread_get_qry_ctx();
	if (qry_ctx != NULL)
		timeoffset = (qry_ctx->starttime && qry_ctx->querytimeout) ?
			(qry_ctx->starttime + qry_ctx->querytimeout) : 0;

	assert(*rl_ptr && *rr_ptr);

	BAT *sorted_l = NULL, *sorted_r = NULL,
		*sorted_cl = NULL, *sorted_cr = NULL,
		*ord_sorted_l = NULL, *ord_sorted_r = NULL,
		*proj_rl = NULL, *proj_rr = NULL,
		*rl = *rl_ptr, *rr = *rr_ptr;

	TRC_DEBUG(ALGO,
			  "(%s, %s, l=%s#" BUNFMT "[%s]%s%s,"
			  "r=%s#" BUNFMT "[%s]%s%s,sl=%s#" BUNFMT "%s%s,"
			  "sr=%s#" BUNFMT "%s%s)\n",
			  fname, "sorted inputs",
			  BATgetId(l), BATcount(l), ATOMname(l->ttype),
			  l->tsorted ? "-sorted" : "",
			  l->trevsorted ? "-revsorted" : "",
			  BATgetId(r), BATcount(r), ATOMname(r->ttype),
			  r->tsorted ? "-sorted" : "",
			  r->trevsorted ? "-revsorted" : "",
			  cl ? BATgetId(cl) : "NULL", cl ? BATcount(cl) : 0,
			  cl && cl->tsorted ? "-sorted" : "",
			  cl && cl->trevsorted ? "-revsorted" : "",
			  cr ? BATgetId(cr) : "NULL", cr ? BATcount(cr) : 0,
			  cr && cr->tsorted ? "-sorted" : "",
			  cr && cr->trevsorted ? "-revsorted" : "");

	bool l_sorted = BATordered(l);
	bool r_sorted = BATordered(r);

	if (l_sorted == FALSE) {
		rc = BATsort(&sorted_l, &ord_sorted_l, NULL,
					 l, NULL, NULL, false, false, false);
		if (rc != GDK_SUCCEED) {
			throw(MAL, fname, "Sorting left input failed");
		} else {
			if (cl) {
				rc = BATsort(&sorted_cl, NULL, NULL,
							 cl, ord_sorted_l, NULL, false, false, false);
				if (rc != GDK_SUCCEED) {
					BBPnreclaim(2, sorted_l, ord_sorted_l);
					throw(MAL, fname, "Sorting left candidates input failed");
				}
			}
		}
	} else {
		sorted_l = l;
		sorted_cl = cl;
	}

	if (r_sorted == FALSE) {
		rc = BATsort(&sorted_r, &ord_sorted_r, NULL,
					 r, NULL, NULL, false, false, false);
		if (rc != GDK_SUCCEED) {
			BBPnreclaim(3, sorted_l, ord_sorted_l, sorted_cl);
			throw(MAL, fname, "Sorting right input failed");
		} else {
			if (cr) {
				rc = BATsort(&sorted_cr, NULL, NULL,
							 cr, ord_sorted_r, NULL, false, false, false);
				if (rc != GDK_SUCCEED) {
					BBPnreclaim(5, sorted_l, ord_sorted_l, sorted_cl, sorted_r, ord_sorted_r);
					throw(MAL, fname, "Sorting right candidates input failed");
				}
			}
		}
	} else {
		sorted_r = r;
		sorted_cr = cr;
	}

	assert(BATordered(sorted_l) && BATordered(sorted_r));

	BATiter li = bat_iterator(sorted_l);
	BATiter ri = bat_iterator(sorted_r);
	assert(ri.vh && r->ttype);

	struct canditer lci, rci;
	oid lbase = sorted_l->hseqbase,
		rbase = sorted_r->hseqbase,
		lo, ro, last_lo = 0;
	const char *lvals = (const char *) li.base,
		*rvals = (const char *) ri.base,
		*lvars = li.vh->base,
		*rvars = ri.vh->base,
		*vl, *vr;
	BUN matches, newcap, n = 0, rx = 0, lx = 0;
	int rskipped = 0, vr_len = 0, cmp = 0;
	size_t counter = 0;

	if (anti)
		STR_JOIN_NESTED_LOOP((str_cmp(vl, vr, vr_len) != 0), str_strlen(vr), fname);
	else
		STARTSWITH_SORTED_LOOP(str_cmp(vl, vr, vr_len), str_strlen(vr), fname);

	assert(!rr || BATcount(rl) == BATcount(rr));
	BATsetcount(rl, BATcount(rl));
	if (rr)
		BATsetcount(rr, BATcount(rr));

	if (BATcount(rl) > 0) {
		if (BATtdense(rl))
			rl->tseqbase = ((oid *) rl->theap->base)[0];
		if (rr && BATtdense(rr))
			rr->tseqbase = ((oid *) rr->theap->base)[0];
	} else {
		rl->tseqbase = 0;
		if (rr)
			rr->tseqbase = 0;
	}

	if (l_sorted == FALSE) {
		proj_rl = BATproject(rl, ord_sorted_l);
		if (!proj_rl) {
			msg = createException(MAL, fname, "Project left pre-sort order failed");
			goto exit;
		} else {
			BBPreclaim(rl);
			*rl_ptr = proj_rl;
		}
	}

	if (rr && r_sorted == FALSE) {
		proj_rr = BATproject(rr, ord_sorted_r);
		if (!proj_rr) {
			BBPreclaim(proj_rl);
			msg = createException(MAL, fname, "Project right pre-sort order failed");
			goto exit;
		} else {
			BBPreclaim(rr);
			*rr_ptr = proj_rr;
		}
	}

	TRC_DEBUG(ALGO,
			  "(%s, l=%s,r=%s)=(%s#" BUNFMT "%s%s,%s#" BUNFMT "%s%s\n",
			  fname,
			  BATgetId(l), BATgetId(r), BATgetId(rl), BATcount(rl),
			  rl->tsorted ? "-sorted" : "",
			  rl->trevsorted ? "-revsorted" : "",
			  rr ? BATgetId(rr) : NULL, rr ? BATcount(rr) : 0,
			  rr && rr->tsorted ? "-sorted" : "",
			  rr && rr->trevsorted ? "-revsorted" : "");

exit:
	if (l_sorted == FALSE)
		BBPnreclaim(3, sorted_l, ord_sorted_l, sorted_cl);

	if (r_sorted == FALSE)
		BBPnreclaim(3, sorted_r, ord_sorted_r, sorted_cr);

	bat_iterator_end(&li);
	bat_iterator_end(&ri);
	return msg;
}

static str
STRjoin(bat *rl_id, bat *rr_id, const bat l_id, const bat r_id,
		const bat cl_id, const bat cr_id, const bit anti, bool icase,
		int (*str_cmp)(const char *, const char *, int), const str fname)
{
	str msg = MAL_SUCCEED;

	BAT *rl = NULL, *rr = NULL, *l = NULL, *r = NULL, *cl = NULL, *cr = NULL;

	if (!(l = BATdescriptor(l_id)) || !(r = BATdescriptor(r_id))) {
		BBPnreclaim(2, l, r);
		throw(MAL, fname, RUNTIME_OBJECT_MISSING);
	}

	if ((cl_id && !is_bat_nil(cl_id) && (cl = BATdescriptor(cl_id)) == NULL) ||
		(cr_id && !is_bat_nil(cr_id) && (cr = BATdescriptor(cr_id)) == NULL)) {
		BBPnreclaim(4, l, r, cl, cr);
		throw(MAL, fname, RUNTIME_OBJECT_MISSING);
	}

	rl = COLnew(0, TYPE_oid, BATcount(l), TRANSIENT);
	if (rr_id)
		rr = COLnew(0, TYPE_oid, BATcount(l), TRANSIENT);

	if (!rl || (rr_id && !rr)) {
		BBPnreclaim(6, l, r, cl, cr, rl, rr);
		throw(MAL, fname, SQLSTATE(HY013) MAL_MALLOC_FAIL);
	}

	set_empty_bat_props(rl);
	if (rr_id)
		set_empty_bat_props(rr);

	assert(ATOMtype(l->ttype) == ATOMtype(r->ttype));
	assert(ATOMtype(l->ttype) == TYPE_str);

	BAT **l_ptr = &l, **r_ptr = &r;

	if (strcmp(fname, "str.containsjoin") == 0) {
		msg = contains_join(rl, rr, l, r, cl, cr, anti, str_cmp, fname);
		if (msg) {
			BBPnreclaim(6, rl, rr, l, r, cl, cr);
			return msg;
		}
	} else {
		struct canditer lci, rci;
		canditer_init(&lci, l, cl);
		canditer_init(&rci, r, cr);

		BUN lcnt = lci.ncand, rcnt = rci.ncand;
		BUN nl_cost = lci.ncand * rci.ncand,
			sorted_cost =
			(BUN) floor(0.8 * (lcnt*log2((double)lcnt)
							   + rcnt*log2((double)rcnt)));

		if (nl_cost < sorted_cost) {
			msg = str_join_nested(rl, rr, *l_ptr, *r_ptr, cl, cr, anti, str_cmp, fname);
		} else {
			BAT *l_low = NULL, *r_low = NULL, *l_rev = NULL, *r_rev = NULL;
			if (icase) {
				l_low = batstr_strlower(*l_ptr);
				if (l_low == NULL) {
					BBPnreclaim(6, rl, rr, *l_ptr, *r_ptr, cl, cr);
					throw(MAL, fname, "Failed lowering strings of left input");
				}
				r_low = batstr_strlower(*r_ptr);
				if (r_low == NULL) {
					BBPnreclaim(7, rl, rr, *l_ptr, *r_ptr, cl, cr, l_low);
					throw(MAL, fname, "Failed lowering strings of right input");
				}
				BBPnreclaim(2, *l_ptr, *r_ptr);
				l_ptr = &l_low;
				r_ptr = &r_low;
			}
			if (strcmp(fname, "str.endswithjoin") == 0) {
				l_rev = batstr_strrev(*l_ptr);
				if (l_rev == NULL) {
					BBPnreclaim(6, rl, rr, *l_ptr, *r_ptr, cl, cr);
					throw(MAL, fname, "Failed reversing strings of left input");
				}
				r_rev = batstr_strrev(*r_ptr);
				if (r_rev == NULL) {
					BBPnreclaim(7, rl, rr, *l_ptr, *r_ptr, cl, cr, l_rev);
					throw(MAL, fname, "Failed reversing strings of right input");
				}
				BBPnreclaim(2, *l_ptr, *r_ptr);
				l_ptr = &l_rev;
				r_ptr = &r_rev;
			}
			msg = startswith_join(&rl, &rr, *l_ptr, *r_ptr, cl, cr,
								  anti, str_is_prefix, fname);
		}
	}

	if (!msg) {
		*rl_id = rl->batCacheid;
		BBPkeepref(rl);
		if (rr_id) {
			*rr_id = rr->batCacheid;
			BBPkeepref(rr);
		}
	} else {
		BBPnreclaim(2, rl, rr);
	}

	BBPnreclaim(4, *l_ptr, *r_ptr, cl, cr);
	return msg;
}

#define STRJOIN_MAPARGS(STK, PCI, RL_ID, RR_ID, L_ID, R_ID, CL_ID, CR_ID, IC_ID, ANTI) \
	do {																\
		RL_ID = getArgReference(STK, PCI, 0);							\
		RR_ID = PCI->retc == 1 ? 0 : getArgReference(STK, PCI, 1);		\
		int i = PCI->retc == 1 ? 1 : 2;								\
		L_ID = getArgReference(STK, PCI, i++);							\
		R_ID = getArgReference(STK, PCI, i++);							\
		IC_ID = PCI->argc - PCI->retc == 7 ?							\
			NULL : getArgReference(stk, pci, i++);						\
		CL_ID = getArgReference(STK, PCI, i++);						\
		CR_ID = getArgReference(STK, PCI, i++);						\
		ANTI = PCI->argc - PCI->retc == 7 ?							\
			getArgReference(STK, PCI, 8) : getArgReference(STK, PCI, 9);\
	} while (0)

static inline str
ignorecase(const bat *ic_id, bool *icase, str fname)
{
	BAT *c = NULL;

	if ((c = BATdescriptor(*ic_id)) == NULL)
		throw(MAL, fname, SQLSTATE(HY002) RUNTIME_OBJECT_MISSING);

	assert(BATcount(c) == 1);

	BATiter bi = bat_iterator(c);
	*icase = *(bit *) BUNtloc(bi, 0);
	bat_iterator_end(&bi);

	BBPreclaim(c);
	return MAL_SUCCEED;
}

/**
 * @rl_id: result left oid
 * @rr_id: result right oid
 * @l_id: left oid
 * @r_id: right oid
 * @cl_id: candidates left oid
 * @cr_id: candidates right oid
 * @ic_id: ignore case oid
 * @anti: anti join oid
 */
static str
STRstartswithjoin(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	(void)cntxt;
	(void)mb;

	str msg = MAL_SUCCEED;
	bat *rl_id = NULL, *rr_id = NULL, *l_id = NULL, *r_id = NULL,
		*cl_id = NULL, *cr_id = NULL, *ic_id = NULL, *anti = NULL;
	bool icase = false;

	STRJOIN_MAPARGS(stk, pci, rl_id, rr_id, l_id, r_id, cl_id, cr_id, ic_id, anti);

	if (pci->argc - pci->retc == 8)
		msg = ignorecase(ic_id, &icase, "str.startswithjoin");

	return msg ? msg : STRjoin(rl_id, rr_id, *l_id, *r_id,
							   cl_id ? *cl_id : 0,
							   cr_id ? *cr_id : 0,
							   *anti, icase, icase ? str_is_iprefix : str_is_prefix,
							   "str.startswithjoin");
}

/**
 * @rl_id: result left oid
 * @rr_id: result right oid
 * @l_id: left oid
 * @r_id: right oid
 * @cl_id: candidates left oid
 * @cr_id: candidates right oid
 * @ic_id: ignore case oid
 * @anti: anti join oid
 */
static str
STRendswithjoin(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	(void) cntxt;
	(void) mb;

	str msg = MAL_SUCCEED;
	bat *rl_id = NULL, *rr_id = NULL, *l_id = NULL, *r_id = NULL,
		*cl_id = NULL, *cr_id = NULL, *ic_id = NULL, *anti = NULL;
	bool icase = false;

	STRJOIN_MAPARGS(stk, pci, rl_id, rr_id, l_id, r_id, cl_id, cr_id, ic_id, anti);

	if (pci->argc - pci->retc == 8)
		msg = ignorecase(ic_id, &icase, "str.endswithjoin");

	return msg ? msg : STRjoin(rl_id, rr_id, *l_id, *r_id,
							   cl_id ? *cl_id : 0, cr_id ? *cr_id : 0,
							   *anti, icase, icase ? str_is_isuffix : str_is_suffix,
							   "str.endswithjoin");
}

/**
 * @rl_id: result left oid
 * @rr_id: result right oid
 * @l_id: left oid
 * @r_id: right oid
 * @cl_id: candidates left oid
 * @cr_id: candidates right oid
 * @ic_id: ignore case oid
 * @anti: anti join oid
 */
static str
STRcontainsjoin(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	(void) cntxt;
	(void) mb;

	str msg = MAL_SUCCEED;
	bat *rl_id = NULL, *rr_id = NULL, *l_id = NULL, *r_id = NULL,
		*cl_id = NULL, *cr_id = NULL, *ic_id = NULL, *anti = NULL;
	bool icase = false;

	STRJOIN_MAPARGS(stk, pci, rl_id, rr_id, l_id, r_id, cl_id, cr_id, ic_id, anti);

	if (pci->argc - pci->retc == 8)
		msg = ignorecase(ic_id, &icase, "str.containsjoin");

	return msg ? msg : STRjoin(rl_id, rr_id, *l_id, *r_id,
							   cl_id ? *cl_id : 0, cr_id ? *cr_id : 0,
							   *anti, icase, icase ? str_icontains : str_contains,
							   "str.containsjoin");
}

#include "mel.h"
mel_func str_init_funcs[] = {
 command("str", "str", STRtostr, false, "Noop routine.", args(1,2, arg("",str),arg("s",str))),
 command("str", "string", STRTail, false, "Return the tail s[offset..n]\nof a string s[0..n].", args(1,3, arg("",str),arg("s",str),arg("offset",int))),
 command("str", "string3", STRSubString, false, "Return substring s[offset..offset+count] of a string s[0..n]", args(1,4, arg("",str),arg("s",str),arg("offset",int),arg("count",int))),
 command("str", "length", STRLength, false, "Return the length of a string.", args(1,2, arg("",int),arg("s",str))),
 command("str", "nbytes", STRBytes, false, "Return the string length in bytes.", args(1,2, arg("",int),arg("s",str))),
 command("str", "unicodeAt", STRWChrAt, false, "get a unicode character\n(as an int) from a string position.", args(1,3, arg("",int),arg("s",str),arg("index",int))),
 command("str", "unicode", STRFromWChr, false, "convert a unicode to a character.", args(1,2, arg("",str),arg("wchar",int))),
 pattern("str", "startswith", STRstartswith, false, "Check if string starts with substring.", args(1,3, arg("",bit),arg("s",str),arg("prefix",str))),
 pattern("str", "startswith", STRstartswith, false, "Check if string starts with substring, icase flag.", args(1,4, arg("",bit),arg("s",str),arg("prefix",str),arg("icase",bit))),
 pattern("str", "endswith", STRendswith, false, "Check if string ends with substring.", args(1,3, arg("",bit),arg("s",str),arg("suffix",str))),
 pattern("str", "endswith", STRendswith, false, "Check if string ends with substring, icase flag.", args(1,4, arg("",bit),arg("s",str),arg("suffix",str),arg("icase",bit))),
 pattern("str", "contains", STRcontains, false, "Check if string haystack contains string needle.", args(1,3, arg("",bit),arg("haystack",str),arg("needle",str))),
 pattern("str", "contains", STRcontains, false, "Check if string chaystack contains string needle, icase flag.", args(1,4, arg("",bit),arg("haystack",str),arg("needle",str),arg("icase",bit))),
 command("str", "toLower", STRlower, false, "Convert a string to lower case.", args(1,2, arg("",str),arg("s",str))),
 command("str", "toUpper", STRupper, false, "Convert a string to upper case.", args(1,2, arg("",str),arg("s",str))),
 pattern("str", "search", STRstr_search, false, "Search for a substring. Returns\nposition, -1 if not found.", args(1,3, arg("",int),arg("s",str),arg("c",str))),
 pattern("str", "search", STRstr_search, false, "Search for a substring, icase flag. Returns\nposition, -1 if not found.", args(1,4, arg("",int),arg("s",str),arg("c",str),arg("icase",bit))),
 pattern("str", "r_search", STRrevstr_search, false, "Reverse search for a substring. Returns\nposition, -1 if not found.", args(1,3, arg("",int),arg("s",str),arg("c",str))),
 pattern("str", "r_search", STRrevstr_search, false, "Reverse search for a substring, icase flag. Returns\nposition, -1 if not found.", args(1,4, arg("",int),arg("s",str),arg("c",str),arg("icase",bit))),
 command("str", "splitpart", STRsplitpart, false, "Split string on delimiter. Returns\ngiven field (counting from one.)", args(1,4, arg("",str),arg("s",str),arg("needle",str),arg("field",int))),
 command("str", "trim", STRStrip, false, "Strip whitespaces around a string.", args(1,2, arg("",str),arg("s",str))),
 command("str", "ltrim", STRLtrim, false, "Strip whitespaces from start of a string.", args(1,2, arg("",str),arg("s",str))),
 command("str", "rtrim", STRRtrim, false, "Strip whitespaces from end of a string.", args(1,2, arg("",str),arg("s",str))),
 command("str", "trim2", STRStrip2, false, "Remove the longest string containing only characters from the second string around the first string.", args(1,3, arg("",str),arg("s",str),arg("s2",str))),
 command("str", "ltrim2", STRLtrim2, false, "Remove the longest string containing only characters from the second string from the start of the first string.", args(1,3, arg("",str),arg("s",str),arg("s2",str))),
 command("str", "rtrim2", STRRtrim2, false, "Remove the longest string containing only characters from the second string from the end of the first string.", args(1,3, arg("",str),arg("s",str),arg("s2",str))),
 command("str", "lpad", STRLpad, false, "Fill up a string to the given length prepending the whitespace character.", args(1,3, arg("",str),arg("s",str),arg("len",int))),
 command("str", "rpad", STRRpad, false, "Fill up a string to the given length appending the whitespace character.", args(1,3, arg("",str),arg("s",str),arg("len",int))),
 command("str", "lpad3", STRLpad3, false, "Fill up the first string to the given length prepending characters of the second string.", args(1,4, arg("",str),arg("s",str),arg("len",int),arg("s2",str))),
 command("str", "rpad3", STRRpad3, false, "Fill up the first string to the given length appending characters of the second string.", args(1,4, arg("",str),arg("s",str),arg("len",int),arg("s2",str))),
 command("str", "substitute", STRSubstitute, false, "Substitute first occurrence of 'src' by\n'dst'.  Iff repeated = true this is\nrepeated while 'src' can be found in the\nresult string. In order to prevent\nrecursion and result strings of unlimited\nsize, repeating is only done iff src is\nnot a substring of dst.", args(1,5, arg("",str),arg("s",str),arg("src",str),arg("dst",str),arg("rep",bit))),
 command("str", "like", STRlikewrap, false, "SQL pattern match function", args(1,3, arg("",bit),arg("s",str),arg("pat",str))),
 command("str", "like3", STRlikewrap3, false, "SQL pattern match function", args(1,4, arg("",bit),arg("s",str),arg("pat",str),arg("esc",str))),
 command("str", "ascii", STRascii, false, "Return unicode of head of string", args(1,2, arg("",int),arg("s",str))),
 command("str", "substring", STRsubstringTail, false, "Extract the tail of a string", args(1,3, arg("",str),arg("s",str),arg("start",int))),
 command("str", "substring3", STRsubstring, false, "Extract a substring from str starting at start, for length len", args(1,4, arg("",str),arg("s",str),arg("start",int),arg("len",int))),
 command("str", "prefix", STRprefix, false, "Extract the prefix of a given length", args(1,3, arg("",str),arg("s",str),arg("l",int))),
 command("str", "suffix", STRsuffix, false, "Extract the suffix of a given length", args(1,3, arg("",str),arg("s",str),arg("l",int))),
 command("str", "stringleft", STRprefix, false, "", args(1,3, arg("",str),arg("s",str),arg("l",int))),
 command("str", "stringright", STRsuffix, false, "", args(1,3, arg("",str),arg("s",str),arg("l",int))),
 command("str", "locate", STRlocate, false, "Locate the start position of a string", args(1,3, arg("",int),arg("s1",str),arg("s2",str))),
 command("str", "locate3", STRlocate3, false, "Locate the start position of a string", args(1,4, arg("",int),arg("s1",str),arg("s2",str),arg("start",int))),
 command("str", "insert", STRinsert, false, "Insert a string into another", args(1,5, arg("",str),arg("s",str),arg("start",int),arg("l",int),arg("s2",str))),
 command("str", "replace", STRreplace, false, "Insert a string into another", args(1,4, arg("",str),arg("s",str),arg("pat",str),arg("s2",str))),
 command("str", "repeat", STRrepeat, false, "", args(1,3, arg("",str),arg("s2",str),arg("c",int))),
 command("str", "space", STRspace, false, "", args(1,2, arg("",str),arg("l",int))),
 command("str", "epilogue", STRepilogue, false, "", args(1,1, arg("",void))),
 command("str", "asciify", STRasciify, false, "Transform string from UTF8 to ASCII", args(1, 2, arg("out",str), arg("in",str))),
 pattern("str", "startswithselect", STRstartswithselect, false, "Select all head values of the first input BAT for which the\ntail value starts with the given prefix.", args(1,5, batarg("",oid),batarg("b",str),batarg("s",oid),arg("prefix",str),arg("anti",bit))),
 pattern("str", "startswithselect", STRstartswithselect, false, "Select all head values of the first input BAT for which the\ntail value starts with the given prefix + icase.", args(1,6, batarg("",oid),batarg("b",str),batarg("s",oid),arg("prefix",str),arg("caseignore",bit),arg("anti",bit))),
 pattern("str", "endswithselect", STRendswithselect, false, "Select all head values of the first input BAT for which the\ntail value end with the given suffix.", args(1,5, batarg("",oid),batarg("b",str),batarg("s",oid),arg("suffix",str),arg("anti",bit))),
 pattern("str", "endswithselect", STRendswithselect, false, "Select all head values of the first input BAT for which the\ntail value end with the given suffix + icase.", args(1,6, batarg("",oid),batarg("b",str),batarg("s",oid),arg("suffix",str),arg("caseignore",bit),arg("anti",bit))),
 pattern("str", "containsselect", STRcontainsselect, false, "Select all head values of the first input BAT for which the\ntail value contains the given needle.", args(1,5, batarg("",oid),batarg("b",str),batarg("s",oid),arg("needle",str),arg("anti",bit))),
 pattern("str", "containsselect", STRcontainsselect, false, "Select all head values of the first input BAT for which the\ntail value contains the given needle + icase.", args(1,6, batarg("",oid),batarg("b",str),batarg("s",oid),arg("needle",str),arg("caseignore",bit),arg("anti",bit))),
 pattern("str", "startswithjoin", STRstartswithjoin, false, "Join the string bat L with the prefix bat R\nwith optional candidate lists SL and SR\nThe result is two aligned bats with oids of matching rows.", args(2,9, batarg("",oid),batarg("",oid),batarg("l",str),batarg("r",str),batarg("sl",oid),batarg("sr",oid),arg("nil_matches",bit),arg("estimate",lng),arg("anti",bit))),
 pattern("str", "startswithjoin", STRstartswithjoin, false, "Join the string bat L with the prefix bat R\nwith optional candidate lists SL and SR\nThe result is two aligned bats with oids of matching rows + icase.", args(2,10, batarg("",oid),batarg("",oid),batarg("l",str),batarg("r",str),batarg("caseignore",bit),batarg("sl",oid),batarg("sr",oid),arg("nil_matches",bit),arg("estimate",lng),arg("anti",bit))),
 pattern("str", "startswithjoin", STRstartswithjoin, false, "The same as STRstartswithjoin, but only produce one output.", args(1,8,batarg("",oid),batarg("l",str),batarg("r",str),batarg("sl",oid),batarg("sr",oid),arg("nil_matches",bit),arg("estimate",lng), arg("anti",bit))),
 pattern("str", "startswithjoin", STRstartswithjoin, false, "The same as STRstartswithjoin, but only produce one output + icase.", args(1,9,batarg("",oid),batarg("l",str),batarg("r",str),batarg("caseignore",bit),batarg("sl",oid),batarg("sr",oid),arg("nil_matches",bit),arg("estimate",lng), arg("anti",bit))),
 pattern("str", "endswithjoin", STRendswithjoin, false, "Join the string bat L with the suffix bat R\nwith optional candidate lists SL and SR\nThe result is two aligned bats with oids of matching rows.", args(2,9, batarg("",oid),batarg("",oid),batarg("l",str),batarg("r",str),batarg("sl",oid),batarg("sr",oid),arg("nil_matches",bit),arg("estimate",lng),arg("anti",bit))),
 pattern("str", "endswithjoin", STRendswithjoin, false, "Join the string bat L with the suffix bat R\nwith optional candidate lists SL and SR\nThe result is two aligned bats with oids of matching rows + icase.", args(2,10, batarg("",oid),batarg("",oid),batarg("l",str),batarg("r",str),batarg("caseignore",bit),batarg("sl",oid),batarg("sr",oid),arg("nil_matches",bit),arg("estimate",lng),arg("anti",bit))),
 pattern("str", "endswithjoin", STRendswithjoin, false, "The same as STRendswithjoin, but only produce one output.", args(1,8,batarg("",oid),batarg("l",str),batarg("r",str),batarg("sl",oid),batarg("sr",oid),arg("nil_matches",bit),arg("estimate",lng), arg("anti",bit))),
 pattern("str", "endswithjoin", STRendswithjoin, false, "The same as STRendswithjoin, but only produce one output + icase.", args(1,9,batarg("",oid),batarg("l",str),batarg("r",str),batarg("caseignore",bit),batarg("sl",oid),batarg("sr",oid),arg("nil_matches",bit),arg("estimate",lng), arg("anti",bit))),
 pattern("str", "containsjoin", STRcontainsjoin, false, "Join the string bat L with the bat R if L contains the string of R\nwith optional candidate lists SL and SR\nThe result is two aligned bats with oids of matching rows.", args(2,9, batarg("",oid),batarg("",oid),batarg("l",str),batarg("r",str),batarg("sl",oid),batarg("sr",oid),arg("nil_matches",bit),arg("estimate",lng),arg("anti",bit))),
 pattern("str", "containsjoin", STRcontainsjoin, false, "Join the string bat L with the bat R if L contains the string of R\nwith optional candidate lists SL and SR\nThe result is two aligned bats with oids of matching rows + icase.", args(2,10, batarg("",oid),batarg("",oid),batarg("l",str),batarg("r",str),batarg("caseignore",bit),batarg("sl",oid),batarg("sr",oid),arg("nil_matches",bit),arg("estimate",lng),arg("anti",bit))),
 pattern("str", "containsjoin", STRcontainsjoin, false, "The same as STRcontainsjoin, but only produce one output.", args(1,8,batarg("",oid),batarg("l",str),batarg("r",str),batarg("sl",oid),batarg("sr",oid),arg("nil_matches",bit),arg("estimate",lng), arg("anti",bit))),
 pattern("str", "containsjoin", STRcontainsjoin, false, "The same as STRcontainsjoin, but only produce one output + icase.", args(1,9,batarg("",oid),batarg("l",str),batarg("r",str),batarg("caseignore",bit),batarg("sl",oid),batarg("sr",oid),arg("nil_matches",bit),arg("estimate",lng), arg("anti",bit))),
 { .imp=NULL }
};
#include "mal_import.h"
#ifdef _MSC_VER
#undef read
#pragma section(".CRT$XCU",read)
#endif
LIB_STARTUP_FUNC(init_str_mal)
{ mal_module2("str", NULL, str_init_funcs, STRprelude, NULL); }
