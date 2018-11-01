;; Copyright 2018 Takashi Kato <ktakashi@ymail.com>
;; See Licence.txt for terms and conditions of use

#!nounbound
#!read-macro=sagittarius/bv-string
(library (sagittarius nginx)
    (export nginx-dispatch-request)
    (import (rnrs)
	    (sagittarius nginx internal))

(define (nginx-dispatch-request uri request response)
  (put-bytevector (nginx-response-output-port response)
		  #*"Hooray! It worked!!\n")
  404)
)
