(set-logic QF_TRA)
; Should take around 3 minutes on a standard PC
(assert (> (sin (exp (exp (exp (/ 57 20))))) 0))
(check-sat)
