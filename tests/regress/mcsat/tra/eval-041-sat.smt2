(set-logic QF_TRA)
; exp(exp 6) minus itself, exactly zero
; value exactly 0
(assert (>= (- (exp (exp 6)) (exp (exp 6))) 0))
(check-sat)
