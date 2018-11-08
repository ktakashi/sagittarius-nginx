;; Copyright 2018 Takashi Kato <ktakashi@ymail.com>
;; See Licence.txt for terms and conditions of use

#!nounbound
(library (sagittarius nginx)
    (export nginx-request-host
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
	    nginx-request-input-port

	    nginx-response-output-port
	    )
    (import (rnrs)
	    (sagittarius nginx internal))


(define (nginx-dispatch-request procedure uri request response)
  (define (->contnet-type-string content-type)
    (cond  ((string? content-type) content-type)
	   ((symbol? content-type) (symbol->string content-type))
	   (else #f)))

  (let-values (((status content-type . headers) (procedure request response)))
    (cond  ((->contnet-type-string content-type) =>
	    (lambda (ctype) (nginx-response-content-type-set! response ctype))))
    ;; add content type and headers here
    status))
)
