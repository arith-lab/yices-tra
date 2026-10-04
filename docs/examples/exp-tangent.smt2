(set-logic QF_TRA)
(declare-fun x () Real)
(assert (< (exp x) (+ 1 x)))
(check-sat)
