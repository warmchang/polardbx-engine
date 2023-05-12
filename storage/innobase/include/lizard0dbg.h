/* Copyright (c) 2018, 2021, Alibaba and/or its affiliates. All rights reserved.
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.
   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL/PolarDB-X Engine hereby grant you an
   additional permission to link the program and your derivative works with the
   separately licensed software that they have included with
   MySQL/PolarDB-X Engine.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */


/** @file include/lizard0dbg.h
  Lizard debug.

 Created 2020-06-01 by Jianwei.zhao
 *******************************************************/

#ifndef lizard0dbg_h
#define lizard0dbg_h

#include "ut0dbg.h"

#define lizard_ut_a(EXPR) ut_a(EXPR)

#if defined UNIV_DEBUG || defined LIZARD_DEBUG

/** Debug assertion. Does nothing unless UNIV_DEBUG || LIZARD_DEBUG is defined.
 */
#define lizard_ut_ad(EXPR) ut_a(EXPR)
/** Debug assertion. Does nothing unless UNIV_DEBUG || LIZARD_DEBUG is defined.
 */
#define lizard_ut_d(EXPR) EXPR

#else
/** Debug assertion. Does nothing unless UNIV_DEBUG || LIZARD_DEBUG is defined.
 */
#define lizard_ut_ad(EXPR)
/** Debug assertion. Does nothing unless UNIV_DEBUG || LIZARD_DEBUG is defined.
 */
#define lizard_ut_d(EXPR)

#endif



#endif
