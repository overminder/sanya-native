(define prn
  (lambda (x)
    (display# x)
    (newline#)))

(define main
  (lambda ()
    (prn 'in-main)
    (append '() '(4 5 6))))

(define append
  (lambda (xs ys)
    (prn 'in-append)
    (prn xs)
    (prn ys)
    (prn (null?# xs))
    (prn (null?# ys))
    (if (null?# xs)
        (trace# 'ys ys)
	(trace# 'cons# (cons# (trace# 'car# (car# (trace# 'get-xs xs)))
			      (append (cdr# xs) ys))))))

