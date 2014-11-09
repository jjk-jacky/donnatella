/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * ioengine-exec.c
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
#include "app.h"
#include "provider.h"
#include "provider-fs.h"
#include "util.h"
#include "macros.h"
#include "debug.h"

DonnaTask *
donna_io_engine_exec_io_task (DonnaProviderFs    *pfs,
                              DonnaApp           *app,
                              DonnaIoType         type,
                              GPtrArray          *sources,
                              DonnaNode          *dest,
                              const gchar        *new_name,
                              fs_parse_cmdline    parser,
                              fs_file_created     file_created,
                              fs_file_deleted     file_deleted,
                              GError            **error);


DonnaTask *
donna_io_engine_exec_io_task (DonnaProviderFs    *pfs,
                              DonnaApp           *app,
                              DonnaIoType         type,
                              GPtrArray          *sources,
                              DonnaNode          *dest,
                              const gchar        *new_name,
                              fs_parse_cmdline    parser,
                              fs_file_created     file_created,
                              fs_file_deleted     file_deleted,
                              GError            **error)
{
    DonnaConfig *config = donna_app_peek_config (app);
    DonnaProvider *provider;
    DonnaTask *task;
    DonnaNode *node;
    const gchar *operation;
    gchar *cmdline;
    gchar *s;

    switch (type)
    {
        case DONNA_IO_COPY:
            operation = "copy";
            break;

        case DONNA_IO_MOVE:
            operation = "move";
            break;

        case DONNA_IO_DELETE:
        case DONNA_IO_UNKNOWN: /* silence warning */
            operation = "delete";
            break;
    }

    if (!donna_config_get_string (config, error, &s,
                "providers/fs/ioengine-exec/%s_cmdline%s",
                operation, (new_name) ? "_new_name" : ""))
    {
        g_prefix_error (error, "IO Engine '%s': Failed to get command line: ",
                "exec");
        return NULL;
    }

    cmdline = parser (s, sources, dest, new_name, app, error);
    g_free (s);
    if (G_UNLIKELY (!cmdline))
    {
        g_prefix_error (error, "IO Engine '%s': Failed to parse command line: ",
                "exec");
        return NULL;
    }

    provider = donna_app_get_provider (app, "exec");
    node = donna_provider_get_node (provider, cmdline, error);
    if (!node)
    {
        g_prefix_error (error, "IO Engine '%s': Failed to get node '%s:%s': ",
                "exec", "exec", cmdline);
        g_free (cmdline);
        g_object_unref (provider);
        return NULL;
    }
    g_free (cmdline);

    task = donna_provider_trigger_node_task (provider, node, error);
    g_object_unref (node);
    g_object_unref (provider);
    if (!task)
    {
        g_prefix_error (error, "IO Engine '%s': Failed to get trigger task: ",
                "exec");
        return NULL;
    }

    g_object_set_data ((GObject *) task, "donna-ioengine-exec", GUINT_TO_POINTER (1));
    return task;
}
