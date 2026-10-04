(set-logic QF_TRA)
(declare-fun x () Real)
; unsat, with zero margin: sin(x + 2 pi) = sin x (difference family, d = 2). No delta
; refutes it: it needs the pair conflict.
(assert (< (sin x) (sin (+ x (* 2 pi)))))
(check-sat)
