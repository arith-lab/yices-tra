(set-logic QF_TRA)
(assert (> (+ (* (exp (sin 1)) 2) (- (sin (exp 2)) 3)) 4))
; (- (+ (* (exp (sin 1)) 2) (- (sin (exp 2)) 3)) 4) =~ -1.466591  =>  unsat
(check-sat)
