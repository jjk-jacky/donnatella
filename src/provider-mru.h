/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * provider-mru.h
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

#ifndef __DONNA_PROVIDER_MRU_H__
#define __DONNA_PROVIDER_MRU_H__

#include "provider-base.h"

G_BEGIN_DECLS

#define DONNA_TYPE_PROVIDER_MRU             (donna_provider_mru_get_type ())
#define DONNA_PROVIDER_MRU(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_PROVIDER_MRU, DonnaProviderMru))
#define DONNA_PROVIDER_MRU_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_PROVIDER_MRU, BonnaProviderMruClass))
#define DONNA_IS_PROVIDER_MRU(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_PROVIDER_MRU))
#define DONNA_IS_PROVIDER_MRU_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_PROVIDER_MRU))
#define DONNA_PROVIDER_MRU_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_PROVIDER_MRU, DonnaProviderMruClass))

typedef struct _DonnaProviderMru            DonnaProviderMru;
typedef struct _DonnaProviderMruClass       DonnaProviderMruClass;
typedef struct _DonnaProviderMruPrivate     DonnaProviderMruPrivate;

struct _DonnaProviderMru
{
    /*< private >*/
    DonnaProviderBase parent;

    DonnaProviderMruPrivate *priv;
};

struct _DonnaProviderMruClass
{
    DonnaProviderBaseClass parent;
};

GType           donna_provider_mru_get_type         (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __DONNA_PROVIDER_MRU_H__ */
