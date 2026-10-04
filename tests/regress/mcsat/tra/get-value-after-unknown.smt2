; After unknown, get-model answers with respect to a candidate model (SMT-LIB 2.6, Section
; 4.2.5); debug builds failed an assert in build_model. Ball arithmetic cannot certify
; (sin x) = (sin 0.5), hence unknown.
(set-option :produce-models true)
(set-logic QF_TRA)
(declare-fun x () Real)
(assert (= (sin x) (sin 0.5)))
(assert (and (> x 0.0) (< x 1.0)))
(check-sat)
(get-model)
