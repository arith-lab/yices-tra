; A let with several bindings must remove all of its names when it ends: before the fix, the
; name pi still denoted y after the let, the sin plugin bounded y as if it were pi, and the
; instance was answered unsat.
(set-logic QF_TRA)
(declare-fun x () Real)
(declare-fun y () Real)
(assert (let ((pi y) (u 1.0)) (< u 2.0)))
(assert (> y 4.0))
(assert (> (sin x) 0.5))
(check-sat)
