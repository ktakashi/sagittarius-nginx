;; example application for Sagittarius NGINX
(library (web cookie)
    (export run)
    (import (rnrs)
	    (rfc cookie)
	    (sagittarius nginx))

(define (run request response) 
  (define out (transcoded-port (nginx-response-output-port response)
			       (native-transcoder)))
  (for-each (lambda (cookie)
	      (put-string out (cookie->string cookie)) (put-string out "\n"))
	    (nginx-request-cookies request))
  (values 200 'text/plain))

)
