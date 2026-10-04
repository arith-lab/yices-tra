(set-logic QF_TRA)
; sin(exp(exp 6)) = -0.40873  =>  unsat
(assert (> (sin (exp (exp 6))) 0))
(check-sat)
