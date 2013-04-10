
(define main
  (lambda ()
    (alloc-loop 10000000)))

(define alloc-loop
  (lambda (n)
    (cons# 1 2)
    (if (<# n 0) 0
        (alloc-loop (-# n 1)))))

