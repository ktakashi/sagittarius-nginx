;; example application for Sagittarius NGINX
(library (web echo)
    (export run)
    (import (rnrs)
	    (sagittarius nginx))

(define (run request response) 
  (define out (transcoded-port (nginx-response-output-port response)
			       (native-transcoder)))
  (case (string->symbol (nginx-request-method request))
    ((POST) 
     (let ((echo (get-bytevector-all (nginx-request-input-port request))))
       (if (eof-object? echo)
	   (put-string out "Test web application!!\n")
	   (put-string out (utf8->string echo))))
     (values 200 'text/plain))
    (else (values 405 'text/plain))))

)
