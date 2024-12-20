; NOTE: Assertions have been autogenerated by utils/update_test_checks.py
; RUN: opt -load %S/../build/pass/libSLPVectorizer-kdw.so -slpvect-kdw -S -mtriple=x86_64-unknown-linux -mcpu=corei7-avx -slp-threshold=-999 < %s | FileCheck %s


; S[0] = %v1 + %v2
; S[1] = %v2 + %v1
; S[2] = %v2 + %v1
; S[3] = %v1 + %v2
;
; We broadcast %v1 and %v2
;

define void @bcast_vals(i64 *%A, i64 *%B, i64 *%S) {
; CHECK-LABEL: @bcast_vals(
; CHECK-NEXT:  entry:
; CHECK-NEXT:    [[A0:%.*]] = load i64, i64* [[A:%.*]], align 8
; CHECK-NEXT:    [[B0:%.*]] = load i64, i64* [[B:%.*]], align 8
; CHECK-NEXT:    [[V1:%.*]] = sub i64 [[A0]], 1
; CHECK-NEXT:    [[V2:%.*]] = sub i64 [[B0]], 1
; CHECK-NEXT:    [[TMP0:%.*]] = insertelement <4 x i64> undef, i64 [[V1]], i32 0
; CHECK-NEXT:    [[TMP1:%.*]] = insertelement <4 x i64> [[TMP0]], i64 [[V1]], i32 1
; CHECK-NEXT:    [[TMP2:%.*]] = insertelement <4 x i64> [[TMP1]], i64 [[V1]], i32 2
; CHECK-NEXT:    [[TMP3:%.*]] = insertelement <4 x i64> [[TMP2]], i64 [[V1]], i32 3
; CHECK-NEXT:    [[TMP4:%.*]] = insertelement <4 x i64> undef, i64 [[V2]], i32 0
; CHECK-NEXT:    [[TMP5:%.*]] = insertelement <4 x i64> [[TMP4]], i64 [[V2]], i32 1
; CHECK-NEXT:    [[TMP6:%.*]] = insertelement <4 x i64> [[TMP5]], i64 [[V2]], i32 2
; CHECK-NEXT:    [[TMP7:%.*]] = insertelement <4 x i64> [[TMP6]], i64 [[V2]], i32 3
; CHECK-NEXT:    [[TMP8:%.*]] = add <4 x i64> [[TMP3]], [[TMP7]]
; CHECK-NEXT:    [[IDXS0:%.*]] = getelementptr inbounds i64, i64* [[S:%.*]], i64 0
; CHECK-NEXT:    [[IDXS1:%.*]] = getelementptr inbounds i64, i64* [[S]], i64 1
; CHECK-NEXT:    [[IDXS2:%.*]] = getelementptr inbounds i64, i64* [[S]], i64 2
; CHECK-NEXT:    [[IDXS3:%.*]] = getelementptr inbounds i64, i64* [[S]], i64 3
; CHECK-NEXT:    [[TMP9:%.*]] = bitcast i64* [[IDXS0]] to <4 x i64>*
; CHECK-NEXT:    store <4 x i64> [[TMP8]], <4 x i64>* [[TMP9]], align 8
; CHECK-NEXT:    ret void
;
entry:
  %A0 = load i64, i64 *%A, align 8
  %B0 = load i64, i64 *%B, align 8

  %v1 = sub i64 %A0, 1
  %v2 = sub i64 %B0, 1

  %Add0 = add i64 %v1, %v2
  %Add1 = add i64 %v2, %v1
  %Add2 = add i64 %v2, %v1
  %Add3 = add i64 %v1, %v2

  %idxS0 = getelementptr inbounds i64, i64* %S, i64 0
  %idxS1 = getelementptr inbounds i64, i64* %S, i64 1
  %idxS2 = getelementptr inbounds i64, i64* %S, i64 2
  %idxS3 = getelementptr inbounds i64, i64* %S, i64 3

  store i64 %Add0, i64 *%idxS0, align 8
  store i64 %Add1, i64 *%idxS1, align 8
  store i64 %Add2, i64 *%idxS2, align 8
  store i64 %Add3, i64 *%idxS3, align 8
  ret void
}

; S[0] = %v1 + %v2
; S[1] = %v3 + %v1
; S[2] = %v5 + %v1
; S[3] = %v1 + %v4
;
; We broadcast %v1.

;
define void @bcast_vals2(i16 *%A, i16 *%B, i16 *%C, i16 *%D, i16 *%E, i32 *%S) {
; CHECK-LABEL: @bcast_vals2(
; CHECK-NEXT:  entry:
; CHECK-NEXT:    [[A0:%.*]] = load i16, i16* [[A:%.*]], align 8
; CHECK-NEXT:    [[B0:%.*]] = load i16, i16* [[B:%.*]], align 8
; CHECK-NEXT:    [[C0:%.*]] = load i16, i16* [[C:%.*]], align 8
; CHECK-NEXT:    [[D0:%.*]] = load i16, i16* [[D:%.*]], align 8
; CHECK-NEXT:    [[E0:%.*]] = load i16, i16* [[E:%.*]], align 8
; CHECK-NEXT:    [[V1:%.*]] = sext i16 [[A0]] to i32
; CHECK-NEXT:    [[TMP0:%.*]] = insertelement <4 x i16> undef, i16 [[B0]], i32 0
; CHECK-NEXT:    [[TMP1:%.*]] = insertelement <4 x i16> [[TMP0]], i16 [[C0]], i32 1
; CHECK-NEXT:    [[TMP2:%.*]] = insertelement <4 x i16> [[TMP1]], i16 [[E0]], i32 2
; CHECK-NEXT:    [[TMP3:%.*]] = insertelement <4 x i16> [[TMP2]], i16 [[D0]], i32 3
; CHECK-NEXT:    [[TMP4:%.*]] = sext <4 x i16> [[TMP3]] to <4 x i32>
; CHECK-NEXT:    [[TMP5:%.*]] = insertelement <4 x i32> undef, i32 [[V1]], i32 0
; CHECK-NEXT:    [[TMP6:%.*]] = insertelement <4 x i32> [[TMP5]], i32 [[V1]], i32 1
; CHECK-NEXT:    [[TMP7:%.*]] = insertelement <4 x i32> [[TMP6]], i32 [[V1]], i32 2
; CHECK-NEXT:    [[TMP8:%.*]] = insertelement <4 x i32> [[TMP7]], i32 [[V1]], i32 3
; CHECK-NEXT:    [[TMP9:%.*]] = add <4 x i32> [[TMP8]], [[TMP4]]
; CHECK-NEXT:    [[IDXS0:%.*]] = getelementptr inbounds i32, i32* [[S:%.*]], i64 0
; CHECK-NEXT:    [[IDXS1:%.*]] = getelementptr inbounds i32, i32* [[S]], i64 1
; CHECK-NEXT:    [[IDXS2:%.*]] = getelementptr inbounds i32, i32* [[S]], i64 2
; CHECK-NEXT:    [[IDXS3:%.*]] = getelementptr inbounds i32, i32* [[S]], i64 3
; CHECK-NEXT:    [[TMP10:%.*]] = bitcast i32* [[IDXS0]] to <4 x i32>*
; CHECK-NEXT:    store <4 x i32> [[TMP9]], <4 x i32>* [[TMP10]], align 8
; CHECK-NEXT:    ret void
;
entry:
  %A0 = load i16, i16 *%A, align 8
  %B0 = load i16, i16 *%B, align 8
  %C0 = load i16, i16 *%C, align 8
  %D0 = load i16, i16 *%D, align 8
  %E0 = load i16, i16 *%E, align 8

  %v1 = sext i16 %A0 to i32
  %v2 = sext i16 %B0 to i32
  %v3 = sext i16 %C0 to i32
  %v4 = sext i16 %D0 to i32
  %v5 = sext i16 %E0 to i32

  %Add0 = add i32 %v1, %v2
  %Add1 = add i32 %v3, %v1
  %Add2 = add i32 %v5, %v1
  %Add3 = add i32 %v1, %v4

  %idxS0 = getelementptr inbounds i32, i32* %S, i64 0
  %idxS1 = getelementptr inbounds i32, i32* %S, i64 1
  %idxS2 = getelementptr inbounds i32, i32* %S, i64 2
  %idxS3 = getelementptr inbounds i32, i32* %S, i64 3

  store i32 %Add0, i32 *%idxS0, align 8
  store i32 %Add1, i32 *%idxS1, align 8
  store i32 %Add2, i32 *%idxS2, align 8
  store i32 %Add3, i32 *%idxS3, align 8
  ret void
}
