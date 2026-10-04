(set-logic QF_TRA)
; sin(exp(exp 10)) = -0.71938  =>  unsat
(assert (> (sin (exp (exp 10))) 0))
(check-sat)
