(set-logic QF_TRA)
; three distinct factors, each squared
; value =~ 60.2976842697
(assert (> (* (exp 1) (exp 1) (sin 2) (sin 2) pi pi) (/ 40704149 675729)))
(check-sat)
