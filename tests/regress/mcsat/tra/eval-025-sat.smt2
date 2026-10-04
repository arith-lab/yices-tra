(set-logic QF_TRA)
; exp is always positive, so adding 1 keeps it above 0  =>  sat
(assert (> (+ 1 (exp (exp (exp 4)))) 0))
(check-sat)
