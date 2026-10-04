; (< x 1.0) is the negative literal (not (>= x 1.0)). Analyzed as a child of the or, it is a
; dependent of x; waking it passed a negative term to the variable lookup (a debug assert).
; It is checked through its atom.
(set-logic QF_TRA)
(declare-fun x () Real)
(declare-fun b () Bool)
(assert (> (ite (or (< x 1.0) b) x 0.0) 0.5))
(check-sat)
