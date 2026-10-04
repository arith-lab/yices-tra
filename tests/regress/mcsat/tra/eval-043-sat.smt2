(set-logic QF_TRA)
; exp(exp 4) plus exp(exp 3)
; value astronomically large and positive
(assert (> (+ (exp (exp 4)) (exp (exp 3))) 0))
(check-sat)
