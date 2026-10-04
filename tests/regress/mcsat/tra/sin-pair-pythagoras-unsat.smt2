(set-logic QF_TRA)
(declare-fun x () Real)
; unsat: sin(x + pi/2) = cos x, so sin^2 x + sin^2(x + pi/2) = 1 (d = 1/2).
(assert (> (+ (* (sin x) (sin x)) (* (sin (+ x (/ pi 2))) (sin (+ x (/ pi 2))))) 1.01))
(check-sat)
