/*
 * Copyright (c) 2018 Takashi Kato <ktakashi@ymail.com>
 * See Licence.txt for terms and conditions of use
 */

// Cygwin workaround
#include <sagittarius.h>
#define LIBSAGITTARIUS_EXT_BODY
#include <sagittarius/extend.h>

// NGINX APIs are not C++ tolerant so wrap with extern "C" here...
extern "C" {
#include <ngx_core.h>
#include <ngx_config.h>
#include <ngx_http.h>

#include "ngx_http_sagittarius_module.c"
}
