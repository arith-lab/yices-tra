(set-logic QF_TRA)
(declare-fun x () Real)
; unsat: with s = sin x and t = sin(x + pi/3), s^2 + t^2 - s t = 3/4 (d = 1/3).
(assert (> (- (+ (* (sin x) (sin x)) (* (sin (+ x (/ pi 3))) (sin (+ x (/ pi 3))))) (* (sin x) (sin (+ x (/ pi 3))))) 0.76))
(check-sat)
