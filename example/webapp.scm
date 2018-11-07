;; example application for Sagittarius NGINX
(library (webapp)
    (export run)
    (import (rnrs)
	    (sagittarius nginx)
	    (pp))

(define (run request response) 
  (define out (transcoded-port (nginx-response-output-port response)
			       (native-transcoder)))
  (put-string out "Test web application!!\n")
  (put-string out (nginx-request-user-agent request)) (newline out)
  (display (nginx-request-connection request) out) (newline out)
  (pp (nginx-request-headers request) out)
  (values 200 'text/plain))

)
