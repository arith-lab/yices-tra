(set-logic QF_TRA)
; x = 1 gives sin 1 < 1; x = 0 gives 2
(declare-fun x () Real)
(assert (or (= x 0) (= x 1)))
(assert (< (ite (= x 1) (sin x) 2) 1))
(check-sat)
