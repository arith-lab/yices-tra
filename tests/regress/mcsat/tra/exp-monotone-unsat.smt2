(set-logic QF_TRA)
(declare-fun x () Real)
(declare-fun y () Real)
; unsat, with zero margin (y -> 0 and x -> 1): (exp x) = y + (exp 1) > (exp 1) gives x > 1,
; since exp is increasing. No delta refutes it: it needs the monotonicity conflict.
(assert (< x 1))
(assert (> y 0))
(assert (= (exp x) (+ y (exp 1))))
(check-sat)
