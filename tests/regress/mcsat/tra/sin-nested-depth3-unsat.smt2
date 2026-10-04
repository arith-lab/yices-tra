(set-logic QF_TRA)
(declare-fun x () Real)
(assert (< (sin (+ 2 (sin (+ x (sin 2))))) 0)) 
(check-sat)
