(set-logic QF_TRA)
(declare-fun x () Real)
; sat: sin x - sin(x + 2 pi/3) > 1.6 on (1.70, 2.49). A control: the pair relations are valid, so the answer
; stays sat.
(assert (> x 1.0))
(assert (< x 3.0))
(assert (> (- (sin x) (sin (+ x (* (/ 2 3) pi)))) 1.6))
(check-sat)
