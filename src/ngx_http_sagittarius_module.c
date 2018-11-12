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

We do not use SgObject here. I'm not sure when the configuration parsing 
happens and the initialisation of Sagittarius happens on the creation of
worker process.
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

#define ngx_str_to_string(s) Sg_Utf8sToUtf32s((const char *)(s)->data, (s)->len)
#define BUFFER_SIZE SG_PORT_DEFAULT_BUFFER_SIZE

typedef struct
{
  SG_HEADER;
  SgObject method;
  SgObject uri;
  SgObject headers;
  SgObject cookies;
  SgObject body;		/* binary input port */
  ngx_http_request_t *rawNginxRequest;
  /* TODO maybe cache the builtin values? */
} SgNginxRequest;
SG_CLASS_DECL(Sg_NginxRequestClass);
#define SG_CLASS_NGINX_REQUEST (&Sg_NginxRequestClass)
#define SG_NGINX_REQUEST(obj)  ((SgNginxRequest *)obj)
#define SG_NGINX_REQUESTP(obj) SG_XTYPEP(obj, SG_CLASS_NGINX_REQUEST)

static void nginx_request_printer(SgObject self, SgPort *port,
				  SgWriteContext *ctx)
{
  Sg_Printf(port, UC("#<nginx-request %A %A>"),
	    SG_NGINX_REQUEST(self)->method,
	    SG_NGINX_REQUEST(self)->uri);
}
SG_DEFINE_BUILTIN_CLASS_SIMPLE(Sg_NginxRequestClass, nginx_request_printer);

static SgObject nr_headers(SgNginxRequest *nr)
{
  if (SG_FALSEP(nr->headers)) {
    SgObject h = SG_NIL, t = SG_NIL;
    ngx_uint_t i;
    ngx_list_t *headers = &nr->rawNginxRequest->headers_in.headers;
    ngx_list_part_t *part = &headers->part;
    ngx_table_elt_t *data = part->elts;
    for (i = 0;; i++) {
      ngx_table_elt_t *e;
      SgObject k, v, o;
      if (i >= part->nelts) {
	if (part->next == NULL) break;
	part = part->next;
	data = part->elts;
	i = 0;
      }
      e = &data[i];
      k = ngx_str_to_string(&e->key);
      v = ngx_str_to_string(&e->value);
      o = SG_LIST2(k, v);
      SG_APPEND1(h, t, o);
    }
    nr->headers = h;
  }
  return nr->headers;
}
static SgObject nr_method(SgNginxRequest *nr)
{
  return nr->method;
}

static SgObject nr_uri(SgNginxRequest *nr)
{
  return nr->uri;
}

static SgObject nr_body(SgNginxRequest *nr)
{
  return nr->body;
}

#define HEADER_FIELD(name, cname, n)					\
  static SgObject SG_CPP_CAT(nr_, cname)(SgNginxRequest *nr)		\
  {									\
    ngx_table_elt_t *e = nr->rawNginxRequest->headers_in. cname;	\
    if (e == NULL) return SG_FALSE;					\
    return ngx_str_to_string(&e->value);				\
  }
#include "builtin_request_fields.inc"
#undef HEADER_FIELD

static SgSlotAccessor nr_slots[] = {
  SG_CLASS_SLOT_SPEC("method",     0, nr_method, NULL),
  SG_CLASS_SLOT_SPEC("uri",        1, nr_uri, NULL),
  SG_CLASS_SLOT_SPEC("headers",    2, nr_headers, NULL),
  SG_CLASS_SLOT_SPEC("input-port", 3, nr_body, NULL),
#define HEADER_FIELD(name, cname, n)				\
  SG_CLASS_SLOT_SPEC(#name, n+3, SG_CPP_CAT(nr_, cname), NULL),
#include "builtin_request_fields.inc"
#undef HEADER_FIELD
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

SG_DEFINE_GETTER("nginx-request-method", "nginx-request",
		 SG_NGINX_REQUESTP, nr_method, SG_NGINX_REQUEST,
		 nginx_request_method);
SG_DEFINE_GETTER("nginx-request-uri", "nginx-request",
		 SG_NGINX_REQUESTP, nr_uri, SG_NGINX_REQUEST,
		 nginx_request_uri);
SG_DEFINE_GETTER("nginx-request-headers", "nginx-request",
		 SG_NGINX_REQUESTP, nr_headers, SG_NGINX_REQUEST,
		 nginx_request_headers);
SG_DEFINE_GETTER("nginx-request-input-port", "nginx-request",
		 SG_NGINX_REQUESTP, nr_body, SG_NGINX_REQUEST,
		 nginx_request_body);
#define HEADER_FIELD(name, cname, n)					\
  SG_DEFINE_GETTER("nginx-request-"#name, "nginx-request",		\
		   SG_NGINX_REQUESTP, SG_CPP_CAT(nr_, cname),		\
		   SG_NGINX_REQUEST,					\
		   SG_CPP_CAT(nginx_request_, cname));
#include "builtin_request_fields.inc"
#undef HEADER_FIELD


typedef struct
{
  SG_HEADER;
  SgObject headers;
  SgObject out;			/* will be an output port */
  ngx_http_request_t *request;
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

static SgObject nres_content_type(SgNginxResponse *nr)
{
  /* TODO cache? */
  ngx_str_t *s = &nr->request->headers_out.content_type;
  return ngx_str_to_string(s);
}

static void nres_content_type_set(SgNginxResponse *nr, SgObject v)
{
  char *r;
  if (!SG_STRINGP(v)) {
    Sg_WrongTypeOfArgumentViolation(SG_INTERN("nginx-response-content-type-set!"),
				    SG_INTERN("string"),
				    v, SG_NIL);
  }
  r = Sg_Utf32sToUtf8s(v);
  nr->request->headers_out.content_type.len = ngx_strlen(r);
  nr->request->headers_out.content_type.data = (unsigned char *)r;
}


static SgObject nres_headers(SgNginxResponse *nr)
{
  if (SG_FALSEP(nr->headers)) {
    SgObject h = SG_NIL, t = SG_NIL;
    ngx_http_request_t *r = nr->request;
    ngx_uint_t i;
    ngx_list_part_t *part;
    ngx_table_elt_t *data;

#define ADD_HEADER(elt)							\
    do {								\
      SgObject n = ngx_str_to_string(&(elt)->key);			\
      SgObject v = ngx_str_to_string(&(elt)->value);			\
      SG_APPEND1(h, t, SG_LIST2(n, v));					\
    } while (0)
    
#define ADD_BUILTIN_HEADER(field)					\
    do {								\
      if (r->headers_out. field) {					\
	ADD_HEADER(r->headers_out. field);				\
      }									\
    } while (0)

    ADD_BUILTIN_HEADER(server);
    ADD_BUILTIN_HEADER(date);
    ADD_BUILTIN_HEADER(content_encoding);
    ADD_BUILTIN_HEADER(location);
    ADD_BUILTIN_HEADER(refresh);
    ADD_BUILTIN_HEADER(last_modified);
    ADD_BUILTIN_HEADER(content_range);
    ADD_BUILTIN_HEADER(accept_ranges);
    ADD_BUILTIN_HEADER(www_authenticate);
    ADD_BUILTIN_HEADER(expires);
    ADD_BUILTIN_HEADER(etag);
#undef ADD_BUILTIN_HEADER

    part = &r->headers_out.headers.part;
    data = part->elts;
    for (i = 0; ; i++) {
      ngx_table_elt_t *e;
      if (i >= part->nelts) {
	if (part->next == NULL) break;
	part = part->next;
	data = part->elts;
	i = 0;
      }
      e = &data[i++];
      ADD_HEADER(e);
#undef ADD_HEADER
    }
    nr->headers = h;
  }
  return nr->headers;
}
static SgObject nres_out(SgNginxResponse *nr)
{
  return nr->out;
}

static SgSlotAccessor nres_slots[] = {
  SG_CLASS_SLOT_SPEC("content-type", 1, nres_content_type, nres_content_type_set),
  SG_CLASS_SLOT_SPEC("headers",      2, nres_headers, NULL),
  SG_CLASS_SLOT_SPEC("output-port",  3, nres_out, NULL),
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

SG_DEFINE_GETTER("nginx-response-content-type", "nginx-response",
		 SG_NGINX_RESPONSEP, nres_content_type, SG_NGINX_RESPONSE,
		 nginx_response_content_type);
SG_DEFINE_SETTER("nginx-response-content-type-set!", "nginx-response",
		 SG_NGINX_RESPONSEP, nres_content_type_set, SG_NGINX_RESPONSE,
		 nginx_response_content_type_set);
SG_DEFINE_GETTER("nginx-response-headers", "nginx-response",
		 SG_NGINX_RESPONSEP, nres_headers, SG_NGINX_RESPONSE,
		 nginx_response_headers);
SG_DEFINE_GETTER("nginx-response-output-port", "nginx-response",
		 SG_NGINX_RESPONSEP, nres_out, SG_NGINX_RESPONSE,
		 nginx_response_output_port);

static void ngx_add_header(SgObject res, SgObject name, SgObject value)
{
  ngx_http_request_t *r = SG_NGINX_RESPONSE(res)->request;
  SgObject s = Sg_StringDownCase(SG_STRING(name));
  unsigned char *uc;

  SG_NGINX_RESPONSE(res)->headers = SG_FALSE; /* reset */
#define set_builtin_header3(field, name, sname)				\
  do {									\
    if (!r->headers_out. field) {					\
      r->headers_out. field = ngx_pcalloc(r->pool, sizeof(ngx_table_elt_t)); \
      ngx_str_set(&r->headers_out. field ->key, name);			\
      r->headers_out. field ->lowcase_key = (unsigned char *)sname;	\
    }									\
    r->headers_out. field ->value.data = uc;				\
    r->headers_out. field ->value.len = ngx_strlen(uc);			\
  } while (0)

#define set_builtin_header(field, name) set_builtin_header3(field, name, #field)

  uc = (unsigned char *)Sg_Utf32sToUtf8s(value);
  /* 
     some of the headers we only allow to send once 
     NOTE: we don't check the format of the header
  */
  if (ustrcmp(SG_STRING_VALUE(s), "server") == 0) {
    /* The Server: header must be set by configuration so ignore */
  } else if (ustrcmp(SG_STRING_VALUE(s), "date") == 0) {
    if (!r->headers_out.date) {
      r->headers_out.date =
	(ngx_table_elt_t *)ngx_list_push(&r->headers_out.headers);
      ngx_str_set(&r->headers_out.date->key, "Date");
      r->headers_out.date->lowcase_key = (unsigned char *)"date";
    }
    set_builtin_header(date, "Date");
    
  } else if (ustrcmp(SG_STRING_VALUE(s), "content-length") == 0) {
    /* The Server: header must be set by the content */
  } else if (ustrcmp(SG_STRING_VALUE(s), "content-encoding") == 0) {
    set_builtin_header3(content_encoding,
			"Content-Encoding", "content-encoding");
  } else if (ustrcmp(SG_STRING_VALUE(s), "location") == 0) {
    set_builtin_header(location, "Location");
  } else if (ustrcmp(SG_STRING_VALUE(s), "refresh") == 0) {
    set_builtin_header(refresh, "Refresh");
  } else if (ustrcmp(SG_STRING_VALUE(s), "last-modified") == 0) {
    set_builtin_header3(last_modified, "Last-Modified", "last-modified");
  } else if (ustrcmp(SG_STRING_VALUE(s), "content-range") == 0) {
    set_builtin_header3(content_range, "Content-Range", "content-range");
  } else if (ustrcmp(SG_STRING_VALUE(s), "accept-ranges") == 0) {
    set_builtin_header3(accept_ranges, "Accept-Ranges", "accept-ranges");
  } else if (ustrcmp(SG_STRING_VALUE(s), "www-authenticate") == 0) {
    set_builtin_header3(www_authenticate,
			"WWW-Authenticate", "www-authenticate");
  } else if (ustrcmp(SG_STRING_VALUE(s), "expires") == 0) {
    set_builtin_header(expires, "Expires");
  } else if (ustrcmp(SG_STRING_VALUE(s), "etag") == 0) {
    set_builtin_header(etag, "ETag");
  } else {
    unsigned char *n;
    ngx_table_elt_t *e;
    e = (ngx_table_elt_t *)ngx_list_push(&r->headers_out.headers);
    n = (unsigned char *)Sg_Utf32sToUtf8s(name);
    e->key.data = n;
    e->key.len = ngx_strlen(n);
    e->value.data = uc;
    e->value.len = ngx_strlen(uc);
    e->lowcase_key = (unsigned char *)Sg_Utf32sToUtf8s(s);
  }

#undef set_builtin_header
#undef set_builtin_header3
}

/* This takes O(n)... */
static void ngx_remove_header_from_list(ngx_http_request_t *r,
					unsigned char *name)
{
  ngx_pool_t *pool = r->pool;
  ngx_list_part_t *part = &r->headers_out.headers.part;
  ngx_table_elt_t *data = part->elts;
  ngx_uint_t i;

  i = 0;
  while (1) {
    ngx_table_elt_t *e;
    if (i >= part->nelts) {
      if (part->next == NULL) break;
      part = part->next;
      data = part->elts;
      i = 0;
    }
    e = &data[i++];
    
    if (ngx_strcmp(e->lowcase_key, name) == 0) {
      /* okay remove it */
      ngx_memmove(data + i - 1, data + i, sizeof(ngx_table_elt_t));
      i--;
      part->nelts--;
      ngx_pfree(pool, e);
    }
  }
}

static void ngx_remove_header(SgObject res, SgObject name)
{
  ngx_http_request_t *r = SG_NGINX_RESPONSE(res)->request;
  SgObject s = Sg_StringDownCase(SG_STRING(name));

  SG_NGINX_RESPONSE(res)->headers = SG_FALSE; /* reset */
#define remove_builtin(field)				\
  do {							\
    if (r->headers_out. field) {			\
      ngx_pfree(r->pool, r->headers_out. field);	\
      r->headers_out. field = NULL;			\
    }							\
  } while (0)
  
  if (ustrcmp(SG_STRING_VALUE(s), "server") == 0) {
    /* The Server: header must be set by configuration so ignore */
  } else if (ustrcmp(SG_STRING_VALUE(s), "date") == 0) {
    if (r->headers_out.date) {
      ngx_remove_header_from_list(r, (unsigned char *)"date");
    }
    remove_builtin(date);
  } else if (ustrcmp(SG_STRING_VALUE(s), "content-length") == 0) {
    /* The Server: header must be set by the content */
  } else if (ustrcmp(SG_STRING_VALUE(s), "content-encoding") == 0) {
    remove_builtin(content_encoding);
  } else if (ustrcmp(SG_STRING_VALUE(s), "location") == 0) {
    remove_builtin(location);
  } else if (ustrcmp(SG_STRING_VALUE(s), "refresh") == 0) {
    remove_builtin(refresh);
  } else if (ustrcmp(SG_STRING_VALUE(s), "last-modified") == 0) {
    remove_builtin(last_modified);
  } else if (ustrcmp(SG_STRING_VALUE(s), "content-range") == 0) {
    remove_builtin(content_range);
  } else if (ustrcmp(SG_STRING_VALUE(s), "accept-ranges") == 0) {
    remove_builtin(accept_ranges);
  } else if (ustrcmp(SG_STRING_VALUE(s), "www-authenticate") == 0) {
    remove_builtin(www_authenticate);
  } else if (ustrcmp(SG_STRING_VALUE(s), "expires") == 0) {
    remove_builtin(expires);
  } else if (ustrcmp(SG_STRING_VALUE(s), "etag") == 0) {
    remove_builtin(etag);
  } else {
    ngx_remove_header_from_list(r, (unsigned char *)Sg_Utf32sToUtf8s(s));
  }
}

/* response operation */
static SgObject nginx_response_add_header(SgObject *argv, int argc, void *data)
{
  if (argc != 3) {
    Sg_WrongNumberOfArgumentsViolation(SG_INTERN("nginx-response?"), 1,
				       argc, SG_NIL);
  }
  if (!SG_NGINX_RESPONSEP(argv[0])) {
    Sg_WrongTypeOfArgumentViolation(SG_INTERN("nginx-response-header-add!"),
				    SG_INTERN("nginx-response"),
				    argv[0], SG_NIL);
  }
  if (!SG_STRING(argv[1])) {
    Sg_WrongTypeOfArgumentViolation(SG_INTERN("nginx-response-header-add!"),
				    SG_INTERN("string"),
				    argv[1], SG_NIL);
  }
  if (!SG_STRING(argv[2])) {
    Sg_WrongTypeOfArgumentViolation(SG_INTERN("nginx-response-header-add!"),
				    SG_INTERN("string"),
				    argv[2], SG_NIL);
  }
  ngx_add_header(argv[0], argv[1], argv[2]);
  return argv[0];
}
static SG_DEFINE_SUBR(nginx_response_add_header_stub, 3, 0,
		      nginx_response_add_header, SG_FALSE, NULL);

static SgObject nginx_response_set_header(SgObject *argv, int argc, void *data)
{
  if (argc != 3) {
    Sg_WrongNumberOfArgumentsViolation(SG_INTERN("nginx-response?"), 1,
				       argc, SG_NIL);
  }
  if (!SG_NGINX_RESPONSEP(argv[0])) {
    Sg_WrongTypeOfArgumentViolation(SG_INTERN("nginx-response-header-set!"),
				    SG_INTERN("nginx-response"),
				    argv[0], SG_NIL);
  }
  if (!SG_STRING(argv[1])) {
    Sg_WrongTypeOfArgumentViolation(SG_INTERN("nginx-response-header-set!"),
				    SG_INTERN("string"),
				    argv[1], SG_NIL);
  }
  if (!SG_STRING(argv[2])) {
    Sg_WrongTypeOfArgumentViolation(SG_INTERN("nginx-response-header-set!"),
				    SG_INTERN("string"),
				    argv[2], SG_NIL);
  }
  ngx_remove_header(argv[0], argv[1]);
  ngx_add_header(argv[0], argv[1], argv[2]);
  return argv[0];
}
static SG_DEFINE_SUBR(nginx_response_set_header_stub, 3, 0,
		      nginx_response_set_header, SG_FALSE, NULL);

static SgObject nginx_response_del_header(SgObject *argv, int argc, void *data)
{
  if (argc != 2) {
    Sg_WrongNumberOfArgumentsViolation(SG_INTERN("nginx-response?"), 1,
				       argc, SG_NIL);
  }
  if (!SG_NGINX_RESPONSEP(argv[0])) {
    Sg_WrongTypeOfArgumentViolation(SG_INTERN("nginx-response-header-remove!"),
				    SG_INTERN("nginx-response"),
				    argv[0], SG_NIL);
  }
  if (!SG_STRING(argv[1])) {
    Sg_WrongTypeOfArgumentViolation(SG_INTERN("nginx-response-header-remove!"),
				    SG_INTERN("string"),
				    argv[1], SG_NIL);
  }
  ngx_remove_header(argv[0], argv[1]);
  return argv[0];
}
static SG_DEFINE_SUBR(nginx_response_del_header_stub, 2, 0,
		      nginx_response_del_header, SG_FALSE, NULL);


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

/* Ports */
static SgClass *port_cpl[] = {
  SG_CLASS_PORT,
  SG_CLASS_TOP,
  NULL
};

/* close */
static int port_close(SgObject self)
{
  SG_PORT(self)->closed = SG_PORT_CLOSED;
  return TRUE;
}

/* request input port */
typedef struct
{
  SgPort              parent;
  ngx_http_request_t *request;
  ngx_chain_t        *current_chain;
  unsigned char      *current_buffer;
  SgObject            temp_inp;
} SgRequestInputPort;
SG_CLASS_DECL(Sg_RequestInputPortClass);
SG_DEFINE_BUILTIN_CLASS(Sg_RequestInputPortClass, Sg_DefaultPortPrinter,
			NULL, NULL, NULL, port_cpl);
#define SG_CLASS_REQUEST_INPUT_PORT (&Sg_RequestInputPortClass)
#define SG_REQUEST_INPUT_PORT(obj) ((SgRequestInputPort *)obj)
#define SG_REQUEST_INPUT_PORTP(obj) SG_XTYPEP(obj, SG_CLASS_REQUEST_INPUT_PORT)

static int request_in_close(SgObject self)
{
  SgRequestInputPort *port = SG_REQUEST_INPUT_PORT(self);
  port_close(self);
  if (!SG_PORTP(port->temp_inp)) {
    Sg_ClosePort(port->temp_inp);
  }
  return TRUE;
}

/* 
   handling request body:
   http://nginx.org/en/docs/dev/development_guide.html#http_request_body
 */
static int64_t request_in_read_u8(SgObject self, uint8_t *buf, int64_t size)
{
  SgRequestInputPort *port = SG_REQUEST_INPUT_PORT(self);
  ngx_http_request_t *r = port->request;
  int64_t read = 0;

  if (r->request_body) {
    if (SG_UNDEFP(port->temp_inp)) {
      if (port->current_chain == NULL) {
	port->current_chain = r->request_body->bufs;
	if (port->current_chain) {
	  port->current_buffer = port->current_chain->buf->pos;
	}
      }
      ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
		    "'sagittarius': reading from buffer %p",
		    port->current_buffer);
    
      if (port->current_buffer) {
	for (; read < size; read++) {
	  if (port->current_buffer == port->current_chain->buf->last) {
	    if (!port->current_chain->buf->last_buf) {
	      port->current_chain = port->current_chain->next;
	      port->current_buffer = port->current_chain->buf->pos;
	    } else {
	      break;
	    }
	  }
	  buf[read] = *port->current_buffer++;
	}
      }
    }
    if (read != size) {
      if (SG_UNDEFP(port->temp_inp)) {
	if (r->request_body->temp_file) {
	  SgObject file;
	  /* open file input port */
	  ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
			"'sagittarius': temp file %V",
			&r->request_body->temp_file->file.name);
	  file = Sg_MakeFileFromFD(r->request_body->temp_file->file.fd);
	  port->temp_inp =
	    Sg_MakeFileBinaryInputPort(SG_FILE(file), SG_BUFFER_MODE_BLOCK);
	} else {
	  port->temp_inp = SG_FALSE;
	}
      }
      if (!SG_FALSEP(port->temp_inp)) {
	/* 
	   it's safe to do this since the owner port is locked.
	   we don't check if the input is exhausted or not here.
	*/
	read += Sg_ReadbUnsafe(SG_PORT(port->temp_inp), buf+read, size-read);
      }
    }
  }
  return read;
}

static int64_t request_in_read_u8_all(SgObject self, uint8_t **buf)
{
  uint8_t b[BUFFER_SIZE];
  SgPort *buffer = NULL;
  SgBytePort byp;
  int64_t r = 0, c;

  while (1) {
    c = request_in_read_u8(self, b, BUFFER_SIZE);
    if (buffer == NULL && c > 0) {
      buffer = SG_PORT(Sg_InitByteArrayOutputPort(&byp, BUFFER_SIZE));
    }
    if (buffer) {
      Sg_WritebUnsafe(buffer, b, 0, c);
    }
    r += c;
    if (c != BUFFER_SIZE) break;
  }
  if (buffer) {
    *buf = Sg_GetByteArrayFromBinaryPort(&byp);
  }
  return r;
}

static SgPortTable request_in_table = {
  NULL,				/* no flush */
  request_in_close,
  NULL,				/* no ready */
  NULL,				/* lock */
  NULL,				/* unlock */
  NULL,				/* position (should we?) */
  NULL,				/* set position (should we?) */
  NULL,				/* open (not used?)*/
  request_in_read_u8,		/* read u8 */
  request_in_read_u8_all,	/* read all */
  NULL,				/* put array */
  NULL,				/* read str */
  NULL,				/* write str */
};

static SgObject make_request_input_port(ngx_http_request_t *request)
{
  SgRequestInputPort *port = SG_NEW(SgRequestInputPort);
  SG_INIT_PORT(port, SG_CLASS_REQUEST_INPUT_PORT, SG_INPUT_PORT,
	       &request_in_table, SG_FALSE);
  port->request = request;
  port->current_chain = NULL;
  port->current_buffer = NULL;
  port->temp_inp = SG_UNDEF;
  return SG_OBJ(port);
}

/* response output port */
typedef struct
{
  SgPort              parent;
  ngx_chain_t        *root;
  ngx_chain_t        *buffer;
  ngx_http_request_t *request;
} SgResponseOutputPort;

SG_CLASS_DECL(Sg_ResponseOutputPortClass);

SG_DEFINE_BUILTIN_CLASS(Sg_ResponseOutputPortClass, Sg_DefaultPortPrinter,
			NULL, NULL, NULL, port_cpl);
#define SG_CLASS_RESPONSE_OUTPUT_PORT (&Sg_ResponseOutputPortClass)
#define SG_RESPONSE_OUTPUT_PORT(obj)  ((SgResponseOutputPort *)obj)
#define SG_RESPONSE_OUTPUT_PORTP(obj)		\
  SG_XTYPEP(obj, SG_CLASS_RESPONSE_OUTPUT_PORT)
#define SG_RESPONSE_OUTPUT_PORT_ROOT(obj)	\
  (SG_RESPONSE_OUTPUT_PORT(obj)->root)
#define SG_RESPONSE_OUTPUT_PORT_BUFFER(obj)	\
  (SG_RESPONSE_OUTPUT_PORT(obj)->buffer)
#define SG_RESPONSE_OUTPUT_PORT_REQUEST(obj)	\
  (SG_RESPONSE_OUTPUT_PORT(obj)->request)

static void allocate_buffer(SgObject self)
{
  ngx_http_request_t *r = SG_RESPONSE_OUTPUT_PORT_REQUEST(self);
  ngx_chain_t *c = (ngx_chain_t *)ngx_pcalloc(r->pool, sizeof(ngx_chain_t));
  ngx_buf_t *b = (ngx_buf_t *)ngx_pcalloc(r->pool, sizeof(ngx_buf_t));

  ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
		"'sagittarius': Allocating response buffer");
  if (c == NULL || b == NULL) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
		  "'sagittarius': Failed to allocate response buffer.");
    raise_nginx_error(SG_INTERN("put-u8"),
		      SG_MAKE_STRING("'sagittarius': [Internal]"
				     " Failed to allocate response buffer"),
		      Sg_MakeNginxError(NGX_HTTP_INTERNAL_SERVER_ERROR),
		      SG_NIL);
  }
  /* TODO maybe we can use nginx allocator? */
  b->pos = b->last = SG_NEW_ATOMIC2(unsigned char *, BUFFER_SIZE);
  b->last_buf = 1;		/* may be reset later */
  b->memory = 1;
  c->buf = b;
  c->next = NULL;
  if (SG_RESPONSE_OUTPUT_PORT_BUFFER(self)) {
    SG_RESPONSE_OUTPUT_PORT_BUFFER(self)->next = c;
  }
  SG_RESPONSE_OUTPUT_PORT_BUFFER(self) = c;
  /* ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, */
  /* 		"'sagittarius': %d bytes allocated", BUFFER_SIZE); */
}

static int64_t response_out_put_u8_array(SgObject self, uint8_t *ba,
					 int64_t size)
{
  int64_t written;
  ngx_buf_t *buf;		/*  current buffer */
  unsigned char *be;
  
  if (!SG_RESPONSE_OUTPUT_PORT_BUFFER(self)) {
    allocate_buffer(self);
    SG_RESPONSE_OUTPUT_PORT_ROOT(self) = SG_RESPONSE_OUTPUT_PORT_BUFFER(self);
  }
  buf = SG_RESPONSE_OUTPUT_PORT_BUFFER(self)->buf;
  be = buf->pos + BUFFER_SIZE;
  for (written = 0; written < size; written++) {
    if (buf->last == be) {
      buf->last_buf = 0;
      allocate_buffer(self);
      buf = SG_RESPONSE_OUTPUT_PORT_BUFFER(self)->buf;
    }
    *buf->last++ = *ba++;
  }
  return written;
}

#define response_out_close port_close

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
  SgObject sym, lib;
  ngx_log_error(NGX_LOG_DEBUG, cycle->log, 0,
		"'sagittarius': Initialising Sagittarius process");
  /* Initialise the sagittarius VM */
  Sg_Init();

  sym = SG_INTERN("(sagittarius nginx internal)");
  ngx_log_error(NGX_LOG_DEBUG, cycle->log, 0,
		"'sagittarius': "
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

  Sg_InsertBinding(SG_LIBRARY(lib), SG_INTERN("nginx-response-header-add!"),
		   &nginx_response_add_header_stub);
  SG_PROCEDURE_NAME(&nginx_response_add_header_stub) =
    SG_MAKE_STRING("nginx-response-header-add!");
  SG_PROCEDURE_TRANSPARENT(&nginx_response_add_header_stub) =
    SG_SUBR_SIDE_EFFECT;

  Sg_InsertBinding(SG_LIBRARY(lib), SG_INTERN("nginx-response-header-set!"),
		   &nginx_response_set_header_stub);
  SG_PROCEDURE_NAME(&nginx_response_set_header_stub) =
    SG_MAKE_STRING("nginx-response-header-set!");
  SG_PROCEDURE_TRANSPARENT(&nginx_response_set_header_stub) =
    SG_SUBR_SIDE_EFFECT;

  Sg_InsertBinding(SG_LIBRARY(lib), SG_INTERN("nginx-response-header-remove!"),
		   &nginx_response_del_header_stub);
  SG_PROCEDURE_NAME(&nginx_response_del_header_stub) =
    SG_MAKE_STRING("nginx-response-header-remove!");
  SG_PROCEDURE_TRANSPARENT(&nginx_response_del_header_stub) =
    SG_SUBR_SIDE_EFFECT;

  
#define INSERT_ACCESSOR(name, cname, effect)				\
  do {									\
    Sg_InsertBinding(SG_LIBRARY(lib), SG_INTERN(name),			\
		     &SG_CPP_CAT(cname, _stub));			\
    SG_PROCEDURE_NAME(&SG_CPP_CAT(cname, _stub)) = SG_MAKE_STRING(name); \
    SG_PROCEDURE_TRANSPARENT(&SG_CPP_CAT(cname, _stub)) = effect;	\
  } while (0)
  
  INSERT_ACCESSOR("nginx-request-method", nginx_request_method,
		  SG_PROC_NO_SIDE_EFFECT);
  INSERT_ACCESSOR("nginx-request-uri", nginx_request_uri,
		  SG_PROC_NO_SIDE_EFFECT);
  INSERT_ACCESSOR("nginx-request-headers", nginx_request_headers,
		  SG_PROC_NO_SIDE_EFFECT);
  INSERT_ACCESSOR("nginx-request-input-port", nginx_request_body,
		  SG_PROC_NO_SIDE_EFFECT);
#define HEADER_FIELD(name, cname, n)		\
  INSERT_ACCESSOR("nginx-request-" #name, SG_CPP_CAT(nginx_request_, cname), \
		  SG_PROC_NO_SIDE_EFFECT);
#include "builtin_request_fields.inc"
#undef HEADER_FIELD
  
  INSERT_ACCESSOR("nginx-response-content-type", nginx_response_content_type,
		  SG_PROC_NO_SIDE_EFFECT);
  INSERT_ACCESSOR("nginx-response-content-type-set!",
		  nginx_response_content_type_set, SG_SUBR_SIDE_EFFECT);
  INSERT_ACCESSOR("nginx-response-headers", nginx_response_headers,
		  SG_PROC_NO_SIDE_EFFECT);
  INSERT_ACCESSOR("nginx-response-output-port", nginx_response_output_port,
		  SG_PROC_NO_SIDE_EFFECT);

#undef INSERT_ACCESSOR

  SG_INIT_CONDITION(SG_CLASS_NGINX_ERROR, lib,
		    "&nginx-error", nginx_error_slots);
  SG_INIT_CONDITION_PRED(SG_CLASS_NGINX_ERROR, lib, "nginx-error?");
  SG_INIT_CONDITION_CTR(SG_CLASS_NGINX_ERROR, lib, "make-nginx-error", 1);
  SG_INIT_CONDITION_ACC(nginx_error_status, lib, "&nginx-error-status");

  ngx_log_error(NGX_LOG_DEBUG, cycle->log, 0,
		"'sagittarius': "
		"'(sagittarius nginx internal)' library is initialised");
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
  if (sg_conf->library.len == 0) {
    ngx_log_error(NGX_LOG_ERR, cf->log, 0,
		  "'sagittarius': 'library' must be specified");
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
  ngx_str_t *value, *elts, *prefix;
  
  sg_conf = (ngx_http_sagittarius_conf_t *)conf;
  value = cf->args->elts;

  if (ngx_strcmp(value[0].data, "load_path") == 0) {
    if (cf->args->nelts < 2) {
      ngx_log_error(NGX_LOG_ERR, cf->log, 0,
		    "'sagittarius': "
		    "'load_path' must take at least one argument");
      return NGX_CONF_ERROR;
    }
    if (!sg_conf->load_paths) {
      sg_conf->load_paths = ngx_array_create(cf->pool, 0, sizeof(ngx_str_t));
    }
    prefix = &cf->cycle->prefix;
    elts = ngx_array_push_n(sg_conf->load_paths, cf->args->nelts-1);

    for (i = 0; i < cf->args->nelts-1; i++) {
      ngx_str_t *s, *v;
      v = &value[i+1];
      s = elts+i;
      s->data = v->data;
      s->len = v->len;
      if (ngx_get_full_name(cf->pool, prefix, s) != NGX_OK) {
	ngx_log_error(NGX_LOG_ERR, cf->log, 0,
		      "'sagittarius': 'load_path' failed to get load path ");
	return NGX_CONF_ERROR;
      }
      ngx_log_error(NGX_LOG_DEBUG, cf->log, 0,
		    "'sagittarius': 'load_path' %V", s);

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
  ngx_str_t nstr = ngx_null_string;
  ngx_http_sagittarius_conf_t *conf;
  conf = (ngx_http_sagittarius_conf_t *)
    ngx_palloc(cf->pool, sizeof(ngx_http_sagittarius_conf_t));
  if (!conf) {
    return NGX_CONF_ERROR;
  }
  conf->library = nstr;
  conf->procedure = nstr;
  conf->load_paths = NULL;
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
  ngxReq->method = ngx_str_to_string(&req->method_name);
  ngxReq->uri = ngx_str_to_string(&req->uri);
  ngxReq->headers = SG_FALSE;	/* initialise lazily */
  ngxReq->cookies = SG_FALSE;	/* initialise lazily */
  ngxReq->body = make_request_input_port(req);
  ngxReq->rawNginxRequest = req;
  return SG_OBJ(ngxReq);
}

static SgObject make_nginx_response(ngx_http_request_t *req)
{
  SgNginxResponse *ngxRes = SG_NEW(SgNginxResponse);
  SG_SET_CLASS(ngxRes, SG_CLASS_NGINX_RESPONSE);
  ngxRes->headers = SG_FALSE;	/* just a cache */
  ngxRes->out = make_response_output_port(req);
  ngxRes->request = req;
  req->headers_out.content_type.len = sizeof("application/octet-stream") - 1;
  req->headers_out.content_type.data = (u_char *) "application/octet-stream";
  
  return SG_OBJ(ngxRes);
}

static ngx_int_t init_base_library(ngx_log_t *log);
static SgObject setup_load_path(volatile SgVM *vm,
				ngx_log_t *log,
				ngx_http_sagittarius_conf_t *sg_conf);
static SgObject retrieve_procedure(ngx_log_t *log,
				   ngx_http_sagittarius_conf_t *sg_conf);
static off_t compute_content_length(ngx_chain_t *out);

static void ngx_http_request_body_init(ngx_http_request_t *r)
{
  ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
		"'sagittarius': request_body is initialised %p",
		r->request_body);
  
}

/* Main handler */
static ngx_int_t ngx_http_sagittarius_handler(ngx_http_request_t *r)
{
  SgObject req, resp, saved_loadpath, proc;
  volatile SgVM *vm;
  volatile SgObject status;
  ngx_int_t rc;
  ngx_chain_t *out;
  ngx_http_sagittarius_conf_t *sg_conf;
  
  ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
		"'sagittarius': Handling Sagittarius request for %V %V.",
		&r->method_name, &r->uri);
  sg_conf = (ngx_http_sagittarius_conf_t *)
    ngx_http_get_module_loc_conf(r, ngx_http_sagittarius_module);
  vm = Sg_VM();
  saved_loadpath = setup_load_path(vm, r->connection->log, sg_conf);
  if (SG_UNDEFP(nginx_dispatch)) {
    if (init_base_library(r->connection->log) != NGX_OK) {
      ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
      return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
  }
  proc = retrieve_procedure(r->connection->log, sg_conf);
  if (!SG_PROCEDUREP(proc)) {
    ngx_http_finalize_request(r, NGX_HTTP_NOT_FOUND);
    return NGX_HTTP_NOT_FOUND;
  }

  if (r->headers_in.content_length_n > 0 || r->headers_in.chunked) {
    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
		  "'sagittarius': reading client body %d, %p",
		  r->headers_in.content_length_n,
		  r->read_event_handler);
    do {
      /* 
	 Retrying doesn't help us for Expect: 100-continue case.
	 Not sure how we should handle that... (I hope it's only curl)
       */
      rc = ngx_http_read_client_request_body(r, ngx_http_request_body_init);
      if (rc != NGX_OK && rc != NGX_AGAIN) {
	ngx_http_finalize_request(r, rc);
	return rc;
      }
    } while (rc == NGX_AGAIN);
  }
  
  /* TODO call initialiser with configuration in location */
  req = make_nginx_request(r);
  resp = make_nginx_response(r);
  SG_UNWIND_PROTECT {
    status = Sg_Apply3(nginx_dispatch, proc, req, resp);
  } SG_WHEN_ERROR {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
		  "'sagittarius': Failed to execute nginx-dispatch-request");
    vm->loadPath = saved_loadpath;
    ngx_http_discard_request_body(r);
    ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
    return NGX_HTTP_INTERNAL_SERVER_ERROR;    
  } SG_END_PROTECT;

  vm->loadPath = saved_loadpath;
    
  /* The procedure didn't consume the request, so discard it */
  rc = ngx_http_discard_request_body(r);

  if (rc != NGX_OK && rc != NGX_AGAIN) {
    ngx_http_finalize_request(r, rc);
    return rc;
  }
  
  if (SG_INTP(status)) {
    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
		  "'sagittarius': Returned status is %d, content-type is '%V'.",
		  SG_INT_VALUE(status),
		  &r->headers_out.content_type);

    /* convert Scheme response to C response */
    out = SG_RESPONSE_OUTPUT_PORT_ROOT(SG_NGINX_RESPONSE(resp)->out);
    r->headers_out.status = SG_INT_VALUE(status);
    r->headers_out.content_length_n = compute_content_length(out);

    rc = ngx_http_send_header(r);
  
    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
      ngx_http_finalize_request(r, rc);
      return rc;
    }
    if (out) {
      rc = ngx_http_output_filter(r, out);
      ngx_http_finalize_request(r, rc);
      return rc;
    } else {
      ngx_http_finalize_request(r, rc);
      return NGX_OK;
    }
  } else {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
		  "'sagittarius': Scheme program returned non fixnum.");
    ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
  }
}

static ngx_int_t init_base_library(ngx_log_t *log)
{
  SgObject sym, lib, o;
  ngx_log_error(NGX_LOG_DEBUG, log, 0,
		"'sagittarius': Initialising '(sagittarius nginx)' library");
  sym = SG_INTERN("(sagittarius nginx)");
  lib = Sg_FindLibrary(sym, FALSE);
  if (SG_FALSEP(lib)) {
    ngx_log_error(NGX_LOG_ERR, log, 0,
		  "'sagittarius': "
		  "Failed to find '(sagittarius nginx)' library");
    return NGX_ERROR;
  }
  ngx_log_error(NGX_LOG_DEBUG, log, 0,
		"'sagittarius': Retrieving nginx-dispatch-request");
  o = Sg_FindBinding(lib, SG_INTERN("nginx-dispatch-request"), SG_UNBOUND);
  if (SG_UNBOUNDP(o)) {
    ngx_log_error(NGX_LOG_ERR, log, 0,
		  "'sagittarius': Failed to retrieve nginx-dispatch-request");
    return NGX_ERROR;
  }
  nginx_dispatch = SG_GLOC_GET(SG_GLOC(o));
  ngx_log_error(NGX_LOG_DEBUG, log, 0,
		"'sagittarius': '(sagittarius nginx)' library is initialised");
  return NGX_OK;
}

static SgObject setup_load_path(volatile SgVM *vm,
				ngx_log_t *log,
				ngx_http_sagittarius_conf_t *sg_conf)
{
  SgObject saved_loadpath = vm->loadPath;
  ngx_uint_t i;
  ngx_str_t *value = sg_conf->load_paths->elts;

  for (i = 0; i < sg_conf->load_paths->nelts; i++) {
    ngx_str_t *s = &value[i];
    ngx_log_error(NGX_LOG_DEBUG, log, 0, "'sagittarius': load path: %V", s);
    /* we prepend the load path */
    Sg_AddLoadPath(ngx_str_to_string(s), FALSE);
  }

  return saved_loadpath;
}

static SgObject retrieve_procedure(ngx_log_t *log,
				   ngx_http_sagittarius_conf_t *sg_conf)
{
  SgObject lib, proc;
  lib = Sg_FindLibrary(Sg_Intern(ngx_str_to_string(&sg_conf->library)), FALSE);
  if (SG_FALSEP(lib)) {
    ngx_log_error(NGX_LOG_ERR, log, 0,
		  "'sagittarius': Web application library '%V' not found.",
		  &sg_conf->library);
    return SG_FALSE;
  }
  proc = Sg_FindBinding(SG_LIBRARY(lib),
			Sg_Intern(ngx_str_to_string(&sg_conf->procedure)),
			SG_UNBOUND);
  if (SG_UNBOUNDP(proc)) {
    ngx_log_error(NGX_LOG_ERR, log, 0,
		  "'sagittarius': Web application procedure '%V' not found.",
		  &sg_conf->procedure);
    return SG_FALSE;
  }
  return SG_GLOC_GET(SG_GLOC(proc));
}

static off_t compute_content_length(ngx_chain_t *out)
{
  ngx_chain_t *c;
  off_t count;

  if (!out) return 0;

  count = 0;
  for (c = out; c; c= c->next) {
    count += ngx_buf_size(c->buf);
  }
  return count;
}
