(set-logic QF_TRA)
(declare-fun x () Real)
; unsat: sin(x + pi) = -sin x (difference family, d = 1).
(assert (> (+ (sin x) (sin (+ x pi))) 0.01))
(check-sat)
