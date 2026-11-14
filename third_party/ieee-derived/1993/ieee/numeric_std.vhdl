-- ----------------------------------------------------------------
-- fsim-derived IEEE package source for VHDL-1993.
--
-- This file is NOT an IEEE original. It is derived from the IEEE
-- P1076 Working Group Open Source Repository file ieee/numeric_std.vhdl
-- (IEEE Std 1076-2019 packages, tag 1076-2019, SHA-256 fcb9b1d05f8d98cd068e464bf20804d432a234128b253218524901bc96d19631)
-- by deleting the declarations and bodies that later revisions of the
-- standard added and by the mechanical rewrites declared in the
-- tables below. The 171 removal rows and 3 rewrite rows applied to
-- this file are rows of scripts/vhdl_ieee_removals.tsv (standard 1993,
-- package numeric_std) and scripts/vhdl_ieee_rewrites.tsv.
-- Rewrite rows (line numbers of the rewrites table): 61, 62, 63.
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
--   Title     :  Standard VHDL Synthesis Packages
--             :  (NUMERIC_STD package declaration)
--             :
--   Library   :  This package shall be compiled into a library
--             :  symbolically named IEEE.
--             :
--   Developers:  IEEE DASC Synthesis Working Group,
--             :  Accellera VHDL-TC, and IEEE P1076 Working Group
--             :
--   Purpose   :  This package defines numeric types and arithmetic functions
--             :  for use with synthesis tools. Two numeric types are defined:
--             :  -- > UNRESOLVED_UNSIGNED: represents an UNSIGNED number
--             :       in vector form
--             :  -- > UNRESOLVED_SIGNED: represents a SIGNED number
--             :       in vector form
--             :  The base element type is type STD_ULOGIC.
--             :  Aliases U_UNSIGNED and U_SIGNED are defined for the types
--             :  UNRESOLVED_UNSIGNED and UNRESOLVED_SIGNED, respectively.
--             :  Two numeric subtypes are defined:
--             :  -- > UNSIGNED: represents UNSIGNED number in vector form
--             :  -- > SIGNED: represents a SIGNED number in vector form
--             :  The element subtypes are the same subtype as STD_LOGIC.
--             :  The leftmost bit is treated as the most significant bit.
--             :  Signed vectors are represented in two's complement form.
--             :  This package contains overloaded arithmetic operators on
--             :  the SIGNED and UNSIGNED types. The package also contains
--             :  useful type conversions functions, clock detection
--             :  functions, and other utility functions.
--             :
--             :  If any argument to a function is a null array, a null array
--             :  is returned (exceptions, if any, are noted individually).
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

library IEEE;
use IEEE.STD_LOGIC_1164.all;

package NUMERIC_STD is
  constant CopyRightNotice : STRING
    := "Copyright © 2008 IEEE. All rights reserved.";


  --============================================================================
  -- Numeric Array Type Definitions
  --============================================================================

  type UNSIGNED is array (NATURAL range <>) of STD_LOGIC;
  type SIGNED is array (NATURAL range <>) of STD_LOGIC;



  --============================================================================
  -- Arithmetic Operators:
  --===========================================================================

  -- Id: A.1
  function "abs" (ARG : SIGNED) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(ARG'LENGTH-1 downto 0)
  -- Result: Returns the absolute value of an UNRESOLVED_SIGNED vector ARG.

  -- Id: A.2
  function "-" (ARG : SIGNED) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(ARG'LENGTH-1 downto 0)
  -- Result: Returns the value of the unary minus operation on a
  --         UNRESOLVED_SIGNED vector ARG.

  --============================================================================

  -- Id: A.3
  function "+" (L, R : UNSIGNED) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(MAXIMUM(L'LENGTH, R'LENGTH)-1 downto 0)
  -- Result: Adds two UNRESOLVED_UNSIGNED vectors that may be of different lengths.

  -- Result subtype: UNRESOLVED_UNSIGNED(L'LENGTH-1 downto 0)
  -- Result: Similar to A.3 where R is a one bit UNRESOLVED_UNSIGNED

  -- Result subtype: UNRESOLVED_UNSIGNED(R'LENGTH-1 downto 0)
  -- Result: Similar to A.3 where L is a one bit UNRESOLVED_UNSIGNED

  -- Id: A.4
  function "+" (L, R : SIGNED) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(MAXIMUM(L'LENGTH, R'LENGTH)-1 downto 0)
  -- Result: Adds two UNRESOLVED_SIGNED vectors that may be of different lengths.

  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Similar to A.4 where R is bit 0 of a non-negative.

  -- Result subtype: UNRESOLVED_SIGNED(R'LENGTH-1 downto 0)
  -- Result: Similar to A.4 where L is bit 0 of a non-negative.

  -- Id: A.5
  function "+" (L : UNSIGNED; R : NATURAL) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(L'LENGTH-1 downto 0)
  -- Result: Adds an UNRESOLVED_UNSIGNED vector, L, with a nonnegative INTEGER, R.

  -- Id: A.6
  function "+" (L : NATURAL; R : UNSIGNED) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(R'LENGTH-1 downto 0)
  -- Result: Adds a nonnegative INTEGER, L, with an UNRESOLVED_UNSIGNED vector, R.

  -- Id: A.7
  function "+" (L : INTEGER; R : SIGNED) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(R'LENGTH-1 downto 0)
  -- Result: Adds an INTEGER, L(may be positive or negative), to an UNRESOLVED_SIGNED
  --         vector, R.

  -- Id: A.8
  function "+" (L : SIGNED; R : INTEGER) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Adds an UNRESOLVED_SIGNED vector, L, to an INTEGER, R.

  --============================================================================

  -- Id: A.9
  function "-" (L, R : UNSIGNED) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(MAXIMUM(L'LENGTH, R'LENGTH)-1 downto 0)
  -- Result: Subtracts two UNRESOLVED_UNSIGNED vectors that may be of different lengths.

  -- Result subtype: UNRESOLVED_UNSIGNED(L'LENGTH-1 downto 0)
  -- Result: Similar to A.9 where R is a one bit UNRESOLVED_UNSIGNED

  -- Result subtype: UNRESOLVED_UNSIGNED(R'LENGTH-1 downto 0)
  -- Result: Similar to A.9 where L is a one bit UNRESOLVED_UNSIGNED

  -- Id: A.10
  function "-" (L, R : SIGNED) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(MAXIMUM(L'LENGTH, R'LENGTH)-1 downto 0)
  -- Result: Subtracts an UNRESOLVED_SIGNED vector, R, from another UNRESOLVED_SIGNED vector, L,
  --         that may possibly be of different lengths.

  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Similar to A.10 where R is bit 0 of a non-negative.

  -- Result subtype: UNRESOLVED_SIGNED(R'LENGTH-1 downto 0)
  -- Result: Similar to A.10 where R is bit 0 of a non-negative.

  -- Id: A.11
  function "-" (L : UNSIGNED; R : NATURAL) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(L'LENGTH-1 downto 0)
  -- Result: Subtracts a nonnegative INTEGER, R, from an UNRESOLVED_UNSIGNED vector, L.

  -- Id: A.12
  function "-" (L : NATURAL; R : UNSIGNED) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(R'LENGTH-1 downto 0)
  -- Result: Subtracts an UNRESOLVED_UNSIGNED vector, R, from a nonnegative INTEGER, L.

  -- Id: A.13
  function "-" (L : SIGNED; R : INTEGER) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Subtracts an INTEGER, R, from an UNRESOLVED_SIGNED vector, L.

  -- Id: A.14
  function "-" (L : INTEGER; R : SIGNED) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(R'LENGTH-1 downto 0)
  -- Result: Subtracts an UNRESOLVED_SIGNED vector, R, from an INTEGER, L.

  --============================================================================

  -- Id: A.15
  function "*" (L, R : UNSIGNED) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED((L'LENGTH+R'LENGTH-1) downto 0)
  -- Result: Performs the multiplication operation on two UNRESOLVED_UNSIGNED vectors
  --         that may possibly be of different lengths.

  -- Id: A.16
  function "*" (L, R : SIGNED) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED((L'LENGTH+R'LENGTH-1) downto 0)
  -- Result: Multiplies two UNRESOLVED_SIGNED vectors that may possibly be of
  --         different lengths.

  -- Id: A.17
  function "*" (L : UNSIGNED; R : NATURAL) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED((L'LENGTH+L'LENGTH-1) downto 0)
  -- Result: Multiplies an UNRESOLVED_UNSIGNED vector, L, with a nonnegative
  --         INTEGER, R. R is converted to an UNRESOLVED_UNSIGNED vector of
  --         SIZE L'LENGTH before multiplication.

  -- Id: A.18
  function "*" (L : NATURAL; R : UNSIGNED) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED((R'LENGTH+R'LENGTH-1) downto 0)
  -- Result: Multiplies an UNRESOLVED_UNSIGNED vector, R, with a nonnegative
  --         INTEGER, L. L is converted to an UNRESOLVED_UNSIGNED vector of
  --         SIZE R'LENGTH before multiplication.

  -- Id: A.19
  function "*" (L : SIGNED; R : INTEGER) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED((L'LENGTH+L'LENGTH-1) downto 0)
  -- Result: Multiplies an UNRESOLVED_SIGNED vector, L, with an INTEGER, R. R is
  --         converted to an UNRESOLVED_SIGNED vector of SIZE L'LENGTH before
  --         multiplication.

  -- Id: A.20
  function "*" (L : INTEGER; R : SIGNED) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED((R'LENGTH+R'LENGTH-1) downto 0)
  -- Result: Multiplies an UNRESOLVED_SIGNED vector, R, with an INTEGER, L. L is
  --         converted to an UNRESOLVED_SIGNED vector of SIZE R'LENGTH before
  --         multiplication.

  --============================================================================
  --
  -- NOTE: If second argument is zero for "/" operator, a severity level
  --       of ERROR is issued.

  -- Id: A.21
  function "/" (L, R : UNSIGNED) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(L'LENGTH-1 downto 0)
  -- Result: Divides an UNRESOLVED_UNSIGNED vector, L, by another UNRESOLVED_UNSIGNED vector, R.

  -- Id: A.22
  function "/" (L, R : SIGNED) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Divides an UNRESOLVED_SIGNED vector, L, by another UNRESOLVED_SIGNED vector, R.

  -- Id: A.23
  function "/" (L : UNSIGNED; R : NATURAL) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(L'LENGTH-1 downto 0)
  -- Result: Divides an UNRESOLVED_UNSIGNED vector, L, by a nonnegative INTEGER, R.
  --         If NO_OF_BITS(R) > L'LENGTH, result is truncated to L'LENGTH.

  -- Id: A.24
  function "/" (L : NATURAL; R : UNSIGNED) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(R'LENGTH-1 downto 0)
  -- Result: Divides a nonnegative INTEGER, L, by an UNRESOLVED_UNSIGNED vector, R.
  --         If NO_OF_BITS(L) > R'LENGTH, result is truncated to R'LENGTH.

  -- Id: A.25
  function "/" (L : SIGNED; R : INTEGER) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Divides an UNRESOLVED_SIGNED vector, L, by an INTEGER, R.
  --         If NO_OF_BITS(R) > L'LENGTH, result is truncated to L'LENGTH.

  -- Id: A.26
  function "/" (L : INTEGER; R : SIGNED) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(R'LENGTH-1 downto 0)
  -- Result: Divides an INTEGER, L, by an UNRESOLVED_SIGNED vector, R.
  --         If NO_OF_BITS(L) > R'LENGTH, result is truncated to R'LENGTH.

  --============================================================================
  --
  -- NOTE: If second argument is zero for "rem" operator, a severity level
  --       of ERROR is issued.

  -- Id: A.27
  function "rem" (L, R : UNSIGNED) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(R'LENGTH-1 downto 0)
  -- Result: Computes "L rem R" where L and R are UNRESOLVED_UNSIGNED vectors.

  -- Id: A.28
  function "rem" (L, R : SIGNED) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(R'LENGTH-1 downto 0)
  -- Result: Computes "L rem R" where L and R are UNRESOLVED_SIGNED vectors.

  -- Id: A.29
  function "rem" (L : UNSIGNED; R : NATURAL) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(L'LENGTH-1 downto 0)
  -- Result: Computes "L rem R" where L is an UNRESOLVED_UNSIGNED vector and R is a
  --         nonnegative INTEGER.
  --         If NO_OF_BITS(R) > L'LENGTH, result is truncated to L'LENGTH.

  -- Id: A.30
  function "rem" (L : NATURAL; R : UNSIGNED) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(R'LENGTH-1 downto 0)
  -- Result: Computes "L rem R" where R is an UNRESOLVED_UNSIGNED vector and L is a
  --         nonnegative INTEGER.
  --         If NO_OF_BITS(L) > R'LENGTH, result is truncated to R'LENGTH.

  -- Id: A.31
  function "rem" (L : SIGNED; R : INTEGER) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Computes "L rem R" where L is UNRESOLVED_SIGNED vector and R is an INTEGER.
  --         If NO_OF_BITS(R) > L'LENGTH, result is truncated to L'LENGTH.

  -- Id: A.32
  function "rem" (L : INTEGER; R : SIGNED) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(R'LENGTH-1 downto 0)
  -- Result: Computes "L rem R" where R is UNRESOLVED_SIGNED vector and L is an INTEGER.
  --         If NO_OF_BITS(L) > R'LENGTH, result is truncated to R'LENGTH.

  --============================================================================
  --
  -- NOTE: If second argument is zero for "mod" operator, a severity level
  --       of ERROR is issued.

  -- Id: A.33
  function "mod" (L, R : UNSIGNED) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(R'LENGTH-1 downto 0)
  -- Result: Computes "L mod R" where L and R are UNRESOLVED_UNSIGNED vectors.

  -- Id: A.34
  function "mod" (L, R : SIGNED) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(R'LENGTH-1 downto 0)
  -- Result: Computes "L mod R" where L and R are UNRESOLVED_SIGNED vectors.

  -- Id: A.35
  function "mod" (L : UNSIGNED; R : NATURAL) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(L'LENGTH-1 downto 0)
  -- Result: Computes "L mod R" where L is an UNRESOLVED_UNSIGNED vector and R
  --         is a nonnegative INTEGER.
  --         If NO_OF_BITS(R) > L'LENGTH, result is truncated to L'LENGTH.

  -- Id: A.36
  function "mod" (L : NATURAL; R : UNSIGNED) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(R'LENGTH-1 downto 0)
  -- Result: Computes "L mod R" where R is an UNRESOLVED_UNSIGNED vector and L
  --         is a nonnegative INTEGER.
  --         If NO_OF_BITS(L) > R'LENGTH, result is truncated to R'LENGTH.

  -- Id: A.37
  function "mod" (L : SIGNED; R : INTEGER) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Computes "L mod R" where L is an UNRESOLVED_SIGNED vector and
  --         R is an INTEGER.
  --         If NO_OF_BITS(R) > L'LENGTH, result is truncated to L'LENGTH.

  -- Id: A.38
  function "mod" (L : INTEGER; R : SIGNED) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(R'LENGTH-1 downto 0)
  -- Result: Computes "L mod R" where L is an INTEGER and
  --         R is an UNRESOLVED_SIGNED vector.
  --         If NO_OF_BITS(L) > R'LENGTH, result is truncated to R'LENGTH.

  -- Result subtype: INTEGER
  -- Result: Finds the leftmost occurrence of the value of Y in ARG.
  --         Returns the index of the occurrence if it exists, or -1 otherwise.

  -- Result subtype: INTEGER
  -- Result: Finds the leftmost occurrence of the value of Y in ARG.
  --         Returns the index of the occurrence if it exists, or -1 otherwise.

  -- Result subtype: INTEGER
  -- Result: Finds the leftmost occurrence of the value of Y in ARG.
  --         Returns the index of the occurrence if it exists, or -1 otherwise.

  -- Result subtype: INTEGER
  -- Result: Finds the leftmost occurrence of the value of Y in ARG.
  --         Returns the index of the occurrence if it exists, or -1 otherwise.

  --============================================================================
  -- Comparison Operators
  --============================================================================

  -- Id: C.1
  function ">" (L, R : UNSIGNED) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L > R" where L and R are UNRESOLVED_UNSIGNED vectors possibly
  --         of different lengths.

  -- Id: C.2
  function ">" (L, R : SIGNED) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L > R" where L and R are UNRESOLVED_SIGNED vectors possibly
  --         of different lengths.

  -- Id: C.3
  function ">" (L : NATURAL; R : UNSIGNED) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L > R" where L is a nonnegative INTEGER and
  --         R is an UNRESOLVED_UNSIGNED vector.

  -- Id: C.4
  function ">" (L : INTEGER; R : SIGNED) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L > R" where L is a INTEGER and
  --         R is an UNRESOLVED_SIGNED vector.

  -- Id: C.5
  function ">" (L : UNSIGNED; R : NATURAL) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L > R" where L is an UNRESOLVED_UNSIGNED vector and
  --         R is a nonnegative INTEGER.

  -- Id: C.6
  function ">" (L : SIGNED; R : INTEGER) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L > R" where L is an UNRESOLVED_SIGNED vector and
  --         R is a INTEGER.

  --============================================================================

  -- Id: C.7
  function "<" (L, R : UNSIGNED) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L < R" where L and R are UNRESOLVED_UNSIGNED vectors possibly
  --         of different lengths.

  -- Id: C.8
  function "<" (L, R : SIGNED) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L < R" where L and R are UNRESOLVED_SIGNED vectors possibly
  --         of different lengths.

  -- Id: C.9
  function "<" (L : NATURAL; R : UNSIGNED) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L < R" where L is a nonnegative INTEGER and
  --         R is an UNRESOLVED_UNSIGNED vector.

  -- Id: C.10
  function "<" (L : INTEGER; R : SIGNED) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L < R" where L is an INTEGER and
  --         R is an UNRESOLVED_SIGNED vector.

  -- Id: C.11
  function "<" (L : UNSIGNED; R : NATURAL) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L < R" where L is an UNRESOLVED_UNSIGNED vector and
  --         R is a nonnegative INTEGER.

  -- Id: C.12
  function "<" (L : SIGNED; R : INTEGER) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L < R" where L is an UNRESOLVED_SIGNED vector and
  --         R is an INTEGER.

  --============================================================================

  -- Id: C.13
  function "<=" (L, R : UNSIGNED) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L <= R" where L and R are UNRESOLVED_UNSIGNED vectors possibly
  --         of different lengths.

  -- Id: C.14
  function "<=" (L, R : SIGNED) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L <= R" where L and R are UNRESOLVED_SIGNED vectors possibly
  --         of different lengths.

  -- Id: C.15
  function "<=" (L : NATURAL; R : UNSIGNED) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L <= R" where L is a nonnegative INTEGER and
  --         R is an UNRESOLVED_UNSIGNED vector.

  -- Id: C.16
  function "<=" (L : INTEGER; R : SIGNED) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L <= R" where L is an INTEGER and
  --         R is an UNRESOLVED_SIGNED vector.

  -- Id: C.17
  function "<=" (L : UNSIGNED; R : NATURAL) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L <= R" where L is an UNRESOLVED_UNSIGNED vector and
  --         R is a nonnegative INTEGER.

  -- Id: C.18
  function "<=" (L : SIGNED; R : INTEGER) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L <= R" where L is an UNRESOLVED_SIGNED vector and
  --         R is an INTEGER.

  --============================================================================

  -- Id: C.19
  function ">=" (L, R : UNSIGNED) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L >= R" where L and R are UNRESOLVED_UNSIGNED vectors possibly
  --         of different lengths.

  -- Id: C.20
  function ">=" (L, R : SIGNED) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L >= R" where L and R are UNRESOLVED_SIGNED vectors possibly
  --         of different lengths.

  -- Id: C.21
  function ">=" (L : NATURAL; R : UNSIGNED) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L >= R" where L is a nonnegative INTEGER and
  --         R is an UNRESOLVED_UNSIGNED vector.

  -- Id: C.22
  function ">=" (L : INTEGER; R : SIGNED) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L >= R" where L is an INTEGER and
  --         R is an UNRESOLVED_SIGNED vector.

  -- Id: C.23
  function ">=" (L : UNSIGNED; R : NATURAL) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L >= R" where L is an UNRESOLVED_UNSIGNED vector and
  --         R is a nonnegative INTEGER.

  -- Id: C.24
  function ">=" (L : SIGNED; R : INTEGER) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L >= R" where L is an UNRESOLVED_SIGNED vector and
  --         R is an INTEGER.

  --============================================================================

  -- Id: C.25
  function "=" (L, R : UNSIGNED) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L = R" where L and R are UNRESOLVED_UNSIGNED vectors possibly
  --         of different lengths.

  -- Id: C.26
  function "=" (L, R : SIGNED) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L = R" where L and R are UNRESOLVED_SIGNED vectors possibly
  --         of different lengths.

  -- Id: C.27
  function "=" (L : NATURAL; R : UNSIGNED) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L = R" where L is a nonnegative INTEGER and
  --         R is an UNRESOLVED_UNSIGNED vector.

  -- Id: C.28
  function "=" (L : INTEGER; R : SIGNED) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L = R" where L is an INTEGER and
  --         R is an UNRESOLVED_SIGNED vector.

  -- Id: C.29
  function "=" (L : UNSIGNED; R : NATURAL) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L = R" where L is an UNRESOLVED_UNSIGNED vector and
  --         R is a nonnegative INTEGER.

  -- Id: C.30
  function "=" (L : SIGNED; R : INTEGER) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L = R" where L is an UNRESOLVED_SIGNED vector and
  --         R is an INTEGER.

  --============================================================================

  -- Id: C.31
  function "/=" (L, R : UNSIGNED) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L /= R" where L and R are UNRESOLVED_UNSIGNED vectors possibly
  --         of different lengths.

  -- Id: C.32
  function "/=" (L, R : SIGNED) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L /= R" where L and R are UNRESOLVED_SIGNED vectors possibly
  --         of different lengths.

  -- Id: C.33
  function "/=" (L : NATURAL; R : UNSIGNED) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L /= R" where L is a nonnegative INTEGER and
  --         R is an UNRESOLVED_UNSIGNED vector.

  -- Id: C.34
  function "/=" (L : INTEGER; R : SIGNED) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L /= R" where L is an INTEGER and
  --         R is an UNRESOLVED_SIGNED vector.

  -- Id: C.35
  function "/=" (L : UNSIGNED; R : NATURAL) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L /= R" where L is an UNRESOLVED_UNSIGNED vector and
  --         R is a nonnegative INTEGER.

  -- Id: C.36
  function "/=" (L : SIGNED; R : INTEGER) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: Computes "L /= R" where L is an UNRESOLVED_SIGNED vector and
  --         R is an INTEGER.

  --============================================================================

  -- Result subtype: UNRESOLVED_UNSIGNED
  -- Result: Returns the lesser of two UNRESOLVED_UNSIGNED vectors that may be
  --         of different lengths.

  -- Result subtype: UNRESOLVED_SIGNED
  -- Result: Returns the lesser of two UNRESOLVED_SIGNED vectors that may be
  --         of different lengths.

  -- Result subtype: UNRESOLVED_UNSIGNED
  -- Result: Returns the lesser of a nonnegative INTEGER, L, and
  --         an UNRESOLVED_UNSIGNED vector, R.

  -- Result subtype: UNRESOLVED_SIGNED
  -- Result: Returns the lesser of an INTEGER, L, and an UNRESOLVED_SIGNED
  --         vector, R.

  -- Result subtype: UNRESOLVED_UNSIGNED
  -- Result: Returns the lesser of an UNRESOLVED_UNSIGNED vector, L, and
  --         a nonnegative INTEGER, R.

  -- Result subtype: UNRESOLVED_SIGNED
  -- Result: Returns the lesser of an UNRESOLVED_SIGNED vector, L, and
  --         an INTEGER, R.

  --============================================================================

  -- Result subtype: UNRESOLVED_UNSIGNED
  -- Result: Returns the greater of two UNRESOLVED_UNSIGNED vectors that may be
  --         of different lengths.

  -- Result subtype: UNRESOLVED_SIGNED
  -- Result: Returns the greater of two UNRESOLVED_SIGNED vectors that may be
  --         of different lengths.

  -- Result subtype: UNRESOLVED_UNSIGNED
  -- Result: Returns the greater of a nonnegative INTEGER, L, and
  --         an UNRESOLVED_UNSIGNED vector, R.

  -- Result subtype: UNRESOLVED_SIGNED
  -- Result: Returns the greater of an INTEGER, L, and an UNRESOLVED_SIGNED
  --         vector, R.

  -- Result subtype: UNRESOLVED_UNSIGNED
  -- Result: Returns the greater of an UNRESOLVED_UNSIGNED vector, L, and
  --         a nonnegative INTEGER, R.

  -- Result subtype: UNRESOLVED_SIGNED
  -- Result: Returns the greater of an UNRESOLVED_SIGNED vector, L, and
  --         an INTEGER, R.

  --============================================================================

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L > R" where L and R are UNRESOLVED_UNSIGNED vectors possibly
  --         of different lengths.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L > R" where L and R are UNRESOLVED_SIGNED vectors possibly
  --         of different lengths.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L > R" where L is a nonnegative INTEGER and
  --         R is an UNRESOLVED_UNSIGNED vector.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L > R" where L is a INTEGER and
  --         R is an UNRESOLVED_SIGNED vector.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L > R" where L is an UNRESOLVED_UNSIGNED vector and
  --         R is a nonnegative INTEGER.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L > R" where L is an UNRESOLVED_SIGNED vector and
  --         R is a INTEGER.

  --============================================================================

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L < R" where L and R are UNRESOLVED_UNSIGNED vectors possibly
  --         of different lengths.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L < R" where L and R are UNRESOLVED_SIGNED vectors possibly
  --         of different lengths.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L < R" where L is a nonnegative INTEGER and
  --         R is an UNRESOLVED_UNSIGNED vector.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L < R" where L is an INTEGER and
  --         R is an UNRESOLVED_SIGNED vector.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L < R" where L is an UNRESOLVED_UNSIGNED vector and
  --         R is a nonnegative INTEGER.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L < R" where L is an UNRESOLVED_SIGNED vector and
  --         R is an INTEGER.

  --============================================================================

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L <= R" where L and R are UNRESOLVED_UNSIGNED vectors possibly
  --         of different lengths.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L <= R" where L and R are UNRESOLVED_SIGNED vectors possibly
  --         of different lengths.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L <= R" where L is a nonnegative INTEGER and
  --         R is an UNRESOLVED_UNSIGNED vector.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L <= R" where L is an INTEGER and
  --         R is an UNRESOLVED_SIGNED vector.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L <= R" where L is an UNRESOLVED_UNSIGNED vector and
  --         R is a nonnegative INTEGER.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L <= R" where L is an UNRESOLVED_SIGNED vector and
  --         R is an INTEGER.

  --============================================================================

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L >= R" where L and R are UNRESOLVED_UNSIGNED vectors possibly
  --         of different lengths.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L >= R" where L and R are UNRESOLVED_SIGNED vectors possibly
  --         of different lengths.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L >= R" where L is a nonnegative INTEGER and
  --         R is an UNRESOLVED_UNSIGNED vector.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L >= R" where L is an INTEGER and
  --         R is an UNRESOLVED_SIGNED vector.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L >= R" where L is an UNRESOLVED_UNSIGNED vector and
  --         R is a nonnegative INTEGER.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L >= R" where L is an UNRESOLVED_SIGNED vector and
  --         R is an INTEGER.

  --============================================================================

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L = R" where L and R are UNRESOLVED_UNSIGNED vectors possibly
  --         of different lengths.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L = R" where L and R are UNRESOLVED_SIGNED vectors possibly
  --         of different lengths.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L = R" where L is a nonnegative INTEGER and
  --         R is an UNRESOLVED_UNSIGNED vector.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L = R" where L is an INTEGER and
  --         R is an UNRESOLVED_SIGNED vector.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L = R" where L is an UNRESOLVED_UNSIGNED vector and
  --         R is a nonnegative INTEGER.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L = R" where L is an UNRESOLVED_SIGNED vector and
  --         R is an INTEGER.

  --============================================================================

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L /= R" where L and R are UNRESOLVED_UNSIGNED vectors possibly
  --         of different lengths.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L /= R" where L and R are UNRESOLVED_SIGNED vectors possibly
  --         of different lengths.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L /= R" where L is a nonnegative INTEGER and
  --         R is an UNRESOLVED_UNSIGNED vector.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L /= R" where L is an INTEGER and
  --         R is an UNRESOLVED_SIGNED vector.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L /= R" where L is an UNRESOLVED_UNSIGNED vector and
  --         R is a nonnegative INTEGER.

  -- Result subtype: STD_ULOGIC
  -- Result: Computes "L /= R" where L is an UNRESOLVED_SIGNED vector and
  --         R is an INTEGER.

  --============================================================================
  -- Shift and Rotate Functions
  --============================================================================

  -- Id: S.1
  function SHIFT_LEFT (ARG : UNSIGNED; COUNT : NATURAL) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(ARG'LENGTH-1 downto 0)
  -- Result: Performs a shift-left on an UNRESOLVED_UNSIGNED vector COUNT times.
  --         The vacated positions are filled with '0'.
  --         The COUNT leftmost elements are lost.

  -- Id: S.2
  function SHIFT_RIGHT (ARG : UNSIGNED; COUNT : NATURAL) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(ARG'LENGTH-1 downto 0)
  -- Result: Performs a shift-right on an UNRESOLVED_UNSIGNED vector COUNT times.
  --         The vacated positions are filled with '0'.
  --         The COUNT rightmost elements are lost.

  -- Id: S.3
  function SHIFT_LEFT (ARG : SIGNED; COUNT : NATURAL) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(ARG'LENGTH-1 downto 0)
  -- Result: Performs a shift-left on an UNRESOLVED_SIGNED vector COUNT times.
  --         The vacated positions are filled with '0'.
  --         The COUNT leftmost elements are lost.

  -- Id: S.4
  function SHIFT_RIGHT (ARG : SIGNED; COUNT : NATURAL) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(ARG'LENGTH-1 downto 0)
  -- Result: Performs a shift-right on an UNRESOLVED_SIGNED vector COUNT times.
  --         The vacated positions are filled with the leftmost
  --         element, ARG'LEFT. The COUNT rightmost elements are lost.

  --============================================================================

  -- Id: S.5
  function ROTATE_LEFT (ARG : UNSIGNED; COUNT : NATURAL) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(ARG'LENGTH-1 downto 0)
  -- Result: Performs a rotate-left of an UNRESOLVED_UNSIGNED vector COUNT times.

  -- Id: S.6
  function ROTATE_RIGHT (ARG : UNSIGNED; COUNT : NATURAL) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(ARG'LENGTH-1 downto 0)
  -- Result: Performs a rotate-right of an UNRESOLVED_UNSIGNED vector COUNT times.

  -- Id: S.7
  function ROTATE_LEFT (ARG : SIGNED; COUNT : NATURAL) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(ARG'LENGTH-1 downto 0)
  -- Result: Performs a logical rotate-left of an UNRESOLVED_SIGNED
  --         vector COUNT times.

  -- Id: S.8
  function ROTATE_RIGHT (ARG : SIGNED; COUNT : NATURAL) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(ARG'LENGTH-1 downto 0)
  -- Result: Performs a logical rotate-right of an UNRESOLVED_SIGNED
  --         vector COUNT times.

  --============================================================================

  --============================================================================

  ------------------------------------------------------------------------------
  --   Note: Function S.9 is not compatible with IEEE Std 1076-1987. Comment
  --   out the function (declaration and body) for IEEE Std 1076-1987 compatibility.
  ------------------------------------------------------------------------------
  -- Id: S.9
  function "sll" (ARG : UNSIGNED; COUNT : INTEGER) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(ARG'LENGTH-1 downto 0)
  -- Result: SHIFT_LEFT(ARG, COUNT)

  ------------------------------------------------------------------------------
  -- Note: Function S.10 is not compatible with IEEE Std 1076-1987. Comment
  --   out the function (declaration and body) for IEEE Std 1076-1987 compatibility.
  ------------------------------------------------------------------------------
  -- Id: S.10
  function "sll" (ARG : SIGNED; COUNT : INTEGER) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(ARG'LENGTH-1 downto 0)
  -- Result: SHIFT_LEFT(ARG, COUNT)

  ------------------------------------------------------------------------------
  --   Note: Function S.11 is not compatible with IEEE Std 1076-1987. Comment
  --   out the function (declaration and body) for IEEE StdL 1076-1987 compatibility.
  ------------------------------------------------------------------------------
  -- Id: S.11
  function "srl" (ARG : UNSIGNED; COUNT : INTEGER) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(ARG'LENGTH-1 downto 0)
  -- Result: SHIFT_RIGHT(ARG, COUNT)

  ------------------------------------------------------------------------------
  --   Note: Function S.12 is not compatible with IEEE Std 1076-1987. Comment
  --   out the function (declaration and body) for IEEE Std 1076-1987 compatibility.
  ------------------------------------------------------------------------------
  -- Id: S.12
  function "srl" (ARG : SIGNED; COUNT : INTEGER) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(ARG'LENGTH-1 downto 0)
  -- Result: UNRESOLVED_SIGNED(SHIFT_RIGHT(UNRESOLVED_UNSIGNED(ARG), COUNT))

  ------------------------------------------------------------------------------
  --   Note: Function S.13 is not compatible with IEEE Std 1076-1987. Comment
  -- out the function (declaration and body) for IEEE Std 1076-1987 compatibility.
  ------------------------------------------------------------------------------
  -- Id: S.13
  function "rol" (ARG : UNSIGNED; COUNT : INTEGER) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(ARG'LENGTH-1 downto 0)
  -- Result: ROTATE_LEFT(ARG, COUNT)

  ------------------------------------------------------------------------------
  --   Note: Function S.14 is not compatible with IEEE Std 1076-1987. Comment
  --   out the function (declaration and body) for IEEE Std 1076-1987 compatibility.
  ------------------------------------------------------------------------------
  -- Id: S.14
  function "rol" (ARG : SIGNED; COUNT : INTEGER) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(ARG'LENGTH-1 downto 0)
  -- Result: ROTATE_LEFT(ARG, COUNT)

  ------------------------------------------------------------------------------
  -- Note: Function S.15 is not compatible with IEEE Std 1076-1987. Comment
  --   out the function (declaration and body) for IEEE Std 1076-1987 compatibility.
  ------------------------------------------------------------------------------
  -- Id: S.15
  function "ror" (ARG : UNSIGNED; COUNT : INTEGER) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(ARG'LENGTH-1 downto 0)
  -- Result: ROTATE_RIGHT(ARG, COUNT)

  ------------------------------------------------------------------------------
  --   Note: Function S.16 is not compatible with IEEE Std 1076-1987. Comment
  --   out the function (declaration and body) for IEEE Std 1076-1987 compatibility.
  ------------------------------------------------------------------------------
  -- Id: S.16
  function "ror" (ARG : SIGNED; COUNT : INTEGER) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(ARG'LENGTH-1 downto 0)
  -- Result: ROTATE_RIGHT(ARG, COUNT)

  -- Result subtype: UNRESOLVED_UNSIGNED(ARG'LENGTH-1 downto 0)
  -- Result: SHIFT_LEFT(ARG, COUNT)

  -- Result subtype: UNRESOLVED_SIGNED(ARG'LENGTH-1 downto 0)
  -- Result: SHIFT_LEFT(ARG, COUNT)

  -- Result subtype: UNRESOLVED_UNSIGNED(ARG'LENGTH-1 downto 0)
  -- Result: SHIFT_RIGHT(ARG, COUNT)

  -- Result subtype: UNRESOLVED_SIGNED(ARG'LENGTH-1 downto 0)
  -- Result: SHIFT_RIGHT(ARG, COUNT)

  --============================================================================
  --   RESIZE Functions
  --============================================================================

  -- Id: R.1
  function RESIZE (ARG : SIGNED; NEW_SIZE : NATURAL) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(NEW_SIZE-1 downto 0)
  -- Result: Resizes the UNRESOLVED_SIGNED vector ARG to the specified size.
  --         To create a larger vector, the new [leftmost] bit positions
  --         are filled with the sign bit (ARG'LEFT). When truncating,
  --         the sign bit is retained along with the rightmost part.

  -- Id: R.2
  function RESIZE (ARG : UNSIGNED; NEW_SIZE : NATURAL) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(NEW_SIZE-1 downto 0)
  -- Result: Resizes the UNRESOLVED_SIGNED vector ARG to the specified size.
  --         To create a larger vector, the new [leftmost] bit positions
  --         are filled with '0'. When truncating, the leftmost bits
  --         are dropped.

  -- Result subtype: UNRESOLVED_UNSIGNED (SIZE_RES'length-1 downto 0)

  -- Result subtype: UNRESOLVED_SIGNED (SIZE_RES'length-1 downto 0)

  --============================================================================
  -- Conversion Functions
  --============================================================================

  -- Id: D.1
  function TO_INTEGER (ARG : UNSIGNED) return NATURAL;
  -- Result subtype: NATURAL. Value cannot be negative since parameter is an
  --             UNRESOLVED_UNSIGNED vector.
  -- Result: Converts the UNRESOLVED_UNSIGNED vector to an INTEGER.

  -- Id: D.2
  function TO_INTEGER (ARG : SIGNED) return INTEGER;
  -- Result subtype: INTEGER
  -- Result: Converts an UNRESOLVED_SIGNED vector to an INTEGER.

  -- Id: D.3
  function TO_UNSIGNED (ARG, SIZE : NATURAL) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(SIZE-1 downto 0)
  -- Result: Converts a nonnegative INTEGER to an UNRESOLVED_UNSIGNED vector with
  --         the specified SIZE.

  -- Id: D.4
  function TO_SIGNED (ARG : INTEGER; SIZE : NATURAL) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(SIZE-1 downto 0)
  -- Result: Converts an INTEGER to a UNRESOLVED_SIGNED vector of the specified SIZE.

  -- Result subtype: UNRESOLVED_UNSIGNED(SIZE_RES'length-1 downto 0)

  -- Result subtype: UNRESOLVED_SIGNED(SIZE_RES'length-1 downto 0)

  --============================================================================
  -- Logical Operators
  --============================================================================

  -- Id: L.1
  function "not" (L : UNSIGNED) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(L'LENGTH-1 downto 0)
  -- Result: Termwise inversion

  -- Id: L.2
  function "and" (L, R : UNSIGNED) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(L'LENGTH-1 downto 0)
  -- Result: Vector AND operation

  -- Id: L.3
  function "or" (L, R : UNSIGNED) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(L'LENGTH-1 downto 0)
  -- Result: Vector OR operation

  -- Id: L.4
  function "nand" (L, R : UNSIGNED) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(L'LENGTH-1 downto 0)
  -- Result: Vector NAND operation

  -- Id: L.5
  function "nor" (L, R : UNSIGNED) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(L'LENGTH-1 downto 0)
  -- Result: Vector NOR operation

  -- Id: L.6
  function "xor" (L, R : UNSIGNED) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(L'LENGTH-1 downto 0)
  -- Result: Vector XOR operation

  -- ---------------------------------------------------------------------------
  -- Note: Function L.7 is not compatible with IEEE Std 1076-1987. Comment
  -- out the function (declaration and body) for IEEE Std 1076-1987 compatibility.
  -- ---------------------------------------------------------------------------
  -- Id: L.7
  function "xnor" (L, R : UNSIGNED) return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(L'LENGTH-1 downto 0)
  -- Result: Vector XNOR operation

  -- Id: L.8
  function "not" (L : SIGNED) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Termwise inversion

  -- Id: L.9
  function "and" (L, R : SIGNED) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Vector AND operation

  -- Id: L.10
  function "or" (L, R : SIGNED) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Vector OR operation

  -- Id: L.11
  function "nand" (L, R : SIGNED) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Vector NAND operation

  -- Id: L.12
  function "nor" (L, R : SIGNED) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Vector NOR operation

  -- Id: L.13
  function "xor" (L, R : SIGNED) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Vector XOR operation

  -- ---------------------------------------------------------------------------
  -- Note: Function L.14 is not compatible with IEEE Std 1076-1987. Comment
  -- out the function (declaration and body) for IEEE Std 1076-1987 compatibility.
  -- ---------------------------------------------------------------------------
  -- Id: L.14
  function "xnor" (L, R : SIGNED) return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Vector XNOR operation

  -- Result subtype: UNRESOLVED_SIGNED(R'LENGTH-1 downto 0)
  -- Result: Scalar/Vector AND operation

  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Vector/Scalar AND operation

  -- Result subtype: UNRESOLVED_SIGNED(R'LENGTH-1 downto 0)
  -- Result: Scalar/Vector OR operation

  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Vector/Scalar OR operation

  -- Result subtype: UNRESOLVED_SIGNED(R'LENGTH-1 downto 0)
  -- Result: Scalar/Vector NAND operation

  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Vector/Scalar NAND operation

  -- Result subtype: UNRESOLVED_SIGNED(R'LENGTH-1 downto 0)
  -- Result: Scalar/Vector NOR operation

  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Vector/Scalar NOR operation

  -- Result subtype: UNRESOLVED_SIGNED(R'LENGTH-1 downto 0)
  -- Result: Scalar/Vector XOR operation

  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Vector/Scalar XOR operation

  -- Result subtype: UNRESOLVED_SIGNED(R'LENGTH-1 downto 0)
  -- Result: Scalar/Vector XNOR operation

  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Vector/Scalar XNOR operation

  -- Result subtype: UNRESOLVED_SIGNED(R'LENGTH-1 downto 0)
  -- Result: Scalar/Vector AND operation

  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Vector/Scalar AND operation

  -- Result subtype: UNRESOLVED_SIGNED(R'LENGTH-1 downto 0)
  -- Result: Scalar/Vector OR operation

  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Vector/Scalar OR operation

  -- Result subtype: UNRESOLVED_SIGNED(R'LENGTH-1 downto 0)
  -- Result: Scalar/Vector NAND operation

  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Vector/Scalar NAND operation

  -- Result subtype: UNRESOLVED_SIGNED(R'LENGTH-1 downto 0)
  -- Result: Scalar/Vector NOR operation

  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Vector/Scalar NOR operation

  -- Result subtype: UNRESOLVED_SIGNED(R'LENGTH-1 downto 0)
  -- Result: Scalar/Vector XOR operation

  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Vector/Scalar XOR operation

  -- Result subtype: UNRESOLVED_SIGNED(R'LENGTH-1 downto 0)
  -- Result: Scalar/Vector XNOR operation

  -- Result subtype: UNRESOLVED_SIGNED(L'LENGTH-1 downto 0)
  -- Result: Vector/Scalar XNOR operation

  -- Result subtype: STD_ULOGIC.
  -- Result: Result of and'ing all of the bits of the vector.

  -- Result subtype: STD_ULOGIC.
  -- Result: Result of nand'ing all of the bits of the vector.

  -- Result subtype: STD_ULOGIC.
  -- Result: Result of or'ing all of the bits of the vector.

  -- Result subtype: STD_ULOGIC.
  -- Result: Result of nor'ing all of the bits of the vector.

  -- Result subtype: STD_ULOGIC.
  -- Result: Result of xor'ing all of the bits of the vector.

  -- Result subtype: STD_ULOGIC.
  -- Result: Result of xnor'ing all of the bits of the vector.

  -- Result subtype: STD_ULOGIC.
  -- Result: Result of and'ing all of the bits of the vector.

  -- Result subtype: STD_ULOGIC.
  -- Result: Result of nand'ing all of the bits of the vector.

  -- Result subtype: STD_ULOGIC.
  -- Result: Result of or'ing all of the bits of the vector.

  -- Result subtype: STD_ULOGIC.
  -- Result: Result of nor'ing all of the bits of the vector.

  -- Result subtype: STD_ULOGIC.
  -- Result: Result of xor'ing all of the bits of the vector.

  -- Result subtype: STD_ULOGIC.
  -- Result: Result of xnor'ing all of the bits of the vector.

  --============================================================================
  -- Match Functions
  --============================================================================

  -- Id: M.1
  function STD_MATCH (L, R : STD_ULOGIC) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: terms compared per STD_LOGIC_1164 intent

  -- Id: M.2
  function STD_MATCH (L, R : UNSIGNED) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: terms compared per STD_LOGIC_1164 intent

  -- Id: M.3
  function STD_MATCH (L, R : SIGNED) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: terms compared per STD_LOGIC_1164 intent

  -- Id: M.5
  function STD_MATCH (L, R : STD_ULOGIC_VECTOR) return BOOLEAN;
  -- Result subtype: BOOLEAN
  -- Result: terms compared per STD_LOGIC_1164 intent

  --============================================================================
  -- Translation Functions
  --============================================================================

  -- Id: T.1
  function TO_01 (S : UNSIGNED; XMAP : STD_ULOGIC := '0') return UNSIGNED;
  -- Result subtype: UNRESOLVED_UNSIGNED(S'RANGE)
  -- Result: Termwise, 'H' is translated to '1', and 'L' is translated
  --         to '0'. If a value other than '0'|'1'|'H'|'L' is found,
  --         the array is set to (others => XMAP), and a warning is
  --         issued.

  -- Id: T.2
  function TO_01 (S : SIGNED; XMAP : STD_ULOGIC := '0') return SIGNED;
  -- Result subtype: UNRESOLVED_SIGNED(S'RANGE)
  -- Result: Termwise, 'H' is translated to '1', and 'L' is translated
  --         to '0'. If a value other than '0'|'1'|'H'|'L' is found,
  --         the array is set to (others => XMAP), and a warning is
  --         issued.

  -- Result subtype: UNRESOLVED_UNSIGNED(S'RANGE)
  -- Result: Termwise, 'H' is translated to '1', 'L' is translated to '0',
  --         and values other than '0'|'1'|'H'|'L' are translated to 'X'.

  -- Result subtype: UNRESOLVED_SIGNED(S'RANGE)
  -- Result: Termwise, 'H' is translated to '1', 'L' is translated to '0',
  --         and values other than '0'|'1'|'H'|'L' are translated to 'X'.

  -- Result subtype: UNRESOLVED_UNSIGNED(S'RANGE)
  -- Result: Termwise, 'H' is translated to '1', 'L' is translated to '0',
  --         and values other than '0'|'1'|'H'|'L'|'Z' are translated to 'X'.

  -- Result subtype: UNRESOLVED_SIGNED(S'RANGE)
  -- Result: Termwise, 'H' is translated to '1', 'L' is translated to '0',
  --         and values other than '0'|'1'|'H'|'L'|'Z' are translated to 'X'.

  -- Result subtype: UNRESOLVED_UNSIGNED(S'RANGE)
  -- Result: Termwise, 'H' is translated to '1', 'L' is translated to '0',
  --         and values other than 'U'|'0'|'1'|'H'|'L' are translated to 'X'.

  -- Result subtype: UNRESOLVED_SIGNED(S'RANGE)
  -- Result: Termwise, 'H' is translated to '1', 'L' is translated to '0',
  --         and values other than 'U'|'0'|'1'|'H'|'L' are translated to 'X'.

  -- Result subtype: BOOLEAN
  -- Result: TRUE if S contains a 'U'|'X'|'Z'|'W'|'-' value, FALSE otherwise.

  -- Result subtype: BOOLEAN
  -- Result: TRUE if S contains a 'U'|'X'|'Z'|'W'|'-' value, FALSE otherwise.

  --============================================================================
  -- string conversion and write operations
  --============================================================================
  -- the following operations are predefined

  -- function TO_STRING (value : UNRESOLVED_UNSIGNED) return STRING;
  -- function TO_STRING (value : UNRESOLVED_SIGNED) return STRING;

  -- explicitly defined operations






























end package NUMERIC_STD;
