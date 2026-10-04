(set-logic QF_TRA)
(assert (< (* (exp (/ 1 2)) (+ (sin 1) 2)) 10))
; (- (* (exp (/ 1 2)) (+ (sin 1) 2)) 10) =~ -5.315206  =>  sat
(check-sat)
