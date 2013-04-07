(define main
  (lambda ()
    (loopsum 0 10 0)))

(define loopsum
  (lambda (start end s)
    (trace# s (if (<# start end)
        (loopsum (+# start 1) end (+# start s))
	s))))

