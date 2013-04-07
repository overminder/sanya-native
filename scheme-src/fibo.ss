(define main
  (lambda ()
    (fibo 10)))

(define fibo
  (lambda (n)
    (if (<# n 2)
        n
	(+# (fibo (-# n 1))
	    (fibo (-# n 2))))))

