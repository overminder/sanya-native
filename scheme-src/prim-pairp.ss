(define main
  (lambda ()
    (trace# (pair?# (cons# 1 2))
	    (trace# (pair?# 1) 0))))
