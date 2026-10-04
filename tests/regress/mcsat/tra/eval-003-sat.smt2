(set-logic QF_TRA)
(assert (< (exp (* (sin (+ (exp 5) (/ (- 2) 1))) 7)) 100000000000))
; (- (exp (* (sin (+ (exp 5) (/ (- 2) 1))) 7)) 100000000000) =~ -99999999246.797897  =>  sat
(check-sat)
