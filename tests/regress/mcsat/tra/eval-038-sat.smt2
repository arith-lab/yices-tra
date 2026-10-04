(set-logic QF_TRA)
; 6th power of exp(3), large magnitude
; value =~ 65659969.1373
(assert (> (* (exp 3) (exp 3) (exp 3) (exp 3) (exp 3) (exp 3)) (/ 47560531342891 725071)))
(check-sat)
