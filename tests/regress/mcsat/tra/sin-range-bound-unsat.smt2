(set-logic QF_TRA)
(declare-fun x () Real)
; unsat. To check if lemmas stating -1 <= sin _ <= 1 are correctly implemented
(assert (or (= (sin x) 2) (= (sin x) (- 2)))) 
(check-sat)
