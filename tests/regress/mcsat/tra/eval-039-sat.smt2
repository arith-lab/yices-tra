(set-logic QF_TRA)
; sin of exp(exp 3)
; value =~ -0.608104702078
(assert (< (sin (exp (exp 3))) (- 0 (/ 392783 646560))))
(check-sat)
