(set-logic QF_TRA)
; 5th power of pi/2, factor in (1,2)
; value =~ 9.56311514954
(assert (> (* (/ pi 2) (/ pi 2) (/ pi 2) (/ pi 2) (/ pi 2)) (/ 8013529 838801)))
(check-sat)
