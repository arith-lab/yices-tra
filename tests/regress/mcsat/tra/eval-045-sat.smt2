(set-logic QF_TRA)
; quotient of two transcendental sums
; value =~ 0.977526360549
(assert (> (/ (+ (exp 1) (sin 1)) (+ pi (/ 1 2))) (/ 962467 985580)))
(check-sat)
