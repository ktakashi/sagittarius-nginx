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
/* 
The configuration looks like this
sagittarius $entry-point{
  load_path foo/bar /baz/; # up to n path (haven't decided the number...)
  library "(your web library)";
}
 */
typedef struct {
  ngx_array_t *load_paths;	/* array of ngx_str_t */
  ngx_str_t library;		/* webapp library */
  ngx_str_t procedure;		/* entry point */
} ngx_http_sagittarius_conf_t;

static char* ngx_http_sagittarius_block(ngx_conf_t *cf,
					ngx_command_t *cmd,
					void *conf);
static char* ngx_http_sagittarius(ngx_conf_t *cf,
				  ngx_command_t *dummy,
				  void *conf);
static void* ngx_http_sagittarius_create_loc_conf(ngx_conf_t *cf);
static char* ngx_http_sagittarius_merge_loc_conf(ngx_conf_t *cf,
						 void *p, void *c);

static ngx_int_t ngx_http_sagittarius_init_process(ngx_cycle_t *cycle);

static ngx_command_t ngx_http_sagittarius_commands[] = {
  {
    ngx_string("sagittarius"),
    NGX_HTTP_LOC_CONF | NGX_CONF_BLOCK | NGX_CONF_TAKE1,
    ngx_http_sagittarius_block,
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
  ngx_http_sagittarius_create_loc_conf,	/* create location configuration */
  ngx_http_sagittarius_merge_loc_conf /* merge location configuration */
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
  SG_CLASS_SLOT_SPEC("headers", 0, nr_headers, NULL),
  SG_CLASS_SLOT_SPEC("body",    1, nr_body, NULL),
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

#define SG_DEFINE_GETTER(who, type, tp, acc, cast, cname)		\
  static SgObject cname(SgObject *argv, int argc, void *data)		\
  {									\
    if (argc != 1) {							\
      Sg_WrongNumberOfArgumentsViolation(SG_INTERN(who), 1, argc, SG_NIL); \
    }									\
    if (!tp(argv[0])) {							\
      Sg_WrongTypeOfArgumentViolation(SG_INTERN(who),SG_INTERN(type),	\
				      argv[0], SG_NIL);			\
    }									\
    return acc(cast(argv[0]));						\
  }									\
  static SG_DEFINE_SUBR(SG_CPP_CAT(cname, _stub), 1, 0, cname,		\
			SG_FALSE, NULL);

#define SG_DEFINE_SETTER(who, type, tp, setter, cast, cname)		\
  static SgObject cname(SgObject *argv, int argc, void *data)		\
  {									\
    if (argc != 2) {							\
      Sg_WrongNumberOfArgumentsViolation(SG_INTERN(who), 2, argc, SG_NIL); \
    }									\
    if (!tp(argv[0])) {							\
      Sg_WrongTypeOfArgumentViolation(SG_INTERN(who),SG_INTERN(type),	\
				      argv[0], SG_NIL);			\
    }									\
    setter(cast(argv[0]), argv[1]);					\
    return SG_UNDEF;							\
  }									\
  static SG_DEFINE_SUBR(SG_CPP_CAT(cname, _stub), 2, 0, cname,		\
			SG_FALSE, NULL);


SG_DEFINE_GETTER("nginx-request-headers", "nginx-request",
		 SG_NGINX_REQUESTP, nr_headers, SG_NGINX_REQUEST,
		 nginx_request_headers);
SG_DEFINE_GETTER("nginx-request-body", "nginx-request",
		 SG_NGINX_REQUESTP, nr_body, SG_NGINX_REQUEST,
		 nginx_request_body);

typedef struct SgNginxResponseRec
{
  SG_HEADER;
  SgObject headers;
  SgObject out;			/* will be an output port */
} SgNginxResponse;
SG_CLASS_DECL(Sg_NginxResponseClass);
#define SG_CLASS_NGINX_RESPONSE (&Sg_NginxResponseClass)
#define SG_NGINX_RESPONSE(obj)  ((SgNginxResponse *)obj)
#define SG_NGINX_RESPONSEP(obj) SG_XTYPEP(obj, SG_CLASS_NGINX_RESPONSE)

static void nginx_response_printer(SgObject self, SgPort *port,
				   SgWriteContext *ctx)
{
  Sg_Putuz(port, UC("#<nginx-response>"));
}
SG_DEFINE_BUILTIN_CLASS_SIMPLE(Sg_NginxResponseClass, nginx_response_printer);

static SgObject nres_headers(SgNginxResponse *nr)
{
  return nr->headers;
}
static SgObject nres_out(SgNginxResponse *nr)
{
  return nr->out;
}

static SgSlotAccessor nres_slots[] = {
  SG_CLASS_SLOT_SPEC("headers",     1, nres_headers, NULL),
  SG_CLASS_SLOT_SPEC("output-port", 2, nres_out, NULL),
  { { NULL } }
};

static SgObject nginx_response_p(SgObject *argv, int argc, void *data)
{
  if (argc != 1) {
    Sg_WrongNumberOfArgumentsViolation(SG_INTERN("nginx-response?"), 1,
				       argc, SG_NIL);
  }
  return SG_MAKE_BOOL(SG_NGINX_RESPONSEP(argv[0]));
}
static SG_DEFINE_SUBR(nginx_response_p_stub, 1, 0, nginx_response_p,
		      SG_FALSE, NULL);

SG_DEFINE_GETTER("nginx-response-headers", "nginx-response",
		 SG_NGINX_RESPONSEP, nres_headers, SG_NGINX_RESPONSE,
		 nginx_response_headers);
SG_DEFINE_GETTER("nginx-response-output-port", "nginx-response",
		 SG_NGINX_RESPONSEP, nres_out, SG_NGINX_RESPONSE,
		 nginx_response_output_port);

/* conditions */
static SgClass *error_cpl[] = {
  SG_CLASS_IO_ERROR,
  SG_ERROR_CONDITION_CPL,
  NULL
};

SG_CLASS_DECL(Sg_NginxErrorClass);
#define SG_CLASS_NGINX_ERROR (&Sg_NginxErrorClass)

typedef struct SgNginxErrorRec
{
  SG_INSTANCE_HEADER;
  SgObject status;
} SgNginxError;
#define SG_NGINX_ERROR(o) ((SgNginxError *)o)
#define SG_NGINX_ERRORP(o) SG_ISA(o, SG_CLASS_NGINX_ERROR)

static void nginx_error_printer(SgObject o, SgPort *p, SgWriteContext *ctx)
{
  Sg_Printf(p, UC("#<%A %A>"), SG_CLASS(Sg_ClassOf(o))->name,
	    SG_NGINX_ERROR(o)->status);
}

SG_DEFINE_CONDITION_ALLOCATOR(nginx_error_allocate, SgNginxError);
SG_DEFINE_CONDITION_ACCESSOR(nginx_error_status, SgNginxError,
			     SG_NGINX_ERRORP, status);
static SgSlotAccessor nginx_error_slots[] = {
  SG_CLASS_SLOT_SPEC("status", 0, nginx_error_status, nginx_error_status_set),
  { { NULL } }
};
SG_DEFINE_BASE_CLASS(Sg_NginxErrorClass, SgNginxError,
		     nginx_error_printer, NULL, NULL,
		     nginx_error_allocate, error_cpl);

static SgObject Sg_MakeNginxError(int status)
{
  SgObject c = nginx_error_allocate(SG_CLASS_NGINX_ERROR, SG_NIL);
  SG_NGINX_ERROR(c)->status = SG_MAKE_INT(status);
  return c;
}

static void raise_nginx_error(SgObject who, SgObject msg,
			      SgObject c, SgObject irr)
{
  SgObject sc;
  if (SG_NULLP(irr)) {
    sc = Sg_Condition(SG_LIST3(c,
			       Sg_MakeWhoCondition(who),
			       Sg_MakeMessageCondition(msg)));
  } else {
    sc = Sg_Condition(SG_LIST4(c,
			       Sg_MakeWhoCondition(who),
			       Sg_MakeMessageCondition(msg),
			       Sg_MakeIrritantsCondition(irr)));
  }
  Sg_Raise(sc, FALSE);
}

typedef struct SgResponseOutputPortRec
{
  SgPort              parent;
  ngx_chain_t        *root;
  ngx_chain_t        *buffer;
  ngx_http_request_t *request;
} SgResponseOutputPort;

SG_CLASS_DECL(Sg_ResponseOutputPortClass);
static SgClass *port_cpl[] = {
  SG_CLASS_PORT,
  SG_CLASS_TOP,
  NULL
};
SG_DEFINE_BUILTIN_CLASS(Sg_ResponseOutputPortClass, Sg_DefaultPortPrinter,
			NULL, NULL, NULL, port_cpl);
#define SG_CLASS_RESPONSE_OUTPUT_PORT (&Sg_ResponseOutputPortClass)
#define SG_RESPONSE_OUTPUT_PORT(obj)  ((SgResponseOutputPort*)obj)
#define SG_RESPONSE_OUTPUT_PORTP(obj)		\
  SG_XTYPEP(obj, SG_CLASS_RESPONSE_OUTPUT_PORT)
#define SG_RESPONSE_OUTPUT_PORT_ROOT(obj)	\
  (SG_RESPONSE_OUTPUT_PORT(obj)->root)
#define SG_RESPONSE_OUTPUT_PORT_BUFFER(obj)	\
  (SG_RESPONSE_OUTPUT_PORT(obj)->buffer)
#define SG_RESPONSE_OUTPUT_PORT_REQUEST(obj)	\
  (SG_RESPONSE_OUTPUT_PORT(obj)->request)

#define BUFFER_SIZE SG_PORT_DEFAULT_BUFFER_SIZE

static int response_out_close(SgObject self)
{
  SG_PORT(self)->closed = SG_PORT_CLOSED;
  return TRUE;
}

static void allocate_buffer(SgObject self, ngx_chain_t *parent)
{
  ngx_http_request_t *r = SG_RESPONSE_OUTPUT_PORT_REQUEST(self);
  ngx_chain_t *c = (ngx_chain_t *)ngx_pcalloc(r->pool, sizeof(ngx_chain_t));
  ngx_buf_t *b = (ngx_buf_t *)ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
  ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
		"Allocating response buffer");
  if (c == NULL || b == NULL) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
		  "Failed to allocate response buffer.");
    raise_nginx_error(SG_INTERN("put-u8"),
		      SG_MAKE_STRING("[Internal]"
				     " Failed to allocate response buffer"),
		      Sg_MakeNginxError(NGX_HTTP_INTERNAL_SERVER_ERROR),
		      SG_NIL);
  }
  /* maybe we can use nginx allocator? */
  b->start = b->pos = b->last = SG_NEW_ATOMIC2(unsigned char *, BUFFER_SIZE);
  b->end = b->start + BUFFER_SIZE;
  b->last_buf = 1;		/* may be reset later*/
  c->buf = b;
  c->next = NULL;
  if (parent) {
    parent->next = c;
  }
  SG_RESPONSE_OUTPUT_PORT_BUFFER(self) = c;
}

static int64_t response_out_put_u8_array(SgObject self, uint8_t *ba,
					 int64_t size)
{
  int64_t written;
  ngx_buf_t *buf;		/*  current buffer */
  if (!SG_RESPONSE_OUTPUT_PORT_BUFFER(self)) {
    allocate_buffer(self, NULL);
    SG_RESPONSE_OUTPUT_PORT_ROOT(self) = SG_RESPONSE_OUTPUT_PORT_BUFFER(self);
  }
  buf = SG_RESPONSE_OUTPUT_PORT_BUFFER(self)->buf;
  for (written = 0; written < size; written++) {
    if (buf->pos == buf->end) {
      buf->last_buf = 0;
      allocate_buffer(self, SG_RESPONSE_OUTPUT_PORT_BUFFER(self));
      buf = SG_RESPONSE_OUTPUT_PORT_BUFFER(self)->buf;
    }
    *buf->pos++ = *ba++;
  }
  return written;
}

static SgPortTable response_out_table = {
  NULL,				/* no flush */
  response_out_close,
  NULL,				/* no ready */
  NULL,				/* lock */
  NULL,				/* unlock */
  NULL,				/* position (should we?) */
  NULL,				/* set position (should we?) */
  NULL,				/* open (not used?)*/
  NULL,				/* read u8 */
  NULL,				/* read all */
  response_out_put_u8_array,
  NULL,				/* read str */
  NULL,				/* write str */
};

static SgObject make_response_output_port(ngx_http_request_t *request)
{
  SgResponseOutputPort *port = SG_NEW(SgResponseOutputPort);
  SG_INIT_PORT(port, SG_CLASS_RESPONSE_OUTPUT_PORT, SG_OUTPUT_PORT,
	       &response_out_table, SG_FALSE);
  port->buffer = NULL;
  port->request = request;
  return SG_OBJ(port);
}

static SgObject nginx_dispatch = SG_UNDEF;
static ngx_int_t ngx_http_sagittarius_init_process(ngx_cycle_t *cycle)
{
  SgObject sym, lib, o;
  ngx_log_error(NGX_LOG_DEBUG, cycle->log, 0,
		"Initialising Sagittarius process");
  /* Initialise the sagittarius VM */
  Sg_Init();

  sym = SG_INTERN("(sagittarius nginx internal)");
  ngx_log_error(NGX_LOG_DEBUG, cycle->log, 0,
		"Initialising '(sagittarius nginx internal)' library");
  lib = Sg_FindLibrary(sym, TRUE);

  Sg_InitStaticClassWithMeta(SG_CLASS_NGINX_REQUEST, UC("<nginx-request>"),
			     SG_LIBRARY(lib), NULL,
			     SG_FALSE, nr_slots, 0);
  Sg_InitStaticClassWithMeta(SG_CLASS_NGINX_RESPONSE, UC("<nginx-response>"),
			     SG_LIBRARY(lib), NULL,
			     SG_FALSE, nres_slots, 0);
  Sg_InitStaticClass(SG_CLASS_RESPONSE_OUTPUT_PORT, UC("<nginx-response-port>"),
		     SG_LIBRARY(lib), NULL, 0);
 
  Sg_InsertBinding(SG_LIBRARY(lib),
		   SG_INTERN("nginx-request?"), &nginx_request_p_stub);
  SG_PROCEDURE_NAME(&nginx_request_p_stub) = SG_MAKE_STRING("nginx-request?");
  SG_PROCEDURE_TRANSPARENT(&nginx_request_p_stub) = SG_PROC_TRANSPARENT;

  Sg_InsertBinding(SG_LIBRARY(lib),
		   SG_INTERN("nginx-response?"), &nginx_response_p_stub);
  SG_PROCEDURE_NAME(&nginx_response_p_stub) = SG_MAKE_STRING("nginx-response?");
  SG_PROCEDURE_TRANSPARENT(&nginx_response_p_stub) = SG_PROC_TRANSPARENT;

#define INSERT_ACCESSOR(name, cname)					\
  do {									\
    Sg_InsertBinding(SG_LIBRARY(lib), SG_INTERN(name),			\
		     &SG_CPP_CAT(cname, _stub));			\
    SG_PROCEDURE_NAME(&SG_CPP_CAT(cname, _stub)) = SG_MAKE_STRING(name); \
    SG_PROCEDURE_TRANSPARENT(&SG_CPP_CAT(cname, _stub)) =		\
      SG_PROC_NO_SIDE_EFFECT;						\
  } while (0)

  INSERT_ACCESSOR("nginx-request-headers", nginx_request_headers);
  INSERT_ACCESSOR("nginx-request-body", nginx_request_body);

  INSERT_ACCESSOR("nginx-response-headers", nginx_response_headers);
  INSERT_ACCESSOR("nginx-response-output-port", nginx_response_output_port);
#undef INSERT_ACCESSOR

  SG_INIT_CONDITION(SG_CLASS_NGINX_ERROR, lib,
		    "&nginx-error", nginx_error_slots);
  SG_INIT_CONDITION_PRED(SG_CLASS_NGINX_ERROR, lib, "nginx-error?");
  SG_INIT_CONDITION_CTR(SG_CLASS_NGINX_ERROR, lib, "make-nginx-error", 1);
  SG_INIT_CONDITION_ACC(nginx_error_status, lib, "&nginx-error-status");

  ngx_log_error(NGX_LOG_DEBUG, cycle->log, 0,
		"'(sagittarius nginx internal)' library is initialised");

  ngx_log_error(NGX_LOG_DEBUG, cycle->log, 0,
		"Initialising '(sagittarius nginx)' library");
  sym = SG_INTERN("(sagittarius nginx)");
  lib = Sg_FindLibrary(sym, FALSE);
  if (SG_FALSEP(lib)) {
    ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
		  "Failed to find '(sagittarius nginx)' library");
    return NGX_ERROR;
  }
  ngx_log_error(NGX_LOG_DEBUG, cycle->log, 0,
		"Retrieving nginx-dispatch-request");
  o = Sg_FindBinding(lib, SG_INTERN("nginx-dispatch-request"), SG_UNBOUND);
  if (SG_UNBOUNDP(o)) {
    ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
		  "Failed to retrieve nginx-dispatch-request");
    return NGX_ERROR;
  }
  nginx_dispatch = SG_GLOC_GET(SG_GLOC(o));
  ngx_log_error(NGX_LOG_DEBUG, cycle->log, 0,
		"'(sagittarius nginx)' library is initialised");
  return NGX_OK;
}

static ngx_int_t ngx_http_sagittarius_handler(ngx_http_request_t *r);
static char* ngx_http_sagittarius_block(ngx_conf_t *cf,
					ngx_command_t *cmd,
					void *conf)
{
  char                        *rv;
  ngx_conf_t                   save;
  ngx_str_t                   *value;
  ngx_http_core_loc_conf_t    *clcf;
  ngx_http_sagittarius_conf_t *sg_conf;
  ngx_log_error(NGX_LOG_DEBUG, cf->log, 0,
		"Handling Sagittarius configuration");

  value = cf->args->elts;
  sg_conf = (ngx_http_sagittarius_conf_t *)conf;
  sg_conf->procedure = (ngx_str_t)value[1];
  ngx_log_error(NGX_LOG_DEBUG, cf->log, 0,
		"'sagittarius' invocation procedure name %V",
		&sg_conf->procedure);
  
  save = *cf;
  cf->handler = ngx_http_sagittarius;
  cf->handler_conf = conf;
  rv = ngx_conf_parse(cf, NULL);
  *cf = save;
  
  if (rv != NGX_CONF_OK) {
    return NGX_CONF_ERROR;
  }
  
  clcf = (ngx_http_core_loc_conf_t *)
    ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
  clcf->handler = ngx_http_sagittarius_handler;
  
  return NGX_CONF_OK;
}

static char* ngx_http_sagittarius(ngx_conf_t *cf,
				  ngx_command_t *dummy,
				  void *conf)
{
  ngx_uint_t i;
  ngx_http_sagittarius_conf_t *sg_conf;
  ngx_str_t *value;
  
  sg_conf = (ngx_http_sagittarius_conf_t *)conf;
  value = cf->args->elts;

  ngx_log_error(NGX_LOG_DEBUG, cf->log, 0,
		"'sagittarius': n=%d", cf->args->nelts);
  for (i = 0; i < cf->args->nelts; i++) {
    ngx_log_error(NGX_LOG_DEBUG, cf->log, 0, "'sagittarius': %V", &value[i]);
  }

  if (ngx_strcmp(value[0].data, "load_path") == 0) {
    if (cf->args->nelts < 2) {
      ngx_log_error(NGX_LOG_ERR, cf->log, 0,
		    "'sagittarius': "
		    "'load_path' must take at least one argument");
      return NGX_CONF_ERROR;
    }
    sg_conf->load_paths = ngx_array_create(cf->pool, cf->args->nelts-1,
					   sizeof(ngx_str_t));
    for (i = 0; i < cf->args->nelts - 1; i++) {
      ngx_str_t *s, *v;
      v = &value[i];
      s = ngx_array_push(sg_conf->load_paths);
      s->data = v->data;
      s->len = v->len;
    }
  } else if (ngx_strcmp(value[0].data, "library") == 0) {
    if (cf->args->nelts != 2) {
      ngx_log_error(NGX_LOG_ERR, cf->log, 0,
		    "'sagittarius': 'library' must take one argument (%d)",
		    cf->args->nelts);
      return NGX_CONF_ERROR;
    }
    sg_conf->library = value[1];
  } else {
    ngx_log_error(NGX_LOG_ERR, cf->log, 0,
		  "'sagittarius': unknown directive %V", &value[0]);
  }

  return NGX_CONF_OK;
}

static void* ngx_http_sagittarius_create_loc_conf(ngx_conf_t *cf)
{
  ngx_http_sagittarius_conf_t *conf;
  conf = (ngx_http_sagittarius_conf_t *)
    ngx_palloc(cf->pool, sizeof(ngx_http_sagittarius_conf_t));
  if (!conf) {
    return NGX_CONF_ERROR;
  }
  return conf;
}

static char* ngx_http_sagittarius_merge_loc_conf(ngx_conf_t *cf,
						 void *p, void *c)
{
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

static SgObject make_nginx_response(ngx_http_request_t *req)
{
  SgNginxResponse *ngxRes = SG_NEW(SgNginxResponse);
  SG_SET_CLASS(ngxRes, SG_CLASS_NGINX_RESPONSE);
  ngxRes->headers = SG_NIL;	/* TODO */
  ngxRes->out = make_response_output_port(req);
  return SG_OBJ(ngxRes);
}


/* Main handler */
static ngx_int_t ngx_http_sagittarius_handler(ngx_http_request_t *r)
{
  SgObject req, uri, resp;
  volatile SgObject status;
  ngx_int_t rc;
  ngx_chain_t *out;

  ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
		"Handling Sagittarius request for %s.", r->uri.data);

  /* TODO call initialiser with configuration in location */
  /* TODO convert body to input port... */
  rc = ngx_http_discard_request_body(r);
  if (rc != NGX_OK && rc != NGX_AGAIN) {
    return rc;
  }

  uri = Sg_Utf8sToUtf32s((const char *)r->uri.data, r->uri.len);
  req = make_nginx_request(r);
  resp = make_nginx_response(r);
  SG_UNWIND_PROTECT {
    status = Sg_Apply3(nginx_dispatch, uri, req, resp);
  } SG_WHEN_ERROR {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
		  "Failed to execute nginx-dispatch");
    return NGX_HTTP_INTERNAL_SERVER_ERROR;    
  } SG_END_PROTECT;

  if (SG_INTP(status)) {
    /* convert Scheme response to C response */
    /* TODO do it above... */
    r->headers_out.status = SG_INT_VALUE(status);
    r->headers_out.content_type.len = sizeof("text/plain") - 1;
    r->headers_out.content_type.data = (u_char *) "text/plain";

    rc = ngx_http_send_header(r);
  
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
      return rc;
    }
    out = SG_RESPONSE_OUTPUT_PORT_BUFFER(SG_NGINX_RESPONSE(resp));
    return ngx_http_output_filter(r, out);
  } else {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
		  "Scheme program returned non fixnum.");
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
  }
}
