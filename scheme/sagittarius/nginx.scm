;; Copyright 2018 Takashi Kato <ktakashi@ymail.com>
;; See Licence.txt for terms and conditions of use

#!nounbound
(library (sagittarius nginx)
    (export nginx-dispatch-request)
    (import (rnrs))

(define (nginx-dispatch-request uri request response)
  (display "Hooray! It worked!!" (current-error-port))
  (newline (current-error-port))
  404)
)
