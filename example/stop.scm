;; test application for heavy call
#!read-macro=sagittarius/bv-string
(library (stop)
    (export run init)
    (import (rnrs)
	    (srfi :18)
	    (srfi :27)
	    (srfi :39)
	    (sagittarius)
	    (only (sagittarius process) getpid)
	    (sagittarius nginx))

(define *p* (make-parameter #f))
(define (init context) (*p* (random-integer 100)))

(define (run request response) 
  (define out (nginx-response-output-port response))
  (define context (nginx-request-context request))
  (let ((wait (get-bytevector-all (nginx-request-input-port request))))
    (cond ((eof-object? wait)
	   (put-bytevector out (string->utf8 (format "random ~a~%" (*p*))))
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
