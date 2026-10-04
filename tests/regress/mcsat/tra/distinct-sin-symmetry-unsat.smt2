; The atoms that the preprocessor creates for a distinct are original constraints, which the TRA
; plugin must check (an earlier version answered sat). Here x = 1, and sin 1 = sin (pi - 1).
(set-logic QF_TRA)
(declare-fun x () Real)
(assert (<= x 1))
(assert (>= x 1))
(assert (distinct (sin x) (sin (- pi x)) 5))
(check-sat)
