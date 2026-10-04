(set-logic QF_TRA)
(declare-fun x () Real)
; sat: sin x + sin(x + pi/3) > 1.6 on (0.65, 1.44). A control: the pair relations are valid, so the answer
; stays sat.
(assert (> x 0.0))
(assert (< x 2.0))
(assert (> (+ (sin x) (sin (+ x (/ pi 3)))) 1.6))
(check-sat)
