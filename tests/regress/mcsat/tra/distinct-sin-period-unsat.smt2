; The atoms that the preprocessor creates for a distinct are original constraints, which the TRA
; plugin must check (an earlier version answered sat). sin x = sin (x + 2 pi) for every x.
(set-logic QF_TRA)
(declare-fun x () Real)
(assert (distinct (sin x) (sin (+ x (* 2 pi))) 5))
(check-sat)
