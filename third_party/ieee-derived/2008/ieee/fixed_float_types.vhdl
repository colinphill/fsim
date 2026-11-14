-- ----------------------------------------------------------------
-- fsim-derived IEEE package source for VHDL-2008.
--
-- This file is NOT an IEEE original. It is derived from the IEEE
-- P1076 Working Group Open Source Repository file ieee/fixed_float_types.vhdl
-- (IEEE Std 1076-2019 packages, tag 1076-2019, SHA-256 649090aaf4dbb7a6bf88a6720a265aaf3be0360f449eb1462777f1c7f48c249e)
-- by deleting the declarations and bodies that later revisions of the
-- standard added and by the mechanical rewrites declared in the
-- tables below. The 0 removal rows and 0 rewrite rows applied to
-- this file are rows of scripts/vhdl_ieee_removals.tsv (standard 2008,
-- package fixed_float_types) and scripts/vhdl_ieee_rewrites.tsv.
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
--   Title      :  Fixed Point and Floating Point types package
--
--   Library   :  This package shall be compiled into a library
--                symbolically named IEEE.
--
--   Developers:  Accellera VHDL-TC and IEEE P1076 Working Group
--
--   Purpose   :  Definitions for use in fixed point and floating point
--                arithmetic packages
--
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

package fixed_float_types is

  -- Types used for generics of fixed_generic_pkg

  type fixed_round_style_type is (fixed_round, fixed_truncate);

  type fixed_overflow_style_type is (fixed_saturate, fixed_wrap);

  -- Type used for generics of float_generic_pkg

  -- These are the same as the C FE_TONEAREST, FE_UPWARD, FE_DOWNWARD,
  -- and FE_TOWARDZERO floating point rounding macros.

  type round_type is (round_nearest,    -- Default, nearest LSB '0'
                      round_inf,        -- Round toward positive infinity
                      round_neginf,     -- Round toward negative infinity
                      round_zero);      -- Round toward zero (truncate)

end package fixed_float_types;
