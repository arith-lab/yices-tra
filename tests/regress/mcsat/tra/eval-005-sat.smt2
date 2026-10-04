(set-logic QF_TRA)
(assert (< (* (+ (exp 2) (+ (sin 1) 2)) (sin (exp 1))) 5))
; (- (* (+ (exp 2) (+ (sin 1) 2)) (sin (exp 1))) 5) =~ -0.797491  =>  sat
(check-sat)
