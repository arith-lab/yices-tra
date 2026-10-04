(set-logic QF_TRA)
; rational coefficient times deep nest
; value =~ -0.800886293078
(assert (< (* (/ 355 113) (sin (exp (exp 2)))) (- 0 (/ 599548 749355))))
(check-sat)
