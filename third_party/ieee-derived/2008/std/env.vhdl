-- ----------------------------------------------------------------
-- fsim-derived IEEE package source for VHDL-2008.
--
-- This file is NOT an IEEE original. It is derived from the IEEE
-- P1076 Working Group Open Source Repository file std/env.vhdl
-- (IEEE Std 1076-2019 packages, tag 1076-2019, SHA-256 59169bc8381578d4176cb6589f2cb7c407539f53d055b64da365c12ea18bf1ae)
-- by deleting the declarations and bodies that later revisions of the
-- standard added and by the mechanical rewrites declared in the
-- tables below. The 83 removal rows and 0 rewrite rows applied to
-- this file are rows of scripts/vhdl_ieee_removals.tsv (standard 2008,
-- package env) and scripts/vhdl_ieee_rewrites.tsv.
-- Rewrite rows (line numbers of the rewrites table): none.
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
--   Title     :  Environment package
--             :
--   Library   :  This package shall be compiled into a library
--             :  symbolically named std.
--             :
--   Developers:  IEEE P1076 Working Group
--             :
--   Purpose   :  This packages defines basic tool interface subprograms
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



package ENV is
  procedure STOP (STATUS: INTEGER);
  procedure STOP;
  procedure FINISH (STATUS: INTEGER);
  procedure FINISH;

  function RESOLUTION_LIMIT return DELAY_LENGTH;

















  -- The predefined operations for this type are as follows:
  -- function"=" (anonymous, anonymous: DIRECTORY_ITEMS) return BOOLEAN;
  -- function"/=" (anonymous, anonymous: DIRECTORY_ITEMS) return BOOLEAN;
  -- procedure DEALLOCATE (P: inout DIRECTORY_ITEMS);

  -- The predefined operations for this type are as follows:
  -- function "="(anonymous, anonymous: DIRECTORY) return BOOLEAN;
  -- function "/="(anonymous, anonymous: DIRECTORY) return BOOLEAN;

  -- The predefined operations for this type are as follows:
  -- function "="(anonymous, anonymous: DIR_OPEN_STATUS) return BOOLEAN;
  -- function "/="(anonymous, anonymous: DIR_OPEN_STATUS) return BOOLEAN;
  -- function "<"(anonymous, anonymous: DIR_OPEN_STATUS) return BOOLEAN;
  -- function "<="(anonymous, anonymous: DIR_OPEN_STATUS) return BOOLEAN;
  -- function ">"(anonymous, anonymous: DIR_OPEN_STATUS) return BOOLEAN;
  -- function ">="(anonymous, anonymous: DIR_OPEN_STATUS) return BOOLEAN;
  -- function MINIMUM (L, R: DIR_OPEN_STATUS) return DIR_OPEN_STATUS;
  -- function MAXIMUM (L, R: DIR_OPEN_STATUS) return DIR_OPEN_STATUS;

  -- The predefined operations for this type are as follows:
  -- function "="(anonymous, anonymous: DIR_CREATE_STATUS) return BOOLEAN;
  -- function "/="(anonymous, anonymous: DIR_CREATE_STATUS) return BOOLEAN;
  -- function "<"(anonymous, anonymous: DIR_CREATE_STATUS) return BOOLEAN;
  -- function "<="(anonymous, anonymous: DIR_CREATE_STATUS) return BOOLEAN;
  -- function ">"(anonymous, anonymous: DIR_CREATE_STATUS) return BOOLEAN;
  -- function ">="(anonymous, anonymous: DIR_CREATE_STATUS) return BOOLEAN;
  -- function MINIMUM (L, R: DIR_CREATE_STATUS) return DIR_CREATE_STATUS;
  -- function MAXIMUM (L, R: DIR_CREATE_STATUS) return DIR_CREATE_STATUS;

  -- The predefined operations for this type are as follows:
  -- function "="(anonymous, anonymous: DIR_DELETE_STATUS) return BOOLEAN;
  -- function "/="(anonymous, anonymous: DIR_DELETE_STATUS) return BOOLEAN;
  -- function "<"(anonymous, anonymous: DIR_DELETE_STATUS) return BOOLEAN;
  -- function "<="(anonymous, anonymous: DIR_DELETE_STATUS) return BOOLEAN;
  -- function ">"(anonymous, anonymous: DIR_DELETE_STATUS) return BOOLEAN;
  -- function ">="(anonymous, anonymous: DIR_DELETE_STATUS) return BOOLEAN;
  -- function MINIMUM (L, R: DIR_DELETE_STATUS) return DIR_DELETE_STATUS;
  -- function MAXIMUM (L, R: DIR_DELETE_STATUS) return DIR_DELETE_STATUS;

  -- The predefined operations for this type are as follows:
  -- function "="(anonymous, anonymous: FILE_DELETE_STATUS) return BOOLEAN;
  -- function "/="(anonymous, anonymous: FILE_DELETE_STATUS) return BOOLEAN;
  -- function "<"(anonymous, anonymous: FILE_DELETE_STATUS) return BOOLEAN;
  -- function "<="(anonymous, anonymous: FILE_DELETE_STATUS) return BOOLEAN;
  -- function ">"(anonymous, anonymous: FILE_DELETE_STATUS) return BOOLEAN;
  -- function ">="(anonymous, anonymous: FILE_DELETE_STATUS) return BOOLEAN;
  -- function MINIMUM (L, R: FILE_DELETE_STATUS) return FILE_DELETE_STATUS;
  -- function MAXIMUM (L, R: FILE_DELETE_STATUS) return FILE_DELETE_STATUS;























end package ENV;
