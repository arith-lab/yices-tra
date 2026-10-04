(set-logic QF_TRA)
(declare-fun x () Real)
; unsat: with s = sin x and t = sin(x + 2 pi/3), s^2 + t^2 + s t = 3/4 (d = 2/3).
(assert (< (+ (* (sin x) (sin x)) (* (sin (+ x (* (/ 2 3) pi))) (sin (+ x (* (/ 2 3) pi)))) (* (sin x) (sin (+ x (* (/ 2 3) pi))))) 0.74))
(check-sat)
