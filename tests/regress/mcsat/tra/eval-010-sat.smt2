(set-logic QF_TRA)
(assert (> (+ (exp (* (sin (exp 1)) 2))
              (- (sin (exp 2)) (exp (sin 3)))
              (* (exp (sin 4)) (sin (exp 5))))
           0))
; (+ (exp (* (sin (exp 1)) 2))
;    (- (sin (exp 2)) (exp (sin 3)))
;    (* (exp (sin 4)) (sin (exp 5)))) =~ 1.693702  =>  sat
(check-sat)
