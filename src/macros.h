/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * macros.h
 * Copyright (C) 2014 Olivier Brunel <jjk@jjacky.com>
 *
 * This file is part of donnatella.
 *
 * donnatella is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * donnatella is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * donnatella. If not, see http://www.gnu.org/licenses/
 */

#ifndef __DONNA_MACROS_H__
#define __DONNA_MACROS_H__

G_BEGIN_DECLS

#include <glib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>      /* isblank() */

/* somehow this one is missing in GLib */
#ifndef g_info
#define g_info(...)     g_log (G_LOG_DOMAIN, G_LOG_LEVEL_INFO, __VA_ARGS__)
#endif

/* custom/user log levels, for extra debug verbosity */
#define DONNA_LOG_LEVEL_DEBUG2      (1 << 8)
#define DONNA_LOG_LEVEL_DEBUG3      (1 << 9)
#define DONNA_LOG_LEVEL_DEBUG4      (1 << 10)

#define g_debug2(...)   g_log (G_LOG_DOMAIN, DONNA_LOG_LEVEL_DEBUG2, __VA_ARGS__)
#define g_debug3(...)   g_log (G_LOG_DOMAIN, DONNA_LOG_LEVEL_DEBUG3, __VA_ARGS__)
#define g_debug4(...)   g_log (G_LOG_DOMAIN, DONNA_LOG_LEVEL_DEBUG4, __VA_ARGS__)

#define DONNA_LOG_DOMAIN    "Donnatella"
#define donna_error(...)    g_log (DONNA_LOG_DOMAIN, G_LOG_LEVEL_WARNING, __VA_ARGS__)

#define streq(s1, s2)           (((s1) == NULL && (s2) == NULL) ? 1 \
        : ((s1) == NULL || (s2) == NULL) ? 0 : strcmp  ((s1), (s2)) == 0)
#define streqn(s1, s2, n)       (((s1) == NULL || (s2) == NULL) ? 0 \
        : strncmp ((s1), (s2), (n)) == 0)
#define strcaseeq(s1, s2)       (((s1) == NULL && (s2) == NULL) ? 1 \
        : ((s1) == NULL || (s2) == NULL) ? 0 : strcasecmp  ((s1), (s2)) == 0)
#define strcaseeqn(s1, s2, n)       (((s1) == NULL || (s2) == NULL) ? 0 \
        : strncasecmp ((s1), (s2), (n)) == 0)

#define skip_blank(s)   for ( ; isblank (*(s)); ++(s)) ;

G_END_DECLS

#endif /* __DONNA_MACROS_H__ */
