/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * common.h
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

#ifndef __DONNA_COMMON_H__
#define __DONNA_COMMON_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#if defined(GIT_VERSION)
#undef PACKAGE_VERSION
#define PACKAGE_VERSION GIT_VERSION
#endif

typedef enum
{
    DONNA_ENABLED_TYPE_UNKNOWN = 0,
    DONNA_ENABLED_TYPE_ENABLED,
    DONNA_ENABLED_TYPE_DISABLED,
    DONNA_ENABLED_TYPE_COMBINE,
    DONNA_ENABLED_TYPE_IGNORE
} DonnaEnabledTypes;

typedef enum
{
    /* so for clipboard operations so can default to cut/copy state */
    DONNA_IO_UNKNOWN = 0,
    DONNA_IO_COPY,
    DONNA_IO_MOVE,
    DONNA_IO_DELETE
} DonnaIoType;

typedef enum
{
    DONNA_CLICK_LEFT        = (1 << 0),
    DONNA_CLICK_MIDDLE      = (1 << 1),
    DONNA_CLICK_RIGHT       = (1 << 2),

    DONNA_CLICK_SINGLE      = (1 << 3),
    DONNA_CLICK_DOUBLE      = (1 << 4),
    DONNA_CLICK_SLOW_DOUBLE = (1 << 5),
} DonnaClick;

typedef enum
{
    DONNA_ARG_TYPE_NOTHING      = 0, /* for no return value */
    DONNA_ARG_TYPE_INT          = (1 << 0),
    DONNA_ARG_TYPE_STRING       = (1 << 1),
    DONNA_ARG_TYPE_TREE_VIEW    = (1 << 2),
    DONNA_ARG_TYPE_NODE         = (1 << 3),
    DONNA_ARG_TYPE_ROW          = (1 << 4),
    DONNA_ARG_TYPE_PATH         = (1 << 5),
    DONNA_ARG_TYPE_ROW_ID       = (1 << 6),

    /*< private >*/
    _DONNA_ARG_TYPE_CUSTOM      = (1 << 14),
    /*< public >*/
    DONNA_ARG_IS_OPTIONAL       = (1 << 15),
    DONNA_ARG_IS_ARRAY          = (1 << 16),
} DonnaArgType;


typedef struct _DonnaApp                DonnaApp;
typedef struct _DonnaAppClass           DonnaAppClass;
typedef struct _DonnaAppPrivate         DonnaAppPrivate;

#define DONNA_TYPE_APP                  (donna_app_get_type ())
#define DONNA_APP(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_APP, DonnaApp))
#define DONNA_APP_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_APP, DonnaAppClass))
#define DONNA_IS_APP(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_APP))
#define DONNA_IS_APP_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_APP))
#define DONNA_APP_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_APP, DonnaAppClass))

GType           donna_app_get_type      (void) G_GNUC_CONST;


typedef struct _DonnaNode               DonnaNode;
typedef struct _DonnaNodeClass          DonnaNodeClass;
typedef struct _DonnaNodePrivate        DonnaNodePrivate;

#define DONNA_TYPE_NODE                 (donna_node_get_type ())
#define DONNA_NODE(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_NODE, DonnaNode))
#define DONNA_NODE_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_NODE, DonnaNodeClass))
#define DONNA_IS_NODE(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_NODE))
#define DONNA_IS_NODE_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_NODE))
#define DONNA_NODE_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_NODE, DonnaNodeClass))

GType           donna_node_get_type         (void) G_GNUC_CONST;


typedef struct _DonnaTask               DonnaTask;
typedef struct _DonnaTaskClass          DonnaTaskClass;
typedef struct _DonnaTaskPrivate        DonnaTaskPrivate;

#define DONNA_TYPE_TASK                 (donna_task_get_type ())
#define DONNA_TASK(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_TASK, DonnaTask))
#define DONNA_TASK_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_TASK, DonnaTaskClass))
#define DONNA_IS_TASK(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_TASK))
#define DONNA_IS_TASK_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_TASK))
#define DONNA_TASK_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_TASK, DonnaTaskClass))

GType           donna_task_get_type     (void) G_GNUC_CONST;


typedef struct _DonnaProvider               DonnaProvider; /* dummy typedef */
typedef struct _DonnaProviderInterface      DonnaProviderInterface;

#define DONNA_TYPE_PROVIDER                 (donna_provider_get_type ())
#define DONNA_PROVIDER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_PROVIDER, DonnaProvider))
#define DONNA_IS_PROVIDER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_PROVIDER))
#define DONNA_PROVIDER_GET_INTERFACE(obj)   (G_TYPE_INSTANCE_GET_INTERFACE ((obj), DONNA_TYPE_PROVIDER, DonnaProviderInterface))

GType           donna_provider_get_type     (void) G_GNUC_CONST;


typedef struct _DonnaTreeView           DonnaTreeView;
typedef struct _DonnaTreeViewPrivate    DonnaTreeViewPrivate;
typedef struct _DonnaTreeViewClass      DonnaTreeViewClass;

typedef struct _DonnaRow                DonnaRow;
typedef struct _DonnaRowId              DonnaRowId;

#define DONNA_TYPE_TREE_VIEW            (donna_tree_view_get_type ())
#define DONNA_TREE_VIEW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_TREE_VIEW, DonnaTreeView))
#define DONNA_TREE_VIEW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_TREE_VIEW, DonnaTreeViewClass))
#define DONNA_IS_TREE_VIEW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_TREE_VIEW))
#define DONNA_IS_TREE_VIEW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), DONNA_TYPE_TREE_VIEW))
#define DONNA_TREE_VIEW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_TREE_VIEW, DonnaTreeViewClass))

GType           donna_tree_view_get_type        (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __DONNA_COMMON_H__ */
