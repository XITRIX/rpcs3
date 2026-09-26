"""Frozen pre-D-243 lowering shapes. Candidates come from the production builders.

Byte operations are integer-only; instruction-shaped ABSDB/AVGB cases retain
the CPUTranslator expression trees rather than substituting backend intrinsics.
"""

CASES = [('extended_fsm8',
  43,
  0,
  '%words = bitcast <16 x i8> %a to <4 x i32>\n'
  '%scalar = extractelement <4 x i32> %words, i32 3\n'
  '%short = trunc i32 %scalar to i16\n'
  '%ins = insertelement <8 x i16> poison, i16 %short, i32 0\n'
  '%dup = shufflevector <8 x i16> %ins, <8 x i16> poison, <8 x i32> zeroinitializer\n'
  '%bytes = bitcast <8 x i16> %dup to <16 x i8>\n'
  '%bits = shufflevector <16 x i8> %bytes, <16 x i8> poison, <16 x i32> <i32 0, i32 0, i32 0, i32 0, i32 0, i32 '
  '0, i32 0, i32 0, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1, i32 1>\n'
  '%masked = and <16 x i8> %bits, <i8 1, i8 2, i8 4, i8 8, i8 16, i8 32, i8 64, i8 128, i8 1, i8 2, i8 4, i8 8, '
  'i8 16, i8 32, i8 64, i8 128>\n'
  '%cmp = icmp eq <16 x i8> %masked, <i8 1, i8 2, i8 4, i8 8, i8 16, i8 32, i8 64, i8 128, i8 1, i8 2, i8 4, i8 '
  '8, i8 16, i8 32, i8 64, i8 128>\n'
  '%result = sext <16 x i1> %cmp to <16 x i8>\n'
  '%out = bitcast <16 x i8> %result to <16 x i8>'),
 ('extended_reverse_add',
  15,
  0,
  '%ar = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %a, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%br = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %b, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%result = add <16 x i8> %ar, %br\n'
  '%out = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %result, <16 x i8> <i8 15, i8 14, i8 13, i8 12, '
  'i8 11, i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)'),
 ('extended_reverse_sub',
  16,
  0,
  '%ar = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %a, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%br = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %b, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%result = sub <16 x i8> %ar, %br\n'
  '%out = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %result, <16 x i8> <i8 15, i8 14, i8 13, i8 12, '
  'i8 11, i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)'),
 ('extended_reverse_mul',
  17,
  0,
  '%ar = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %a, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%br = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %b, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%result = mul <16 x i8> %ar, %br\n'
  '%out = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %result, <16 x i8> <i8 15, i8 14, i8 13, i8 12, '
  'i8 11, i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)'),
 ('extended_reverse_shl',
  18,
  0,
  '%ar = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %a, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%br = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %b, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%count = and <16 x i8> %br, <i8 7, i8 7, i8 7, i8 7, i8 7, i8 7, i8 7, i8 7, i8 7, i8 7, i8 7, i8 7, i8 7, i8 '
  '7, i8 7, i8 7>\n'
  '%result = shl <16 x i8> %ar, %count\n'
  '%out = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %result, <16 x i8> <i8 15, i8 14, i8 13, i8 12, '
  'i8 11, i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)'),
 ('extended_reverse_lshr',
  19,
  0,
  '%ar = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %a, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%br = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %b, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%count = and <16 x i8> %br, <i8 7, i8 7, i8 7, i8 7, i8 7, i8 7, i8 7, i8 7, i8 7, i8 7, i8 7, i8 7, i8 7, i8 '
  '7, i8 7, i8 7>\n'
  '%result = lshr <16 x i8> %ar, %count\n'
  '%out = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %result, <16 x i8> <i8 15, i8 14, i8 13, i8 12, '
  'i8 11, i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)'),
 ('extended_reverse_ashr',
  20,
  0,
  '%ar = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %a, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%br = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %b, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%count = and <16 x i8> %br, <i8 7, i8 7, i8 7, i8 7, i8 7, i8 7, i8 7, i8 7, i8 7, i8 7, i8 7, i8 7, i8 7, i8 '
  '7, i8 7, i8 7>\n'
  '%result = ashr <16 x i8> %ar, %count\n'
  '%out = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %result, <16 x i8> <i8 15, i8 14, i8 13, i8 12, '
  'i8 11, i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)'),
 ('extended_reverse_eq',
  21,
  0,
  '%ar = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %a, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%br = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %b, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%cmp = icmp eq <16 x i8> %ar, %br\n'
  '%result = sext <16 x i1> %cmp to <16 x i8>\n'
  '%out = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %result, <16 x i8> <i8 15, i8 14, i8 13, i8 12, '
  'i8 11, i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)'),
 ('extended_reverse_ugt',
  22,
  0,
  '%ar = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %a, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%br = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %b, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%cmp = icmp ugt <16 x i8> %ar, %br\n'
  '%result = sext <16 x i1> %cmp to <16 x i8>\n'
  '%out = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %result, <16 x i8> <i8 15, i8 14, i8 13, i8 12, '
  'i8 11, i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)'),
 ('extended_reverse_sgt',
  23,
  0,
  '%ar = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %a, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%br = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %b, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%cmp = icmp sgt <16 x i8> %ar, %br\n'
  '%result = sext <16 x i1> %cmp to <16 x i8>\n'
  '%out = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %result, <16 x i8> <i8 15, i8 14, i8 13, i8 12, '
  'i8 11, i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)'),
 ('extended_reverse_ctpop',
  24,
  0,
  '%ar = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %a, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%br = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %b, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%result = call <16 x i8> @llvm.ctpop.v16i8(<16 x i8> %ar)\n'
  '%out = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %result, <16 x i8> <i8 15, i8 14, i8 13, i8 12, '
  'i8 11, i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)'),
 ('extended_reverse_uabd',
  25,
  0,
  '%ar = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %a, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%br = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %b, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%result = call <16 x i8> @llvm.aarch64.neon.uabd.v16i8(<16 x i8> %ar, <16 x i8> %br)\n'
  '%out = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %result, <16 x i8> <i8 15, i8 14, i8 13, i8 12, '
  'i8 11, i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)'),
 ('extended_reverse_urhadd',
  26,
  0,
  '%ar = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %a, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%br = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %b, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%result = call <16 x i8> @llvm.aarch64.neon.urhadd.v16i8(<16 x i8> %ar, <16 x i8> %br)\n'
  '%out = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %result, <16 x i8> <i8 15, i8 14, i8 13, i8 12, '
  'i8 11, i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)'),
 ('extended_reverse_umin',
  27,
  0,
  '%ar = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %a, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%br = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %b, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%result = call <16 x i8> @llvm.umin.v16i8(<16 x i8> %ar, <16 x i8> %br)\n'
  '%out = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %result, <16 x i8> <i8 15, i8 14, i8 13, i8 12, '
  'i8 11, i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)'),
 ('extended_reverse_umax',
  28,
  0,
  '%ar = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %a, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%br = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %b, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%result = call <16 x i8> @llvm.umax.v16i8(<16 x i8> %ar, <16 x i8> %br)\n'
  '%out = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %result, <16 x i8> <i8 15, i8 14, i8 13, i8 12, '
  'i8 11, i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)'),
 ('extended_reverse_smin',
  29,
  0,
  '%ar = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %a, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%br = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %b, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%result = call <16 x i8> @llvm.smin.v16i8(<16 x i8> %ar, <16 x i8> %br)\n'
  '%out = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %result, <16 x i8> <i8 15, i8 14, i8 13, i8 12, '
  'i8 11, i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)'),
 ('extended_reverse_smax',
  30,
  0,
  '%ar = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %a, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%br = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %b, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%result = call <16 x i8> @llvm.smax.v16i8(<16 x i8> %ar, <16 x i8> %br)\n'
  '%out = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %result, <16 x i8> <i8 15, i8 14, i8 13, i8 12, '
  'i8 11, i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)'),
 ('extended_reverse_absdb',
  31,
  0,
  '%ar = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %a, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%br = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %b, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%cmp0 = icmp ult <16 x i8> %ar, %br\n'
  '%lo = select <16 x i1> %cmp0, <16 x i8> %ar, <16 x i8> %br\n'
  '%cmp1 = icmp ult <16 x i8> %ar, %br\n'
  '%hi = select <16 x i1> %cmp1, <16 x i8> %br, <16 x i8> %ar\n'
  '%result = sub <16 x i8> %hi, %lo\n'
  '%out = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %result, <16 x i8> <i8 15, i8 14, i8 13, i8 12, '
  'i8 11, i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)'),
 ('extended_reverse_avgb',
  32,
  0,
  '%ar = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %a, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%br = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %b, <16 x i8> <i8 15, i8 14, i8 13, i8 12, i8 11, '
  'i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)\n'
  '%ax = zext <16 x i8> %ar to <16 x i16>\n'
  '%bx = zext <16 x i8> %br to <16 x i16>\n'
  '%sum = add <16 x i16> %ax, %bx\n'
  '%rounded = add <16 x i16> %sum, <i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, '
  'i16 1, i16 1, i16 1, i16 1, i16 1>\n'
  '%half = lshr <16 x i16> %rounded, <i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 1, i16 '
  '1, i16 1, i16 1, i16 1, i16 1, i16 1>\n'
  '%result = trunc <16 x i16> %half to <16 x i8>\n'
  '%out = call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> %result, <16 x i8> <i8 15, i8 14, i8 13, i8 12, '
  'i8 11, i8 10, i8 9, i8 8, i8 7, i8 6, i8 5, i8 4, i8 3, i8 2, i8 1, i8 0>)')]

def _v(width, value):
    return '<' + ', '.join(f'i{width} {value}' for _ in range(16)) + '>'

_reverse = '<' + ', '.join(f'i8 {i}' for i in range(15, -1, -1)) + '>'
def _rev(source):
    return f'call <16 x i8> @llvm.aarch64.neon.tbl1.v16i8(<16 x i8> {source}, <16 x i8> {_reverse})'
_pre = '%ar = ' + _rev('%a') + '\n%br = ' + _rev('%b') + '\n'
CASES += [
    ('guard_byte_shared_predicate', 44, 0, _pre +
     '%cmp = icmp ugt <16 x i8> %ar, %br\n%mask = sext <16 x i1> %cmp to <16 x i8>\n'
     '%extra = zext <16 x i1> %cmp to <16 x i8>\n%rev = ' + _rev('%mask') + '\n%out = xor <16 x i8> %rev, %extra'),
    ('guard_byte_wide_add', 45, 0, _pre +
     '%aw = bitcast <16 x i8> %ar to <4 x i32>\n%bw = bitcast <16 x i8> %br to <4 x i32>\n'
     '%sum = add <4 x i32> %aw, %bw\n%result = bitcast <4 x i32> %sum to <16 x i8>\n%out = ' + _rev('%result')),
    ('guard_byte_shared_add', 46, 0, _pre +
     '%sum = add <16 x i8> %ar, %br\n%rev = ' + _rev('%sum') + '\n%out = xor <16 x i8> %rev, %sum'),
    ('guard_byte_shared_minmax_predicate', 47, 0, _pre +
     '%cmp = icmp ult <16 x i8> %ar, %br\n%result = select <16 x i1> %cmp, <16 x i8> %ar, <16 x i8> %br\n'
     '%extra = sext <16 x i1> %cmp to <16 x i8>\n%rev = ' + _rev('%result') + '\n%out = xor <16 x i8> %rev, %extra'),
]
_average = next(body for name, _, _, body in CASES if name == 'extended_reverse_avgb')
CASES += [
    ('guard_byte_average_floor', 48, 0, _average.replace('%sum, ' + _v(16, 1), '%sum, zeroinitializer')),
    ('guard_byte_average_signed', 49, 0, _average.replace('zext <16 x i8>', 'sext <16 x i8>')),
    ('guard_byte_average_shared_sum', 50, 0, _average.replace('%out = ', '%rev = ') +
     '\n%extra = trunc <16 x i16> %sum to <16 x i8>\n%out = xor <16 x i8> %rev, %extra'),
]


# Equality masks at SPU halfword/word widths and byte-immediate compares.
# Include every encoded byte value without counting those as separate wins.
for _width in (16, 32):
    _n = 128 // _width
    _ty = f'<{_n} x i{_width}>'
    for _pred in ('eq', 'ugt'):
        for _cast in ('sext', 'zext'):
            _positive = _pred == 'eq' and _cast == 'sext'
            _name = ('extended_' if _positive else 'guard_') + f'reverse_compare{_width}_{_pred}_{_cast}'
            _body = _pre + f'%aw = bitcast <16 x i8> %ar to {_ty}\n%bw = bitcast <16 x i8> %br to {_ty}\n'
            _body += f'%cmp = icmp {_pred} {_ty} %aw, %bw\n%mask = {_cast} <{_n} x i1> %cmp to {_ty}\n'
            _body += f'%result = bitcast {_ty} %mask to <16 x i8>\n%out = ' + _rev('%result')
            CASES.append((_name, 51, _width | (256 if _pred == 'ugt' else 0) | (512 if _cast == 'zext' else 0), _body))
for _pred, _kind in [('eq', 52), ('ugt', 53), ('sgt', 54)]:
    for _immediate in range(256):
        _name = f'extended_reverse_immediate_{_pred}_{_immediate}'
        _body = '%ar = ' + _rev('%a') + f'\n%cmp = icmp {_pred} <16 x i8> %ar, ' + _v(8, _immediate)
        _body += '\n%mask = sext <16 x i1> %cmp to <16 x i8>\n%out = ' + _rev('%mask')
        CASES.append((_name, _kind, _immediate, _body))
