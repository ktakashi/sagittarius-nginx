/*
 * Copyright (c) 2018 Takashi Kato <ktakashi@ymail.com>
 * See Licence.txt for terms and conditions of use
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include <sagittarius.h>
/* 
   References:
   - https://www.evanmiller.org/nginx-modules-guide.html
   - https://www.nginx.com/resources/wiki/extending/api/
*/
/* 
The configuration looks like this
sagittarius $entry-point [$init] [$cleanup] {
  load_path foo/bar /baz/; # up to n path (haven't decided the number...)
  library "(your web library)";
  parameter var0 value0;
  parameter var1 varlu1;
  filter name1 "do-filter" 0 "(your filter library)";
  filter_parameter name1 key1 value1; # filter parameter for filter 'name1'
  # if the library is the same as the web app library
  filter name2 "do-filter" 1;
  thread_pool_name pool_name; # refering the name of thread pool
}

We do not use SgObject here. I'm not sure when the configuration parsing 
happens and the initialisation of Sagittarius happens on the creation of
worker process.
 */
typedef struct
{
  ngx_array_t *load_paths;	/* array of ngx_str_t */
  ngx_str_t library;		/* webapp library */
  ngx_str_t procedure;		/* entry point */
  ngx_str_t init_proc;		/* initialisation */
  ngx_str_t cleanup_proc;	/* clean up (how?) */
  /* context parameters, this is a temporary storage */
  ngx_array_t *parameters;	/* array of ngx_table_elt_t */
  ngx_array_t *filters;		/* array of sagittarius_filter_t */
  ngx_str_t pool_name;		/* thread pool name */
} ngx_http_sagittarius_conf_t;

typedef struct
{
  ngx_str_t name;
  ngx_str_t procedure;
  int order;
  int has_library;
  ngx_str_t library;
  ngx_array_t *parameters;
} sagittarius_filter_t;

static char* ngx_http_sagittarius_block(ngx_conf_t *cf,
					ngx_command_t *cmd,
					void *conf);
static char* ngx_http_sagittarius(ngx_conf_t *cf,
				  ngx_command_t *dummy,
				  void *conf);
static ngx_int_t ngx_http_sagittarius_preconfiguration(ngx_conf_t *cf);
static ngx_int_t ngx_http_sagittarius_postconfiguration(ngx_conf_t *cf);
static void* ngx_http_sagittarius_create_loc_conf(ngx_conf_t *cf);
static char* ngx_http_sagittarius_merge_loc_conf(ngx_conf_t *cf,
						 void *p, void *c);

static ngx_int_t ngx_http_sagittarius_init_process(ngx_cycle_t *cycle);
static void      ngx_http_sagittarius_exit_process(ngx_cycle_t *cycle);

static ngx_command_t ngx_http_sagittarius_commands[] = {
  {
    ngx_string("sagittarius"),
    NGX_HTTP_LOC_CONF | NGX_CONF_BLOCK | NGX_CONF_TAKE123,
    ngx_http_sagittarius_block,
    NGX_HTTP_LOC_CONF_OFFSET,
    0,
    NULL
  }
};

static ngx_http_module_t ngx_http_sagittarius_module_ctx = {
  ngx_http_sagittarius_preconfiguration, /* preconfiguration */
  ngx_http_sagittarius_postconfiguration, /* postconfiguration */
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
  ngx_http_sagittarius_exit_process, /* exit process */
  NULL,				/* exit master */
  NGX_MODULE_V1_PADDING
};

#define ngx_str_to_string(s) Sg_Utf8sToUtf32s((const char *)(s)->data, (s)->len)
#define BUFFER_SIZE SG_PORT_DEFAULT_BUFFER_SIZE

typedef struct
{
  SG_HEADER;
  /* application context path. i.e. location path */
  SgObject path;
  SgObject parameters;		/* parameters */
  /* internal use only */
  SgObject library;		/* context library */
  SgObject cleanup;		/* cleanup procedure if exists */
  SgObject procedure;		/* entry point */
} SgNginxContext;
SG_CLASS_DECL(Sg_NginxContextClass)
#define SG_CLASS_NGINX_CONTEXT (&Sg_NginxContextClass)
#define SG_NGINX_CONTEXT(obj)  ((SgNginxContext *)obj)
#define SG_NGINX_CONTEXTP(obj) SG_XTYPEP(obj, SG_CLASS_NGINX_CONTEXT)
static void nginx_context_printer(SgObject self, SgPort *port,
				  SgWriteContext *ctx)
{
  Sg_Printf(port, UC("#<nginx-context %A>"), SG_NGINX_CONTEXT(self)->path);
}
SG_DEFINE_BUILTIN_CLASS_SIMPLE(Sg_NginxContextClass, nginx_context_printer);

static SgObject nc_path(SgNginxContext *nc)
{
  return nc->path;
}
static SgObject nc_parameters(SgNginxContext *nc)
{
  return nc->parameters;
}
static SgSlotAccessor nc_slots[] = {
  SG_CLASS_SLOT_SPEC("path",      0, nc_path, NULL),
  SG_CLASS_SLOT_SPEC("parameters",1, nc_parameters, NULL),
  { { NULL } }
};

static SgObject nginx_context_p(SgObject *argv, int argc, void *data)
{
  if (argc != 1) {
    Sg_WrongNumberOfArgumentsViolation(SG_INTERN("nginx-context?"), 1,
				       argc, SG_NIL);
  }
  return SG_MAKE_BOOL(SG_NGINX_CONTEXT(argv[0]));
}
static SG_DEFINE_SUBR(nginx_context_p_stub, 1, 0, nginx_context_p,
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

SG_DEFINE_GETTER("nginx-context-path", "nginx-context",
		 SG_NGINX_CONTEXTP, nc_path, SG_NGINX_CONTEXT,
		 nginx_context_path);
SG_DEFINE_GETTER("nginx-context-parameters", "nginx-context",
		 SG_NGINX_CONTEXTP, nc_parameters, SG_NGINX_CONTEXT,
		 nginx_context_parameters);
static SgObject nginx_context_parameter_ref(SgObject *argv, int argc,
					    void *data)
{
  if (argc != 2) {
    Sg_WrongNumberOfArgumentsViolation(SG_INTERN("nginx-context-parameter-ref"),
				       2, argc, SG_NIL);
  }
  if (!SG_NGINX_CONTEXTP(argv[0])) {
    Sg_WrongTypeOfArgumentViolation(SG_INTERN("nginx-context-parameter-ref"),
				    SG_INTERN("nginx-context"),
				    argv[0], SG_NIL);
  }
  if (!SG_STRINGP(argv[1])) {
    Sg_WrongTypeOfArgumentViolation(SG_INTERN("nginx-context-parameter-ref"),
				    SG_INTERN("string"),
				    argv[1], SG_NIL);
  }
  return Sg_HashTableRef(SG_HASHTABLE(SG_NGINX_CONTEXT(argv[0])->parameters),
			 argv[1], SG_FALSE);
}
static SG_DEFINE_SUBR(nginx_context_parameter_ref_stub, 2, 0,
		      nginx_context_parameter_ref, SG_FALSE, NULL);

typedef struct
{
  SG_HEADER;
  SgObject name;
  SgObject parameters;		/* parameters */
} SgNginxFilterContext;
SG_CLASS_DECL(Sg_NginxFilterContextClass)
#define SG_CLASS_NGINX_FILTER_CONTEXT (&Sg_NginxFilterContextClass)
#define SG_NGINX_FILTER_CONTEXT(obj)  ((SgNginxFilterContext *)obj)
#define SG_NGINX_FILTER_CONTEXTP(obj)		\
  SG_XTYPEP(obj, SG_CLASS_NGINX_FILTER_CONTEXT)
static void nginx_filter_context_printer(SgObject self, SgPort *port,
					 SgWriteContext *ctx)
{
  Sg_Printf(port, UC("#<nginx-filter-context %A>"),
	    SG_NGINX_FILTER_CONTEXT(self)->name);
}
SG_DEFINE_BUILTIN_CLASS_SIMPLE(Sg_NginxFilterContextClass,
			       nginx_filter_context_printer);

static SgObject nf_name(SgNginxFilterContext *nf)
{
  return nf->name;
}

static SgObject nf_parameters(SgNginxFilterContext *nf)
{
  return nf->parameters;
}

static SgSlotAccessor nf_slots[] = {
  SG_CLASS_SLOT_SPEC("name",      0, nf_name, NULL),
  SG_CLASS_SLOT_SPEC("parameters",1, nf_parameters, NULL),
  { { NULL } }
};

static SgObject nginx_filter_context_p(SgObject *argv, int argc, void *data)
{
  if (argc != 1) {
    Sg_WrongNumberOfArgumentsViolation(SG_INTERN("nginx-filter-context?"), 1,
				       argc, SG_NIL);
  }
  return SG_MAKE_BOOL(SG_NGINX_FILTER_CONTEXT(argv[0]));
}
static SG_DEFINE_SUBR(nginx_filter_context_p_stub, 1, 0,
		      nginx_filter_context_p, SG_FALSE, NULL);

static SgObject nginx_filter_context_parameter_ref(SgObject *argv, int argc,
						   void *data)
{
  SgObject parameters;
  if (argc != 2) {
    Sg_WrongNumberOfArgumentsViolation(SG_INTERN("nginx-filter-context-parameter-ref"),
				       2, argc, SG_NIL);
  }
  if (!SG_NGINX_FILTER_CONTEXTP(argv[0])) {
    Sg_WrongTypeOfArgumentViolation(SG_INTERN("nginx-filter-context-filter-parameter-ref"),
				    SG_INTERN("nginx-filter-context"),
				    argv[0], SG_NIL);
  }
  if (!SG_STRINGP(argv[1])) {
    Sg_WrongTypeOfArgumentViolation(SG_INTERN("nginx-filter-context-parameter-ref"),
				    SG_INTERN("string"),
				    argv[1], SG_NIL);
  }
  parameters = SG_NGINX_FILTER_CONTEXT(argv[0])->parameters;
  if (SG_FALSEP(parameters)) return SG_FALSE;
  return Sg_HashTableRef(SG_HASHTABLE(parameters), argv[1], SG_FALSE);
}
static SG_DEFINE_SUBR(nginx_filter_context_parameter_ref_stub, 2, 0,
		      nginx_filter_context_parameter_ref, SG_FALSE, NULL);

SG_DEFINE_GETTER("nginx-filter-context-name", "nginx-filter-context",
		 SG_NGINX_FILTER_CONTEXTP, nf_name, SG_NGINX_FILTER_CONTEXT,
		 nginx_filter_context_name);


typedef struct
{
  SG_HEADER;
  SgObject method;
  SgObject uri;
  SgObject headers;
  SgObject cookies;
  int      cookies_parsed_p;
  SgObject query_string;	/* query string */
  /* NGINX doesn't provide fragment so users need to parse manually */
  SgObject original_uri;	/* original uri (incl. query and fragment) */
  SgObject request_line;	/* Request line */
  SgObject schema;		/* http/https */
  SgObject body;		/* binary input port */
  SgObject context;
  SgObject peer_certificate;
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

static SgObject nr_query_string(SgNginxRequest *nr)
{
  if (SG_FALSEP(nr->query_string)) {
    nr->query_string = ngx_str_to_string(&nr->rawNginxRequest->args);
  }
  return nr->query_string;
}

static SgObject nr_original_uri(SgNginxRequest *nr)
{
  if (SG_FALSEP(nr->original_uri)) {
    nr->original_uri = ngx_str_to_string(&nr->rawNginxRequest->unparsed_uri);
  }
  return nr->original_uri;
}

static SgObject nr_request_line(SgNginxRequest *nr)
{
  if (SG_FALSEP(nr->request_line)) {
    nr->request_line = ngx_str_to_string(&nr->rawNginxRequest->request_line);
  }
  return nr->request_line;
}

static SgObject nr_schema(SgNginxRequest *nr)
{
  if (SG_FALSEP(nr->schema)) {
    nr->schema = ngx_str_to_string(&nr->rawNginxRequest->schema);
  }
  return nr->schema;
}

static SgObject nr_body(SgNginxRequest *nr)
{
  return nr->body;
}

static SgObject nr_context(SgNginxRequest *nr)
{
  return nr->context;
}

static SgObject nr_cookies(SgNginxRequest *nr)
{
  if (SG_FALSEP(nr->cookies)) {
    ngx_http_request_t *r = nr->rawNginxRequest;
    ngx_uint_t i, len = r->headers_in.cookies.nelts;
    ngx_table_elt_t **data = (ngx_table_elt_t **)r->headers_in.cookies.elts;
    SgObject h = SG_NIL, t = SG_NIL;

    for (i = 0; i < len; i++) {
      ngx_table_elt_t *e = data[i];
      SG_APPEND1(h, t, ngx_str_to_string(&e->value));
    }
    nr->cookies = h;
  }
  return nr->cookies;
}

static void nr_cookies_set(SgNginxRequest *nr, SgObject cookies)
{
  /* one short update by (sagittarius nginx) */
  if (SG_FALSEP(nr->cookies) || nr->cookies_parsed_p) {
    Sg_Error(UC("Invalid usage of cookies slot"));
  }
  if (!SG_LISTP(cookies)) {
    Sg_WrongTypeOfArgumentViolation(SG_INTERN("nginx-request-cookies-set!"),
				    SG_INTERN("list"), cookies, SG_NIL);
  }
  nr->cookies_parsed_p = TRUE;
  nr->cookies = cookies;
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

static SgObject x509_to_bytevector(X509 *x509)
{
  int len;
  unsigned char *p;
  SgObject bv;
  
  len = i2d_X509(x509, NULL);
  bv = Sg_MakeByteVector(len, 0);
  p = SG_BVECTOR_ELEMENTS(bv);
  i2d_X509(x509, &p);
  
  return bv;
}

static SgObject nr_peer_certificate(SgNginxRequest *nr)
{
  if (SG_UNDEFP(nr->peer_certificate)) {
    ngx_http_request_t *r = nr->rawNginxRequest;
    ngx_connection_t *c = r->connection;
    ngx_ssl_connection_t *ssl = c->ssl;
    if (ssl) {
      X509 *x509 = SSL_get_peer_certificate(c->ssl->connection);
      nr->peer_certificate = x509_to_bytevector(x509);
      
      X509_free(x509);
    } else {
      nr->peer_certificate = SG_FALSE;
    }
  }
  return nr->peer_certificate;
}


static SgSlotAccessor nr_slots[] = {
  SG_CLASS_SLOT_SPEC("method",     0, nr_method, NULL),
  SG_CLASS_SLOT_SPEC("uri",        1, nr_uri, NULL),
  SG_CLASS_SLOT_SPEC("headers",    2, nr_headers, NULL),
  SG_CLASS_SLOT_SPEC("cookies",    3, nr_cookies, nr_cookies_set),
  SG_CLASS_SLOT_SPEC("query-string", 4, nr_query_string, NULL),
  SG_CLASS_SLOT_SPEC("original-uri", 5, nr_original_uri, NULL),
  SG_CLASS_SLOT_SPEC("request-line", 6, nr_request_line, NULL),
  SG_CLASS_SLOT_SPEC("schema",     7, nr_schema, NULL),
  SG_CLASS_SLOT_SPEC("input-port", 8, nr_body, NULL),
  SG_CLASS_SLOT_SPEC("context", 9, nr_context, NULL),
  SG_CLASS_SLOT_SPEC("peer-certificate", 10, nr_peer_certificate, NULL),
#define HEADER_FIELD(name, cname, n)				\
  SG_CLASS_SLOT_SPEC(#name, n+10, SG_CPP_CAT(nr_, cname), NULL),
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

SG_DEFINE_GETTER("nginx-request-method", "nginx-request",
		 SG_NGINX_REQUESTP, nr_method, SG_NGINX_REQUEST,
		 nginx_request_method);
SG_DEFINE_GETTER("nginx-request-uri", "nginx-request",
		 SG_NGINX_REQUESTP, nr_uri, SG_NGINX_REQUEST,
		 nginx_request_uri);
SG_DEFINE_GETTER("nginx-request-headers", "nginx-request",
		 SG_NGINX_REQUESTP, nr_headers, SG_NGINX_REQUEST,
		 nginx_request_headers);
SG_DEFINE_GETTER("nginx-request-cookies", "nginx-request",
		 SG_NGINX_REQUESTP, nr_cookies, SG_NGINX_REQUEST,
		 nginx_request_cookies);
SG_DEFINE_SETTER("nginx-request-cookies-set!", "nginx-request",
		 SG_NGINX_REQUESTP, nr_cookies_set, SG_NGINX_REQUEST,
		 nginx_request_cookies_set);
SG_DEFINE_GETTER("nginx-request-query-string", "nginx-request",
		 SG_NGINX_REQUESTP, nr_query_string, SG_NGINX_REQUEST,
		 nginx_request_query_string);
SG_DEFINE_GETTER("nginx-request-original-uri", "nginx-request",
		 SG_NGINX_REQUESTP, nr_original_uri, SG_NGINX_REQUEST,
		 nginx_request_original_uri);
SG_DEFINE_GETTER("nginx-request-request-line", "nginx-request",
		 SG_NGINX_REQUESTP, nr_request_line, SG_NGINX_REQUEST,
		 nginx_request_request_line);
SG_DEFINE_GETTER("nginx-request-schema", "nginx-request",
		 SG_NGINX_REQUESTP, nr_schema, SG_NGINX_REQUEST,
		 nginx_request_schema);
SG_DEFINE_GETTER("nginx-request-input-port", "nginx-request",
		 SG_NGINX_REQUESTP, nr_body, SG_NGINX_REQUEST,
		 nginx_request_body);
SG_DEFINE_GETTER("nginx-request-context", "nginx-request",
		 SG_NGINX_REQUESTP, nr_context, SG_NGINX_REQUEST,
		 nginx_request_context);
SG_DEFINE_GETTER("nginx-request-peer-certificate", "nginx-request",
		 SG_NGINX_REQUESTP, nr_peer_certificate, SG_NGINX_REQUEST,
		 nginx_request_peer_certificate);
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
    r->headers_out. field ->hash = Sg_StringHash(value, 0);		\
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
    
    e = ngx_list_push(&r->headers_out.headers);
    n = (unsigned char *)Sg_Utf32sToUtf8s(name);
    e->key.data = n;
    e->key.len = ngx_strlen(n);
    e->value.data = uc;
    e->value.len = ngx_strlen(uc);
    e->lowcase_key = (unsigned char *)Sg_Utf32sToUtf8s(s);
    e->hash = Sg_StringHash(value, 0);
  }

#undef set_builtin_header
#undef set_builtin_header3
}

/* This takes O(n)... */
static void ngx_remove_header_from_list(ngx_http_request_t *r,
					unsigned char *name)
{
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
      e->hash = 0;		/* easy pisy */
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
  if (!SG_STRINGP(argv[1])) {
    Sg_WrongTypeOfArgumentViolation(SG_INTERN("nginx-response-header-add!"),
				    SG_INTERN("string"),
				    argv[1], SG_NIL);
  }
  if (!SG_STRINGP(argv[2])) {
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
  if (!SG_STRINGP(argv[1])) {
    Sg_WrongTypeOfArgumentViolation(SG_INTERN("nginx-response-header-set!"),
				    SG_INTERN("string"),
				    argv[1], SG_NIL);
  }
  if (!SG_STRINGP(argv[2])) {
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
  if (!SG_STRINGP(argv[1])) {
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
static ngx_thread_mutex_t global_lock;

static ngx_int_t ngx_http_sagittarius_init_process(ngx_cycle_t *cycle)
{
  SgObject sym, lib;
  ngx_log_error(NGX_LOG_DEBUG, cycle->log, 0,
		"'sagittarius': Initialising Sagittarius process");

  if (ngx_thread_mutex_create(&global_lock, cycle->log) != NGX_OK) {
    ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
		"'sagittarius': Failed to initialise the mutex");
    return NGX_ERROR;
  }
  /* Initialise the sagittarius VM */
  Sg_Init();

  sym = SG_INTERN("(sagittarius nginx internal)");
  ngx_log_error(NGX_LOG_DEBUG, cycle->log, 0,
		"'sagittarius': "
		"Initialising '(sagittarius nginx internal)' library");
  lib = Sg_FindLibrary(sym, TRUE);

  Sg_InitStaticClassWithMeta(SG_CLASS_NGINX_CONTEXT, UC("<nginx-context>"),
			     SG_LIBRARY(lib), NULL,
			     SG_FALSE, nc_slots, 0);
  Sg_InitStaticClassWithMeta(SG_CLASS_NGINX_FILTER_CONTEXT,
			     UC("<nginx-filter-context>"),
			     SG_LIBRARY(lib), NULL,
			     SG_FALSE, nf_slots, 0);
  Sg_InitStaticClassWithMeta(SG_CLASS_NGINX_REQUEST, UC("<nginx-request>"),
			     SG_LIBRARY(lib), NULL,
			     SG_FALSE, nr_slots, 0);
  Sg_InitStaticClassWithMeta(SG_CLASS_NGINX_RESPONSE, UC("<nginx-response>"),
			     SG_LIBRARY(lib), NULL,
			     SG_FALSE, nres_slots, 0);
  Sg_InitStaticClass(SG_CLASS_RESPONSE_OUTPUT_PORT, UC("<nginx-response-port>"),
		     SG_LIBRARY(lib), NULL, 0);

  Sg_InsertBinding(SG_LIBRARY(lib),
		   SG_INTERN("nginx-context?"), &nginx_context_p_stub);
  SG_PROCEDURE_NAME(&nginx_context_p_stub) = SG_MAKE_STRING("nginx-context?");
  SG_PROCEDURE_TRANSPARENT(&nginx_context_p_stub) = SG_PROC_TRANSPARENT;

  Sg_InsertBinding(SG_LIBRARY(lib), SG_INTERN("nginx-context-parameter-ref"),
		   &nginx_context_parameter_ref_stub);
  SG_PROCEDURE_NAME(&nginx_context_parameter_ref_stub) =
    SG_MAKE_STRING("nginx-context-parameter-ref");
  SG_PROCEDURE_TRANSPARENT(&nginx_context_parameter_ref_stub) =
    SG_PROC_NO_SIDE_EFFECT;

  Sg_InsertBinding(SG_LIBRARY(lib),
		   SG_INTERN("nginx-filter-context?"),
		   &nginx_filter_context_p_stub);
  SG_PROCEDURE_NAME(&nginx_filter_context_p_stub) =
    SG_MAKE_STRING("nginx-filter-context?");
  SG_PROCEDURE_TRANSPARENT(&nginx_filter_context_p_stub) = SG_PROC_TRANSPARENT;

  Sg_InsertBinding(SG_LIBRARY(lib),
		   SG_INTERN("nginx-filter-context-parameter-ref"),
		   &nginx_filter_context_parameter_ref_stub);
  SG_PROCEDURE_NAME(&nginx_filter_context_parameter_ref_stub) =
    SG_MAKE_STRING("nginx-filter-context-parameter-ref");
  SG_PROCEDURE_TRANSPARENT(&nginx_filter_context_parameter_ref_stub) =
    SG_PROC_NO_SIDE_EFFECT;
  Sg_InsertBinding(SG_LIBRARY(lib),
		   SG_INTERN("nginx-filter-context-name"),
		   &nginx_filter_context_name_stub);
  SG_PROCEDURE_NAME(&nginx_filter_context_name_stub) =
    SG_MAKE_STRING("nginx-filter-context-name");
  SG_PROCEDURE_TRANSPARENT(&nginx_filter_context_name_stub) =
    SG_PROC_NO_SIDE_EFFECT;

  
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
  
  INSERT_ACCESSOR("nginx-context-path", nginx_context_path,
		  SG_PROC_NO_SIDE_EFFECT);
  INSERT_ACCESSOR("nginx-context-parameters", nginx_context_parameters,
		  SG_PROC_NO_SIDE_EFFECT);

  INSERT_ACCESSOR("nginx-request-method", nginx_request_method,
		  SG_PROC_NO_SIDE_EFFECT);
  INSERT_ACCESSOR("nginx-request-uri", nginx_request_uri,
		  SG_PROC_NO_SIDE_EFFECT);
  INSERT_ACCESSOR("nginx-request-headers", nginx_request_headers,
		  SG_PROC_NO_SIDE_EFFECT);
  INSERT_ACCESSOR("nginx-request-cookies", nginx_request_cookies,
		  SG_PROC_NO_SIDE_EFFECT);
  INSERT_ACCESSOR("nginx-request-cookies-set!", nginx_request_cookies_set,
		  SG_SUBR_SIDE_EFFECT);
  INSERT_ACCESSOR("nginx-request-query-string", nginx_request_query_string,
		  SG_PROC_NO_SIDE_EFFECT);
  INSERT_ACCESSOR("nginx-request-original-uri", nginx_request_original_uri,
		  SG_PROC_NO_SIDE_EFFECT);
  INSERT_ACCESSOR("nginx-request-request-line", nginx_request_request_line,
		  SG_PROC_NO_SIDE_EFFECT);
  INSERT_ACCESSOR("nginx-request-schema", nginx_request_schema,
		  SG_PROC_NO_SIDE_EFFECT);
  INSERT_ACCESSOR("nginx-request-input-port", nginx_request_body,
		  SG_PROC_NO_SIDE_EFFECT);
  INSERT_ACCESSOR("nginx-request-context", nginx_request_context,
		  SG_PROC_NO_SIDE_EFFECT);
  INSERT_ACCESSOR("nginx-request-peer-certificate",
		  nginx_request_peer_certificate, SG_PROC_NO_SIDE_EFFECT);
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

static ngx_rbtree_t       nginx_contexts;
static ngx_rbtree_node_t  sentinel;
typedef struct
{
  ngx_str_node_t sn;
  SgObject       context;
  ngx_http_sagittarius_conf_t *conf; /* temporary storage */
} nginx_context_node_t;

static void call_cleanup(ngx_cycle_t *cycle, ngx_rbtree_node_t *node)
{
  nginx_context_node_t *cn;
  if (node != nginx_contexts.sentinel) {
    cn = (nginx_context_node_t *)node;
    if (!SG_FALSEP(cn->context) &&
	!SG_FALSEP(SG_NGINX_CONTEXT(cn->context)->cleanup)) {
      ngx_log_error(NGX_LOG_DEBUG, cycle->log, 0,
		    "'sagittarius': Cleaning up context '%V'", &cn->sn.str);
      SG_UNWIND_PROTECT {
	Sg_Apply1(SG_NGINX_CONTEXT(cn->context)->cleanup, cn->context);
      } SG_WHEN_ERROR {
	ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
		      "'sagittarius': Failed to call cleanup procedure");
      } SG_END_PROTECT;
    } else {
      ngx_log_error(NGX_LOG_DEBUG, cycle->log, 0,
		    "'sagittarius': Skip cleaning up context '%V'",
		    &cn->sn.str);
    }
    call_cleanup(cycle, node->left);
    call_cleanup(cycle, node->right);
  }
}

static void ngx_http_sagittarius_exit_process(ngx_cycle_t *cycle)
{
  /* go through the rbtree */
  /* http://nginx.org/en/docs/dev/development_guide.html#red_black_tree */
  ngx_log_error(NGX_LOG_DEBUG, cycle->log, 0, "'sagittarius': Cleaning up");
  call_cleanup(cycle, nginx_contexts.root);
}

static ngx_int_t ngx_http_sagittarius_preconfiguration(ngx_conf_t *cf)
{
  ngx_rbtree_init(&nginx_contexts, &sentinel, ngx_str_rbtree_insert_value);
  return NGX_OK;
}

static void init_thread_pool(ngx_conf_t *cf, ngx_rbtree_node_t *node)
{
  nginx_context_node_t *cn;
  if (node != nginx_contexts.sentinel) {
    ngx_str_t nstr = ngx_null_string;
    cn = (nginx_context_node_t *)node;

    if (cn->conf->pool_name.len != 0) {
      ngx_thread_pool_t *tp = ngx_thread_pool_add(cf, &cn->conf->pool_name);
      if (tp == NULL) {
	ngx_log_error(NGX_LOG_WARN, cf->log, 0,
		      "'sagittarius': Failed to add thread pool '%V' for '%V'",
		      &cn->conf->pool_name, &cn->sn.str);
	cn->conf->pool_name = nstr; /* reset the name */
      } else {
	ngx_log_error(NGX_LOG_DEBUG, cf->log, 0,
		      "'sagittarius': Added thread pool '%V' for '%V'",
		      &cn->conf->pool_name, &cn->sn.str);
      }
    }
    init_thread_pool(cf, node->left);
    init_thread_pool(cf, node->right);
  }
}

static ngx_int_t ngx_http_sagittarius_postconfiguration(ngx_conf_t *cf)
{
  /* initialise the thread pool */
  init_thread_pool(cf, nginx_contexts.root);
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
  uint32_t                     hash;
  nginx_context_node_t        *node;
  ngx_log_error(NGX_LOG_DEBUG, cf->log, 0,
		"'sagittarius': Handling Sagittarius configuration");

  clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
  hash = ngx_crc32_long(clcf->name.data, clcf->name.len);
  node = (nginx_context_node_t *)
    ngx_str_rbtree_lookup(&nginx_contexts, &clcf->name, hash);
  sg_conf = (ngx_http_sagittarius_conf_t *)conf;
  if (!node) {
    /* okay insert it */
    node = ngx_palloc(cf->pool, sizeof(nginx_context_node_t));
    if (!node) {
      ngx_log_error(NGX_LOG_ERR, cf->log, 0,
		    "'sagittarius': Failed to allocate context node");
      return NGX_CONF_ERROR;
    }
    node->sn.node.key = hash;
    node->sn.str = clcf->name;
    node->context = SG_FALSE;
    node->conf = sg_conf;	/* for thread pool */
    /* 
       the sn.node is the top most location of the struct. thus inserthing
       this also means inserting the node itself.
     */
    ngx_rbtree_insert(&nginx_contexts, &node->sn.node);
  }
  
  value = cf->args->elts;
  sg_conf->procedure = (ngx_str_t)value[1];
  if (cf->args->nelts > 2) {
    sg_conf->init_proc = (ngx_str_t)value[2];
  }
  if (cf->args->nelts > 3) {
    sg_conf->cleanup_proc = (ngx_str_t)value[3];
  }
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
  
  clcf->handler = ngx_http_sagittarius_handler;
  
  return NGX_CONF_OK;
}

static sagittarius_filter_t *get_or_push(ngx_log_t *log,
					 ngx_array_t *filters, ngx_str_t *name)
{
  sagittarius_filter_t *e, *values;
  ngx_str_t null_str = ngx_null_string;
  ngx_uint_t i;
  values = filters->elts;

  for (i = 0; i < filters->nelts; i++) {
    e = &values[i];
    if (ngx_strcmp(e->name.data, name->data) == 0) return e;
  }
  e = ngx_array_push(filters);
  /* new creation... */
  e->procedure = null_str;
  e->library = null_str;
  e->name.data = name->data;
  e->name.len = name->len;
  return e;
}

static char* ngx_http_sagittarius(ngx_conf_t *cf,
				  ngx_command_t *dummy,
				  void *conf)
{
  ngx_uint_t                  i;
  ngx_http_sagittarius_conf_t *sg_conf;
  ngx_str_t *value, *elts, *prefix;
  
  sg_conf = (ngx_http_sagittarius_conf_t *)conf;
  value = cf->args->elts;
#define allocate_array(to, init, st)					\
  do {									\
    if (!(to) ) {							\
      (to) = ngx_array_create(cf->pool, (init), sizeof(st));		\
      if (!(to) ) {							\
	ngx_log_error(NGX_LOG_ERR, cf->log, 0,				\
		      "'sagittarius': Failed to allocate an array");	\
	return NGX_CONF_ERROR;						\
      }									\
    }									\
  } while (0)
  
  if (ngx_strcmp(value[0].data, "load_path") == 0) {
    if (cf->args->nelts < 2) {
      ngx_log_error(NGX_LOG_ERR, cf->log, 0,
		    "'sagittarius': "
		    "'load_path' must take at least one argument");
      return NGX_CONF_ERROR;
    }
    allocate_array(sg_conf->load_paths, 0, ngx_str_t);
    prefix = &cf->cycle->prefix;
    elts = ngx_array_push_n(sg_conf->load_paths, cf->args->nelts-1);

    for (i = 0; i < cf->args->nelts-1; i++) {
      elts[i] = value[i+1];
      if (ngx_get_full_name(cf->pool, prefix, &elts[i]) != NGX_OK) {
	ngx_log_error(NGX_LOG_ERR, cf->log, 0,
		      "'sagittarius': 'load_path' failed to get load path ");
	return NGX_CONF_ERROR;
      }
      ngx_log_error(NGX_LOG_DEBUG, cf->log, 0,
		    "'sagittarius': 'load_path' %V", &elts[i]);

    }
  } else if (ngx_strcmp(value[0].data, "library") == 0) {
    if (cf->args->nelts != 2) {
      ngx_log_error(NGX_LOG_ERR, cf->log, 0,
		    "'sagittarius': 'library' must take one argument (%d)",
		    cf->args->nelts);
      return NGX_CONF_ERROR;
    }
    sg_conf->library = value[1];
  } else if (ngx_strcmp(value[0].data, "parameter") == 0) {
    ngx_keyval_t *e;
    /* 
       allocate at least one here since we don't use ngx_array_push_n.
       I think it's a bug of NGINX ngx_array_push since it doesn't handle
       zero initial array properly...
    */
    allocate_array(sg_conf->parameters, 1, ngx_keyval_t);
    if (cf->args->nelts != 3) {
      ngx_log_error(NGX_LOG_ERR, cf->log, 0,
		    "'sagittarius': 'parameter' must contain "
		    "2 elements (var and val)");
      return NGX_CONF_ERROR;
    }
    e = ngx_array_push(sg_conf->parameters);
    e->key = value[1];
    e->value = value[2];
  } else if (ngx_strcmp(value[0].data, "filter") == 0) {
    sagittarius_filter_t *e;
    allocate_array(sg_conf->filters, 1, sagittarius_filter_t);
    if (cf->args->nelts < 4) {
      ngx_log_error(NGX_LOG_ERR, cf->log, 0,
		    "'sagittarius': 'filter' must contain at least"
		    "3 elements (name, entry_point and order)");
      return NGX_CONF_ERROR;
    }
    e = get_or_push(cf->log, sg_conf->filters, &value[1]);
    e->procedure = value[2];
    e->order = ngx_atoi(value[3].data, value[3].len);
    if (cf->args->nelts == 5) {
      e->has_library = TRUE;
      e->library = value[4];
    } else {
      e->has_library = FALSE;
    }
  } else if (ngx_strcmp(value[0].data, "filter_parameter") == 0) {
    sagittarius_filter_t *e;
    ngx_keyval_t *kv;
    allocate_array(sg_conf->filters, 1, sagittarius_filter_t);
    if (cf->args->nelts != 4) {
      ngx_log_error(NGX_LOG_ERR, cf->log, 0,
		    "'sagittarius': 'filter_parameter' must contain"
		    "3 elements (name, key and value)");
      return NGX_CONF_ERROR;
    }
    /* We allow reverse order of configuration  */    
    e = get_or_push(cf->log, sg_conf->filters, &value[1]);
    allocate_array(e->parameters, 1, ngx_keyval_t);
    kv = ngx_array_push(e->parameters);
    kv->key = value[2];
    kv->value = value[3];
  } else if (ngx_strcmp(value[0].data, "thread_pool_name") == 0) {
    if (cf->args->nelts != 2) {
      ngx_log_error(NGX_LOG_ERR, cf->log, 0,
		    "'sagittarius': 'thread_pool' must contain"
		    "1 element (pool_name)");
      return NGX_CONF_ERROR;
    }
    sg_conf->pool_name = value[1];
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
  conf = ngx_palloc(cf->pool, sizeof(ngx_http_sagittarius_conf_t));
  if (!conf) {
    return NGX_CONF_ERROR;
  }
  conf->library = nstr;
  conf->procedure = nstr;
  conf->load_paths = NULL;
  conf->init_proc = nstr;
  conf->cleanup_proc = nstr;
  conf->parameters = NULL;
  conf->filters = NULL;
  conf->pool_name = nstr;
  return conf;
}

static char* ngx_http_sagittarius_merge_loc_conf(ngx_conf_t *cf,
						 void *p, void *c)
{
  return NGX_CONF_OK;
}

#define retrieve_procedure(r, lib, log, str)				\
  do {									\
    SgObject t__ = Sg_FindBinding(SG_LIBRARY(lib),			\
				  Sg_Intern(ngx_str_to_string(str)),	\
				  SG_UNBOUND);				\
    if (SG_UNBOUNDP(t__)) {						\
      ngx_log_error(NGX_LOG_WARN, (log), 0,				\
		    "'sagittarius': Procedure '%V' not found",		\
		    (str));						\
      (r) = t__;							\
    } else {								\
      (r) = SG_GLOC_GET(SG_GLOC(t__));					\
    }									\
  } while (0)

static int filter_compare(const void *a, const void *b)
{
  sagittarius_filter_t *fa, *fb;
  fa = (sagittarius_filter_t *)a;
  fb = (sagittarius_filter_t *)b;

  return fb->order - fa->order;
}

static SgObject filter_caller(SgObject *args, int argc, void *data)
{
  SgObject ctx = SG_CAR(SG_OBJ(data));
  SgObject filter = SG_CADR(SG_OBJ(data));
  SgObject next = SG_CDDR(SG_OBJ(data));
  return Sg_VMApply4(filter, ctx, args[0], args[1], next);
}

static SgObject combine_filter(sagittarius_filter_t *fc,
			       SgObject filter, SgObject next)
{
  SgNginxFilterContext *ctx = SG_NEW(SgNginxFilterContext);
  ngx_keyval_t *e, *value;
  ngx_uint_t i;

  SG_SET_CLASS(ctx, SG_CLASS_NGINX_FILTER_CONTEXT);

  ctx->name = ngx_str_to_string(&fc->name);
  if (fc->parameters) {
    ctx->parameters = Sg_MakeHashTableSimple(SG_HASH_STRING,
					     fc->parameters->nelts);
    value = fc->parameters->elts;
    for (i = 0; i < fc->parameters->nelts; i++) {
      e = &value[i];
      Sg_HashTableSet(ctx->parameters,
		      ngx_str_to_string(&e->key),
		      ngx_str_to_string(&e->value),
		      0);
    }
  } else {
    ctx->parameters = SG_FALSE;
  }
  /* 
     (lambda (request response) (filter context request response next))
   */
  return Sg_MakeSubr(filter_caller, Sg_Cons(ctx, Sg_Cons(filter, next)), 2, 0,
		     SG_MAKE_STRING("filter-chain"));
}

static
SgObject get_filter_applied_procedure(SgObject library,
				      ngx_log_t *log,
				      ngx_http_sagittarius_conf_t *sg_conf)
{
  SgObject proc, next;
  ngx_uint_t i;
  sagittarius_filter_t *values;
  
  retrieve_procedure(proc, library, log, &sg_conf->procedure);
  if (SG_UNBOUNDP(proc)) {
    ngx_log_error(NGX_LOG_ERR, log, 0,
		  "'sagittarius': Web application procedure '%V' not found.",
		  &sg_conf->procedure);
    return SG_FALSE;
  }

  if (!sg_conf->filters) return proc;
  
  ngx_qsort(sg_conf->filters->elts, sg_conf->filters->nelts,
	    sg_conf->filters->size, filter_compare);
  values = sg_conf->filters->elts;
  next = proc;
  for (i = 0; i < sg_conf->filters->nelts; i++) {
    sagittarius_filter_t *f = &values[i];
    SgObject lib = library, filter;
    
    if (f->procedure.len == 0) continue;
    if (f->has_library) {
      lib = Sg_FindLibrary(Sg_Intern(ngx_str_to_string(&f->library)), FALSE);
      if (SG_FALSEP(lib)) {
	ngx_log_error(NGX_LOG_ERR, log, 0,
		      "'sagittarius': Web filter library %V not found.",
		      &f->library);
	continue;
      }
    }
    retrieve_procedure(filter, lib, log, &f->procedure);
    ngx_log_error(NGX_LOG_DEBUG, log, 0,
		  "'sagittarius': Combining filter %V (order %d).",
		  &f->name, f->order);
    next = combine_filter(f, filter, next);
  }
  return next;
}

static SgObject make_nginx_context(ngx_http_request_t *r)
{
  ngx_http_core_loc_conf_t *clcf;
  ngx_http_sagittarius_conf_t *sg_conf;
  ngx_array_t *params;
  ngx_keyval_t *value;
  ngx_uint_t i;
  SgNginxContext *c = SG_NEW(SgNginxContext);
  SG_SET_CLASS(c, SG_CLASS_NGINX_CONTEXT);

  clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
  sg_conf = ngx_http_get_module_loc_conf(r, ngx_http_sagittarius_module);
  c->path = ngx_str_to_string(&clcf->name);
  c->cleanup = SG_FALSE;
  c->procedure = SG_FALSE;
  params = sg_conf->parameters;
  if (params) {
    c->parameters = Sg_MakeHashTableSimple(SG_HASH_STRING, params->nelts);
    value = params->elts;
    for (i = 0; i < params->nelts; i++) {
      ngx_keyval_t *e = &value[i];
      Sg_HashTableSet(c->parameters,
		      ngx_str_to_string(&e->key),
		      ngx_str_to_string(&e->value),
		      0);
    }
  } else {
    c->parameters = Sg_MakeHashTableSimple(SG_HASH_STRING, 0);
  }
  c->library = 
    Sg_FindLibrary(Sg_Intern(ngx_str_to_string(&sg_conf->library)), FALSE);
  if (!SG_FALSEP(c->library)) {
    if (sg_conf->cleanup_proc.len != 0) {
      retrieve_procedure(c->cleanup, c->library, r->connection->log,
			 &sg_conf->cleanup_proc);
    }
    if (sg_conf->init_proc.len != 0) {
      SgObject p;
      retrieve_procedure(p, c->library, r->connection->log,
			 &sg_conf->init_proc);
      if (!SG_UNBOUNDP(p)) {
	ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
		      "'sagittarius': calling init procedure '%V'",
		      &sg_conf->init_proc);
	SG_UNWIND_PROTECT {
	  Sg_Apply1(SG_GLOC_GET(SG_GLOC(p)), c);
	} SG_WHEN_ERROR {
	  ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
			"'sagittarius': Failed to call init procedure");
	} SG_END_PROTECT;
      }
    }
    c->procedure = get_filter_applied_procedure(c->library,
						r->connection->log,
						sg_conf);
  } else {
    /* log it */
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
		  "'sagittarius': Web application library '%V' not found.",
		  &sg_conf->library);
  }
  /* mark as immutable for Scheme world*/
  SG_HASHTABLE(c->parameters)->immutablep = TRUE;
  return SG_OBJ(c);
}

static SgObject get_context(ngx_http_request_t *r)
{
  ngx_http_core_loc_conf_t *clcf;
  nginx_context_node_t     *node;
  uint32_t                  hash;

  clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
  hash = ngx_crc32_long(clcf->name.data, clcf->name.len);
  node = (nginx_context_node_t *)
    ngx_str_rbtree_lookup(&nginx_contexts, &clcf->name, hash);
  
  if (!node) {
    /* this should never happend but in case of something overlooked */
    ngx_log_error(NGX_LOG_WARN, r->connection->log, 0,
		  "'sagittarius': Unknown context node");
    /* we create fresh ones */
    return make_nginx_context(r);
  }
  /* okay it's already inserted :) */
  if (!SG_FALSEP(node->context)) return node->context;

  ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
		"'sagittarius': Creating a context of %V", &clcf->name);
  if (ngx_thread_mutex_lock(&global_lock, r->connection->log) != NGX_OK) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
		  "'sagittarius': Failed to lock the mutex");
    return SG_UNDEF;
  }
  
  node->context = make_nginx_context(r);

  if (ngx_thread_mutex_unlock(&global_lock, r->connection->log) != NGX_OK) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
		  "'sagittarius': Failed to unlock the mutex");
    return SG_UNDEF;
  }
  
  return node->context;
}


static SgObject make_nginx_request(ngx_http_request_t *req, SgObject context)
{
  SgNginxRequest *ngxReq = SG_NEW(SgNginxRequest);
  SG_SET_CLASS(ngxReq, SG_CLASS_NGINX_REQUEST);
  ngxReq->method = ngx_str_to_string(&req->method_name);
  ngxReq->uri = ngx_str_to_string(&req->uri);
  ngxReq->headers = SG_FALSE;	/* initialise lazily */
  ngxReq->cookies = SG_FALSE;	/* initialise lazily */
  ngxReq->cookies_parsed_p = FALSE;
  ngxReq->query_string = SG_FALSE; /* initialise lazily */
  ngxReq->original_uri = SG_FALSE; /* initialise lazily */
  ngxReq->request_line = SG_FALSE; /* initialise lazily */
  ngxReq->schema = SG_FALSE; /* initialise lazily */
  ngxReq->body = make_request_input_port(req);
  ngxReq->rawNginxRequest = req;
  ngxReq->context = context;
  ngxReq->peer_certificate = SG_UNDEF;
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
static off_t compute_content_length(ngx_chain_t *out);

static ngx_int_t sagittarius_call(ngx_http_request_t *r)
{
  SgObject req, resp, saved_loadpath, proc, context;
  volatile SgVM *vm;
  volatile SgObject status;
  ngx_int_t rc;
  ngx_chain_t *out;
  ngx_http_sagittarius_conf_t *sg_conf;

  sg_conf = ngx_http_get_module_loc_conf(r, ngx_http_sagittarius_module);
  vm = Sg_VM();
  saved_loadpath = setup_load_path(vm, r->connection->log, sg_conf);
  /* The dispatcher will always be the same so no lock needed */
  if (SG_UNDEFP(nginx_dispatch)) {
    if (init_base_library(r->connection->log) != NGX_OK) {
      return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
  }
  context = get_context(r);
  proc = SG_NGINX_CONTEXT(context)->procedure;
  if (!SG_PROCEDUREP(proc)) {
    return NGX_HTTP_NOT_FOUND;
  }
  
  req = make_nginx_request(r, context);
  resp = make_nginx_response(r);

  SG_UNWIND_PROTECT {
    status = Sg_Apply3(nginx_dispatch, proc, req, resp);
  } SG_WHEN_ERROR {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
		  "'sagittarius': Failed to execute nginx-dispatch-request");
    vm->loadPath = saved_loadpath;
    ngx_http_discard_request_body(r);
    return NGX_HTTP_INTERNAL_SERVER_ERROR;    
  } SG_END_PROTECT;

  vm->loadPath = saved_loadpath;
    
  /* The procedure didn't consume the request, so discard it */
  rc = ngx_http_discard_request_body(r);

  if (rc != NGX_OK && rc != NGX_AGAIN) {
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
    r->header_only = (r->headers_out.content_length_n == 0);

    rc = ngx_http_send_header(r);
  
    if (rc == NGX_ERROR) {
      ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
		    "'sagittarius': Failed to send header %d'.", rc);
      return rc;
    }
    
    if (out) {
      ngx_http_output_filter(r, out);
    }
    return SG_INT_VALUE(status);

  } else {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
		  "'sagittarius': Scheme program returned non fixnum.");
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
  }
}

static void ngx_http_request_body_init(ngx_http_request_t *r)
{
  /* ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0, */
  /* 		"'sagittarius': content callback"); */
  /* Just handling the request the same as GET (or no content request) */
  ngx_http_finalize_request(r, sagittarius_call(r));
}

/* Main handler */
static ngx_int_t ngx_http_sagittarius_handler(ngx_http_request_t *r)
{
  ngx_int_t rc;
  ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
		"'sagittarius': Handling Sagittarius request for %V %V.",
		&r->method_name, &r->uri);

  if (r->headers_in.content_length_n > 0 || r->headers_in.chunked) {
    ngx_log_error(NGX_LOG_DEBUG, r->connection->log, 0,
		  "'sagittarius': reading client body %d",
		  r->headers_in.content_length_n);
    rc = ngx_http_read_client_request_body(r, ngx_http_request_body_init);
    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
      return rc;
    }
    return NGX_DONE;
  }
  return sagittarius_call(r);
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
  ngx_str_t *value;
  
  if (sg_conf->load_paths) {
    value = sg_conf->load_paths->elts;
    for (i = 0; i < sg_conf->load_paths->nelts; i++) {
      ngx_str_t *s = &value[i];
      ngx_log_error(NGX_LOG_DEBUG, log, 0, "'sagittarius': load path: %V", s);
      /* we prepend the load path */
      Sg_AddLoadPath(ngx_str_to_string(s), FALSE);
    }
  }

  return saved_loadpath;
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
