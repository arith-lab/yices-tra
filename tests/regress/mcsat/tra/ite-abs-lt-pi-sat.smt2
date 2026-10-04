(set-logic QF_TRA)
; |x| < 1 < pi
(declare-fun x () Real)
(assert (< x 1))
(assert (> x (- 1)))
(assert (< (ite (>= x 0) x (- x)) pi))
(check-sat)
