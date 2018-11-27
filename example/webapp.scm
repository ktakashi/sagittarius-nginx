;; example application for Sagittarius NGINX
(library (webapp)
    (export run)
    (import (rnrs)
	    (sagittarius nginx))

(define (run request response) 
  (define out (transcoded-port (nginx-response-output-port response)
			       (native-transcoder)))
  (let ((echo (get-bytevector-all (nginx-request-input-port request))))
    (cond ((eof-object? echo)
	   (put-string out "Test web application!!\n")
	   (put-string out (nginx-context-path (nginx-request-context request)))
	   (newline out))
	  (else (put-string out (utf8->string echo)))))

  (nginx-response-header-add! response "Foo" "bar")
  (values 200 'text/plain))

)
