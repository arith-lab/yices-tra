(set-logic QF_TRA)

; Stress test for compute_abstraction: 150 ground facts, all true, so the
; solver must evaluate every one of them (no early termination).
;
; [1]-[20]    same as expr-evaluation-list.smt2
; [21]-[46]   polynomials with rational coefficients   (handle_poly)
; [47]-[72]   power products with repeated factors     (handle_power_product)
; [73]-[98]   divisions by non-constant denominators   (handle_divisibility)
; [99]-[124]  nested transcendental applications        (handle_function_call)
; [125]-[150] polynomial + division + nesting combined
;
; Every denominator is bounded away from 0 by construction. Values were
; computed with mpmath at 60 digits; each bound is the coarsest integer or
; half-integer leaving a margin of at least max(0.25, 2% of the value),
; kept coarse so the rational coefficients stay small.

; [1] 3 function applications, value =~ -0.500000
(assert (< (- (+ (sin pi) (exp 0)) 1.5) 0))

; [2] 4 function applications, value =~ 0.718282
(assert (> (- (* (exp 1) (sin (/ pi 2))) 2) 0))

; [3] 5 function applications, value =~ -0.440247
(assert (< (- (- (+ (exp 1) (sin 1)) (sin 0)) 4) 0))

; [4] 7 function applications, value =~ 0.287355
(assert (> (- (+ (* (exp 1) (sin 1)) (- (sin pi) (exp 0))) 1) 0))

; [5] 8 function applications, value =~ 0.562493
(assert (> (- (* (+ (exp 1) (sin 2)) (- (exp 0) (sin (/ pi 4)))) 0.5) 0))

; [6] 9 function applications, value =~ -0.949665
(assert (< (- (+ (sin (exp 1)) (* (exp (sin 1)) 2) (- (exp 0) (sin pi))) 7) 0))

; [7] 10 function applications, value =~ -2.683703
(assert (< (- (* (+ (exp 1) (sin 1) (exp 2)) (- (sin (exp 1)) (exp (sin 2)))) (- 20)) 0))

; [8] 13 function applications, value =~ 0.840676
(assert (> (- (+ (* (exp 1) (sin 1)) (* (exp 2) (sin 2)) (* (exp 3) (sin 3)) (- (sin pi) (exp 0))) 10) 0))

; [9] 14 function applications, value =~ -0.617164
(assert (< (- (+ (sin (* (exp 1) (sin 2))) (exp (/ (sin (exp 2)) 5)) (* (sin 3) (exp (sin 4))) (exp 0)) 3.5) 0))

; [10] 28 function applications, value =~ 4.075723
(assert (> (+ 50 (* (exp (sin (exp (sin 1)))) (sin (exp (sin (exp 2))))) (* (+ (exp 1) (sin 2) (exp 3) (sin 4)) (- (sin (exp 1)) (exp (sin 2)))) (/ (+ (sin pi) (exp 0)) (+ 1 (exp (sin 1))))) 0))

; [11] 7 function applications, value =~ -2.994363
(assert (< (- (+ (sin (* 2 pi)) (exp 1) (* (sin 1) (exp 1))) 8) 0))

; [12] 9 function applications, value =~ 3.722177
(assert (> (- (* (+ (exp 1) (sin 3)) (- (exp 2) (sin 1)) (sin (/ pi 2))) 15) 0))

; [13] 11 function applications, value =~ -7.291130
(assert (< (* (+ (exp 1) (exp 2) (exp 3)) (- (sin 1) (sin 2) (sin 3)) (/ pi (exp 1))) 0))

; [14] 12 function applications, value =~ 11.306335
(assert (> (- (- (* (exp 1) (exp 2) (sin 1)) (+ (sin (exp 3)) (exp (sin 4)) (sin (sin 5)))) 5) 0))

; [15] 13 function applications, value =~ 1.381413
(assert (> (- (+ (sin (exp (sin 2))) (exp (sin (exp 1))) (* (sin 3) (sin 4)) (/ (exp 1) (exp 2))) 1) 0))

; [16] 15 function applications, value =~ -1009.180782
(assert (< (- (- (* (+ (exp 1) (sin 2)) (- (exp 3) (sin 4))) (* (+ (exp 5) (sin 1)) (- (exp 2) (sin 3)))) 3) 0))

; [17] 17 function applications, value =~ 2.443251
(assert (> (- (+ (* (sin 1) (sin 2) (sin 3)) (exp (+ (sin 1) (sin 2))) (/ (sin (exp 1)) (exp (sin 2))) (sin (exp (exp 0)))) 4) 0))

; [18] 17 function applications, value =~ -0.155557
(assert (< (- (+ (sin (* (exp 1) (sin 2) (exp 3))) (exp (/ (sin (exp 4)) 5)) (* (sin 5) (exp (sin 6))) (exp (sin (exp 0)))) 2) 0))

; [19] 20 function applications, value =~ -54.566292
(assert (< (- (+ (* (exp 1) (sin 2) (exp 3) (sin 4)) (- (sin (exp 1)) (exp (sin 2)) (sin (exp 3))) (/ (+ (sin 1) (exp 1)) (+ 1 (exp (sin 2))))) 15) 0))

; [20] 38 function applications, value =~ -55.247067
(assert (< (- (+ 50 (* (exp (sin (exp (sin 1)))) (sin (exp (sin (exp 2))))) (* (+ (exp 1) (sin 2) (exp 3) (sin 4)) (- (sin (exp 1)) (exp (sin 2)))) (/ (+ (sin pi) (exp 0)) (+ 1 (exp (sin 1)))) (* (sin (exp (sin 3))) (exp (sin (exp 4))) (- (exp 1) (sin 1)))) 60) 0))


; ---- polynomials with rational coefficients ----
; [21] 4 function applications, value =~ 0.994879
(assert (< (- (+ (* (/ 4 3) (sin 5)) (* (/ 1 2) (sin 5)) (* (/ 5 2) (exp (/ 1 2))) (* (/ 3 4) (sin 1)) (- 2)) 2) 0))
; [22] 3 function applications, value =~ 2.396348
(assert (> (- (+ (* (/ 1 4) pi) (* (/ 4 3) (sin (/ pi 2))) (* (/ 3 4) (exp (/ 1 2))) (* (/ 3 3) (sin 5)) 0) 2) 0))
; [23] 2 function applications, value =~ 1.414214
(assert (< (- (+ (* (/ 4 3) (sin 0)) (* (/ 4 2) (sin (/ pi 4))) 0) 2) 0))
; [24] 3 function applications, value =~ 10.755865
(assert (> (- (+ (* (/ 5 4) (sin 1)) (* (/ 4 2) pi) (* (/ 3 2) (sin (/ pi 3))) (* (/ 5 2) (exp (/ 1 2))) (- 2)) 10) 0))
; [25] 2 function applications, value =~ 0.279009
(assert (< (- (+ (* (/ 2 4) (sin 2)) (* (/ 2 4) (exp (/ 1 2))) (- 1)) 1) 0))
; [26] 2 function applications, value =~ 8.162191
(assert (> (- (+ (* (/ 1 4) pi) (* (/ 5 3) (sin 3)) (* (/ 2 2) pi) (* (/ 3 3) (exp 0)) 3) 7) 0))
; [27] 1 function applications, value =~ 20.014100
(assert (< (- (+ (* (/ 5 3) pi) (* (/ 4 2) (exp 2)) 0) 21) 0))
; [28] 2 function applications, value =~ 1.534142
(assert (> (- (+ (* (/ 4 3) (sin 1)) (* (/ 1 4) (exp (/ 1 2))) 0) 1) 0))
; [29] 2 function applications, value =~ 4.894416
(assert (< (- (+ (* (/ 5 3) (exp 1)) (* (/ 3 2) (sin 2)) (- 1)) 6) 0))
; [30] 1 function applications, value =~ 7.202494
(assert (> (- (+ (* (/ 2 2) pi) (* (/ 5 4) (exp (/ 1 2))) 2) 6) 0))
; [31] 1 function applications, value =~ 3.648168
(assert (< (- (+ (* (/ 5 3) pi) (* (/ 1 4) (exp (/ 1 2))) (- 2)) 4) 0))
; [32] 2 function applications, value =~ 1.403283
(assert (> (- (+ (* (/ 4 2) (exp (/ 1 2))) (* (/ 3 4) (sin 3)) (- 2)) 1) 0))
; [33] 3 function applications, value =~ 2.248106
(assert (< (- (+ (* (/ 1 2) (sin 5)) (* (/ 2 3) (sin (/ pi 2))) (* (/ 5 4) (exp (/ 1 2))) 0) 3) 0))
; [34] 3 function applications, value =~ 6.033302
(assert (> (- (+ (* (/ 4 2) (exp (/ 1 2))) (* (/ 4 4) (exp 0)) (* (/ 5 2) (sin (/ pi 3))) (* (/ 1 2) pi) (- 2)) 5) 0))
; [35] 3 function applications, value =~ 3.316676
(assert (< (- (+ (* (/ 2 2) (exp (/ 1 2))) (* (/ 4 4) pi) (* (/ 1 4) (sin 2)) (* (/ 3 2) (sin (/ pi 3))) (- 3)) 4) 0))
; [36] 1 function applications, value =~ 4.985988
(assert (> (- (+ (* (/ 3 4) (sin (/ pi 2))) (* (/ 5 3) pi) (- 1)) 4) 0))
; [37] 2 function applications, value =~ 3.119287
(assert (< (- (+ (* (/ 2 2) (sin (/ pi 4))) (* (/ 1 4) (exp (/ 1 2))) 2) 4) 0))
; [38] 2 function applications, value =~ 6.448295
(assert (> (- (+ (* (/ 5 4) (sin (/ pi 2))) (* (/ 4 3) (exp (/ 1 2))) 3) 6) 0))
; [39] 2 function applications, value =~ -3.134564
(assert (< (- (+ (* (/ 3 3) (sin 5)) (* (/ 1 2) (exp (/ 1 2))) (- 3)) (- 2)) 0))
; [40] 4 function applications, value =~ 6.810379
(assert (> (- (+ (* (/ 1 2) (sin 5)) (* (/ 5 2) (exp 0)) (* (/ 4 4) (sin 3)) (* (/ 4 4) (exp (/ 1 2))) 3) 6) 0))
; [41] 3 function applications, value =~ -2.420710
(assert (< (- (+ (* (/ 5 3) (sin (/ pi 3))) (* (/ 3 4) (sin (/ pi 3))) (* (/ 4 2) (sin 4)) (- 3)) (- 2)) 0))
; [42] 2 function applications, value =~ 1.070112
(assert (> (- (+ (* (/ 2 4) (sin 5)) (* (/ 1 3) (exp (/ 1 2))) 1) 0) 0))
; [43] 3 function applications, value =~ 7.924439
(assert (< (- (+ (* (/ 3 2) (exp 1)) (* (/ 4 3) (exp (/ 1 2))) (* (/ 4 4) (exp (/ 1 2))) 0) 9) 0))
; [44] 1 function applications, value =~ 10.277160
(assert (> (- (+ (* (/ 2 3) pi) (* (/ 5 2) pi) (* (/ 5 4) pi) (* (/ 5 3) (sin 5)) (- 2)) 10) 0))
; [45] 3 function applications, value =~ 1.855958
(assert (< (- (+ (* (/ 2 2) (exp 0)) (* (/ 2 4) (sin 3)) (* (/ 1 4) pi) (* (/ 3 3) (exp 0)) (- 1)) 3) 0))
; [46] 1 function applications, value =~ 2.212153
(assert (> (- (+ (* (/ 1 2) (sin 3)) (* (/ 3 3) pi) (- 1)) 1) 0))

; ---- power products ----
; [47] 5 function applications, value =~ 54.598150
(assert (< (- (* (sin (/ pi 2)) (sin (/ pi 2)) (sin (/ pi 2)) (exp 2) (exp 2)) 56) 0))
; [48] 2 function applications, value =~ 26.828366
(assert (> (- (* pi pi (exp (/ 1 2)) (exp (/ 1 2))) 26) 0))
; [49] 0 function applications, value =~ 97.409091
(assert (< (- (* pi pi pi pi) 100) 0))
; [50] 0 function applications, value =~ 31.006277
(assert (> (- (* pi pi pi) 30) 0))
; [51] 4 function applications, value =~ -0.623502
(assert (< (- (* (sin 5) (sin 5) (sin 5) (sin (/ pi 4))) 0) 0))
; [52] 3 function applications, value =~ 9.869604
(assert (> (- (* (exp 0) (exp 0) (exp 0) pi pi) 9) 0))
; [53] 2 function applications, value =~ 9.075454
(assert (< (- (* (sin 5) (sin 5) pi pi) 10) 0))
; [54] 4 function applications, value =~ 0.649519
(assert (> (- (* (sin (/ pi 3)) (sin (/ pi 3)) (sin (/ pi 3)) (exp 0)) 0) 0))
; [55] 5 function applications, value =~ 0.708073
(assert (< (- (* (sin (/ pi 2)) (sin (/ pi 2)) (sin (/ pi 2)) (sin 1) (sin 1)) 1) 0))
; [56] 3 function applications, value =~ 6.410496
(assert (> (- (* (sin (/ pi 3)) (sin (/ pi 3)) (sin (/ pi 3)) pi pi) 6) 0))
; [57] 2 function applications, value =~ 0.196552
(assert (< (- (* (sin 3) (sin 3) pi pi) 1) 0))
; [58] 4 function applications, value =~ 0.866025
(assert (> (- (* (sin (/ pi 2)) (sin (/ pi 2)) (sin (/ pi 2)) (sin (/ pi 3))) 0) 0))
; [59] 3 function applications, value =~ 12.182494
(assert (< (- (* (exp 1) (exp 1) (exp (/ 1 2))) 13) 0))
; [60] 3 function applications, value =~ -0.625741
(assert (> (- (* (sin 2) (sin 2) (sin 4)) (- 1)) 0))
; [61] 0 function applications, value =~ 97.409091
(assert (< (- (* pi pi pi pi) 100) 0))
; [62] 2 function applications, value =~ 15.503138
(assert (> (- (* pi pi pi (sin (/ pi 4)) (sin (/ pi 4))) 15) 0))
; [63] 5 function applications, value =~ 12.182494
(assert (< (- (* (exp (/ 1 2)) (exp (/ 1 2)) (exp (/ 1 2)) (exp (/ 1 2)) (exp (/ 1 2))) 13) 0))
; [64] 0 function applications, value =~ 31.006277
(assert (> (- (* pi pi pi) 30) 0))
; [65] 2 function applications, value =~ 0.062564
(assert (< (- (* (sin 3) (sin 3) pi) 1) 0))
; [66] 1 function applications, value =~ 26.828366
(assert (> (- (* pi pi (exp 1)) 26) 0))
; [67] 4 function applications, value =~ 7.389056
(assert (< (- (* (exp (/ 1 2)) (exp (/ 1 2)) (exp (/ 1 2)) (exp (/ 1 2))) 8) 0))
; [68] 3 function applications, value =~ 1.236541
(assert (> (- (* (sin (/ pi 3)) (sin (/ pi 3)) (exp (/ 1 2))) 0) 0))
; [69] 3 function applications, value =~ 4.481689
(assert (< (- (* (exp (/ 1 2)) (exp (/ 1 2)) (exp (/ 1 2))) 5) 0))
; [70] 3 function applications, value =~ 6.399110
(assert (> (- (* (exp 1) (exp 1) (sin (/ pi 3))) 6) 0))
; [71] 3 function applications, value =~ 1.648721
(assert (< (- (* (sin (/ pi 2)) (sin (/ pi 2)) (exp (/ 1 2))) 2) 0))
; [72] 4 function applications, value =~ 27.299075
(assert (> (- (* (sin (/ pi 4)) (sin (/ pi 4)) (exp 2) (exp 2)) 26) 0))

; ---- divisions by non-constant denominators ----
; [73] 2 function applications, value =~ 0.560504
(assert (< (- (/ (+ (exp 0) pi) (exp 2)) 1) 0))
; [74] 2 function applications, value =~ 1.423571
(assert (> (- (/ (+ pi (exp 0)) (+ 2 (sin 2))) 1) 0))
; [75] 1 function applications, value =~ 2.311455
(assert (< (- (/ (+ pi pi) (exp 1)) 3) 0))
; [76] 3 function applications, value =~ 2.355828
(assert (> (- (/ (+ (sin (/ pi 4)) (exp (/ 1 2))) (exp 0)) 2) 0))
; [77] 4 function applications, value =~ 0.666667
(assert (< (- (/ (+ (sin (/ pi 2)) (exp 0)) (+ 3 (* (sin 0) (sin 1)))) 1) 0))
; [78] 3 function applications, value =~ 0.238406
(assert (> (- (/ (+ (exp 0) (exp 0)) (+ (exp 2) 1)) (- 1)) 0))
; [79] 4 function applications, value =~ -0.261420
(assert (< (- (/ (+ (sin 3) (sin 5)) (+ 3 (* (sin 2) (sin 3)))) 0) 0))
; [80] 2 function applications, value =~ 1.646554
(assert (> (- (/ (+ pi (exp (/ 1 2))) (+ 2 (sin 2))) 1) 0))
; [81] 3 function applications, value =~ 0.168578
(assert (< (- (/ (+ (sin (/ pi 4)) (sin (/ pi 4))) (+ (exp 2) 1)) 1) 0))
; [82] 3 function applications, value =~ 0.897578
(assert (> (- (/ (+ (sin 4) (exp 2)) (exp 2)) 0) 0))
; [83] 3 function applications, value =~ 3.297443
(assert (< (- (/ (+ (exp (/ 1 2)) (exp (/ 1 2))) (exp 0)) 4) 0))
; [84] 3 function applications, value =~ 0.443409
(assert (> (- (/ (+ (sin 0) (exp (/ 1 2))) (+ (exp 1) 1)) 0) 0))
; [85] 3 function applications, value =~ 1.707107
(assert (< (- (/ (+ (sin (/ pi 4)) (exp 0)) (exp 0)) 2) 0))
; [86] 2 function applications, value =~ 1.570796
(assert (> (- (/ (+ pi (sin 0)) (+ 2 (sin 0))) 1) 0))
; [87] 2 function applications, value =~ 2.070796
(assert (< (- (/ (+ (exp 0) pi) (+ (exp 0) 1)) 3) 0))
; [88] 3 function applications, value =~ 0.296838
(assert (> (- (/ (+ (exp (/ 1 2)) (sin 1)) (+ (exp 2) 1)) 0) 0))
; [89] 4 function applications, value =~ 0.507098
(assert (< (- (/ (+ (exp 0) (sin 2)) (+ 3 (* (sin 1) (sin 2)))) 1) 0))
; [90] 3 function applications, value =~ 0.886819
(assert (> (- (/ (+ (exp (/ 1 2)) (exp (/ 1 2))) (+ (exp 1) 1)) 0) 0))
; [91] 2 function applications, value =~ 2.070796
(assert (< (- (/ (+ pi (sin (/ pi 2))) (+ (exp 0) 1)) 3) 0))
; [92] 4 function applications, value =~ 0.495605
(assert (> (- (/ (+ (exp 0) (sin (/ pi 3))) (+ 3 (* (sin 1) (sin 2)))) 0) 0))
; [93] 4 function applications, value =~ 1.239427
(assert (< (- (/ (+ (exp 0) (exp 1)) (+ 3 (* (sin 0) (sin 1)))) 2) 0))
; [94] 3 function applications, value =~ 0.318827
(assert (> (- (/ (+ (exp (/ 1 2)) (sin (/ pi 4))) (exp 2)) 0) 0))
; [95] 3 function applications, value =~ 1.177914
(assert (< (- (/ (+ (sin (/ pi 4)) (exp (/ 1 2))) (+ (exp 0) 1)) 2) 0))
; [96] 3 function applications, value =~ 1.000000
(assert (> (- (/ (+ (sin (/ pi 2)) (sin 0)) (exp 0)) 0) 0))
; [97] 3 function applications, value =~ -0.251817
(assert (< (- (/ (+ (sin (/ pi 4)) (sin 5)) (exp 0)) 0) 0))
; [98] 2 function applications, value =~ 0.587010
(assert (> (- (/ (+ (sin 5) pi) (+ (exp 1) 1)) 0) 0))

; ---- nested transcendental applications ----
; [99] 3 function applications, value =~ 1.270884
(assert (< (- (exp (/ (sin (/ (sin (/ pi 2)) 2)) 2)) 2) 0))
; [100] 4 function applications, value =~ -0.611758
(assert (> (- (sin (/ (exp (exp (sin (/ pi 4)))) 2)) (- 1)) 0))
; [101] 4 function applications, value =~ 3.540898
(assert (< (- (exp (exp (/ (exp (sin 4)) 2))) 4) 0))
; [102] 3 function applications, value =~ 1.228005
(assert (> (- (exp (/ (sin (exp 1)) 2)) 0) 0))
; [103] 2 function applications, value =~ 11.081075
(assert (< (- (exp (/ (exp (/ pi 2)) 2)) 12) 0))
; [104] 4 function applications, value =~ 0.663813
(assert (> (- (sin (/ (exp (/ (sin (sin 1)) 2)) 2)) 0) 0))
; [105] 3 function applications, value =~ 0.070443
(assert (< (- (sin (sin (/ (sin 3) 2))) 1) 0))
; [106] 2 function applications, value =~ 11.081075
(assert (> (- (exp (/ (exp (/ pi 2)) 2)) 10) 0))
; [107] 3 function applications, value =~ 0.989262
(assert (< (- (sin (exp (/ (sin (/ pi 4)) 2))) 2) 0))
; [108] 2 function applications, value =~ 0.671591
(assert (> (- (sin (/ (exp (/ pi 2)) 2)) 0) 0))
; [109] 4 function applications, value =~ -0.348558
(assert (< (- (sin (exp (exp (/ (exp (/ 1 2)) 2)))) 0) 0))
; [110] 3 function applications, value =~ 1.615146
(assert (> (- (exp (sin (/ (exp 0) 2))) 1) 0))
; [111] 4 function applications, value =~ 4.776530
(assert (< (- (exp (/ (exp (/ (exp (/ (exp (/ 1 2)) 2)) 2)) 2)) 6) 0))
; [112] 3 function applications, value =~ 5.200326
(assert (> (- (exp (exp (/ (exp 0) 2))) 4) 0))
; [113] 4 function applications, value =~ 0.774971
(assert (< (- (exp (sin (exp (exp 2)))) 2) 0))
; [114] 3 function applications, value =~ 0.515362
(assert (> (- (sin (/ (exp (exp (/ 1 2))) 2)) 0) 0))
; [115] 3 function applications, value =~ 0.758606
(assert (< (- (sin (exp (/ (exp (/ 1 2)) 2))) 2) 0))
; [116] 4 function applications, value =~ 2.027628
(assert (> (- (exp (/ (exp (sin (/ (sin (/ pi 4)) 2))) 2)) 1) 0))
; [117] 3 function applications, value =~ 1.598657
(assert (< (- (exp (exp (sin 4))) 2) 0))
; [118] 3 function applications, value =~ 0.469604
(assert (> (- (sin (/ (sin (/ (exp 1) 2)) 2)) 0) 0))
; [119] 2 function applications, value =~ -0.912578
(assert (< (- (sin (exp pi)) 0) 0))
; [120] 3 function applications, value =~ 15.154262
(assert (> (- (exp (exp (sin (/ pi 2)))) 14) 0))
; [121] 3 function applications, value =~ 1.383779
(assert (< (- (exp (/ (sin (sin (/ pi 4))) 2)) 2) 0))
; [122] 4 function applications, value =~ 2.351497
(assert (> (- (exp (/ (exp (/ (exp (/ (sin 3) 2)) 2)) 2)) 2) 0))
; [123] 3 function applications, value =~ 0.633631
(assert (< (- (exp (/ (sin (exp pi)) 2)) 1) 0))
; [124] 4 function applications, value =~ 1.209832
(assert (> (- (exp (sin (/ (exp (sin 5)) 2))) 0) 0))

; ---- mixed: polynomial + division + nesting ----
; [125] 8 function applications, value =~ 187.454334
(assert (< (- (+ (+ (* (/ 1 4) pi) (* (/ 4 2) (exp (/ 1 2))) (* (/ 2 3) (exp (/ 1 2))) 0) (/ (+ (exp (/ 1 2)) (sin 2)) (exp 1)) (exp (exp (exp (/ 1 2))))) 192) 0))
; [126] 9 function applications, value =~ 6.605936
(assert (> (- (+ (+ (* (/ 2 4) (sin 1)) (* (/ 1 3) (sin (/ pi 4))) (* (/ 5 4) pi) (* (/ 5 2) (sin (/ pi 2))) (- 3)) (/ (+ (exp 1) (exp (/ 1 2))) (+ 2 (sin 1))) (sin (exp (exp pi)))) 6) 0))
; [127] 7 function applications, value =~ 5.403574
(assert (< (- (+ (+ (* (/ 5 3) (exp 0)) (* (/ 3 4) (exp 0)) 0) (/ (+ pi (sin (/ pi 3))) (exp 2)) (exp (sin (exp 2)))) 6) 0))
; [128] 8 function applications, value =~ 12.761340
(assert (> (- (+ (+ (* (/ 3 3) (exp 1)) (* (/ 5 3) (sin 1)) (* (/ 4 2) (sin (/ pi 3))) (* (/ 5 3) (exp 0)) 1) (/ (+ (exp 0) pi) (exp 1)) (exp (sin (/ pi 2)))) 12) 0))
; [129] 7 function applications, value =~ 13.230903
(assert (< (- (+ (+ (* (/ 4 2) (exp 1)) (* (/ 5 3) pi) 0) (/ (+ (exp (/ 1 2)) pi) (+ (exp 1) 1)) (exp (/ (sin (/ (sin (exp (/ 1 2))) 2)) 2))) 14) 0))
; [130] 8 function applications, value =~ 121.857549
(assert (> (- (+ (+ (* (/ 1 4) (exp 2)) (* (/ 4 2) (sin 5)) (- 2)) (/ (+ (exp 1) (sin 1)) (+ 3 (* (sin 2) (sin 3)))) (exp (exp (/ pi 2)))) 119) 0))
; [131] 8 function applications, value =~ 10.227231
(assert (< (- (+ (+ (* (/ 4 4) (sin (/ pi 2))) (* (/ 1 3) pi) (* (/ 5 2) (sin (/ pi 4))) (* (/ 4 2) (exp (/ 1 2))) 1) (/ (+ pi (exp 0)) (+ 2 (sin 1))) (exp (/ (sin (/ (exp pi) 2)) 2))) 11) 0))
; [132] 8 function applications, value =~ 5.525304
(assert (> (- (+ (+ (* (/ 5 4) (sin 5)) (* (/ 2 3) (sin 5)) (* (/ 3 3) pi) (* (/ 4 4) (exp (/ 1 2))) 0) (/ (+ (exp 1) pi) (+ (exp 1) 1)) (sin (exp (/ (sin (/ pi 2)) 2)))) 5) 0))
; [133] 8 function applications, value =~ 24.427198
(assert (< (- (+ (+ (* (/ 5 3) pi) (* (/ 4 3) (sin (/ pi 3))) (* (/ 3 4) pi) (* (/ 3 2) (exp 2)) 3) (/ (+ (exp (/ 1 2)) (sin (/ pi 3))) (+ 2 (sin 0))) (sin (sin (/ (sin (/ pi 4)) 2)))) 25) 0))
; [134] 8 function applications, value =~ 7.917251
(assert (> (- (+ (+ (* (/ 3 3) (sin (/ pi 2))) (* (/ 4 2) (sin (/ pi 3))) 3) (/ (+ (exp 0) (sin (/ pi 4))) (exp 0)) (sin (/ (sin (exp (/ 1 2))) 2))) 7) 0))
; [135] 7 function applications, value =~ 7.683057
(assert (< (- (+ (+ (* (/ 3 4) (exp (/ 1 2))) (* (/ 4 4) pi) 2) (/ (+ (exp (/ 1 2)) (sin 2)) (+ (exp 2) 1)) (exp (sin (sin pi)))) 8) 0))
; [136] 9 function applications, value =~ 3.807890
(assert (> (- (+ (+ (* (/ 3 4) (exp 1)) (* (/ 3 2) (sin (/ pi 3))) (* (/ 2 4) (exp 1)) (- 3)) (/ (+ (sin 3) (exp 2)) (+ 2 (sin 2))) (sin (/ (sin (exp (/ pi 2))) 2))) 3) 0))
; [137] 8 function applications, value =~ 14.141876
(assert (< (- (+ (+ (* (/ 5 3) (exp 2)) (* (/ 2 2) (exp (/ 1 2))) (- 1)) (/ (+ (sin (/ pi 4)) (exp (/ 1 2))) (+ (exp 2) 1)) (sin (exp (sin (/ pi 4))))) 15) 0))
; [138] 7 function applications, value =~ 11.386037
(assert (> (- (+ (+ (* (/ 5 3) (exp (/ 1 2))) (* (/ 5 3) pi) 2) (/ (+ (exp (/ 1 2)) (exp (/ 1 2))) (+ (exp 1) 1)) (sin (/ (exp (exp (/ 1 2))) 2))) 11) 0))
; [139] 9 function applications, value =~ 5.068908
(assert (< (- (+ (+ (* (/ 2 3) (sin (/ pi 3))) (* (/ 3 3) pi) (* (/ 4 3) (exp (/ 1 2))) (- 2)) (/ (+ (exp (/ 1 2)) (exp (/ 1 2))) (+ (exp 2) 1)) (sin (exp (/ (exp (/ (exp 0) 2)) 2)))) 6) 0))
; [140] 7 function applications, value =~ 13.254227
(assert (> (- (+ (+ (* (/ 3 3) (exp 1)) (* (/ 1 2) pi) (* (/ 1 4) pi) (* (/ 1 3) pi) 2) (/ (+ (sin (/ pi 4)) (sin (/ pi 2))) (+ (exp 1) 1)) (exp (exp (/ (sin (/ pi 3)) 2)))) 12) 0))
; [141] 9 function applications, value =~ 12.755685
(assert (< (- (+ (+ (* (/ 4 2) (sin 1)) (* (/ 4 2) (exp 1)) 2) (/ (+ (exp 2) (sin (/ pi 2))) (+ 3 (* (sin 0) (sin 1)))) (sin (sin (exp (/ 1 2))))) 14) 0))
; [142] 9 function applications, value =~ 2.660381
(assert (> (- (+ (+ (* (/ 4 2) (sin (/ pi 4))) (* (/ 1 2) (exp (/ 1 2))) (* (/ 2 2) (exp (/ 1 2))) (- 2)) (/ (+ (sin 3) (sin 4)) (exp 1)) (sin (exp (/ (sin (/ pi 3)) 2)))) 2) 0))
; [143] 8 function applications, value =~ 5.528379
(assert (< (- (+ (+ (* (/ 1 3) (exp 0)) (* (/ 1 4) (sin (/ pi 2))) (* (/ 2 3) pi) 0) (/ (+ (exp 2) (sin 4)) (exp 1)) (sin (exp (sin (/ pi 2))))) 6) 0))
; [144] 9 function applications, value =~ 10.232108
(assert (> (- (+ (+ (* (/ 3 3) (exp (/ 1 2))) (* (/ 4 2) pi) (* (/ 4 4) (exp 0)) (* (/ 4 2) (sin (/ pi 2))) (- 3)) (/ (+ (sin (/ pi 2)) (exp 1)) (+ 2 (sin 0))) (exp (sin (sin 5)))) 9) 0))
; [145] 7 function applications, value =~ 4.816319
(assert (< (- (+ (+ (* (/ 5 3) (sin (/ pi 3))) (* (/ 3 2) (exp (/ 1 2))) 0) (/ (+ (sin (/ pi 2)) pi) (exp 2)) (sin (sin (/ (sin (/ pi 4)) 2)))) 6) 0))
; [146] 9 function applications, value =~ 4.775320
(assert (> (- (+ (+ (* (/ 3 4) pi) (* (/ 5 3) (sin 2)) (* (/ 3 3) (exp (/ 1 2))) (* (/ 5 3) (sin 1)) (- 2)) (/ (+ (sin (/ pi 2)) (exp 0)) (exp 1)) (sin (exp (exp (/ 1 2))))) 4) 0))
; [147] 10 function applications, value =~ 6.140505
(assert (< (- (+ (+ (* (/ 1 3) (exp (/ 1 2))) (* (/ 1 4) (sin (/ pi 3))) (* (/ 1 4) (exp 2)) (* (/ 4 2) (sin (/ pi 2))) (- 1)) (/ (+ (sin (/ pi 4)) pi) (+ 2 (sin 2))) (exp (/ (sin (/ (sin (sin (/ pi 3))) 2)) 2))) 7) 0))
; [148] 8 function applications, value =~ 14.514810
(assert (> (- (+ (+ (* (/ 5 4) (exp 2)) (* (/ 2 2) (exp (/ 1 2))) (* (/ 1 3) (exp 1)) 0) (/ (+ pi (sin 2)) (exp 1)) (exp (/ (sin (/ (sin (/ pi 3)) 2)) 2))) 14) 0))
; [149] 10 function applications, value =~ 8.060185
(assert (< (- (+ (+ (* (/ 5 3) (sin (/ pi 3))) (* (/ 3 3) pi) (* (/ 3 2) (sin (/ pi 4))) (* (/ 4 3) (sin 0)) (- 1)) (/ (+ pi (exp 1)) (+ 3 (* (sin 0) (sin 1)))) (exp (/ (sin (exp (/ (exp (/ 1 2)) 2))) 2))) 9) 0))
; [150] 10 function applications, value =~ 1.638727
(assert (> (- (+ (+ (* (/ 1 3) (exp (/ 1 2))) (* (/ 4 2) (sin (/ pi 3))) (* (/ 2 3) (sin (/ pi 2))) (- 2)) (/ (+ (sin 5) (sin 0)) (+ 3 (* (sin 2) (sin 3)))) (sin (exp (/ (sin (/ pi 2)) 2)))) 1) 0))

; overall expected result: sat (all 150 ground facts above are true)
(check-sat)
