;; Copyright 2018 Takashi Kato <ktakashi@ymail.com>
;; See Licence.txt for terms and conditions of use

#!nounbound
(library (sagittarius nginx)
    (export nginx-dispatch-request
	    *nginx:response-output-port*)
    (import (rnrs)
	    (sagittarius)
	    (sagittarius nginx internal)
	    (srfi :39 parameters))

(define *nginx:response-output-port* (make-parameter #f))
(define (nginx-dispatch-request procedure uri request response)
  (parameterize ((*nginx:response-output-port*
		  (nginx-response-output-port response)))
    (let-values (((status content-type . headers) (procedure uri)))
      (cond  ((string? content-type) (nginx-response-content-type-set! response content-type))
	     ((symbol? content-type)
	      (nginx-response-content-type-set! response (symbol->string content-type))))
      ;; add content type and headers here
      status)))
)
