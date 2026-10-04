(set-logic QF_TRA)
; sin(exp(exp 7)) = -0.97544  =>  unsat
(assert (> (sin (exp (exp 7))) 0))
(check-sat)
