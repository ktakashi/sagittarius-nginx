;; example application for Sagittarius NGINX
(library (web test-app)
    (export run)
    (import (rnrs)
	    (sagittarius)
	    (sagittarius nginx))

(define (run request response) 
  (define out (transcoded-port (nginx-response-output-port response)
			       (native-transcoder)))
  (put-string out "Test application\n")
  (nginx-response-header-add! response "X-Sagittarius" (sagittarius-version))
  (values 200 'text/plain))

)
