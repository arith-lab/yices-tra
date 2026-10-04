(set-logic QF_TRA)
; product of sums divided by a sum
; value =~ 46.1894612838
(assert (> (/ (* (+ (exp 1) (sin 1)) (+ (exp 2) (sin 2)) (+ pi 1)) (+ (exp (/ 1 2)) 1)) (/ 222549 4823)))
(check-sat)
