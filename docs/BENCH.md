# CPU per mode

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

