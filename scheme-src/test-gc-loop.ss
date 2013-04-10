(define main
  (lambda ()
    (loopAlloc 0)))

(define loopAlloc
  (lambda (n)
    (cons# 1 2)
    (if (<# 1000 n)
        (begin
          (trace# n (loopAlloc 0)))
	(loopAlloc (+# n 1)))))

