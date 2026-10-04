(set-logic QF_TRA)
(declare-fun x () Real)
; unsat, with zero margin: sin(x + 2 pi) = sin x, where x = 1/3 is the root in (0, 1) of
; (3x - 1)(x^2 - 2), which NA stores as an algebraic number of degree 3.
(assert (= (+ (* 3 x x x) (* (- 1) x x) (* (- 6) x) 2) 0))
(assert (> x 0.0))
(assert (< x 1.0))
(assert (< (sin x) (sin (+ x (* 2 pi)))))
(check-sat)
