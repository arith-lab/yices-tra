(set-logic QF_TRA)
; sin(exp(exp 11)) = -0.23913  =>  unsat
(assert (> (sin (exp (exp 11))) 0))
(check-sat)
