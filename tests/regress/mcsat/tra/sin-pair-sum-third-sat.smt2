(set-logic QF_TRA)
(declare-fun x () Real)
; sat: sin x - sin(pi/3 - x) > 1.6 on (1.70, 2.49). A control: the pair relations are valid, so the answer
; stays sat.
(assert (> x 1.0))
(assert (< x 3.0))
(assert (> (- (sin x) (sin (- (/ pi 3) x))) 1.6))
(check-sat)
