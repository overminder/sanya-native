(define main
  (lambda ()
    (loopAlloc 0)))

(define loopAlloc
  (lambda (n)
    (traceTwo (cons# 1 2) (cons# 1 2))
    (if (<# 1000 n)
        (begin
          (trace# n (loopAlloc 0)))
	(loopAlloc (+# n 1)))))

(define traceTwo
  (lambda (a b)
    (trace# a b)))

