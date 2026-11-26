# CPU per mode

**1.5.0: the emulator, built like the firmware, and every mode checked.** `tools/bench/run.sh` compiled the DSP without several of the firmware's flags (`-funroll-loops`, `-fno-move-loop-invariants`, `-finline-functions-called-once`, the STM32F4 / `ARM_MATH_CM4` defines); it now uses exactly the flags `make -f monsoon/makefile` uses (for the same settings the bench moves by a few percent, e.g. CHLOE 2132 → 2115). `tools/bench/verify.sh` runs every mode and option on QEMU's STM32F405 through the firmware's own int16 audio path (`Engine::Process`, dither included), on input that is bit-identical everywhere (integer oscillators + LCG noise), from a fresh engine each time, and the same code on the Mac (`host/build/verify`), then compares (`tools/bench/verify_compare.py`): every mode starts, nothing clips, fully dry is the input within 1 LSB in every mode, ARM and Mac agree within 0.2 dB (about a third bit-identical; the ARM build fuses multiply-adds), and CPU of the audio interrupt (`Engine::Process`) per 32-frame block from block 32 on (the fade-in covers the first 15); the spectral modes' FFT work runs between interrupts and is in run.sh's figures, not these. Four runs each: typical (knobs mid, FREEZE for the last third, a TRIG, ADC-like knob noise), heavy, dry (WET 0, mono bass off) and a sweep of all 16 panel corners (every slider at each end, TUNE ±24, BAL/FBK/RVB at their ends), which reports its costliest corner.

The sweep found what the fixed-knob bench never saw: CHLOE's PT2399 at short delays needed 6434 instructions per frame (SIZE at the bottom: ~46 bits per sample per side through the delta modulator; even SIZE noon cost ~2350), its BBD 2483, and TAPE at ×4 speed 2311. The PT2399's clock now stops at 160 kHz and the BBD's at 64 kHz, with a shorter line below that (same delay time: host/tests_chloe.cc), and TAPE tops out at ×3. 1.5.0, 3200 blocks per run (mean / worst block, instructions per stereo frame):

| Mode | opt | typical mean / worst | heavy mean / worst | sweep: costliest corner / worst block | dry: max dev (LSB) |
|---|---|---|---|---|---|
| spectral | 1 | 301 / 301 | 301 / 301 | 303 / 323 | 1 |
| spectral | 2 | 781 / 781 | 781 / 781 | 781 / 781 | 1 |
| convolve | 1 | 517 / 542 | 517 / 558 | 517 / 558 | 1 |
| resonator | 1 | 1796 / 1962 | 1792 / 1958 | 1825 / 1918 | 1 |
| difference | 1 | 1734 / 1737 | 1734 / 1746 | 1735 / 1743 | 1 |
| cross | 1 | 818 / 819 | 818 / 819 | 818 / 825 | 1 |
| stretch | 1 | 377 / 421 | 378 / 421 | 420 / 421 | 1 |
| stretch | 2 | 350 / 381 | 350 / 381 | 381 / 381 | 1 |
| spreek | 1 | 798 / 1081 | 798 / 1082 | 863 / 1081 | 1 |
| spreek | 2 | 793 / 1076 | 788 / 1071 | 856 / 1071 | 1 |
| capture | 1 | 1763 / 1795 | 2149 / 2167 | 2166 / 2183 | 1 |
| gendy | 1 | 1258 / 1448 | 1253 / 1724 | 1542 / 1746 | 1 |
| shimmer | 1 | 1439 / 2152 | 1591 / 2295 | 1447 / 1453 | 1 |
| tape | 1 | 1719 / 2003 | 1697 / 1867 | 2091 / 2148 | 1 |
| tape | 2 | 1359 / 1550 | 1320 / 1461 | 1643 / 1664 | 1 |
| plates | 1 | 1943 / 2031 | 1946 / 2039 | 2200 / 2289 | 1 |
| chloe | 1 | 2151 / 2234 | 1879 / 2205 | 2193 / 2372 | 1 |
| chloe | 2 | 1887 / 1987 | 1849 / 1957 | 2068 / 2212 | 1 |
| chloe | 3 | 2041 / 2126 | 2031 / 2113 | 2115 / 2264 | 1 |
| chloe | 4 | 1692 / 1764 | 1689 / 1776 | 1762 / 1907 | 1 |
| chloe | 5 | 2140 / 2229 | 1869 / 2201 | 2189 / 2368 | 1 |
| chloe | 6 | 1877 / 1983 | 1838 / 1952 | 2064 / 2208 | 1 |
| chloe | 7 | 2034 / 2108 | 2018 / 2108 | 2111 / 2252 | 1 |
| chloe | 8 | 1686 / 1761 | 1673 / 1772 | 1755 / 1895 | 1 |
| faltung | 1 | 937 / 944 | 935 / 942 | 935 / 935 | 1 |
| quell | 1 | 317 / 317 | 318 / 318 | 318 / 318 | 1 |
| leitner | 1 | 1149 / 1149 | 1144 / 1154 | 1145 / 1149 | 1 |
| tantri | 1 | 2058 / 2153 | 2059 / 2210 | 2068 / 2098 | 1 |
| harmonizer | 1 | 807 / 808 | 965 / 972 | 963 / 965 | 1 |
| harmonizer | 2 | 807 / 808 | 965 / 972 | 963 / 966 | 1 |
| harmonizer | 3 | 807 / 808 | 965 / 971 | 963 / 965 | 1 |
| harmonizer | 4 | 805 / 806 | 962 / 973 | 961 / 966 | 1 |
| putz | 1 | 696 / 696 | 696 / 696 | 696 / 697 | 1 |
| putz | 2 | 696 / 696 | 696 / 696 | 696 / 697 | 1 |
| ensemble | 1 | 1598 / 1601 | 1598 / 1601 | 1496 / 1601 | 1 |
| barberpole | 1 | 1371 / 1413 | 1401 / 1413 | 1405 / 1413 | 1 |
| specdelay | 1 | 299 / 300 | 299 / 300 | 299 / 300 | 1 |
| choir | 1 | 1648 / 1690 | 1649 / 1690 | 1648 / 1689 | 1 |
| folder | 1 | 1895 / 1902 | 1892 / 1902 | 1906 / 1932 | 1 |
| folder | 2 | 1895 / 1902 | 1892 / 1902 | 1906 / 1926 | 1 |
| hall | 1 | 1813 / 1890 | 1809 / 1891 | 1892 / 1899 | 1 |
| bloom | 1 | 1429 / 1465 | 1420 / 1462 | 1455 / 1462 | 1 |
| gamelan | 1 | 2036 / 2069 | 2033 / 2071 | 2031 / 2042 | 1 |
| gamelan | 2 | 2030 / 2063 | 2027 / 2065 | 2026 / 2036 | 1 |
| mirror | 1 | 774 / 774 | 774 / 775 | 774 / 778 | 1 |
| polish | 1 | 955 / 956 | 956 / 957 | 956 / 957 | 1 |

Still close to the line (worst blocks ≈ 2150–2300, one block late = the mode skips one block): CAPTURE with DENSITY and RVB up (2166), PLATES with everything up (2200), SHIMMER and TANTRI on the TRIG block, CHLOE's tube at full drive.

**1.3.0: knob noise.** The table below holds every knob perfectly still. A module's knobs never are: after smoothing, the ADC still moves each pot by ~0.001 and PITCH (through V/OCT) by ~0.03 semitone. `BENCH_DEFS="-DBENCH_JITTER" tools/bench/run.sh` adds that noise. In 1.2 three modes retuned from their knobs on every twitch: PLATES 1362 → 2223, GAMELAN 1842 → 2090, RESONATOR 1846 → 2029 (worst block 2148; on a module it glitched and the mode menu stopped answering). In 1.3 they retune only once a knob moves past its noise (5 cents on PITCH, 0.002 on the pots) and RESONATOR retunes two strings a block: with the noise, PLATES 1364, GAMELAN 1844, RESONATOR 1852 (worst block 1970). And whatever a mode costs, a block that runs past 85 % of its time now makes the next block skip the mode, so the panel and the mode menu always answer.

**1.3.0: SPECTRAL option 2, SMEAR** (`BENCH_DEFS="-DBENCH_OPTION=1"`): 1644 per frame typical (ISR 764 with the FDN reverb at RVB 0.5, background 879).

Measured with `tools/bench/run.sh` (QEMU STM32F405 model, instruction counts per stereo frame, engine + output section included, Process + Prepare averaged). Budget ≈ 2400; real cycles are roughly 1.0–1.3× these numbers.

Bench knob settings: typical = sliders 0.4–0.6, PITCH +7, WET 0.7, BAL 0.6, FBK 0.4, RVB 0.5; heavy = DENSITY 1, SIZE 0.95, TEXTURE 0.9, FBK 0.9, RVB 0.9, PITCH −12.

1.1.0 vs 1.0.0. In 1.0 every mode paid for the whole output section (FDN reverb ~430, widener ~65, feedback loop + tamer ~110). In 1.1 the widener is gone, the mono bass costs ~52, and the feedback loop and FDN only run for modes that map a knob to them, so most of that budget went into the new per-mode BAL/FBK/RVB controls. No mode got slower.

1.2.0: at the default buffer quality (48 kHz 16-bit) every mode measures the same as 1.1 (within a few instructions): the buffer modes check the quality once per block and run a separate copy of their read loop for the lower qualities. The buffer modes at the lower qualities, `BENCH_DEFS="-DBENCH_QUALITY=q" tools/bench/run.sh`, typical / heavy:

| Mode | 24k 16-bit | 16k 16-bit | 48k 8-bit | 24k 8-bit | 16k 8-bit |
|---|---|---|---|---|---|
| grains | 1187 / 1188 | 1171 / 1172 | 1030 / 1031 | 1189 / 1190 | 1172 / 1173 |
| stretch | 2059 / 2142 | 2014 / 2098 | 1881 / 1964 | 2058 / 2141 | 2014 / 2097 |
| capture | 1804 / 2163 | 1795 / 2155 | 1723 / 2083 | 1805 / 2164 | 1796 / 2155 |
| tape | 1918 / 1905 | 1920 / 1893 | 2166 / 2120 | 2120 / 2080 | 2095 / 2067 |
| barberpole | 1668 / 1707 | 1652 / 1691 | 1517 / 1556 | 1675 / 1715 | 1666 / 1698 |
| bloom | 1809 / 1802 | 1794 / 1787 | 1670 / 1661 | 1829 / 1823 | 1814 / 1806 |

The lower rates add the anti-alias decimator (~80 per channel per frame) on the record side; STRETCH reads its analysis window in one pass. Worst single interrupt block: TAPE at 48k 8-bit, 2338 (1.1: 2105).

**GRAINS with a full buffer.** The table below warms up for 250 blocks (0.17 s), which leaves GRAINS' buffer mostly empty, so few grains can spawn. With `-DBENCH_WARMUP=4500` (3 s): 1.3.0 measured 2160 typical / 2313 heavy (worst block 2465 / 2685), above what the real module handles (~2150 worst block), so the whole output glitched, dry signal included, at every buffer quality. 1.3.1 decodes each grain's stretch of the buffer once per block and interpolates the grain window every 4 samples (output within -105 dB of 1.3.0, bit-identical reads): 1772 typical / 1590 heavy, worst block 2037 at 48 kHz 16-bit; 1726–1851 typical, worst 1977–2076, at the other qualities. (Earlier GRAINS figures of 2755 / 3060 were wrong: the bench's micro-benchmarks reused the engine's memory after GRAINS had started, leaving garbage grains running. The bench now restarts the engine first; `-DBENCH_ONLY=MODE_GRAINS -DBENCH_GRAINS` runs one mode and prints the active grains.)

| Mode | 1.0 typical | 1.1 typical | 1.0 heavy | 1.1 heavy | 1.1 worst ISR block (typ/heavy) |
|---|---|---|---|---|---|
| grains | 1643.9 | 1618.2 | 1050.8 | 1029.8 | 1616/1027 |
| spectral | 1924.4 | 1421.6 | 1894.3 | 1384.6 | 298/300 |
| convolve | 1803.7 | 1394.7 | 2104.7 | 1777.7 | 549/549 |
| resonator | 2223.4 | 1845.8 | 2227.5 | 1848.3 | 1964/1970 |
| difference | 1935.4 | 1897.2 | 1918.6 | 1889.1 | 1899/1889 |
| cross | 1585.3 | 1474.5 | 1577.5 | 1464.9 | 797/791 |
| stretch | 2387.9 | 1771.7 | 2440.2 | 1858.9 | 416/416 |
| spreek | 1652.4 | 1080.3 | 1645.4 | 1080.3 | 1017/1017 |
| capture | 1792.1 | 1718.8 | 2097.1 | 2078.4 | 1721/2077 |
| gendy | 1512.9 | 1317.7 | 1484.3 | 1286.9 | 1520/1830 |
| shimmer | 1874.1 | 1530.8 | 1835.1 | 1494.1 | 2224/2188 |
| tape | 2371.8 | 1937.3 | 2341.8 | 1905.2 | 2105/2012 |
| plates | 1458.9 | 1362.0 | 1455.1 | 1360.4 | 1366/1366 |
| chloe | 2587.9 | 2133.5 | 2340.9 | 1890.4 | 2132/1889 |
| faltung | 1781.8 | 1592.2 | 1734.8 | 1545.5 | 924/926 |
| quell | 2268.1 | 1806.9 | 2265.3 | 1806.9 | 317/317 |
| leitner | 1362.2 | 1191.4 | 1353.9 | 1181.0 | 1189/1183 |
| tantri | 2250.4 | 2055.7 | 2238.4 | 2045.6 | 2108/2087 |
| harmonizer | 2028.4 | 1440.3 | 2030.3 | 1440.3 | 805/806 |
| putz | 1980.1 | 1443.9 | 1980.7 | 1443.9 | 753/753 |
| ensemble | 1728.3 | 1573.0 | 1719.8 | 1563.9 | 1571/1564 |
| barberpole | 1851.7 | 1488.9 | 1894.6 | 1528.0 | 1487/1526 |
| specdelay | 1706.8 | 1124.6 | 1692.6 | 1116.4 | 301/301 |
| choir | 1779.9 | 1579.6 | 1769.6 | 1570.9 | 1613/1603 |
| folder | 1905.1 | 1747.1 | 1893.1 | 1738.9 | 1747/1738 |
| hall | 2215.1 | 1715.9 | 2203.5 | 1709.4 | 1714/1707 |
| bloom | 1948.5 | 1505.1 | 1938.6 | 1498.3 | 1507/1500 |
| gamelan | 2045.9 | 1842.0 | 2038.9 | 1839.7 | 1890/1890 |
| mirror | 2160.8 | 2009.9 | 2081.1 | 1936.5 | 760/760 |
| polish | 1560.9 | 1002.2 | 1558.3 | 1001.9 | 1000/1000 |

Output-section parts (instructions per frame): FDN reverb 432, feedback-loop tamer 78, mono bass 52, limiter 32.

PLATES recomputes its mode coefficients while BAL is moving (≈ +500 per frame during the 10 ms glide, not in the table: the bench holds BAL still), still well under budget.

## FFT size (1.4)

`BENCH_DEFS="-DBENCH_FFT=n"` runs the spectral modes at one FFT setting (1 = 256 … 4 = 2048; PUTZ and SPECDELAY stop at 1024). Typical / heavy insns per frame, 250-block warm-up; the worst ISR block doesn't change with the size (the FFTs run between interrupts):

| Mode (own size) | own | 256 | 512 | 1024 | 2048 |
|---|---|---|---|---|---|
| spectral (2048) | 1448 / 1405 | 1440 / 1391 | 1439 / 1401 | 1440 / 1396 | 1448 / 1405 |
| spectral SMEAR (512) | 1627 | 1627 | — | — | 1708 |
| cross (2048) | 1480 / 1470 | 1462 / 1451 | 1470 / 1458 | 1466 / 1456 | 1480 / 1470 |
| stretch (2048) | 1784 / 1873 | 1696 / 1772 | 1727 / 1809 | 1684 / 1769 | 1784 / 1873 |
| quell (1024) | 1805 | 1833 | 1880 | 1805 | 1988 |
| putz (1024) | 1453 | 1449 | 1463 | 1453 | 1453 (1024) |
| specdelay (512) | 1135 / 1126 | 1129 / 1119 | 1135 / 1126 | 1135 / 1126 | 1135 / 1126 (1024) |
| mirror (1024) | 2009 / 1938 | 2014 / 1948 | 2050 / 1980 | 2009 / 1938 | 2116 / 2042 |

