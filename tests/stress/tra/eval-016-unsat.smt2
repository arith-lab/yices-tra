(set-logic QF_TRA)
; sin(exp(exp 16)) = -0.99984  =>  unsat
; Slow: pinning the sign needs exp(16) to about 12.8 million bits.
(assert (> (sin (exp (exp 16))) 0))
(check-sat)
