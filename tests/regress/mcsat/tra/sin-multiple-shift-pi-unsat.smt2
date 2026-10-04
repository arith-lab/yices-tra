(set-logic QF_TRA)
(declare-fun x () Real)
; unsat, with zero margin: sin(3x + pi) = -sin 3x = 4 sin^3 x - 3 sin x (multiple-angle relation,
; k = 3, d = 1: the sign changes with d).
(assert (> (sin (+ (* 3 x) pi)) (- (* 4 (sin x) (sin x) (sin x)) (* 3 (sin x)))))
(check-sat)
