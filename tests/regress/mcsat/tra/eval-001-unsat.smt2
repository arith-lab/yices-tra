(set-logic QF_TRA)
(assert (<= (- (+ (+ (* (exp 1) pi) (/ (sin pi) 3))) 5) 0))
; (- (+ (+ (* (exp 1) pi) (/ (sin pi) 3))) 5) =~ 3.539734  =>  sat
(check-sat)
