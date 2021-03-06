/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * statusprovider.c
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

#include "statusprovider.h"

enum
{
    STATUS_CHANGED,
    NB_SIGNALS
};

static guint donna_status_provider_signals[NB_SIGNALS] = { 0 };

static gboolean
default_set_tooltip (DonnaStatusProvider    *sp,
                     guint                   id,
                     guint                   index,
                     GtkTooltip             *tooltip)
{
    return FALSE;
}

G_DEFINE_INTERFACE (DonnaStatusProvider, donna_status_provider, G_TYPE_OBJECT)

static void
donna_status_provider_default_init (DonnaStatusProviderInterface *interface)
{
    interface->set_tooltip = default_set_tooltip;

    donna_status_provider_signals[STATUS_CHANGED] =
        g_signal_new ("status-changed",
                DONNA_TYPE_STATUS_PROVIDER,
                G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
                G_STRUCT_OFFSET (DonnaStatusProviderInterface, status_changed),
                NULL,
                NULL,
                g_cclosure_marshal_VOID__UINT,
                G_TYPE_NONE,
                1,
                G_TYPE_UINT);
}

/* signals */

void
donna_status_provider_status_changed (DonnaStatusProvider *sp,
                                      guint                id)
{
    GQuark detail;
    gchar buf[8];

    g_return_if_fail (DONNA_IS_STATUS_PROVIDER (sp));
    g_return_if_fail (id > 0);

    snprintf (buf, 8, "%d", id);
    detail = g_quark_from_string (buf);
    g_signal_emit (sp, donna_status_provider_signals[STATUS_CHANGED], detail, id);
}


/* API */

guint
donna_status_provider_create_status (DonnaStatusProvider    *sp,
                                     gpointer                config,
                                     GError                **error)
{
    DonnaStatusProviderInterface *interface;

    g_return_val_if_fail (DONNA_IS_STATUS_PROVIDER (sp), NULL);

    interface = DONNA_STATUS_PROVIDER_GET_INTERFACE (sp);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->create_status != NULL, NULL);

    return (*interface->create_status) (sp, config, error);
}

void
donna_status_provider_free_status (DonnaStatusProvider    *sp,
                                   guint                   id)
{
    DonnaStatusProviderInterface *interface;

    g_return_val_if_fail (DONNA_IS_STATUS_PROVIDER (sp), NULL);
    g_return_val_if_fail (id > 0, NULL);

    interface = DONNA_STATUS_PROVIDER_GET_INTERFACE (sp);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->free_status != NULL, NULL);

    return (*interface->free_status) (sp, id);
}

const gchar *
donna_status_provider_get_renderers (DonnaStatusProvider    *sp,
                                     guint                   id)
{
    DonnaStatusProviderInterface *interface;

    g_return_val_if_fail (DONNA_IS_STATUS_PROVIDER (sp), NULL);
    g_return_val_if_fail (id > 0, NULL);

    interface = DONNA_STATUS_PROVIDER_GET_INTERFACE (sp);

    g_return_val_if_fail (interface != NULL, NULL);
    g_return_val_if_fail (interface->get_renderers != NULL, NULL);

    return (*interface->get_renderers) (sp, id);
}

void
donna_status_provider_render (DonnaStatusProvider    *sp,
                              guint                   id,
                              guint                   index,
                              GtkCellRenderer        *renderer)
{
    DonnaStatusProviderInterface *interface;

    g_return_if_fail (DONNA_IS_STATUS_PROVIDER (sp));
    g_return_val_if_fail (id > 0, NULL);
    g_return_if_fail (renderer != NULL);

    interface = DONNA_STATUS_PROVIDER_GET_INTERFACE (sp);

    g_return_if_fail (interface != NULL);
    g_return_if_fail (interface->render != NULL);

    return (*interface->render) (sp, id, index, renderer);
}

gboolean
donna_status_provider_set_tooltip (DonnaStatusProvider    *sp,
                                   guint                   id,
                                   guint                   index,
                                   GtkTooltip             *tooltip)
{
    DonnaStatusProviderInterface *interface;

    g_return_val_if_fail (DONNA_IS_STATUS_PROVIDER (sp), FALSE);
    g_return_val_if_fail (id > 0, NULL);
    g_return_val_if_fail (GTK_IS_TOOLTIP (tooltip), FALSE);

    interface = DONNA_STATUS_PROVIDER_GET_INTERFACE (sp);

    g_return_val_if_fail (interface != NULL, FALSE);
    g_return_val_if_fail (interface->set_tooltip != NULL, FALSE);

    return (*interface->set_tooltip) (sp, id, index, tooltip);
}
