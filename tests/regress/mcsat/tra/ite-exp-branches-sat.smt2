(set-logic QF_TRA)
; for x in (ln 2, 1) the first branch exp x exceeds 2
(declare-fun x () Real)
(assert (> x 0))
(assert (< x 1))
(assert (> (ite (>= x 0.5) (exp x) (exp (- x))) 2))
(check-sat)
