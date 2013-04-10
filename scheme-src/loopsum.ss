(define main
  (lambda ()
    (trace# (loopsum 0 100000 0)
	    0)))

(define loopsum
  (lambda (start end s)
    (if (<# start end)
        (loopsum (+# start 1) end (+# start s))
	s)))

