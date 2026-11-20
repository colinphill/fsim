-- ----------------------------------------------------------------
-- fsim-derived IEEE package source for VHDL-2000.
--
-- This file is NOT an IEEE original. It is derived from the IEEE
-- P1076 Working Group Open Source Repository file std/textio.vhdl
-- (IEEE Std 1076-2019 packages, tag 1076-2019, SHA-256 01028097080267c951b06d31ea4b4b98a4047674dbbcf4791e691e6e043ee978)
-- by deleting the declarations and bodies that later revisions of the
-- standard added and by the mechanical rewrites declared in the
-- tables below. The 26 removal rows and 1 rewrite rows applied to
-- this file are rows of scripts/vhdl_ieee_removals.tsv (standard 2002 and 2000,
-- package textio) and scripts/vhdl_ieee_rewrites.tsv.
-- Rewrite rows (line numbers of the rewrites table): 99.
--
-- The 2019 file's own notice (Copyright 2019 IEEE P1076 WG Authors,
-- Apache License, Version 2.0) follows unchanged and applies to this
-- derived work; see the LICENSE and AUTHORS files of
-- third_party/ieee-1076-2019.
-- End of the fsim derivation header; the 2019 text follows.
-- -----------------------------------------------------------------
-- 
-- Copyright 2019 IEEE P1076 WG Authors
-- 
-- See the LICENSE file distributed with this work for copyright and
-- licensing information and the AUTHORS file.
-- 
-- This file to you under the Apache License, Version 2.0 (the "License").
-- You may obtain a copy of the License at
-- 
--     http://www.apache.org/licenses/LICENSE-2.0
-- 
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS,
-- WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
-- implied.  See the License for the specific language governing
-- permissions and limitations under the License.
-- 
--   Library   :  This package shall be compiled into a library
--             :  symbolically named std.
--             :
--   Developers:  IEEE P1076 Working Group
--             :
--   Purpose   :  This packages defines subprograms for file I/O
--             :
--   Note      :  This package may be modified to include additional data
--             :  required by tools, but it must in no way change the
--             :  external interfaces or simulation behavior of the
--             :  description. It is permissible to add comments and/or
--             :  attributes to the package declarations, but not to change
--             :  or delete any original lines of the package declaration.
--             :  The package body may be changed only in accordance with
--             :  the terms of Clause 16 of this standard.
--             :
-- --------------------------------------------------------------------

package TEXTIO is
  -- Type definitions for text I/O:
  type LINE is access STRING; -- A LINE is a pointer to a STRING value.
  -- The predefined operations for this type are as follows:
  -- function"=" (anonymous, anonymous: LINE) return BOOLEAN;
  -- function"/=" (anonymous, anonymous: LINE) return BOOLEAN;
  -- procedure DEALLOCATE (P: inout LINE);

  -- The predefined operations for this type are as follows:
  -- function "="(anonymous, anonymous: LINE_VECTOR) return BOOLEAN;
  -- function "/="(anonymous, anonymous: LINE_VECTOR) return BOOLEAN;
  -- function "&"(anonymous: LINE_VECTOR; anonymous: LINE_VECTOR) return LINE_VECTOR;
  -- function "&"(anonymous: LINE_VECTOR; anonymous: LINE) return LINE_VECTOR;
  -- function "&"(anonymous: LINE; anonymous: LINE_VECTOR) return LINE_VECTOR;
  -- function "&"(anonymous: LINE; anonymous: LINE) return LINE_VECTOR;

  type TEXT is file of STRING; -- A file of variable-length ASCII records.
  -- The predefined operations for this type are as follows:
  -- procedure FILE_OPEN (file F: TEXT; External_Name; in STRING; Open_Kind: in FILE_OPEN_KIND := READ_MODE);
  -- procedure FILE_OPEN (Status: out FILE_OPEN_STATUS; file F: TEXT; External_Name: in STRING; Open_Kind: in FILE_OPEN_KIND := READ_MODE);
  -- procedure FILE_REWIND (file F: FT);
  -- procedure FILE_SEEK (file F: FT; Offset : INTEGER; Origin : FILE_ORIGIN_KIND := FILE_ORIGIN_BEGIN);
  -- procedure FILE_TRUNCATE (file F: FT; Size : INTEGER; Origin : FILE_ORIGIN_KIND := FILE_ORIGIN_BEGIN);
  -- function  FILE_MODE (file F: FT) return FILE_OPEN_KIND;
  -- function  FILE_TELL (file F: FT; Origin : FILE_ORIGIN_KIND := FILE_ORIGIN_BEGIN) return INTEGER;
  -- function  FILE_SIZE (file F: FT) return INTEGER;
  -- procedure FILE_CLOSE (file F: TEXT);
  -- procedure READ (file F: TEXT; VALUE: out STRING);
  -- procedure WRITE (file F: TEXT; VALUE: in STRING);
  -- procedure FLUSH (file F: TEXT);
  -- function  ENDFILE (file F: TEXT) return BOOLEAN;

  type SIDE is (RIGHT, LEFT); -- For justifying output data within fields.
  -- The predefined operations for this type are as follows:
  -- function "=" (anonymous, anonymous: SIDE) return BOOLEAN;
  -- function "/=" (anonymous, anonymous: SIDE) return BOOLEAN;
  -- function "<" (anonymous, anonymous: SIDE) return BOOLEAN;
  -- function "<=" (anonymous, anonymous: SIDE) return BOOLEAN;
  -- function ">" (anonymous, anonymous: SIDE) return BOOLEAN;
  -- function ">=" (anonymous, anonymous: SIDE) return BOOLEAN;
  -- function MINIMUM (L, R: SIDE) return SIDE;
  -- function MAXIMUM (L, R: SIDE) return SIDE;
  -- function TO_STRING (VALUE: SIDE) return STRING;

  subtype WIDTH is NATURAL; -- For specifying widths of output fields.
  -- Standard text files:
  file INPUT: TEXT open READ_MODE is "STD_INPUT";
  file OUTPUT: TEXT open WRITE_MODE is "STD_OUTPUT";

  -- Input routines for standard types:
  procedure READLINE (file F: TEXT; L: out LINE);
  procedure READ (L: inout LINE; VALUE: out BIT; GOOD: out BOOLEAN);
  procedure READ (L: inout LINE; VALUE: out BIT);
  procedure READ (L: inout LINE; VALUE: out BIT_VECTOR; GOOD: out BOOLEAN);
  procedure READ (L: inout LINE; VALUE: out BIT_VECTOR);
  procedure READ (L: inout LINE; VALUE: out BOOLEAN; GOOD: out BOOLEAN);
  procedure READ (L: inout LINE; VALUE: out BOOLEAN);
  procedure READ (L: inout LINE; VALUE: out CHARACTER; GOOD: out BOOLEAN);
  procedure READ (L: inout LINE; VALUE: out CHARACTER);
  procedure READ (L: inout LINE; VALUE: out INTEGER; GOOD: out BOOLEAN);
  procedure READ (L: inout LINE; VALUE: out INTEGER);
  procedure READ (L: inout LINE; VALUE: out REAL; GOOD: out BOOLEAN);
  procedure READ (L: inout LINE; VALUE: out REAL);
  procedure READ (L: inout LINE; VALUE: out STRING; GOOD: out BOOLEAN);
  procedure READ (L: inout LINE; VALUE: out STRING);
  procedure READ (L: inout LINE; VALUE: out TIME; GOOD: out BOOLEAN);
  procedure READ (L: inout LINE; VALUE: out TIME);

  -- Output routines for standard types:
  procedure WRITELINE (file F: TEXT; L: inout LINE);
  procedure WRITE (L: inout LINE; VALUE: in BIT; JUSTIFIED: in SIDE:= RIGHT; FIELD: in WIDTH := 0);
  procedure WRITE (L: inout LINE; VALUE: in BIT_VECTOR; JUSTIFIED: in SIDE:= RIGHT; FIELD: in WIDTH := 0);
  procedure WRITE (L: inout LINE; VALUE: in BOOLEAN; JUSTIFIED: in SIDE:= RIGHT; FIELD: in WIDTH := 0);
  procedure WRITE (L: inout LINE; VALUE: in CHARACTER; JUSTIFIED: in SIDE:= RIGHT; FIELD: in WIDTH := 0);
  procedure WRITE (L: inout LINE; VALUE: in INTEGER; JUSTIFIED: in SIDE:= RIGHT; FIELD: in WIDTH := 0);
  procedure WRITE (L: inout LINE; VALUE: in REAL; JUSTIFIED: in SIDE:= RIGHT; FIELD: in WIDTH := 0; DIGITS: in NATURAL:= 0);
  procedure WRITE (L: inout LINE; VALUE: in STRING; JUSTIFIED: in SIDE:= RIGHT; FIELD: in WIDTH := 0);
  procedure WRITE (L: inout LINE; VALUE: in TIME; JUSTIFIED: in SIDE:= RIGHT; FIELD: in WIDTH := 0; UNIT: in TIME:= ns);
end package TEXTIO;
