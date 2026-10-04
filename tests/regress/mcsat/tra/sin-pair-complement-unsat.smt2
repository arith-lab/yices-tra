(set-logic QF_TRA)
(declare-fun x () Real)
; unsat: sin(pi/2 - x) = cos x, so the sum of the squares is 1 (sum family, d = 1/2).
(assert (> (+ (* (sin x) (sin x)) (* (sin (- (/ pi 2) x)) (sin (- (/ pi 2) x)))) 1.01))
(check-sat)
