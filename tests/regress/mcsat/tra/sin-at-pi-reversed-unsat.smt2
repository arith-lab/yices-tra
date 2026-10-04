(set-logic QF_TRA)
; sin-at-pi-unsat.smt2 with the assertions in the other order, so that x is a
; first-time seen variable when (= x pi) is preprocessed. Solving it for x is
; sound (x is replaced by pi everywhere), solving it for pi is not.
(declare-fun x () Real)
(assert (= x pi))
(assert (= (sin x) 1))
(check-sat)
