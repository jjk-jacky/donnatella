/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * taskui.c
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

#include "config.h"

#include <glib-object.h>
#include "taskui.h"

/* Note: always unref taskui from main thread UI only */

G_DEFINE_INTERFACE (DonnaTaskUi, donna_taskui, G_TYPE_OBJECT)

static void
donna_taskui_default_init (DonnaTaskUiInterface *klass)
{
}

void
donna_taskui_set_title (DonnaTaskUi     *taskui,
                        const gchar     *title)
{
    gchar *dup;

    g_return_if_fail (DONNA_IS_TASKUI (taskui));

    dup = g_strdup (title);
    donna_taskui_take_title (taskui, dup);
}

void
donna_taskui_take_title (DonnaTaskUi    *taskui,
                         gchar          *title)
{
    DonnaTaskUiInterface *interface;

    g_return_if_fail (DONNA_IS_TASKUI (taskui));

    interface = DONNA_TASKUI_GET_INTERFACE (taskui);

    g_return_if_fail (interface != NULL);
    g_return_if_fail (interface->take_title != NULL);

    return (*interface->take_title) (taskui, title);
}

void
donna_taskui_show (DonnaTaskUi *taskui)
{
    DonnaTaskUiInterface *interface;

    g_return_if_fail (DONNA_IS_TASKUI (taskui));

    interface = DONNA_TASKUI_GET_INTERFACE (taskui);

    g_return_if_fail (interface != NULL);
    g_return_if_fail (interface->show != NULL);

    return (*interface->show) (taskui);
}
