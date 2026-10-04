(set-logic QF_TRA)
(declare-fun x () Real)
; unsat in the scope (sin(x + 2 pi) = sin x), then sat after the pop: the pair conflicts
; learned in the scope do not survive it wrongly.
(push 1)
(assert (< (sin x) (sin (+ x (* 2 pi)))))
(check-sat)
(pop 1)
(assert (> x 0.0))
(assert (< x 2.0))
(assert (> (+ (sin x) (sin (+ x (* 2 pi)))) 1.5))
(check-sat)
