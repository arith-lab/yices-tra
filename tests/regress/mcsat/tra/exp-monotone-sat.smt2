(set-logic QF_TRA)
(declare-fun x () Real)
(declare-fun y () Real)
; sat: x = 0 and y = 1/2 give (exp y) = 1.6487... < 2 = (exp x) + 1. A control: the
; monotonicity lemmas are valid, so whatever the search learns, the answer stays sat.
(assert (= x 0))
(assert (< x y))
(assert (< (exp y) (+ (exp x) 1)))
(check-sat)
