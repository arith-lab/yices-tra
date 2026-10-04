(set-logic QF_TRA)
(declare-fun x () Real)
; unsat, with zero margin: sin(pi - x) = sin x (sum family, d = 1).
(assert (< (sin x) (sin (- pi x))))
(check-sat)
