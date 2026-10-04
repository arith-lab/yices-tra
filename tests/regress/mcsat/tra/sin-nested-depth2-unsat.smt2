(set-logic QF_TRA)
(declare-fun x () Real)
; unsat: sin(2+sin(x)) is always positive.
(assert (< (sin (+ 2 (sin x))) 0)) 
(check-sat)
