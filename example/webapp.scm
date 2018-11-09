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
  (put-string out (nginx-request-method request)) (newline out)
  (put-string out (nginx-request-uri request)) (newline out)
  (put-string out (nginx-request-user-agent request)) (newline out)
  (cond ((nginx-request-content-type request) =>
	 (lambda (v) (put-string out v) (newline out))))
  (pp (nginx-request-headers request) out)
  (let ((bv (get-bytevector-all (nginx-request-input-port request))))
    (unless (eof-object? bv) (put-string out (utf8->string bv))))
  (values 200 'text/plain))

)
