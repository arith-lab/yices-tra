(set-logic QF_TRA)
(assert (< (exp (* (sin (* (+ (exp 3) (exp 2)) (- (exp 1) (sin 1)))) 4)) 1000000))
; (- (exp (* (sin (* (+ (exp 3) (exp 2)) (- (exp 1) (sin 1)))) 4)) 1000000) =~ -999952.851502  =>  sat
(check-sat)
