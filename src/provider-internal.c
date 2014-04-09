/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * provider-internal.c
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

#include <gtk/gtk.h>
#include "provider-internal.h"
#include "provider.h"
#include "node.h"
#include "app.h"
#include "macros.h"
#include "debug.h"


struct _DonnaProviderInternalPrivate
{
    guint last;
};


/* DonnaProvider */
static const gchar *    provider_internal_get_domain    (DonnaProvider      *provider);
static DonnaProviderFlags provider_internal_get_flags   (DonnaProvider      *provider);
static DonnaTask *      provider_internal_has_node_children_task (
                                                         DonnaProvider       *provider,
                                                         DonnaNode           *node,
                                                         DonnaNodeType        node_types,
                                                         GError             **error);
static DonnaTask *      provider_internal_get_node_children_task (
                                                         DonnaProvider       *provider,
                                                         DonnaNode           *node,
                                                         DonnaNodeType        node_types,
                                                         GError             **error);
static DonnaTask *      provider_internal_trigger_node_task (
                                                         DonnaProvider       *provider,
                                                         DonnaNode           *node,
                                                         GError             **error);
/* DonnaProviderBase */
static DonnaTaskState   provider_internal_new_node      (DonnaProviderBase  *provider,
                                                         DonnaTask          *task,
                                                         const gchar        *location);
static void             provider_internal_unref_node    (DonnaProviderBase  *provider,
                                                         DonnaNode          *node);

static void
provider_internal_provider_init (DonnaProviderInterface *interface)
{
    interface->get_domain               = provider_internal_get_domain;
    interface->get_flags                = provider_internal_get_flags;
    interface->has_node_children_task   = provider_internal_has_node_children_task;
    interface->get_node_children_task   = provider_internal_get_node_children_task;
    interface->trigger_node_task        = provider_internal_trigger_node_task;
}

G_DEFINE_TYPE_WITH_CODE (DonnaProviderInternal, donna_provider_internal,
        DONNA_TYPE_PROVIDER_BASE,
        G_IMPLEMENT_INTERFACE (DONNA_TYPE_PROVIDER, provider_internal_provider_init)
        )

static void
donna_provider_internal_class_init (DonnaProviderInternalClass *klass)
{
    DonnaProviderBaseClass *pb_class;

    pb_class = (DonnaProviderBaseClass *) klass;

    pb_class->task_visibility.new_node      = DONNA_TASK_VISIBILITY_INTERNAL_FAST;

    pb_class->new_node      = provider_internal_new_node;
    pb_class->unref_node    = provider_internal_unref_node;

    g_type_class_add_private (klass, sizeof (DonnaProviderInternalPrivate));
}

static void
donna_provider_internal_init (DonnaProviderInternal *provider)
{
    provider->priv = G_TYPE_INSTANCE_GET_PRIVATE (provider,
            DONNA_TYPE_PROVIDER_INTERNAL,
            DonnaProviderInternalPrivate);
}


/* DonnaProvider */

static const gchar *
provider_internal_get_domain (DonnaProvider      *provider)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_INTERNAL (provider), NULL);
    return "internal";
}

static DonnaProviderFlags
provider_internal_get_flags (DonnaProvider      *provider)
{
    g_return_val_if_fail (DONNA_IS_PROVIDER_INTERNAL (provider),
            DONNA_PROVIDER_FLAG_INVALID);
    return DONNA_PROVIDER_FLAG_FLAT;
}


/* DonnaProviderBase */

static DonnaTaskState
provider_internal_new_node (DonnaProviderBase  *provider,
                            DonnaTask          *task,
                            const gchar        *location)
{
    donna_task_set_error (task, DONNA_PROVIDER_ERROR,
            DONNA_PROVIDER_ERROR_LOCATION_NOT_FOUND,
            "Provider 'internal': Location '%s' doesn't exist", location);
    return DONNA_TASK_FAILED;
}

static void
provider_internal_unref_node (DonnaProviderBase  *_provider,
                              DonnaNode          *node)
{
    GValue v_destroy = G_VALUE_INIT;
    GValue v_data = G_VALUE_INIT;
    DonnaNodeHasValue has;
    GDestroyNotify destroy;

    donna_node_get (node, FALSE,
            "_internal_data",    &has, &v_data,
            "_internal_destroy", &has, &v_destroy,
            NULL);

    destroy = g_value_get_pointer (&v_destroy);
    if (destroy)
        destroy (g_value_get_pointer (&v_data));

    g_value_unset (&v_data);
    g_value_unset (&v_destroy);
}

static DonnaTaskState
trigger_node (DonnaTask *task, DonnaNode *node)
{
    GValue v_worker = G_VALUE_INIT;
    GValue v_data = G_VALUE_INIT;
    DonnaNodeHasValue has;
    internal_worker_fn worker;
    DonnaTaskState ret;

    donna_node_get (node, FALSE,
            "_internal_worker",  &has, &v_worker,
            "_internal_data",    &has, &v_data,
            NULL);

    worker = g_value_get_pointer (&v_worker);
    g_value_unset (&v_worker);
    /* no worker means node was already triggered/it already ran */
    if (G_UNLIKELY (!worker))
    {
        gchar *location = donna_node_get_location (node);
        donna_task_set_error (task, DONNA_PROVIDER_ERROR,
                DONNA_PROVIDER_ERROR_INVALID_CALL,
                "Provider 'internal': Node '%s' has already been triggered",
                location);
        g_free (location);
        g_value_unset (&v_data);
        return DONNA_TASK_FAILED;
    }

    ret = worker (task, node, g_value_get_pointer (&v_data));

    /* mark things as done */
    g_value_set_pointer (&v_data, NULL);
    donna_node_set_property_value (node, "_internal_worker", &v_data);
    donna_node_set_property_value (node, "_internal_destroy", &v_data);
    g_value_unset (&v_data);

    return ret;
}

static DonnaTask *
provider_internal_trigger_node_task (DonnaProvider       *provider,
                                     DonnaNode           *node,
                                     GError             **error)
{
    DonnaTask *task;
    DonnaNodeHasValue has;
    GValue v = G_VALUE_INIT;

    task = donna_task_new ((task_fn) trigger_node, g_object_ref (node),
            g_object_unref);
    donna_node_get (node, FALSE, "_internal_visibility", &has, &v, NULL);
    donna_task_set_visibility (task, g_value_get_uint (&v));
    g_value_unset (&v);

    DONNA_DEBUG (TASK, NULL,
            gchar *fl = donna_node_get_full_location (node);
            donna_task_take_desc (task, g_strdup_printf (
                    "trigger_node() for node '%s'", fl));
            g_free (fl));

    return task;
}

struct children
{
    DonnaNode *node;
    DonnaNodeType node_types;
    gboolean get_children;
};

static void
free_children (struct children *c)
{
    g_object_unref (c->node);
    g_free (c);
}

static DonnaTaskState
has_get_children (DonnaTask *task, struct children *c)
{
    GValue v_worker = G_VALUE_INIT;
    GValue v_data = G_VALUE_INIT;
    DonnaNodeHasValue has;
    internal_children_fn worker;
    DonnaTaskState ret;

    donna_node_get (c->node, FALSE,
            "_internal_worker",  &has, &v_worker,
            "_internal_data",    &has, &v_data,
            NULL);

    worker = g_value_get_pointer (&v_worker);
    g_value_unset (&v_worker);

    ret = worker (task, c->node, c->node_types, c->get_children,
            g_value_get_pointer (&v_data));

    g_value_unset (&v_data);
    free_children (c);
    return ret;
}

static DonnaTask *
has_get_children_task (DonnaProvider    *provider,
                       DonnaNode        *node,
                       DonnaNodeType     node_types,
                       gboolean          get_children,
                       GError          **error)
{
    DonnaTask *task;
    DonnaNodeHasValue has;
    GValue v = G_VALUE_INIT;
    struct children *c;

    c = g_new (struct children, 1);
    c->node = g_object_ref (node);
    c->node_types = node_types;
    c->get_children = get_children;

    task = donna_task_new ((task_fn) has_get_children, c,
            (GDestroyNotify) free_children);
    donna_node_get (node, FALSE, "_internal_visibility", &has, &v, NULL);
    donna_task_set_visibility (task, g_value_get_uint (&v));
    g_value_unset (&v);

    DONNA_DEBUG (TASK, NULL,
            gchar *fl = donna_node_get_full_location (node);
            donna_task_take_desc (task, g_strdup_printf (
                    "%s_children() for node '%s'",
                    (get_children) ? "get" : "has", fl));
            g_free (fl));

    return task;
}

static DonnaTask *
provider_internal_has_node_children_task (DonnaProvider       *provider,
                                          DonnaNode           *node,
                                          DonnaNodeType        node_types,
                                          GError             **error)
{
    return has_get_children_task (provider, node, node_types, FALSE, error);
}

static DonnaTask *
provider_internal_get_node_children_task (DonnaProvider       *provider,
                                          DonnaNode           *node,
                                          DonnaNodeType        node_types,
                                          GError             **error)

{
    return has_get_children_task (provider, node, node_types, TRUE, error);
}

DonnaNode *
donna_provider_internal_new_node (DonnaProviderInternal  *pi,
                                  const gchar            *name,
                                  gboolean                icon_is_gicon,
                                  gconstpointer           _icon,
                                  const gchar            *desc,
                                  DonnaNodeType           node_type,
                                  gboolean                sensitive,
                                  DonnaTaskVisibility     visibility,
                                  internal_fn             fn,
                                  gpointer                data,
                                  GDestroyNotify          destroy,
                                  GError                **error)
{
    DonnaProviderBaseClass *klass;
    DonnaProviderBase *pb;
    DonnaNodeFlags flags = 0;
    DonnaNode *node;
    GIcon *icon = NULL;
    gchar location[64];
    GValue v = G_VALUE_INIT;

    g_return_val_if_fail (DONNA_IS_PROVIDER_INTERNAL (pi), NULL);
    g_return_val_if_fail (fn != NULL, NULL);
    pb = (DonnaProviderBase *) pi;
    klass = DONNA_PROVIDER_BASE_GET_CLASS (pb);

    if (_icon)
    {
        if (!icon_is_gicon)
            icon = g_themed_icon_new ((const gchar *) _icon);
        else
            icon = (GIcon *) _icon;
        flags |= DONNA_NODE_ICON_EXISTS;
    }
    if (desc)
        flags |= DONNA_NODE_DESC_EXISTS;
    snprintf (location, 64, "%u", (guint) g_atomic_int_add (&pi->priv->last, 1) + 1);

    node = donna_node_new ((DonnaProvider *) pi, location, node_type,
            NULL, (refresher_fn) gtk_true, NULL, name, flags);
    if (G_UNLIKELY (!node))
    {
        g_set_error (error, DONNA_PROVIDER_ERROR, DONNA_PROVIDER_ERROR_OTHER,
                "Provider 'internal': Unable to create a new node");
        return NULL;
    }

    if (icon)
    {
        g_value_init (&v, G_TYPE_ICON);
        if (icon_is_gicon)
            g_value_set_object (&v, icon);
        else
            g_value_take_object (&v, icon);
        donna_node_set_property_value (node, "icon", &v);
        g_value_unset (&v);
    }

    if (desc)
    {
        g_value_init (&v, G_TYPE_STRING);
        g_value_set_string (&v, desc);
        donna_node_set_property_value (node, "desc", &v);
        g_value_unset (&v);
    }

    g_value_init (&v, G_TYPE_POINTER);
    g_value_set_pointer (&v, fn);
    if (G_UNLIKELY (!donna_node_add_property (node, "_internal_worker",
                    G_TYPE_POINTER, &v, (refresher_fn) gtk_true, NULL, NULL, NULL, error)))
    {
        g_prefix_error (error, "Provider 'internal': Cannot create new node, "
                "failed to add property '_internal_worker': ");
        g_value_unset (&v);
        g_object_unref (node);
        return NULL;
    }

    g_value_set_pointer (&v, data);
    if (G_UNLIKELY (!donna_node_add_property (node, "_internal_data",
                    G_TYPE_POINTER, &v, (refresher_fn) gtk_true, NULL, NULL, NULL, error)))
    {
        g_prefix_error (error, "Provider 'internal': Cannot create new node, "
                "failed to add property '_internal_data': ");
        g_value_unset (&v);
        g_object_unref (node);
        return NULL;
    }

    g_value_set_pointer (&v, destroy);
    if (G_UNLIKELY (!donna_node_add_property (node, "_internal_destroy",
                    G_TYPE_POINTER, &v, (refresher_fn) gtk_true, NULL, NULL, NULL, error)))
    {
        g_prefix_error (error, "Provider 'internal': Cannot create new node, "
                "failed to add property '_internal_destroy': ");
        g_value_unset (&v);
        g_object_unref (node);
        return NULL;
    }
    g_value_unset (&v);

    g_value_init (&v, G_TYPE_UINT);
    g_value_set_uint (&v, visibility);
    if (G_UNLIKELY (!donna_node_add_property (node, "_internal_visibility",
                    G_TYPE_UINT, &v, (refresher_fn) gtk_true, NULL, NULL, NULL, error)))
    {
        g_prefix_error (error, "Provider 'internal': Cannot create new node, "
                "failed to add property '_internal_visibility': ");
        g_value_unset (&v);
        g_object_unref (node);
        return NULL;
    }
    g_value_unset (&v);

    if (!sensitive)
    {
        g_value_init (&v, G_TYPE_BOOLEAN);
        g_value_set_boolean (&v, FALSE);
        if (G_UNLIKELY (!donna_node_add_property (node, "menu-is-sensitive",
                        G_TYPE_BOOLEAN, &v, (refresher_fn) gtk_true, NULL, NULL, NULL, error)))
        {
            g_prefix_error (error, "Provider 'internal': Cannot create new node, "
                    "failed to add property 'menu-is-sensitive': ");
            g_value_unset (&v);
            g_object_unref (node);
            return NULL;
        }
        g_value_unset (&v);
    }

    klass->lock_nodes (pb);
    klass->add_node_to_cache (pb, node);
    klass->unlock_nodes (pb);

    return node;
}
