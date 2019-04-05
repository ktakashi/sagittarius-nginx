;; example application for Sagittarius NGINX
#!read-macro=sagittarius/bv-string
(library (webapp)
    (export run)
    (import (rnrs)
	    (sagittarius)
	    (sagittarius nginx))

(define (run request response) 
  (define out (transcoded-port (nginx-response-output-port response)
			       (native-transcoder)))
  (define context (nginx-request-context request))
  (let ((echo (get-bytevector-all (nginx-request-input-port request))))
    (cond ((eof-object? echo)
	   (put-string out "Test web application!!\n")
	   (put-string out (nginx-context-path context))
	   (newline out)
	   (put-string out (nginx-context-parameter-ref context "key0"))
	   (newline out))
	  ((bytevector=? echo #*"no-content"))
	  (else (put-string out (utf8->string echo))))
    
    (nginx-response-header-add! response "Foo" "bar")
    (if (bytevector=? echo #*"no-content")
	(values 204 'text/plain)
	(values 200 'text/plain))))

)
