(define main
  (lambda ()
    (consumeSomeStack 10)))

(define consumeSomeStack
  (lambda (n)
    (if (<# n 0)
        (plainLoop)
	(begin
	  (consumeSomeStack (-# n 1))
	  0))))

(define plainLoop
  (lambda ()
    (cons# 1 2)
    (plainLoop)))

