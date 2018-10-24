/*
 * Copyright (c) 2018 Takashi Kato <ktakashi@ymail.com>
 * See Licence.txt for terms and conditions of use
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

/* 
   References:
   - https://www.evanmiller.org/nginx-modules-guide.html
   - https://www.nginx.com/resources/wiki/extending/api/
*/

static ngx_command_t ngx_http_sagittarius_commands[] = {
  
};

static ngx_http_module_t ngx_http_sagittarius_module_ctx = {
  NULL, 			/* preconfiguration */
  NULL,				/* postconfiguration */
  NULL, 			/* create main confiugraetion */
  NULL, 			/* init main configuration */
  NULL,				/* create server configuration */
  NULL,				/* merge server configuration */
  NULL,				/* create location configuration */
  NULL				/* merge location configuration */
};

ngx_module_t ngx_http_sagittarius_module = {
  NGX_MODULE_V1,
  &ngx_http_sagittarius_module_ctx,
  ngx_http_sagittarius_commands,
  NGX_HTTP_MODULE,
  NULL,				/* init master */
  NULL,                          /* init module */
  NULL,                          /* init process */
  NULL,                          /* init thread */
  NULL,                          /* exit thread */
  NULL,                          /* exit process */
  NULL,                          /* exit master */
  NGX_MODULE_V1_PADDING
};
