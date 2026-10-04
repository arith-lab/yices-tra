(set-logic QF_TRA)
(declare-fun x () Real)
; unsat: with s = sin x and t = sin(2 pi/3 - x), s^2 + t^2 - s t = 3/4 (sum family, d = 2/3).
(assert (< (- (+ (* (sin x) (sin x)) (* (sin (- (* (/ 2 3) pi) x)) (sin (- (* (/ 2 3) pi) x)))) (* (sin x) (sin (- (* (/ 2 3) pi) x)))) 0.74))
(check-sat)
