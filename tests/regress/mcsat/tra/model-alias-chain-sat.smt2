; x := (sin y) and w := (ite (> y 0) (sin (sin y)) 1) contain applications that never reach the
; trail; z := (ite (> y 0) pi 1) needs a value for pi. The TRA plugin gives all of them values.
(set-option :produce-models true)
(set-logic QF_TRA)
(declare-fun x () Real)
(declare-fun y () Real)
(declare-fun z () Real)
(declare-fun w () Real)
(assert (= x (sin y)))
(assert (= y 1))
(assert (= z (ite (> y 0) pi 1)))
(assert (= w (ite (> y 0) (sin (sin y)) 1)))
(check-sat)
(get-model)
