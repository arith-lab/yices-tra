(set-logic QF_TRA)
(declare-fun x () Real)
; sat: 2 sin x < -1.5 on (-2, -0.85). A control: the pair relations are valid, so the answer
; stays sat.
(assert (> x (- 2.0)))
(assert (< x 0.0))
(assert (< (- (sin x) (sin (+ x pi))) (- 1.5)))
(check-sat)
