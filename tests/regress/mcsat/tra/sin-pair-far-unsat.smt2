(set-logic QF_TRA)
(declare-fun x () Real)
; unsat, with zero margin: sin(x - 7 pi/2) = cos x, so the sum of the squares is 1
; (d = -7/2, n = -21).
(assert (< (+ (* (sin x) (sin x)) (* (sin (- x (* (/ 7 2) pi))) (sin (- x (* (/ 7 2) pi))))) 1))
(check-sat)
