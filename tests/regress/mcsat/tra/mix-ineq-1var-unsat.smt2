(set-logic QF_TRA)
(declare-fun x () Real)
(assert (<= (sin (exp (sin x))) 0))
(check-sat)
