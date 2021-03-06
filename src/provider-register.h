/*
 * donnatella - Copyright (C) 2014 Olivier Brunel
 *
 * provider-register.h
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

#ifndef __DONNA_PROVIDER_REGISTER_H__
#define __DONNA_PROVIDER_REGISTER_H__

#include "provider-base.h"

G_BEGIN_DECLS

#define DONNA_TYPE_PROVIDER_REGISTER            (donna_provider_register_get_type ())
#define DONNA_PROVIDER_REGISTER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), DONNA_TYPE_PROVIDER_REGISTER, DonnaProviderRegister))
#define DONNA_PROVIDER_REGISTER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), DONNA_TYPE_PROVIDER_REGISTER, BonnaProviderRegisterClass))
#define DONNA_IS_PROVIDER_REGISTER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DONNA_TYPE_PROVIDER_REGISTER))
#define DONNA_IS_PROVIDER_REGISTER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), DONNA_TYPE_PROVIDER_REGISTER))
#define DONNA_PROVIDER_REGISTER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), DONNA_TYPE_PROVIDER_REGISTER, DonnaProviderRegisterClass))

typedef struct _DonnaProviderRegister           DonnaProviderRegister;
typedef struct _DonnaProviderRegisterClass      DonnaProviderRegisterClass;
typedef struct _DonnaProviderRegisterPrivate    DonnaProviderRegisterPrivate;

#define DONNA_PROVIDER_REGISTER_ERROR           g_quark_from_static_string ("DonnaProviderRegister-Error")
typedef enum
{
    DONNA_PROVIDER_REGISTER_ERROR_INVALID_NAME,
    DONNA_PROVIDER_REGISTER_ERROR_INVALID_FORMAT,
    DONNA_PROVIDER_REGISTER_ERROR_OTHER,
} DonnaProviderRegisterError;

typedef enum
{
    DONNA_REGISTER_UNKNOWN = 0, /* e.g. when getting CLIPBOARD as uri-list */
    DONNA_REGISTER_CUT,
    DONNA_REGISTER_COPY
} DonnaRegisterType;

typedef enum
{
    DONNA_DROP_REGISTER_NOT,
    DONNA_DROP_REGISTER_ALWAYS,
    DONNA_DROP_REGISTER_ON_CUT,
} DonnaDropRegister;

typedef enum
{
    DONNA_REGISTER_FILE_NODES,  /* w/ full locations */
    DONNA_REGISTER_FILE_FILE,   /* w/ filenames */
    DONNA_REGISTER_FILE_URIS,   /* w/ uris */
} DonnaRegisterFile;


struct _DonnaProviderRegister
{
    DonnaProviderBase parent;

    DonnaProviderRegisterPrivate *priv;
};

struct _DonnaProviderRegisterClass
{
    DonnaProviderBaseClass parent;
};

GType       donna_provider_register_get_type    (void) G_GNUC_CONST;

G_END_DECLS

#endif /* __DONNA_PROVIDER_REGISTER_H__ */
