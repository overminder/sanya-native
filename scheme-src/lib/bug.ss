
(define getSomeList
  (lambda ()
    '(1 2 3 4 5)))

(define main
  (lambda ()
    (display# (loop (getSomeList)))))

(define loop
  (lambda (x)
    (loop (cons# (car# '(1 2)) '(3 4)))))

