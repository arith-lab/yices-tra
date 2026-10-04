(set-logic QF_TRA)
(assert (> (+ (* (exp 1) (sin 1)) (* (exp 2) (sin 2))) 3))
; (+ (* (exp 1) (sin 1)) (* (exp 2) (sin 2))) =~ 9.006205  =>  sat
(check-sat)

; Bug isolation (does NOT crash on a normal run of this file), 
; but crashes with --trace tra::any. The solver aborts with
;   terms/term_substitution.c:1167: subst_composite: Assertion `false' failed.
; This is because there are some parts of term_substitution.c that are still TODO. 
; However, in our case, it seems it is rather because we still have to implement 
; correctly the conflict analysis part of the tra-plugin, and for some reason 
; the NA plugin misbehaves. In particular, it explains the conflict with an
; ARITH_ROOT_ATOM term (since exp1*sin1 + exp2*sin2 is jointly nonlinear in the
; purified sin/exp variables, CAD needs a root atom rather than a plain
; inequality). Asserting that new atom fires tra_plugin_new_term_notify, whose
; tracing calls tra_depurify_term -> _o_yices_subst_term on it, and
; subst_composite has never implemented substitution for ARITH_ROOT_ATOM.
; So, perhaps, even after handling the conflict analysis, we will still have 
; a similar problem, and we will need to properly fix term_substitution.c
