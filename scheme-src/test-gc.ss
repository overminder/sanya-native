(define main
  (lambda ()
    (consumeSomeStack 10)))

(define consumeSomeStack
  (lambda (n)
    (if (<# n 0)
        (plainLoop)
	(begin
	  (cons# 1 2)
	  (consumeSomeStack (-# n 1))
	  0))))

(define plainLoop
  (lambda ()
    (cons# 1 2)
    (plainLoop)))

(define consumeAllStack
  (lambda ()
    (cons# 1 2)
    (consumeAllStack)
    1))

