; MCSAT asserts the conjuncts of an assertion separately, so the equality
; (= x (exp L)) is solved by the preprocessor (x is an alias of (exp 1)), hence sat.
(set-logic QF_TRA)
(declare-fun x () Real)
(declare-fun L () Real)
(assert (and (= x (exp L)) (= L 1) (< 2 x) (< x 3)))
(check-sat)
