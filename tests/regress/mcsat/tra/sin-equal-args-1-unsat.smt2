(set-logic QF_TRA)
(declare-fun x () Real)
(declare-fun y () Real)
(assert (= x 2)) 
(assert (= y 3))
(assert (< (sin (+ x 1)) (sin y)))
(check-sat)

