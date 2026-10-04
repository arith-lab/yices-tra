(set-logic QF_TRA)
; exp(exp 4) times sin(1)
; value astronomically large and positive
(assert (> (* (exp (exp 4)) (sin 1)) 0))
(check-sat)
