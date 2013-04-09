(define main
  (lambda ()
    (loopPrint 0 10 1)))

(define loopPrint
  (lambda (start end x)
    (if (<# start end)
        (trace# x (loopPrint (+# start 1) end x))
	0)))

