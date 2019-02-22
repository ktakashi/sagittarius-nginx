;; Copyright 2018 Takashi Kato <ktakashi@ymail.com>
;; See Licence.txt for terms and conditions of use

#!nounbound
(library (sagittarius nginx)
    (export nginx-request?
	    nginx-request-method
	    nginx-request-uri
	    nginx-request-host
	    nginx-request-connection
	    nginx-request-if-modified-since
	    nginx-request-if-unmodified-since
	    nginx-request-if-match
	    nginx-request-if-none-match
	    nginx-request-user-agent
	    nginx-request-referer
	    nginx-request-content-length
	    nginx-request-content-range
	    nginx-request-content-type
	    nginx-request-range
	    nginx-request-if-range
	    nginx-request-transfer-encoding
	    nginx-request-te
	    nginx-request-expect
	    nginx-request-upgrade
	    nginx-request-accept-encoding
	    nginx-request-via
	    nginx-request-authorization
	    nginx-request-keep-alive
	    nginx-request-accept
	    nginx-request-accept-language
	    nginx-request-headers ;; alist
	    nginx-request-cookies ;; alist
	    nginx-request-query-string
	    nginx-request-original-uri
	    nginx-request-request-line
	    ;; nginx-request-schema ;; this seems useless...
	    nginx-request-input-port
	    nginx-request-context
	    nginx-request-peer-certificate

	    nginx-response?
	    nginx-response-output-port
	    nginx-response-headers
	    nginx-response-header-add!
	    nginx-response-header-set!
	    nginx-response-header-remove!

	    nginx-context?
	    nginx-context-path
	    nginx-context-parameter-ref
	    nginx-context-parameters
	    )
    (import (rnrs)
	    (rfc cookie)
	    (srfi :1 lists)
	    (sagittarius nginx internal))


(define (nginx-dispatch-request procedure request response)
  (define (->contnet-type-string content-type)
    (cond  ((string? content-type) content-type)
	   ((symbol? content-type) (symbol->string content-type))
	   (else #f)))
  (define (safe-parse-cookies-string str)
    (guard (e (else '())) (parse-cookies-string str)))
  (nginx-request-cookies-set! request
    (append-map safe-parse-cookies-string (nginx-request-cookies request)))
  (let-values (((status content-type) (procedure request response)))
    (cond  ((->contnet-type-string content-type) =>
	    (lambda (ctype) (nginx-response-content-type-set! response ctype))))
    status))
)
