(define main
  (lambda ()
    (myif 1 2 3)))

(define myif
  (lambda (x y z)
    (if (<# x y) (+# z y) z)))

