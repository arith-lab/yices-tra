(set-logic QF_TRA)
(declare-fun x () Real)
; unsat, with zero margin: cos 2x = sin(2x + pi/2) = 1 - 2 sin^2 x (multiple-angle relation, k = 2,
; d = 1/2).
(assert (< (sin (+ (* 2 x) (* (/ 1 2) pi))) (- 1 (* 2 (sin x) (sin x)))))
(check-sat)
