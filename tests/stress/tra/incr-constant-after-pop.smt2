; The constant 1/2 of (sin 0.5) is first registered inside a popped scope; after the pop it must
; still have its value (it was decided freely: wrong unsat, debug assert in conflict.c).
(set-logic QF_TRA)
(declare-fun x () Real)
(declare-fun a () Bool)
(assert (> x 0))
(assert (< x 1))
(push 1)
(assert (= (sin x) (sin 0.5)))
(pop 1)
(assert (=> a (= (sin x) (sin 0.5))))
(check-sat)
