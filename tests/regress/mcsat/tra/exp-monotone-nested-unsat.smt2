(set-logic QF_TRA)
(declare-fun x () Real)
(declare-fun y () Real)
; unsat, with zero margin (y -> (exp x)): (exp y) >= (exp (exp x)) gives y >= (exp x), since
; exp is increasing. The argument of one application is itself an application.
(assert (< y (exp x)))
(assert (>= (exp y) (exp (exp x))))
(check-sat)
