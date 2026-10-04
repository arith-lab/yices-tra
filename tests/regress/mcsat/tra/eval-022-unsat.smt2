(set-logic QF_TRA)
; sin(exp(exp 9)) = +0.80114  =>  unsat
(assert (< (sin (exp (exp 9))) 0))
(check-sat)
