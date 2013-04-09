(define main
  (lambda ()
    (fibo 30)))

(define fibo
  (lambda (n)
    (if (<# n 2)
        n
	(+# (fibo (-# n 1))
	    (fibo (-# n 2))))))

