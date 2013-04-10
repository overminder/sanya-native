(define main
  (lambda ()
    (display# (< 5 'a))
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
	    (error# (tag 'not-an-integer y)))
	(error# (tag 'not-an-integer x)))))

(define car
  (lambda (xs)
    (if (pair?# xs)
        (car# xs)
	(error# (tag 'not-a-pair xs)))))

(define tag
  (lambda (x y)
    (cons# x (cons# y '()))))

