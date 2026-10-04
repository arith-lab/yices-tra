(set-logic QF_TRA)
(declare-fun x () Real)
(assert (< (sin (+ 2 (sin (+ x (sin pi))))) 0)) 
(check-sat)
