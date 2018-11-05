;; Copyright 2018 Takashi Kato <ktakashi@ymail.com>
;; See Licence.txt for terms and conditions of use

#!nounbound
#!read-macro=sagittarius/bv-string
(library (sagittarius nginx)
    (export nginx-dispatch-request)
    (import (rnrs)
	    (sagittarius)
	    (sagittarius nginx internal))

(define (nginx-dispatch-request uri request response)
  (let ((out (transcoded-port (nginx-response-output-port response)
			      (native-transcoder))))
    (display (load-path) out) (newline out)
    (display "Hooray!!" out) (newline out)
    404))
)
