(set-logic QF_TRA)
(declare-fun x () Real)
; sat: sin x + cos x > 1.3 on (0.38, 1.19). A control: the pair relations are valid, so the answer
; stays sat.
(assert (> x 0.0))
(assert (< x 1.5))
(assert (> (+ (sin x) (sin (+ x (/ pi 2)))) 1.3))
(check-sat)
