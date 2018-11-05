;; example application for Sagittarius NGINX
(library (webapp)
    (export run)
    (import (rnrs)
	    (sagittarius nginx))

(define (run url) 
  (define out (transcoded-port (*nginx:response-output-port*)
			       (native-transcoder)))
  (put-string out "Test web application!!")
  (values 200 'text/plain))

)
