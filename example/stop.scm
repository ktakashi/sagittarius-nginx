;; test application for heavy call
#!read-macro=sagittarius/bv-string
(library (stop)
    (export run)
    (import (rnrs)
	    (srfi :18)
	    (sagittarius)
	    (only (sagittarius process) getpid)
	    (sagittarius nginx))

(define (run request response) 
  (define out (nginx-response-output-port response))
  (define context (nginx-request-context request))
  (format (current-error-port) "PID: ~s~%" (getpid))
  (let ((wait (get-bytevector-all (nginx-request-input-port request))))
    (cond ((eof-object? wait)
	   (put-bytevector out #*"Use POST + n second\n"))
	  (else
	   (let ((n (string->number (utf8->string wait))))
	     (cond (n
		    (thread-sleep! n)
		    (put-bytevector out #*"Waited for ")
		    (put-bytevector out wait)
		    (put-bytevector out #*" second(s)\n"))
		   (else
		    (put-bytevector out #*"Request wasn't a number ")
		    (put-bytevector out wait)
		    (put-bytevector out #*"\n")))))))
  (values 200 'text/plain))

)
