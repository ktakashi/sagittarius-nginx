/*
 * Copyright (c) 2018 Takashi Kato <ktakashi@ymail.com>
 * See Licence.txt for terms and conditions of use
 */

/* ngx_core.h defines LF as a macro which conflicts, so first include
   this */
#include <sagittarius.h>

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

/* 
   References:
   - https://www.evanmiller.org/nginx-modules-guide.html
   - https://www.nginx.com/resources/wiki/extending/api/
*/

static char* ngx_http_sagittarius(ngx_conf_t *cf,
				  ngx_command_t *cmd,
				  void *conf);

static ngx_int_t ngx_http_sagittarius_init_process(ngx_cycle_t *cycle);

static ngx_command_t ngx_http_sagittarius_commands[] = {
  {
    ngx_string("sagittarius"),
    NGX_HTTP_LOC_CONF | NGX_CONF_NOARGS,
    ngx_http_sagittarius,
    NGX_HTTP_LOC_CONF_OFFSET,
    0,
    NULL
  }
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
  NULL,				/* init module */
  ngx_http_sagittarius_init_process, /* init process */
  NULL,				/* init thread */
  NULL,				/* exit thread */
  NULL,				/* exit process */
  NULL,				/* exit master */
  NGX_MODULE_V1_PADDING
};


typedef struct SgNginxRequestRec
{
  SG_HEADER;
  SgObject headers;
  SgObject body;		/* will be a input port */
  ngx_http_request_t *rawNginxRequest;
} SgNginxRequest;
SG_CLASS_DECL(Sg_NginxRequestClass);
#define SG_CLASS_NGINX_REQUEST (&Sg_NginxRequestClass)
#define SG_NGINX_REQUEST(obj)  ((SgNginxRequest *)obj)
#define SG_NGINX_REQUESTP(obj) SG_XTYPEP(obj, SG_CLASS_NGINX_REQUEST)

static void nginx_request_printer(SgObject self, SgPort *port,
				  SgWriteContext *ctx)
{
  Sg_Printf(port, UC("#<nginx-request %x>"),
	    SG_NGINX_REQUEST(self)->rawNginxRequest);
}
SG_DEFINE_BUILTIN_CLASS_SIMPLE(Sg_NginxRequestClass, nginx_request_printer);

static SgObject nr_headers(SgNginxRequest *nr)
{
  return nr->headers;
}
static SgObject nr_body(SgNginxRequest *nr)
{
  return nr->body;
}

static SgSlotAccessor nr_slots[] = {
  SG_CLASS_SLOT_SPEC("headers",   0, nr_headers, NULL),
  SG_CLASS_SLOT_SPEC("body", 1, nr_body, NULL),
  { { NULL } }
};

static SgObject nginx_request_p(SgObject *argv, int argc, void *data)
{
  if (argc != 1) {
    Sg_WrongNumberOfArgumentsViolation(SG_INTERN("nginx-request?"), 1,
				       argc, SG_NIL);
  }
  return SG_MAKE_BOOL(SG_NGINX_REQUESTP(argv[0]));
}
static SG_DEFINE_SUBR(nginx_request_p_stub, 1, 0, nginx_request_p,
		      SG_FALSE, NULL);

static SgObject sagittarius_nginx_symbol = SG_UNDEF;
static SgObject sagittarius_nginx_library = SG_UNDEF;
static SgObject nginx_dispatch = SG_UNDEF;
static ngx_int_t ngx_http_sagittarius_init_process(ngx_cycle_t *cycle)
{
  SgObject o;
  /* Initialise the sagittarius VM */
  Sg_Init();

  sagittarius_nginx_symbol = SG_INTERN("(sagittarius nginx)");
  sagittarius_nginx_library = Sg_FindLibrary(sagittarius_nginx_symbol, FALSE);
  Sg_InitStaticClassWithMeta(SG_CLASS_NGINX_REQUEST, UC("<nginx-request>"),
			     sagittarius_nginx_library, NULL,
			     SG_FALSE, nr_slots, 0);

  Sg_InsertBinding(sagittarius_nginx_library, SG_INTERN("nginx-request?"),
		   &nginx_request_p_stub);
  SG_PROCEDURE_NAME(&nginx_request_p_stub) = SG_MAKE_STRING("nginx-request?");
  SG_PROCEDURE_TRANSPARENT(&nginx_request_p_stub) = SG_PROC_TRANSPARENT;

  o = Sg_FindBinding(sagittarius_nginx_library,
		     SG_INTERN("nginx-dispatch-request"), SG_UNBOUND);
  if (SG_UNBOUNDP(o)) {
    return NGX_ERROR;
  }
  nginx_dispatch = SG_GLOC_GET(SG_GLOC(o));
  /* TODO call initialiser with configuration in location */
  return NGX_OK;
}

static ngx_int_t ngx_http_sagittarius_handler(ngx_http_request_t *r);
static char* ngx_http_sagittarius(ngx_conf_t *cf,
				  ngx_command_t *cmd,
				  void *conf)
{
  ngx_http_core_loc_conf_t  *clcf;

  ngx_log_debug(NGX_LOG_DEBUG, cf->log, 0,
		"Handling Sagittarius configuration");
  clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
  clcf->handler = ngx_http_sagittarius_handler;
  
  return NGX_CONF_OK;
}

static SgObject make_nginx_request(ngx_http_request_t *req)
{
  SgNginxRequest *ngxReq = SG_NEW(SgNginxRequest);
  SG_SET_CLASS(ngxReq, SG_CLASS_NGINX_REQUEST);
  ngxReq->headers = SG_NIL;	/* TODO */
  ngxReq->body = SG_FALSE;	/* TODO */
  ngxReq->rawNginxRequest = req;
  return SG_OBJ(ngxReq);
}

/* Main handler */
static ngx_int_t ngx_http_sagittarius_handler(ngx_http_request_t *r)
{
  SgObject req, uri;
  ngx_int_t     rc;
  ngx_buf_t    *b;
  ngx_chain_t   out;

  ngx_log_debug(NGX_LOG_DEBUG, r->connection->log, 0,
		"Handling Sagittarius request for %s.", r->uri.data);
  
  /* TODO convert body to input port... */
  rc = ngx_http_discard_request_body(r);
  if (rc != NGX_OK && rc != NGX_AGAIN) {
    return rc;
  }

  uri = Sg_Utf8sToUtf32s((const char *)r->uri.data, r->uri.len);
  req = make_nginx_request(r);
  /* resp = */ Sg_Apply2(nginx_dispatch, uri, req);

  /* convert Scheme response to C response */
  /* TODO do it above... */
  r->headers_out.status = NGX_HTTP_OK;
  r->headers_out.content_type.len = sizeof("text/plain") - 1;
  r->headers_out.content_type.data = (u_char *) "text/plain";

  rc = ngx_http_send_header(r);
  
  if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
    return rc;
  }

  b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
  if (b == NULL) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
		  "Failed to allocate response buffer.");
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
  }

  out.buf = b;
  out.next = NULL;

  b->pos = (unsigned char *)"OK";
  b->last = (unsigned char *)(((uintptr_t)b->pos) + 2);
  b->memory = 1;
  b->last_buf = 1;
  
  return ngx_http_output_filter(r, &out);  
}
