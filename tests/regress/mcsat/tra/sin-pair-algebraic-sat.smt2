(set-logic QF_TRA)
(declare-fun x () Real)
; sat: x = sqrt 2 gives sin x + sin(x + 2 pi) = 2 sin(sqrt 2) = 1.9755... > 1.9. A control: the pair
; relations are valid, so the answer stays sat.
(assert (= (* x x) 2.0))
(assert (> (+ (sin x) (sin (+ x (* 2 pi)))) 1.9))
(check-sat)
