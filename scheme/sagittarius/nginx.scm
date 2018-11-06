;; Copyright 2018 Takashi Kato <ktakashi@ymail.com>
;; See Licence.txt for terms and conditions of use

#!nounbound
(library (sagittarius nginx)
    (export nginx-response-output-port)
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
