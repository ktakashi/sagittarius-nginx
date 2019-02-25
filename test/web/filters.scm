(library (web filters)
    (export run
	    filter0
	    filter1
	    filter2)
    (import (rnrs)
	    (sagittarius nginx))

(define (run request response)
  (put-bytevector (nginx-response-output-port response)
		  (string->utf8
		   (if (nginx-request-peer-certificate request)
		       "secure"
		       "Not secure")))
  (values 200 'text/plain))

(define (filter0 context request response next)
  (put-bytevector (nginx-response-output-port response)
		  (string->utf8
		   (nginx-filter-context-parameter-ref context "key0")))
  (put-bytevector (nginx-response-output-port response)
		  (string->utf8 "filter 0\n"))
  (next request response))
(define (filter1 context request response next)
  (put-bytevector (nginx-response-output-port response)
		  (string->utf8 "filter 1\n"))
  (next request response))
(define (filter2 context request response next)
  (put-bytevector (nginx-response-output-port response)
		  (string->utf8 "filter 2\n"))
  (next request response))
)
