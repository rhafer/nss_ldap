/* Copyright (C) 1997 Luke Howard.
   This file is part of the nss_ldap library.
   Contributed by Luke Howard, <lukeh@xedoc.com>, 1997.

   The nss_ldap library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The nss_ldap library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the nss_ldap library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   $Id$
 */

#ifndef _LDAP_NSS_LDAP_DL_H
#define _LDAP_NSS_LDAP_DL_H

#ifdef DL_NSS

#ifdef OSF1
#define NSS_REENTRANT_FUNCTIONS
#endif

#include "irs-nss.h" /* for buffer sizes */

#ifdef OSF1
#define INIT_HANDLE()	do { \
	if (_nss_ldap_libc_handle == NULL) \
		_nss_ldap_libc_handle = dlopen("/usr/shlib/libc_r.so", RTLD_LAZY); \
	} while (0)
#else
#define INIT_HANDLE()	do { \
	if (_nss_ldap_libc_handle == NULL) \
		_nss_ldap_libc_handle = dlopen("/usr/lib/libc.so", RTLD_LAZY); \
	} while (0)
#endif


#endif

#endif /* _LDAP_NSS_LDAP_DL_H */

