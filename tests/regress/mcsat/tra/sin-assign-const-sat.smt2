(set-logic QF_TRA)
(declare-fun x () Real)
(assert (= x (sin 3))) 
(check-sat)
