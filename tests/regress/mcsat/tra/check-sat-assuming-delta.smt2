; Under the assumption b, the only trails found are delta-consistent: the default mode refines
; delta, which backtracks below the assumptions; they must be decided again (debug builds
; failed an assert in mcsat_backtrack_to). Ball arithmetic cannot certify (sin x) = (sin 0.5),
; hence unknown. Without the assumption, the instance is sat.
(set-logic QF_TRA)
(declare-fun x () Real)
(declare-fun b () Bool)
(assert (=> b (= (sin x) (sin 0.5))))
(assert (and (> x 0.0) (< x 1.0)))
(check-sat-assuming (b))
(check-sat-assuming ((not b)))
