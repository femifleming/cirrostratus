# CPU per mode

Measured with `tools/bench/run.sh` (QEMU STM32F405 model, instruction counts, engine + output section included). Budget ≈ 2400; real cycles are roughly 1.0–1.3× these numbers.

| Mode | Typical | Heavy |
|---|---|---|
| grains | 1643.9 | 1050.8 |
| spectral | 1924.4 | 1894.3 |
| convolve | 1803.7 | 2104.7 |
| resonator | 2223.4 | 2227.5 |
| difference | 1935.4 | 1918.6 |
| cross | 1585.3 | 1577.5 |
| stretch | 2387.9 | 2440.2 |
| spreek | 1652.4 | 1645.4 |
| capture | 1792.1 | 2097.1 |
| gendy | 1512.9 | 1484.3 |
| shimmer | 1874.1 | 1835.1 |
| tape | 2371.8 | 2341.8 |
| plates | 1458.9 | 1455.1 |
| chloe | 2587.9 | 2340.9 |
| faltung | 1781.8 | 1734.8 |
| quell | 2268.1 | 2265.3 |
| leitner | 1362.2 | 1353.9 |
| tantri | 2250.4 | 2238.4 |
| harmonizer | 2028.4 | 2030.3 |
| putz | 1980.1 | 1980.7 |
| ensemble | 1728.3 | 1719.8 |
| barberpole | 1851.7 | 1894.6 |
| specdelay | 1706.8 | 1692.6 |
| choir | 1779.9 | 1769.6 |
| folder | 1905.1 | 1893.1 |
| hall | 2215.1 | 2203.5 |
| bloom | 1948.5 | 1938.6 |
| gamelan | 2045.9 | 2038.9 |
| mirror | 2160.8 | 2081.1 |
| polish | 1560.9 | 1558.3 |
