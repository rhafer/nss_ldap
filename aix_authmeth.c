/* Copyright (C) 2002-2004 Luke Howard.
   This file is part of the nss_ldap library.
   Contributed by Luke Howard, <lukeh@padl.com>, 2002.

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
 */

/*
 * Glue code to support AIX loadable authentication modules.
 */

#include "config.h"

static char rcsId[] =
  "$Id$";

#define AIX_TEST_HARNESS 1

#ifdef AIX_TEST_HARNESS
#define HAVE_USERSEC_H 1
#include "irs.h"
#include "irs-nss.h"
#endif

#ifdef HAVE_USERSEC_H

#include <stdlib.h>
#include <string.h>
#ifdef AIX_TEST_HARNESS
#include "usersec.h"
#else
#include <usersec.h>
#endif

#ifdef HAVE_LBER_H
#include <lber.h>
#endif
#ifdef HAVE_LDAP_H
#include <ldap.h>
#endif

#include "ldap-nss.h"
#include "util.h"

#define TABLE_KEY_ALL	"ALL"
#define TABLE_USER	"user"
#define TABLE_GROUP	"group"

static struct irs_gr *grp_conn = NULL;
static struct irs_pw *pwd_conn = NULL;

extern void *gr_pvtinit (void);
extern void *pw_pvtinit (void);

/* from ldap-grp.c */
extern char *_nss_ldap_getgrset (char *user);

/* search arguments for getentry method */
typedef struct ldap_uess_args {
  /* argument block */
  const char *lua_key;
  const char *lua_table;
  char **lua_attributes;
  attrval_t *lua_results; /* UESS results */
  int lua_size;

  /* private */
  ldap_map_selector_t lua_map;
  size_t lua__bufsiz;
  size_t lua__buflen;
  char *lua__buffer;
} ldap_uess_args_t; 

static NSS_STATUS uess_get_char(LDAP *ld, LDAPMessage *e, ldap_uess_args_t *arg, int index);
static NSS_STATUS uess_get_char_ex(LDAP *ld, LDAPMessage *e, ldap_uess_args_t *arg, int index, const char *attribute);
static NSS_STATUS uess_get_int(LDAP *ld, LDAPMessage *e, ldap_uess_args_t *arg, int index);
static NSS_STATUS uess_get_pgrp(LDAP *ld, LDAPMessage *e, ldap_uess_args_t *arg, int index);
static NSS_STATUS uess_get_groupsids(LDAP *ld, LDAPMessage *e, ldap_uess_args_t *arg, int index);
static NSS_STATUS uess_get_registry(LDAP *ld, LDAPMessage *e, ldap_uess_args_t *arg, int index);
static NSS_STATUS uess_get_gecos(LDAP *ld, LDAPMessage *e, ldap_uess_args_t *arg, int index);
static NSS_STATUS uess_get_pwd(LDAP *ld, LDAPMessage *e, ldap_uess_args_t *arg, int index);

/* dispatch table for retrieving UESS attribute from an LDAP entry */
struct ldap_uess_fn {
  const char *luf_attribute;
  NSS_STATUS (*luf_translator)(LDAP *ld, LDAPMessage *e, ldap_uess_args_t *, int);
} ldap_uess_fn_t;

static struct ldap_uess_fn __uess_fns[] = {
  { S_GECOS, uess_get_gecos },
  { S_GROUPSIDS, uess_get_groupsids },
  { S_HOME, uess_get_char },
  { S_ID, uess_get_int },
  { S_PWD, uess_get_pwd },
  { S_REGISTRY, uess_get_registry },
  { S_SHELL, uess_get_char },
  { S_PGRP, uess_get_pgrp },
  { NULL, NULL }
};

static void *
_nss_ldap_open (const char *name, const char *domain,
		const int mode, char *options)
{
  /* Currently we do not use the above parameters */
  grp_conn = (struct irs_gr *) gr_pvtinit ();
  pwd_conn = (struct irs_pw *) pw_pvtinit ();

  return NULL;
}

static int
_nss_ldap_close (void *token)
{
  if (grp_conn != NULL)
    {
      (grp_conn->close) (grp_conn);
      grp_conn = NULL;
    }

  if (pwd_conn != NULL)
    {
      (pwd_conn->close) (pwd_conn);
      pwd_conn = NULL;
    }

  return AUTH_SUCCESS;
}

static struct group *
_nss_ldap_getgrgid (gid_t gid)
{
  if (grp_conn == NULL)
    return NULL;

  return (grp_conn->bygid) (grp_conn, gid);
}

static struct group *
_nss_ldap_getgrnam (const char *name)
{
  if (grp_conn == NULL)
    return NULL;

  return (grp_conn->byname)(grp_conn, name);
}

static struct passwd *
_nss_ldap_getpwuid (uid_t uid)
{
  if (pwd_conn == NULL)
    return NULL;

  return (pwd_conn->byuid) (pwd_conn, uid);
}

static struct passwd *
_nss_ldap_getpwnam (const char *name)
{
  if (pwd_conn == NULL)
    return NULL;

  return (pwd_conn->byname) (pwd_conn, name);
}

static struct group *
_nss_ldap_getgracct (void *id, int type)
{
  if (grp_conn == NULL)
    return NULL;

  if (type == SEC_INT)
    return (grp_conn->bygid) (grp_conn, *(gid_t *) id);
  else
    return (grp_conn->byname) (grp_conn, (char *) id);
}

static int
_nss_ldap_authenticate (char *user, char *response, int *reenter,
			char **message)
{
  NSS_STATUS stat;
  int rc;

  debug ("==> _nss_ldap_authenticate");

  *reenter = FALSE;
  *message = NULL;

  stat = _nss_ldap_proxy_bind (user, response);

  switch (stat)
    {
    case NSS_TRYAGAIN:
      rc = AUTH_FAILURE;
      break;
    case NSS_NOTFOUND:
      rc = AUTH_NOTFOUND;
      break;
    case NSS_SUCCESS:
      rc = AUTH_SUCCESS;
      break;
    default:
    case NSS_UNAVAIL:
      rc = AUTH_UNAVAIL;
      break;
    }

  debug ("<== _nss_ldap_authenticate");

  return rc;
}

/*
 * Support this for when proxy authentication is disabled.
 * There may be some re-entrancy issues here; not sure
 * if we are supposed to return allocated memory or not,
 * this is not documented. I am assuming not in line with
 * the other APIs.
 */
static char *
_nss_ldap_getpasswd (char *user)
{
  struct passwd *pw;
  static char pwdbuf[32];
  char *p = NULL;

  debug ("==> _nss_ldap_getpasswd");

  pw = _nss_ldap_getpwnam (user);
  if (pw != NULL)
    {
      if (strlen (pw->pw_passwd) > sizeof (pwdbuf) - 1)
	{
	  errno = ERANGE;
	}
      else
	{
	  strcpy (pwdbuf, pw->pw_passwd);
	  p = pwdbuf;
	}
    }
  else
    {
      errno = ENOENT; /* user does not exist */
    }

  debug ("<== _nss_ldap_getpasswd");

  return p;
}

/*
 * Convert a UESS table string to an nss_ldap map type
 */
static ldap_map_selector_t table2map(const char *table)
{
  if (strcmp(table, TABLE_USER) == 0)
    return LM_PASSWD;
  else if (strcmp(table, TABLE_GROUP) == 0)
    return LM_GROUP;

  return LM_NONE;
}

/*
 * Convert a UESS key to an nss_ldap internal search query
 */
static ldap_args_t *key2filter(char *key, ldap_map_selector_t map,
			       ldap_args_t *a, const char **filter)
{
  if (strcmp(key, TABLE_KEY_ALL) == 0)
    {
      if (map == LM_PASSWD)
	*filter = _nss_ldap_filt_getpwent;
      else
	*filter = _nss_ldap_filt_getgrent;

      return NULL; /* indicates enumeration */
    }

  LA_INIT (*a);
  LA_TYPE (*a) = LA_TYPE_STRING;
  LA_STRING (*a) = key;

  if (map == LM_PASSWD)
    *filter = _nss_ldap_filt_getpwnam;
  else
    *filter = _nss_ldap_filt_getgrnam;

  return a;
}

/*
 * Map a UESS attribute to an LDAP attribute
 */
static const char *uess2ldapattr(ldap_map_selector_t map, const char *attribute)
{
  if (strcmp(attribute, S_USERS) == 0)
    return ATM (passwd, uid);
  else if (strcmp(attribute, S_GROUPS) == 0)
    return ATM (group, cn);
  else if (strcmp(attribute, S_ID) == 0)
    {
      if (map == LM_PASSWD)
	return ATM (passwd, uidNumber);
      else
	return ATM (group, gidNumber);
    }
  else if (strcmp(attribute, S_PWD) == 0)
    return AT(userPassword);
  else if (strcmp(attribute, S_HOME) == 0)
    return ATM(passwd, homeDirectory);
  else if (strcmp(attribute, S_SHELL) == 0)
    return ATM(passwd, loginShell);
  else if (strcmp(attribute, S_GECOS) == 0)
    return ATM(passwd, gecos);
  else if (strcmp(attribute, S_PGRP) == 0)
    return ATM(group, cn);

  return NULL;
}

/*
 * Get primary group name for a user
 */
static NSS_STATUS uess_get_pgrp(LDAP *ld, LDAPMessage *e,
			       ldap_uess_args_t *lua, int i)
{
  char **vals;
  LDAPMessage *res;
  const char *attrs[2];
  NSS_STATUS stat;
  ldap_args_t a;

  vals = ldap_get_values (ld, e, ATM(passwd, gidNumber));
  if (vals == NULL)
    return NSS_NOTFOUND;

  LA_INIT (a);
  LA_TYPE (a) = LA_TYPE_STRING;
  LA_STRING (a) = vals[0];

  attrs[0] = ATM(group, cn);
  attrs[1] = NULL;

  stat = _nss_ldap_search_s (&a, _nss_ldap_filt_getgrgid, LM_GROUP,
			     attrs, 1, &res);
  if (stat != NSS_SUCCESS)
    {
      ldap_value_free (vals);
      return NSS_NOTFOUND;
    }

  ldap_value_free (vals);

  e = ldap_first_entry (ld, res);
  if (e == NULL)
    {
      ldap_msgfree (res);
      return NSS_NOTFOUND;
    }

  stat = uess_get_char(ld, e, lua, i);

  ldap_msgfree (res);

  return stat;
}

/*
 * Get groups to which a user belongs 
 */
static NSS_STATUS uess_get_groupsids(LDAP *ld, LDAPMessage *e,
				    ldap_uess_args_t *lua, int i)
{
  char *p, *q;
  size_t len;

  /* XXX deadlock? */
  p = _nss_ldap_getgrset ((char *)lua->lua_key);
  if (p == NULL)
    return NSS_NOTFOUND;

  len = strlen(p);
  q = malloc(len + 2);
  if (q == NULL)
    {
      errno = ENOMEM;
      return NSS_NOTFOUND;
    }

  memcpy(q, p, len + 1);
  q[len + 1] = '\0';

  free (p);
  p = NULL;

  for (p = q; *p != '\0'; p++)
    {
      if (*p == ',')
	*p++ = '\0';
    }

  lua->lua_results[i].attr_un.au_char = q;

  return NSS_SUCCESS;
}

/*
 * Get a mapped UESS string attribute
 */
static NSS_STATUS uess_get_char(LDAP *ld, LDAPMessage *e,
				ldap_uess_args_t *lua, int i)
{
  const char *attribute;

  attribute = uess2ldapattr(lua->lua_map, lua->lua_attributes[i]);
  if (attribute == NULL)
    return NSS_NOTFOUND;

  return uess_get_char_ex(ld, e, lua, i, attribute);
}

/*
 * Get a specific LDAP attribute
 */
static NSS_STATUS uess_get_char_ex(LDAP *ld, LDAPMessage *e,
			     ldap_uess_args_t *lua, int i,
			     const char *attribute)
{
  char **vals;
  attrval_t *av = &lua->lua_results[i];

  vals = ldap_get_values(ld, e, attribute);
  if (vals == NULL)
    return NSS_NOTFOUND;

  if (vals[0] == NULL)
    {
      ldap_value_free (vals);
      return NSS_NOTFOUND;
    }

  av->attr_un.au_char = strdup (vals[0]);
  if (av->attr_un.au_char == NULL)
    {
      ldap_value_free (vals);
      return NSS_TRYAGAIN;
    }

  ldap_value_free (vals);
  return NSS_SUCCESS;
}

/*
 * Get an encoded crypt password
 */
static NSS_STATUS uess_get_pwd(LDAP *ld, LDAPMessage *e,
			       ldap_uess_args_t *lua, int i)
{
  char **vals;
  attrval_t *av = &lua->lua_results[i];
  const char *pwd;
  const char *attribute;

  attribute = uess2ldapattr(lua->lua_map, lua->lua_attributes[i]);
  if (attribute == NULL)
    return NSS_NOTFOUND;

  vals = ldap_get_values(ld, e, attribute);
  pwd = _nss_ldap_locate_userpassword (vals);

  av->attr_un.au_char = strdup (pwd);
  if (vals != NULL)
    ldap_value_free (vals);

  return (av->attr_un.au_char == NULL) ? NSS_TRYAGAIN : NSS_SUCCESS;
}

/*
 * Get a UESS integer attribute
 */
static NSS_STATUS uess_get_int(LDAP *ld, LDAPMessage *e,
			     ldap_uess_args_t *lua, int i)
{
  const char *attribute;
  char **vals;
  attrval_t *av = &lua->lua_results[i];

  attribute = uess2ldapattr(lua->lua_map, lua->lua_attributes[i]);
  if (attribute == NULL)
    return NSS_NOTFOUND;

  vals = ldap_get_values(ld, e, attribute);
  if (vals == NULL)
    return NSS_NOTFOUND;

  if (vals[0] == NULL)
    {
      ldap_value_free (vals);
      return NSS_NOTFOUND;
    }

  av->attr_un.au_int = atoi(vals[0]);
  ldap_value_free (vals);
  return NSS_SUCCESS;
}

/*
 * Get the name of this registry
 */
static NSS_STATUS uess_get_registry(LDAP *ld, LDAPMessage *e,
				    ldap_uess_args_t *lua, int i)
{
  lua->lua_results[i].attr_un.au_char = strdup("NSS_LDAP");
  if (lua->lua_results[i].attr_un.au_char == NULL)
    return NSS_TRYAGAIN;

  return NSS_SUCCESS;
}

/*
 * Get the GECOS/cn attribute
 */
static NSS_STATUS uess_get_gecos(LDAP *ld, LDAPMessage *e,
				 ldap_uess_args_t *lua, int i)
{
  NSS_STATUS stat;

  stat = uess_get_char(ld, e, lua, i);
  if (stat == NSS_NOTFOUND)
    {
      stat = uess_get_char_ex(ld, e, lua, i, ATM(passwd, cn));
    }

  return stat; 
}

static NSS_STATUS
do_parse_uess_getentry (LDAP *ld, LDAPMessage * e,
			ldap_state_t * pvt, void *result,
			char *buffer, size_t buflen)
{
  ldap_uess_args_t *lua = (ldap_uess_args_t *)pvt;
  int i;
  char **vals;
  size_t len;
  const char *attribute;
  NSS_STATUS stat;

  /* If a buffer is supplied, then we are enumerating. */
  if (lua->lua__buffer != NULL)
    {
      attrval_t *av = lua->lua_results;

      attribute = uess2ldapattr(lua->lua_map, lua->lua_attributes[0]);

      if (av->attr_flag != 0 || attribute == NULL)
	return NSS_NOTFOUND;

      vals = ldap_get_values (ld, e, attribute);
      if (vals == NULL)
	return NSS_NOTFOUND;	  

      if (vals[0] == NULL)
	{
	  ldap_value_free (vals);
	  return NSS_NOTFOUND;
	}

      len = strlen (vals[0]) + 1; /* for string terminator */

      if (lua->lua__buflen < len + 1) /* for list terminator */
	{
	  size_t grow = len + 1;
	  size_t offset = (lua->lua__buffer - av->attr_un.au_char);

	  grow += NSS_BUFSIZ - 1;
	  grow -= (grow % NSS_BUFSIZ);

	  av->attr_un.au_char = realloc(lua->lua__buffer, lua->lua__bufsiz + grow);
	  if (av->attr_un.au_char == NULL)
	    {
	      ldap_value_free (vals);
	      return NSS_TRYAGAIN;
	    }
	  /* reset buffer pointer in case realloc() returned a new region */
	  lua->lua__buffer = &av->attr_un.au_char[offset];
	  lua->lua__buflen += grow;
	  lua->lua__bufsiz += grow;
	}

      memcpy (lua->lua__buffer, vals[0], len);
      lua->lua__buflen -= len;
      lua->lua__buffer += len;
      ldap_value_free (vals);

      lua->lua__buffer[0] = '\0'; /* ensure _list_ is always terminated */
    }
  else
    {
      for (i = 0; i < lua->lua_size; i++)
	{
	  int j;
	  attrval_t *av = &lua->lua_results[i];

	  av->attr_flag = -1;
	  av->attr_un.au_char = NULL;

	  for (j = 0; __uess_fns[i].luf_attribute != NULL; j++)
	    {
	      if (strcmp(__uess_fns[i].luf_attribute, lua->lua_attributes[i]) == 0)
		{
		  stat = (__uess_fns[i].luf_translator)(ld, e, lua, i);
		  switch (stat)
		    {
		      case NSS_SUCCESS:
			av->attr_flag = 0;
			break;
		      case NSS_TRYAGAIN:
			return NSS_TRYAGAIN;
			break;
		      default:
			break;
		    }
		}
	    }
	}
    }

  return NSS_SUCCESS;
}

static int
_nss_ldap_getentry (char *key, char *table, char *attributes[],
		    attrval_t results[], int size)
{
  NSS_STATUS stat;
  ent_context_t *ctx = NULL;
  ldap_args_t a, *ap;
  const char *filter;
  int erange = 0;
  ldap_uess_args_t lua;

  debug ("==> _nss_ldap_getentry");

  lua.lua_key = key;
  lua.lua_table = table;
  lua.lua_attributes = attributes;
  lua.lua_results = results;
  lua.lua_size = size;

  lua.lua_map = table2map(table);
  if (lua.lua_map == LM_NONE)
    {
      errno = EINVAL;
      debug ("<== _nss_ldap_getentry");
      return -1;
    }

  lua.lua__buffer = NULL;
  lua.lua__bufsiz = 0;
  lua.lua__buflen = 0;

  ap = key2filter(key, lua.lua_map, &a, &filter);
  if (ap == NULL) /* enumeration */
    {
      if (size != 1)
	{
	  errno = EINVAL;
	  debug ("<== _nss_ldap_getentry");
	  return -1;
	}

      lua.lua__bufsiz = NSS_BUFSIZ;
      lua.lua__buflen = lua.lua__bufsiz;
      lua.lua__buffer = results[0].attr_un.au_char = malloc(lua.lua__bufsiz);
      if (lua.lua__buffer == NULL)
	{
	  errno = ENOMEM;
	  debug ("<== _nss_ldap_getentry");
	  return -1;
	}
      results[0].attr_flag = 0;
    }

  _nss_ldap_enter ();
  if (_nss_ldap_ent_context_init (&ctx) == NULL)
    {
      _nss_ldap_leave ();
      if (results[0].attr_un.au_char != NULL)
	free (results[0].attr_un.au_char);
      errno = ENOMEM;
      debug ("<== _nss_ldap_getentry");
      return -1;
    }

  stat = _nss_ldap_getent_ex (ap, &ctx, (void *)&lua, NULL, 0,
			      &erange, filter, lua.lua_map,
			      NULL, do_parse_uess_getentry);
  _nss_ldap_ent_context_release (ctx);
  free (ctx);
  _nss_ldap_leave ();

  if (stat != NSS_SUCCESS)
    {
      if (erange)
	errno = ERANGE;
      else
	errno = ENOENT;

      debug ("<== _nss_ldap_getentry");
      return -1;
    }

  debug ("<== _nss_ldap_getentry");
  return AUTH_SUCCESS;
}

#if 0
/* not implemented yet */
static int
_nss_ldap_getgrusers (char *group, void *result, int type, int *size)
{
  struct group *gr;
  struct irs_gr *be;

  be = (struct irs_gr *) gr_pvtinit ();
  if (be == NULL)
    {
      errno = ENOSYS;
      return -1;
    }

  gr = (be->byname)(be, group);
  if (gr == NULL)
    {
      errno = ENOENT;
      (be->close) (be);
      return -1;
    }

  (be->close) (be);
  return AUTH_SUCCESS;
}

static int
_nss_ldap_normalize (char *longname, char *shortname)
{
}
#endif

int
nss_ldap_initialize (struct secmethod_table *meths)
{
  memset (meths, 0, sizeof (*meths));

  /* Identification methods */
  meths->method_getpwnam = _nss_ldap_getpwnam;
  meths->method_getpwuid = _nss_ldap_getpwuid;
  meths->method_getgrnam = _nss_ldap_getgrnam;
  meths->method_getgrgid = _nss_ldap_getgrgid;
  meths->method_getgrset = _nss_ldap_getgrset;
  meths->method_getentry = _nss_ldap_getentry;
#if 0
  meths->method_getgrusers = _nss_ldap_getgrusers;
  meths->method_normalize = _nss_ldap_normalize;
#endif

  /*
   * These casts are necessary because the prototypes 
   * in the AIX headers are wrong.
   */
  meths->method_getgracct = (int (*)(void *, int))_nss_ldap_getgracct;
  meths->method_getpasswd = (int (*)(char *))_nss_ldap_getpasswd;

  /* Support methods */
  meths->method_open = _nss_ldap_open;
  meths->method_close = _nss_ldap_close;

  /* Authentication methods */
  meths->method_authenticate = _nss_ldap_authenticate;

  return AUTH_SUCCESS;
}

#endif /* HAVE_USERSEC_H */

