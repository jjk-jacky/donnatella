/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * donnatella.c
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

#include <locale.h>
#include <gtk/gtk.h>
#include "app.h"

int
main (int argc, char *argv[])
{
    setlocale (LC_ALL, "");
    gtk_init (&argc, &argv);
    return donna_app_run (g_object_new (DONNA_TYPE_APP, NULL), argc, argv);
}
