(define main
  (lambda ()
    (trace# (integer?# 1)
	    (trace# (integer?# main)
		    0))))

