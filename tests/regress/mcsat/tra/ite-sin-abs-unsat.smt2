(set-logic QF_TRA)
; y = sin |x| with |x| < 1/2, so y < sin(1/2) < 0.48 < 0.5
(declare-fun x () Real)
(declare-fun y () Real)
(assert (< x 0.5))
(assert (> x (- 0.5)))
(assert (= y (ite (>= x 0) (sin x) (- (sin x)))))
(assert (> y 0.5))
(check-sat)
