(set-logic QF_TRA)
; e^2 - e*e is exactly 0, so the assertion holds  =>  sat
; Only answerable delta-sat: the two sides are abstracted independently, so the
; difference is a tiny interval around 0 and never exactly [0,0].
(assert (= (- (exp 2) (* (exp 1) (exp 1))) 0))
(check-sat)
