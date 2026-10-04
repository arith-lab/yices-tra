(set-logic QF_TRA)
(assert (> (+ (* (exp 1) (sin 1)) (* (exp 2) (sin 2)) (* (exp 3) (sin 3))
              (* (exp 4) (sin 4)) (- (exp 5) (sin 5)))
           0))
; (+ (* (exp 1) (sin 1)) (* (exp 2) (sin 2)) (* (exp 3) (sin 3))
;    (* (exp 4) (sin 4)) (- (exp 5) (sin 5))) =~ 119.892743  =>  sat
(check-sat)
