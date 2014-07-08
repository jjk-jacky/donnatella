/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * provider-filter.h
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

#ifndef __DONNA_PROVIDER_FILTER_H__
#define __DONNA_PROVIDER_FILTER_H__

#include "provider-base.h"
#include "filter.h"

G_BEGIN_DECLS

#define DONNA_TYPE_PROVIDER_FILTER              (donna_provider_filter_get_type ())
#define DONNA_PROVIDER_FILTER(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_PROVIDER_FILTER, DonnaProviderFilter))
#define DONNA_PROVIDER_FILTER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_PROVIDER_FILTER, BonnaProviderFilterClass))
#define DONNA_IS_PROVIDER_FILTER(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_PROVIDER_FILTER))
#define DONNA_IS_PROVIDER_FILTER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_PROVIDER_FILTER))
#define DONNA_PROVIDER_FILTER_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_PROVIDER_FILTER, DonnaProviderFilterClass))

typedef struct _DonnaProviderFilter             DonnaProviderFilter;
typedef struct _DonnaProviderFilterClass        DonnaProviderFilterClass;
typedef struct _DonnaProviderFilterPrivate      DonnaProviderFilterPrivate;

struct _DonnaProviderFilter
{
    /*< private >*/
    DonnaProviderBase parent;

    DonnaProviderFilterPrivate *priv;
};

struct _DonnaProviderFilterClass
{
    DonnaProviderBaseClass parent;
};

GType           donna_provider_filter_get_type      (void) G_GNUC_CONST;

DonnaFilter *   donna_provider_filter_get_filter    (DonnaProviderFilter    *pf,
                                                     const gchar            *filter_str,
                                                     GError                **error);
DonnaFilter *   donna_provider_filter_get_filter_from_node (
                                                     DonnaProviderFilter    *pf,
                                                     DonnaNode              *node,
                                                     GError                **error);
DonnaNode *     donna_provider_filter_get_node_for_filter (
                                                     DonnaProviderFilter    *pf,
                                                     DonnaFilter            *filter,
                                                     GError                **error);

G_END_DECLS

#endif /* __DONNA_PROVIDER_FILTER_H__ */
