(set-logic QF_TRA)
; quotient with small denominator
; value =~ 2718.28182846
(assert (> (/ (exp 1) (+ (sin pi) (/ 1 1000))) (/ 1230315936 453061)))
(check-sat)
