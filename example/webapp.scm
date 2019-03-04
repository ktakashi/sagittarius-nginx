;; example application for Sagittarius NGINX
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
	  (else (put-string out (utf8->string echo)))))

  (nginx-response-header-add! response "Foo" "bar")
  (values 200 'text/plain))

)
