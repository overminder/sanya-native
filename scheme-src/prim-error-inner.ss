(define main
  (lambda ()
    (inner-error 0)))

(define id
  (lambda (x)
    x))

(define inner-error
  (lambda (n)
    (if (<# n 10)
        (inner-error (+# n 1))
	(error# (quote oops)))))

