
(define main
  (lambda ()
    (cons# 1 (plainLoop))))

(define plainLoop
  (lambda ()
    (cons# 1 2)
    (plainLoop)))

