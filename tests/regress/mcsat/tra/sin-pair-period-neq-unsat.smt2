(set-logic QF_TRA)
(declare-fun x () Real)
; unsat: sin(x - 4 pi) = sin x (d = -4). An equality atom: the conflict cuts either side.
(assert (not (= (sin (- x (* 4 pi))) (sin x))))
(check-sat)
