/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * command.h
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

#ifndef __DONNA_COMMAND_H__
#define __DONNA_COMMAND_H__

#include <gtk/gtk.h>
#include "provider-command.h"
#include "app.h"
#include "node.h"
#include "task.h"

G_BEGIN_DECLS

#define _get_choice(choices, sel)   \
    _donna_get_choice_from_list (sizeof (choices) / sizeof (choices[0]), choices, sel)
#define _get_flags(choices, flags, sel) \
    _donna_get_flags_from_list (sizeof (choices) / sizeof (choices[0]), choices, flags, sel)

struct command
{
    gchar               *name;
    guint                argc;
    DonnaArgType        *arg_type;
    DonnaArgType         return_type;
    DonnaTaskVisibility  visibility;
    command_fn           func;
    gpointer             data;
    GDestroyNotify       destroy;
};

/* used from app.c to get return_type of command triggered via socket */
struct command *    _donna_command_init_parse       (DonnaProviderCommand    *pc,
                                                     gchar                   *cmdline,
                                                     gchar                  **first_arg,
                                                     GError                 **error);


gint                _donna_get_choice_from_list     (guint           nb,
                                                     const gchar    *choices[],
                                                     const gchar    *sel);
guint               _donna_get_flags_from_list      (guint           nb,
                                                     const gchar    *choices[],
                                                     guint           flags[],
                                                     gchar          *sel);

G_END_DECLS

#endif /* __DONNA_COMMAND_H__ */
