(define display
  (lambda (x)
    (trace# x 0)))

(define main
  (lambda ()
    (define a 1)
    (display a)
    (set! a 2)
    (display a)
    (set! main 5)
    (display main)
    (main)))

