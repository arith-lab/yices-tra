(set-logic QF_TRA)
; sin(exp(exp 8)) = -0.75455  =>  unsat
(assert (> (sin (exp (exp 8))) 0))
(check-sat)
