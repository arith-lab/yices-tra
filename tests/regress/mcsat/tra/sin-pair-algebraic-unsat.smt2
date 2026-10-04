(set-logic QF_TRA)
(declare-fun x () Real)
; unsat, with zero margin: sin(x + 2 pi) = sin x, with x = +-sqrt 2 algebraic on the trail. The pair is
; found from the exact difference of the two algebraic arguments.
(assert (= (* x x) 2.0))
(assert (< (sin x) (sin (+ x (* 2 pi)))))
(check-sat)
