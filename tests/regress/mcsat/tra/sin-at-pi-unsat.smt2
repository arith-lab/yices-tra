(set-logic QF_TRA)
(declare-fun x () Real)
(assert (= (sin x) 1)) 
(assert (= x pi))
; incorrectly gives sat, while 
; (assert (and (= (sin x) 1) (= x pi))) 
; correctly gives unsat
; similarly, adding 
; (assert (= (sin pi) 0)) 
; also works. Perhaps it is some optimization that removes (= x pi)
; if pi only occurs once?
; Yes, resolved by commenting out some code in preprocessor.c
(check-sat)
