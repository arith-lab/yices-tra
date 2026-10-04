(set-logic QF_TRA)
; After x := 1/2 the last two assertions preprocess to the same atom, so two
; original constraints share one preprocessed form.
(declare-fun x () Real)
(assert (= x 0.5))
(assert (>= (sin x) 0))
(assert (>= (sin 0.5) 0))
(check-sat)
