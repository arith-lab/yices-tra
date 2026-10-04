; The atoms that the preprocessor creates for a distinct are original constraints, which the TRA
; plugin must check (an earlier version answered sat). Here x = 0, so (exp x) = 1.
(set-logic QF_TRA)
(declare-fun x () Real)
(assert (<= x 0))
(assert (>= x 0))
(assert (distinct (exp x) 1 5))
(check-sat)
