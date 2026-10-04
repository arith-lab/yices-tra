(set-logic QF_TRA)
(declare-fun n () Real)
(declare-fun r () Real)
(assert (>= n 2))
(assert (>
     (+
        (/ (exp (- (/ (* (- r 1) (- r 1)) (* 2 (- n 1))))) 2)
        (/ (exp (- (/ (* (+ r 1) (+ r 1)) (* 2 (- n 1))))) 2)
      )
     (exp (- (/ (* r r) (* 2 n))))
))
(check-sat)