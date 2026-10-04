(set-logic QF_TRA)
(declare-fun x () Real)
; sat: x = 1/2 gives sin(3/2) = 0.9974... > sin(1/2) + 1/2 = 0.9794... A control: the multiple-angle
; relations are valid, so the answer stays sat.
(assert (<= 0 x 1))
(assert (> (sin (* 3 x)) (+ (sin x) (/ 1 2))))
(check-sat)
