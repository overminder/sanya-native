(define main
  (lambda ()
    (trace# (fibo 10) 0)))

(define fibo
  (lambda (n)
    (cons# 1 2)
    (if (<# n 2)
        n
	(+# (fibo (-# n 1))
	    (fibo (-# n 2))))))

