(set-logic QF_TRA)
; x = 1 gives y = sin 1 > 0.8; x = 0 gives y = 2
(declare-fun x () Real)
(declare-fun y () Real)
(assert (or (= x 0) (= x 1)))
(assert (= y (ite (= x 1) (sin x) 2)))
(assert (< y 0.8))
(check-sat)
