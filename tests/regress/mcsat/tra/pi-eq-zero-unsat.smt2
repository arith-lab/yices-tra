(set-logic QF_TRA)
; (= pi 0) is an ARITH_EQ_ATOM of arity 1: the preprocessor must not solve it
; for pi, which would record the substitution pi := 0 and answer sat.
(assert (= pi 0))
(check-sat)
