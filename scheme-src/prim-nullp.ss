(define main
  (lambda ()
    (trace# (null?# (cons# 1 2))
	    (trace# (null?# (quote ())) 0))))

