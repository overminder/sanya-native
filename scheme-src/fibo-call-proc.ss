(define main
  (lambda ()
    (display# (fibo 40))
    (newline#)))

(define +
  (lambda (x y)
    (if (integer?# x)
        (if (integer?# y)
	    (+# x y)
	    (error# '+-rhs-is-not-integer))
	(error# '+-lhs-is-not-integer))))

(define -
  (lambda (x y)
    (if (integer?# x)
        (if (integer?# y)
	    (-# x y)
	    (error# '--rhs-is-not-integer))
	(error# '--lhs-is-not-integer))))

(define <
  (lambda (x y)
    (if (integer?# x)
        (if (integer?# y)
	    (<# x y)
	    (error# '<-rhs-is-not-integer))
	(error# '<-lhs-is-not-integer))))

(define fibo
  (lambda (n)
    (if (< n 2)
        n
	(+ (fibo (- n 1))
	   (fibo (- n 2))))))

