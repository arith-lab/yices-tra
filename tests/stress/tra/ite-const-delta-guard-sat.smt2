(set-logic QF_TRA)
; sin pi = 0: the guard sits on the boundary and is decided only up to delta
(declare-fun y () Real)
(assert (= y (ite (>= (sin pi) 0) 1 (- 1))))
(assert (> y 0))
(check-sat)
