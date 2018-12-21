;; example application for Sagittarius NGINX
;; simple REPL
(library (repl)
    (export run init clean)
    (import (rnrs)
	    (rnrs eval)
	    (redis)
	    (srfi :19 time)
	    (srfi :39 parameters)
	    (rfc uuid)
	    (rfc cookie)
	    (sagittarius nginx))
(define *redis-connection* (make-parameter #f))
    
(define (init context)
  (*redis-connection*
   (redis-connection-open! 
    (make-redis-connection
     (nginx-context-parameter-ref context "redis-host")
     (nginx-context-parameter-ref context "redis-port")))))

(define (clean context)
  (redis-connection-close! (*redis-connection*)))

(define (run request response) 
  (define out (transcoded-port (nginx-response-output-port response)
			       (native-transcoder)))
  (define context (nginx-request-context request))
  (define session (get-session request response))
  (define (save e*)
    (let-values (((out extract) (open-string-output-port)))
      (write e* out)
      (redis-set (*redis-connection*) (cookie-name session) (extract))))
  (define (eval-history bv env)
    (let ((in (open-bytevector-input-port bv (native-transcoder))))
      (let loop ((e* (read in)) (last #f))
	(if  (null? e*)
	     last
	     (loop (cdr e*) (eval (car e*) env))))))
  (let* ((history (redis-get (*redis-connection*) (cookie-name session)))
	 (env (environment '(rnrs)))
	 (last-result (cond ((and history (eval-history history env)))
			    (else #f))))
    (case (string->symbol (nginx-request-method request))
      ((POST)
       (let ((in (transcoded-port (nginx-request-input-port request)
				  (native-transcoder))))
	 (let loop ((r last-result) (e* '()))
	   (let ((e (read in)))
	     (cond ((eof-object? e)
		    (save (reverse e*))
		    (display r out)
		    (newline out))
		   (else (loop (eval e env) (cons e e*))))))))
      (else (display last-result out) (newline out)))
    (values 200 'text/plain)))

(define period (make-time 'time-duration 0 900))
(define (get-session request response)
  (define (session-id=? name cookie) (string=? name (cookie-name cookie)))
  (cond ((member "SESSION-ID" (nginx-request-cookies request) session-id=?)
	 => car)
	(else
	 (let* ((time (add-duration (current-time) period))
		(path (nginx-context-path (nginx-request-context request)))
		(cookie (make-cookie "SESSION-ID" (uuid->string (make-v4-uuid))
				     :path path
				     :expires (time-utc->date time))))
	   (nginx-response-header-add! response
	    "Set-Cookie" (cookie->string cookie))
	   cookie))))
	 

)
