
(define foo
  (lambda () 0))

(define main
  (lambda ()
    (trace# 1
      (trace# 2
        (trace# 3 4)))))

