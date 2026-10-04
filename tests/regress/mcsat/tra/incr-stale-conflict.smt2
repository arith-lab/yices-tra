; A conflict found at the base level of a popped scope must not be reported again after the pop
; (the TRA plugin kept it in cache_conflicts).
(set-logic QF_TRA)
(declare-fun x () Real)
(push 1)
(assert (= pi 3.14159264))
(check-sat)
(pop 1)
(assert (> x 0))
(check-sat)
(push 1)
(assert (> pi 3.15))
(pop 1)
(check-sat)
