;; example application for Sagittarius NGINX
(library (webapp)
    (export run)
    (import (rnrs)
	    (sagittarius nginx))

(define (run request response) 
  (define out (transcoded-port (nginx-response-output-port response)
			       (native-transcoder)))
  (put-string out "Test web application!!")
  (values 200 'text/plain))

)
