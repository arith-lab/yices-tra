(set-logic QF_TRA)
; both branches are bounded by 1 in absolute value, so the ite cannot exceed 1
(declare-fun x () Real)
(declare-fun y () Real)
(assert (> x 0))
(assert (< x 1))
(assert (= y (ite (>= x 0.5) (sin x) (- (sin x)))))
(assert (> y 1))
(check-sat)
