(set-logic QF_TRA)
(assert (< (- 1 (+ (exp (- 1)) (sin (- 3)))) 100))
; (- (- 1 (+ (exp (- 1)) (sin (- 3)))) 100) =~ -99.226759  =>  sat
(check-sat)
