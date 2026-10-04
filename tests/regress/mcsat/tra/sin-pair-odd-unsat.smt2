(set-logic QF_TRA)
(declare-fun x () Real)
; unsat, with zero margin: sin(-x) = -sin x (sum family, d = 0: the equation has no pi).
(assert (> (+ (sin x) (sin (- x))) 0))
(check-sat)
