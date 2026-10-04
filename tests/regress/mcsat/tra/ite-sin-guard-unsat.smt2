(set-logic QF_TRA)
; sin 4 < 0, so y = -pi, and -pi > 3 is false
(declare-fun x () Real)
(declare-fun y () Real)
(assert (= x 4))
(assert (= y (ite (>= (sin x) 0) pi (- pi))))
(assert (> y 3))
(check-sat)
