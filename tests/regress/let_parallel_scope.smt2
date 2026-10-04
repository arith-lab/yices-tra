; A let with several bindings must remove all of its names when it ends: before the fix, the
; first binding (x := 0.0) stayed visible, so (> x 3.0) was read as (> 0.0 3.0) (unsat).
(set-logic QF_LRA)
(declare-fun x () Real)
(assert (let ((x 0.0) (u 1.0)) (< u 2.0)))
(assert (> x 3.0))
(check-sat)
