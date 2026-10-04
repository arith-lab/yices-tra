; The sin anchors (sin pi) = 0 and (sin 0) = 0 must hold after a pop, also when the first sin
; application has a purified argument (before the fix: timeout, since they were emitted once).
(set-logic QF_TRA)
(declare-fun x () Real)
(push 1)
(assert (> (sin (+ x 1)) 0.5))
(pop 1)
(assert (not (= (sin pi) 0)))
(check-sat)
