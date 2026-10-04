(set-logic QF_TRA)
(declare-fun x () Real)
(declare-fun y () Real)
(declare-fun z () Real)
; unsat: by convexity, (exp x) + (exp y) >= 2 (exp (x+y)/2), so the equality gives
; z >= (x+y)/2 + ln 2 > (x+y)/2 + 69/100. The sum strategy of exp refutes it with one
; Jensen conflict on the group (exp x) + (exp y) against (exp z), with c = 1 and A = 2.
(assert (= (+ (exp x) (exp y)) (exp z)))
(assert (< z (+ (/ (+ x y) 2) (/ 69 100))))
(check-sat)
