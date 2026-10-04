; After the pop, x keeps its MCSAT variable; (= x 0.5) must not be solved as a first occurrence of
; x, or the model reports the stale value of the popped scope.
(set-option :produce-models true)
(set-logic QF_TRA)
(declare-fun x () Real)
(push 1)
(assert (> (exp x) 10))
(assert (< x 2))
(check-sat)
(pop 1)
(assert (= x 0.5))
(check-sat)
(get-model)
