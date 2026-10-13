#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
using Fn = void (*)(const void*, void*, uint32_t);
struct Case
{
	unsigned kind;
	bool chain;
	Fn baseline, candidate;
};
extern "C" void table_0_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_0_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_0_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_0_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_1_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_1_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_1_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_1_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_2_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_2_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_2_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_2_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_3_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_3_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_3_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_3_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_4_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_4_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_4_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_4_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_5_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_5_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_5_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_5_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_6_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_6_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_6_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_6_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_7_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_7_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_7_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_7_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_8_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_8_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_8_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_8_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_9_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_9_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_9_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_9_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_10_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_10_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_10_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_10_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_11_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_11_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_11_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_11_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_12_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_12_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_12_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_12_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_13_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_13_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_13_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_13_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_14_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_14_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_14_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_14_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_15_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_15_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_15_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_15_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_16_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_16_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_16_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_16_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_17_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_17_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_17_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_17_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_18_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_18_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_18_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_18_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_19_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_19_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_19_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_19_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_20_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_20_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_20_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_20_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_21_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_21_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_21_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_21_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_22_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_22_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_22_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_22_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_23_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_23_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_23_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_23_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_24_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_24_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_24_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_24_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_25_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_25_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_25_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_25_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_26_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_26_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_26_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_26_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_27_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_27_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_27_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_27_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_28_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_28_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_28_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_28_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_29_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_29_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_29_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_29_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_30_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_30_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_30_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_30_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_31_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_31_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_31_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_31_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_32_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_32_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_32_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_32_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_33_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_33_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_33_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_33_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_34_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_34_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_34_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_34_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_35_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_35_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_35_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_35_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_36_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_36_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_36_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_36_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_37_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_37_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_37_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_37_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_38_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_38_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_38_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_38_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_39_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_39_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_39_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_39_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_40_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_40_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_40_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_40_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_41_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_41_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_41_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_41_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_42_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_42_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_42_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_42_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_43_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_43_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_43_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_43_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_44_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_44_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_44_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_44_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_45_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_45_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_45_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_45_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_46_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_46_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_46_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_46_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_47_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_47_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_47_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_47_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_48_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_48_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_48_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_48_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_49_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_49_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_49_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_49_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_50_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_50_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_50_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_50_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_51_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_51_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_51_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_51_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_52_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_52_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_52_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_52_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_53_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_53_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_53_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_53_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_54_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_54_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_54_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_54_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_55_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_55_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_55_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_55_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_56_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_56_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_56_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_56_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_57_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_57_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_57_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_57_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_58_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_58_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_58_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_58_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_59_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_59_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_59_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_59_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_60_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_60_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_60_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_60_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_61_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_61_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_61_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_61_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_62_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_62_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_62_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_62_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_63_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_63_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_63_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_63_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_64_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_64_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_64_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_64_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_65_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_65_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_65_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_65_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_66_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_66_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_66_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_66_chain_candidate(const void*, void*, uint32_t);
extern "C" void table_67_batch_baseline(const void*, void*, uint32_t);
extern "C" void table_67_batch_candidate(const void*, void*, uint32_t);
extern "C" void table_67_chain_baseline(const void*, void*, uint32_t);
extern "C" void table_67_chain_candidate(const void*, void*, uint32_t);
Case cases[] = {{0, false, table_0_batch_baseline, table_0_batch_candidate},
	{0, true, table_0_chain_baseline, table_0_chain_candidate},
	{1, false, table_1_batch_baseline, table_1_batch_candidate},
	{1, true, table_1_chain_baseline, table_1_chain_candidate},
	{2, false, table_2_batch_baseline, table_2_batch_candidate},
	{2, true, table_2_chain_baseline, table_2_chain_candidate},
	{3, false, table_3_batch_baseline, table_3_batch_candidate},
	{3, true, table_3_chain_baseline, table_3_chain_candidate},
	{4, false, table_4_batch_baseline, table_4_batch_candidate},
	{4, true, table_4_chain_baseline, table_4_chain_candidate},
	{5, false, table_5_batch_baseline, table_5_batch_candidate},
	{5, true, table_5_chain_baseline, table_5_chain_candidate},
	{6, false, table_6_batch_baseline, table_6_batch_candidate},
	{6, true, table_6_chain_baseline, table_6_chain_candidate},
	{7, false, table_7_batch_baseline, table_7_batch_candidate},
	{7, true, table_7_chain_baseline, table_7_chain_candidate},
	{8, false, table_8_batch_baseline, table_8_batch_candidate},
	{8, true, table_8_chain_baseline, table_8_chain_candidate},
	{9, false, table_9_batch_baseline, table_9_batch_candidate},
	{9, true, table_9_chain_baseline, table_9_chain_candidate},
	{10, false, table_10_batch_baseline, table_10_batch_candidate},
	{10, true, table_10_chain_baseline, table_10_chain_candidate},
	{11, false, table_11_batch_baseline, table_11_batch_candidate},
	{11, true, table_11_chain_baseline, table_11_chain_candidate},
	{12, false, table_12_batch_baseline, table_12_batch_candidate},
	{12, true, table_12_chain_baseline, table_12_chain_candidate},
	{13, false, table_13_batch_baseline, table_13_batch_candidate},
	{13, true, table_13_chain_baseline, table_13_chain_candidate},
	{14, false, table_14_batch_baseline, table_14_batch_candidate},
	{14, true, table_14_chain_baseline, table_14_chain_candidate},
	{15, false, table_15_batch_baseline, table_15_batch_candidate},
	{15, true, table_15_chain_baseline, table_15_chain_candidate},
	{16, false, table_16_batch_baseline, table_16_batch_candidate},
	{16, true, table_16_chain_baseline, table_16_chain_candidate},
	{17, false, table_17_batch_baseline, table_17_batch_candidate},
	{17, true, table_17_chain_baseline, table_17_chain_candidate},
	{18, false, table_18_batch_baseline, table_18_batch_candidate},
	{18, true, table_18_chain_baseline, table_18_chain_candidate},
	{19, false, table_19_batch_baseline, table_19_batch_candidate},
	{19, true, table_19_chain_baseline, table_19_chain_candidate},
	{20, false, table_20_batch_baseline, table_20_batch_candidate},
	{20, true, table_20_chain_baseline, table_20_chain_candidate},
	{21, false, table_21_batch_baseline, table_21_batch_candidate},
	{21, true, table_21_chain_baseline, table_21_chain_candidate},
	{22, false, table_22_batch_baseline, table_22_batch_candidate},
	{22, true, table_22_chain_baseline, table_22_chain_candidate},
	{23, false, table_23_batch_baseline, table_23_batch_candidate},
	{23, true, table_23_chain_baseline, table_23_chain_candidate},
	{24, false, table_24_batch_baseline, table_24_batch_candidate},
	{24, true, table_24_chain_baseline, table_24_chain_candidate},
	{25, false, table_25_batch_baseline, table_25_batch_candidate},
	{25, true, table_25_chain_baseline, table_25_chain_candidate},
	{26, false, table_26_batch_baseline, table_26_batch_candidate},
	{26, true, table_26_chain_baseline, table_26_chain_candidate},
	{27, false, table_27_batch_baseline, table_27_batch_candidate},
	{27, true, table_27_chain_baseline, table_27_chain_candidate},
	{28, false, table_28_batch_baseline, table_28_batch_candidate},
	{28, true, table_28_chain_baseline, table_28_chain_candidate},
	{29, false, table_29_batch_baseline, table_29_batch_candidate},
	{29, true, table_29_chain_baseline, table_29_chain_candidate},
	{30, false, table_30_batch_baseline, table_30_batch_candidate},
	{30, true, table_30_chain_baseline, table_30_chain_candidate},
	{31, false, table_31_batch_baseline, table_31_batch_candidate},
	{31, true, table_31_chain_baseline, table_31_chain_candidate},
	{32, false, table_32_batch_baseline, table_32_batch_candidate},
	{32, true, table_32_chain_baseline, table_32_chain_candidate},
	{33, false, table_33_batch_baseline, table_33_batch_candidate},
	{33, true, table_33_chain_baseline, table_33_chain_candidate},
	{34, false, table_34_batch_baseline, table_34_batch_candidate},
	{34, true, table_34_chain_baseline, table_34_chain_candidate},
	{35, false, table_35_batch_baseline, table_35_batch_candidate},
	{35, true, table_35_chain_baseline, table_35_chain_candidate},
	{36, false, table_36_batch_baseline, table_36_batch_candidate},
	{36, true, table_36_chain_baseline, table_36_chain_candidate},
	{37, false, table_37_batch_baseline, table_37_batch_candidate},
	{37, true, table_37_chain_baseline, table_37_chain_candidate},
	{38, false, table_38_batch_baseline, table_38_batch_candidate},
	{38, true, table_38_chain_baseline, table_38_chain_candidate},
	{39, false, table_39_batch_baseline, table_39_batch_candidate},
	{39, true, table_39_chain_baseline, table_39_chain_candidate},
	{40, false, table_40_batch_baseline, table_40_batch_candidate},
	{40, true, table_40_chain_baseline, table_40_chain_candidate},
	{41, false, table_41_batch_baseline, table_41_batch_candidate},
	{41, true, table_41_chain_baseline, table_41_chain_candidate},
	{42, false, table_42_batch_baseline, table_42_batch_candidate},
	{42, true, table_42_chain_baseline, table_42_chain_candidate},
	{43, false, table_43_batch_baseline, table_43_batch_candidate},
	{43, true, table_43_chain_baseline, table_43_chain_candidate},
	{44, false, table_44_batch_baseline, table_44_batch_candidate},
	{44, true, table_44_chain_baseline, table_44_chain_candidate},
	{45, false, table_45_batch_baseline, table_45_batch_candidate},
	{45, true, table_45_chain_baseline, table_45_chain_candidate},
	{46, false, table_46_batch_baseline, table_46_batch_candidate},
	{46, true, table_46_chain_baseline, table_46_chain_candidate},
	{47, false, table_47_batch_baseline, table_47_batch_candidate},
	{47, true, table_47_chain_baseline, table_47_chain_candidate},
	{48, false, table_48_batch_baseline, table_48_batch_candidate},
	{48, true, table_48_chain_baseline, table_48_chain_candidate},
	{49, false, table_49_batch_baseline, table_49_batch_candidate},
	{49, true, table_49_chain_baseline, table_49_chain_candidate},
	{50, false, table_50_batch_baseline, table_50_batch_candidate},
	{50, true, table_50_chain_baseline, table_50_chain_candidate},
	{51, false, table_51_batch_baseline, table_51_batch_candidate},
	{51, true, table_51_chain_baseline, table_51_chain_candidate},
	{52, false, table_52_batch_baseline, table_52_batch_candidate},
	{52, true, table_52_chain_baseline, table_52_chain_candidate},
	{53, false, table_53_batch_baseline, table_53_batch_candidate},
	{53, true, table_53_chain_baseline, table_53_chain_candidate},
	{54, false, table_54_batch_baseline, table_54_batch_candidate},
	{54, true, table_54_chain_baseline, table_54_chain_candidate},
	{55, false, table_55_batch_baseline, table_55_batch_candidate},
	{55, true, table_55_chain_baseline, table_55_chain_candidate},
	{56, false, table_56_batch_baseline, table_56_batch_candidate},
	{56, true, table_56_chain_baseline, table_56_chain_candidate},
	{57, false, table_57_batch_baseline, table_57_batch_candidate},
	{57, true, table_57_chain_baseline, table_57_chain_candidate},
	{58, false, table_58_batch_baseline, table_58_batch_candidate},
	{58, true, table_58_chain_baseline, table_58_chain_candidate},
	{59, false, table_59_batch_baseline, table_59_batch_candidate},
	{59, true, table_59_chain_baseline, table_59_chain_candidate},
	{60, false, table_60_batch_baseline, table_60_batch_candidate},
	{60, true, table_60_chain_baseline, table_60_chain_candidate},
	{61, false, table_61_batch_baseline, table_61_batch_candidate},
	{61, true, table_61_chain_baseline, table_61_chain_candidate},
	{62, false, table_62_batch_baseline, table_62_batch_candidate},
	{62, true, table_62_chain_baseline, table_62_chain_candidate},
	{63, false, table_63_batch_baseline, table_63_batch_candidate},
	{63, true, table_63_chain_baseline, table_63_chain_candidate},
	{64, false, table_64_batch_baseline, table_64_batch_candidate},
	{64, true, table_64_chain_baseline, table_64_chain_candidate},
	{65, false, table_65_batch_baseline, table_65_batch_candidate},
	{65, true, table_65_chain_baseline, table_65_chain_candidate},
	{66, false, table_66_batch_baseline, table_66_batch_candidate},
	{66, true, table_66_chain_baseline, table_66_chain_candidate},
	{67, false, table_67_batch_baseline, table_67_batch_candidate},
	{67, true, table_67_chain_baseline, table_67_chain_candidate}};
using V = std::array<uint8_t, 16>;
V reference(V input, unsigned kind)
{
	V middle{}, result{};
	for (unsigned i = 0; i < 16; ++i)
	{
		unsigned x = kind == 0 ? i : kind == 1 ? 15 - i :
		                         kind == 2     ? (i + 4) % 16 :
		                         kind == 3     ? (i + 254) % 256 :
		                         kind < 20     ? (i + kind - 4) % 16 :
		                         kind < 36     ? (i + kind - 20) % 32 :
		                         kind < 52     ? (i * 3 + kind) % 256 :
		                                         (kind * 13 + i * 7) % 256;
		middle[i] = x < 16 ? input[x] : 0;
	}
	for (unsigned i = 0; i < 16; ++i)
	{
		unsigned y = kind == 0 ? i : kind == 1 ? 15 - i :
		                         kind == 2     ? (i + 12) % 16 :
		                         kind == 3     ? i + 3 :
		                         kind < 20     ? (i * 7 + 3) % 16 :
		                         kind < 36     ? i :
		                         kind < 52     ? (kind - 36) * 17 :
		                                         (kind * 29 + i * 3) % 32;
		result[i] = y < 16 ? middle[y] : 0;
	}
	return result;
}
int main(int argc, char**)
{
	constexpr unsigned n = 8192;
	std::vector<V> input(n), a(n), b(n);
	uint64_t state = 0x873440023;
	for (auto& v : input)
		for (auto& x : v)
		{
			state ^= state << 13;
			state ^= state >> 7;
			state ^= state << 17;
			x = state;
		}
	for (unsigned value = 0; value < 256; ++value)
		for (unsigned lane = 0; lane < 16; ++lane)
		{
			input[value * 16 + lane] = {};
			input[value * 16 + lane][lane] = value;
		}
	for (auto& c : cases)
	{
		c.baseline(input.data(), a.data(), n);
		c.candidate(input.data(), b.data(), n);
		V prior{};
		for (unsigned i = 0; i < n; ++i)
		{
			V v = input[i];
			if (c.chain)
				for (unsigned j = 0; j < 16; ++j)
					v[j] ^= prior[j];
			auto expected = reference(v, c.kind);
			if (a[i] != expected || b[i] != expected)
			{
				printf("FAIL kind=%u chain=%u index=%u\n", c.kind, c.chain, i);
				return 1;
			}
			prior = expected;
		}
	}
	puts("PASS: 68 constant-table pairs, batch/chained, every byte value at every source lane plus random vectors");
	if (argc > 1)
		return 0;
	volatile uint8_t sink = 0;
	for (auto& c : cases)
	{
		if (c.kind >= 4)
			continue;
		std::vector<double> t[2];
		for (unsigned round = 0; round < 12; ++round)
			for (unsigned order = 0; order < 2; ++order)
			{
				unsigned mode = (round + order) % 2;
				auto fn = mode ? c.candidate : c.baseline;
				auto start = std::chrono::steady_clock::now();
				for (unsigned rep = 0; rep < 500; ++rep)
					fn(input.data(), b.data(), n);
				double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / (500 * n);
				sink = b[round][0];
				if (round >= 2)
					t[mode].push_back(ns);
			}
		for (auto& v : t)
			std::sort(v.begin(), v.end());
		double old = (t[0][4] + t[0][5]) / 2, now = (t[1][4] + t[1][5]) / 2;
		printf("kind=%u chain=%u baseline=%.6f candidate=%.6f ns/vector reduction=%.2f%%\n", c.kind, c.chain, old, now, 100 * (1 - now / old));
	}
	(void)sink;
}
