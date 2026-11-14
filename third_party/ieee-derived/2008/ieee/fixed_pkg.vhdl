-- ----------------------------------------------------------------
-- fsim-derived IEEE package source for VHDL-2008.
--
-- This file is NOT an IEEE original. It is derived from the IEEE
-- P1076 Working Group Open Source Repository file ieee/fixed_pkg.vhdl
-- (IEEE Std 1076-2019 packages, tag 1076-2019, SHA-256 bbd601960c294aa0675a91b4576f1fcfff2d612fe911ad8cc51ca2c10b1565cd)
-- by deleting the declarations and bodies that later revisions of the
-- standard added and by the mechanical rewrites declared in the
-- tables below. The 0 removal rows and 0 rewrite rows applied to
-- this file are rows of scripts/vhdl_ieee_removals.tsv (standard 2008,
-- package fixed_pkg) and scripts/vhdl_ieee_rewrites.tsv.
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
--
--   Title     :  Fixed-point package (Instantiated package declaration)
--             :
--   Library   :  This package shall be compiled into a library
--             :  symbolically named IEEE.
--             :
--   Developers:  Accellera VHDL-TC and IEEE P1076 Working Group
--             :
--   Purpose   :  This packages defines basic binary fixed point
--             :  arithmetic functions
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
-- $Revision: 1220 $
-- $Date: 2008-04-10 17:16:09 +0930 (Thu, 10 Apr 2008) $
-- --------------------------------------------------------------------

library IEEE;

package fixed_pkg is new IEEE.fixed_generic_pkg
  generic map (
    fixed_round_style    => IEEE.fixed_float_types.fixed_round,
    fixed_overflow_style => IEEE.fixed_float_types.fixed_saturate,
    fixed_guard_bits     => 3,
    no_warning           => false
    );
