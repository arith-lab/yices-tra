; The one-time lemmas (sin pi) = 0 and the bounds on pi, first added inside a popped scope, must
; hold after the pop (the search did not terminate).
(set-logic QF_TRA)
(declare-fun x () Real)
(push 1)
(assert (> (sin x) 0.5))
(pop 1)
(assert (not (= (sin pi) 0)))
(check-sat)
