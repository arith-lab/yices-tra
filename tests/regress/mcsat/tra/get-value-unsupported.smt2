; get-value is unsupported in QF_TRA: the model gives pi, sin and exp approximate values, so the
; printed values of terms could be approximations.
(set-option :produce-models true)
(set-logic QF_TRA)
(declare-fun x () Real)
(assert (> (sin x) 0.5))
(check-sat)
(get-value (x (sin x)))
