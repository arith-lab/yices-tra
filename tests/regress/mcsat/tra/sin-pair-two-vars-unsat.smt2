(set-logic QF_TRA)
(declare-fun x () Real)
(declare-fun y () Real)
; unsat, with zero margin: x + y = pi gives sin y = sin x. The relation is found from the trail
; values, not from the syntax.
(assert (<= (+ x y) pi))
(assert (>= (+ x y) pi))
(assert (< (sin x) (sin y)))
(check-sat)
