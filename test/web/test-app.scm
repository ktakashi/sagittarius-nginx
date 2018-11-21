;; example application for Sagittarius NGINX
(library (web test-app)
    (export run)
    (import (rnrs)
	    (sagittarius)
	    (sagittarius nginx))

(define (run request response)
  (define out (transcoded-port (nginx-response-output-port response)
			       (native-transcoder)))
  (define uri (nginx-request-uri request))
  (define (put-key&value out key value)
    (put-string out key) (put-string out "=")
    (put-string out value) (newline out))
  (cond ((string=? "/test-app/acc" uri)
	 (put-key&value out "uri" uri)
	 (put-key&value out "original-uri" (nginx-request-original-uri request))
	 (put-key&value out "query" (nginx-request-query-string request))
	 (put-key&value out "request-line" (nginx-request-request-line request))
	 )
	(else
	 (put-string out "Test application\n")
	 (nginx-response-header-add! response "X-Sagittarius"
				     (sagittarius-version))))
  (values 200 'text/plain))

)
