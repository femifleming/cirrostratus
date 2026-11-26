#!/usr/bin/env python3
"""Firmware .bin -> QPSK audio for the Clouds/Monsoon audio bootloader.

A Python 3 port of Mutable Instruments' stm_audio_bootloader/qpsk/encoder.py
(c) Emilie Gillet, MIT license, fixed to the STM32F4 target the bootloader
uses (48 kHz, 6 kHz carrier, 12 kbps, 256-byte packets, 16 KB blocks).
"""
import argparse
import struct
import zlib

import numpy as np

SECTOR_BASES = [0x08000000, 0x08004000, 0x08008000, 0x0800C000, 0x08010000,
                0x08020000, 0x08040000, 0x08060000, 0x08080000, 0x080A0000,
                0x080C0000, 0x080E0000]
BLOCK_SIZE = 16384
APP_START = 0x08008000


class QpskEncoder:
    def __init__(self, sample_rate, carrier, bit_rate, packet_size):
        period = sample_rate // carrier
        symbol_time = sample_rate // (bit_rate // 2)
        assert symbol_time % period == 0 or period % symbol_time == 0
        assert sample_rate % carrier == 0 and sample_rate % bit_rate == 0
        self.sr, self.br, self.carrier = sample_rate, bit_rate, carrier
        self.sample_index = 0
        self.packet_size = packet_size

    def _encode(self, symbols):
        ratio = self.sr // self.br * 2
        s = np.asarray(symbols, dtype=int)
        even = 2 * np.repeat(s % 2, ratio) - 1
        odd = 2 * np.repeat(s // 2, ratio) - 1
        q, i = even / np.sqrt(2.0), odd / np.sqrt(2.0)
        n = len(q)
        t = (np.arange(0.0, n) + self.sample_index) / self.sr
        self.sample_index += n
        phase = 2 * np.pi * self.carrier * t
        return q * np.sin(phase) + i * np.cos(phase)

    def blank(self, duration):
        return self._encode(np.zeros(int(duration * self.br / 8) * 4, dtype=int))

    def packet(self, data):
        data = bytes(data) + b'\xff' * (self.packet_size - len(data))
        crc = zlib.crc32(data) & 0xffffffff
        preamble = [0] * 8 + [0x99] * 4 + [0xcc] * 4
        crc_bytes = [crc >> 24, (crc >> 16) & 0xff, (crc >> 8) & 0xff, crc & 0xff]
        symbols = []
        for byte in preamble + list(data) + crc_bytes:
            symbols += [(byte >> 6) & 3, (byte >> 4) & 3, (byte >> 2) & 3, byte & 3]
        return self._encode(symbols)

    def code(self, data, page_size, blank_duration):
        if len(data) % page_size:
            data += b'\xff' * (page_size - len(data) % page_size)
        written = 0
        for offset in range(0, len(data), self.packet_size):
            yield self.packet(data[offset:offset + self.packet_size])
            written += 1
            if written == page_size // self.packet_size:
                yield self.blank(blank_duration)
                written = 0


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('firmware')
    p.add_argument('-s', '--sample_rate', type=int, default=48000)
    p.add_argument('-c', '--carrier_frequency', type=int, default=6000)
    p.add_argument('-b', '--baud_rate', type=int, default=12000)
    p.add_argument('-p', '--packet_size', type=int, default=256)
    p.add_argument('-o', '--output_file')
    a = p.parse_args()
    data = open(a.firmware, 'rb').read()
    out = a.output_file or a.firmware.replace('.bin', '') + '.wav'
    enc = QpskEncoder(a.sample_rate, a.carrier_frequency, a.baud_rate, a.packet_size)

    chunks = [np.zeros(a.sample_rate), enc.blank(1.0)]
    for x in range(0, len(data), BLOCK_SIZE):
        address = APP_START + x
        pause = 3.5 if address in SECTOR_BASES else 0.2
        chunks += list(enc.code(data[x:x + BLOCK_SIZE], BLOCK_SIZE, pause))
    chunks.append(enc.blank(5.0))
    signal = np.concatenate(chunks)
    pcm = (np.clip(signal, -1, 1) * 32767).astype('<i2')
    with open(out, 'wb') as f:
        f.write(b'RIFF' + struct.pack('<L', 36 + pcm.nbytes) + b'WAVEfmt ')
        f.write(struct.pack('<LHHLLHH', 16, 1, 1, a.sample_rate, a.sample_rate * 2, 2, 16))
        f.write(b'data' + struct.pack('<L', pcm.nbytes))
        f.write(pcm.tobytes())
    print('%s: %d bytes of firmware, %.1f s of audio' % (out, len(data), len(signal) / a.sample_rate))


if __name__ == '__main__':
    main()
