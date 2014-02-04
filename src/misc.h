/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * misc.h
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

#ifndef __DONNA_MISC_H__
#define __DONNA_MISC_H__

#include "common.h"

G_BEGIN_DECLS

gboolean        _key_press_ctrl_a_cb                (GtkEntry       *entry,
                                                     GdkEventKey    *event);
gchar *         _resolve_path                       (DonnaNode      *node,
                                                     const gchar    *path);

G_END_DECLS

#endif /* __DONNA_MISC_H__ */
