; x and y are eliminated by the preprocessor (x := pi, y := (sin 3)); their transcendental
; substitutes never reach the trail, so the TRA plugin supplies their model values.
(set-option :produce-models true)
(set-logic QF_TRA)
(declare-fun x () Real)
(declare-fun y () Real)
(assert (= x pi))
(assert (= y (sin 3)))
(check-sat)
(get-model)
