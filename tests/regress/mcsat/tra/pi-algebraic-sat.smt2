(set-logic QF_TRA)
(declare-fun x () Real)
(declare-fun y () Real)
(assert (> x (/ pi 3)))
(assert (= y 2)) 
(assert (= y (* x x))) 
; A bit weird: NA is never asked to decide y, "someone" derives y = 2.
(check-sat)
