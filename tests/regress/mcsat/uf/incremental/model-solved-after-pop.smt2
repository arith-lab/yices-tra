; After the pop, x keeps its MCSAT variable; (= x 0.5) must not be solved as a first occurrence of
; x, or the model reports the stale value of the popped scope.
(set-option :produce-models true)
(set-logic QF_UFNRA)
(declare-fun f (Real) Real)
(declare-fun x () Real)
(push 1)
(assert (> (f x) 10))
(assert (> x 3))
(check-sat)
(pop 1)
(assert (= x 0.5))
(check-sat)
(get-value (x))
