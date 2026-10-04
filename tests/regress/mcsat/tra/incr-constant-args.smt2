; Constants registered inside a popped scope (1, 2, 0) must keep their values after the pop.
(set-logic QF_TRA)
(push 1)
(assert (> (+ (sin 1) (exp 2)) 0))
(check-sat)
(pop 1)
(assert (> (sin pi) 0))
(check-sat)
