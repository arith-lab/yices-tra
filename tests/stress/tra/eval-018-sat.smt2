(set-logic QF_TRA)
; The denominator is exactly 0, and division by zero is uninterpreted in SMT-LIB,
; so a model may give the quotient any value  =>  sat
(assert (> (/ 1 (- (exp 2) (* (exp 1) (exp 1)))) 0))
(check-sat)
