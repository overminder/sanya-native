(define getSomeList
  (lambda ()
    '(1 2 3 4 5)))

(define reverse
  (lambda (xs)
    (reverse' xs '())))

(define reverse'
  (lambda (in out)
    (if (null?# in)
        out
	(reverse' (cdr in) (cons# (car# in) out)))))

(define cdr
  (lambda (xs)
    (if (pair?# xs)
        (cdr# xs)
	(error# 'cdr-not-a-pair))))

(define main
  (lambda ()
    (display# (reverse (getSomeList)))))

