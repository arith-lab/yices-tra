(set-logic QF_TRA)
(declare-fun x () Real)
; unsat, with zero margin: sin(pi - 3x) = sin 3x = 3 sin x - 4 sin^3 x (multiple-angle relation,
; k = -3, d = 1).
(assert (< (sin (- pi (* 3 x))) (- (* 3 (sin x)) (* 4 (sin x) (sin x) (sin x)))))
(check-sat)
