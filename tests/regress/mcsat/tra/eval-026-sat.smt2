(set-logic QF_TRA)
; exp(exp(exp 4)) is astronomically larger than 1  =>  sat
(assert (> (exp (exp (exp 4))) 1))
(check-sat)
