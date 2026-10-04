(set-logic QF_TRA)
(assert (> (* (+ (exp 1) 1) (sin 3)) 20))
; (- (* (+ (exp 1) 1) (sin 3)) 20) =~ -19.475276  =>  unsat
(check-sat)
