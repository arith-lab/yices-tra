(set-logic QF_TRA)
(declare-fun x () Real)
; unsat: with s = sin x and t = sin(pi/3 - x), s^2 + t^2 + s t = 3/4 (sum family, d = 1/3).
(assert (> (+ (* (sin x) (sin x)) (* (sin (- (/ pi 3) x)) (sin (- (/ pi 3) x))) (* (sin x) (sin (- (/ pi 3) x)))) 0.76))
(check-sat)
