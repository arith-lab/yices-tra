(set-logic QF_TRA)
; 8 summands, coefficients in (1,2)
; value =~ 14.5128742718
(assert (> (+ (* (/ 3 2) (exp (/ 1 2))) (* (/ 7 5) (sin 1)) (* (/ 11 8) pi) (* (/ 13 9) (exp (/ 1 3))) (* (/ 17 12) (sin 2)) (* (/ 19 13) (exp (/ 1 5))) (* (/ 23 16) (sin 3)) (/ 5 4)) (/ 2114673 145856)))
(check-sat)
