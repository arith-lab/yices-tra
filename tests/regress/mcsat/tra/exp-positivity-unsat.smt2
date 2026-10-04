(set-logic QF_TRA)
(declare-fun x () Real)
; unsat. To check if lemma (exp x) > 0 if correctly implemented
(assert (<= (exp x) 0)) 
(check-sat)
