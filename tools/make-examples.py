#!/usr/bin/env python3
"""Writes the example takes that a fresh recorder starts with: main/examples/*.mid

    python3 tools/make-examples.py

Three short public-domain pieces, entered by hand (not copied from any MIDI file) and
simplified in places: the opening of Beethoven's Fur Elise, the first part of Mozart's
Rondo alla turca and the first bars of Bach's Prelude in C, BWV 846. Timing and loudness are
nudged a little, with a fixed random seed, so they sound played rather than typed; Fur Elise
and the Prelude use the sustain pedal so the page has something to show for it.

The file names follow the recorder's own scheme (<name>_<seconds>s_<notes>n.mid), with the
title in place of the time of day.
"""
import os
import random
import struct

NAMES = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def pitch(s):
    """'C4' -> 60, 'F#5' -> 78, 'Bb2' -> 46."""
    n = NAMES[s[0]]
    i = 1
    while s[i] in "#b":
        n += 1 if s[i] == "#" else -1
        i += 1
    return n + 12 * (int(s[i:]) + 1)


class Piece:
    """Notes on a sixteenth-note grid; rendered to milliseconds at the end."""

    def __init__(self, sixteenth_ms, seed):
        self.step = sixteenth_ms
        self.rnd = random.Random(seed)
        self.notes = []      # (start16, dur16, pitch, velocity)
        self.pedal = []      # (down16, up16)
        self.bar = 0         # start of the current bar, in sixteenths

    def add(self, at, names, dur=1, vel=60):
        for nm in names.split():
            self.notes.append((self.bar + at, dur, pitch(nm), vel))

    def ped(self, down, up):
        self.pedal.append((self.bar + down, self.bar + up))

    def next(self, length):
        self.bar += length

    def render(self, slow_last=0):
        """-> list of (ms, bytes) MIDI events. slow_last: sixteenths at the end to broaden."""
        first = min(s for s, _, _, _ in self.notes)       # pickup notes sit before bar 1
        if first < 0:
            self.notes = [(s - first, d, p, v) for s, d, p, v in self.notes]
            self.pedal = [(a - first, b - first) for a, b in self.pedal]
        end16 = max(s + d for s, d, _, _ in self.notes)

        def ms(t16):
            t = t16 * self.step
            if slow_last and t16 > end16 - slow_last:      # a small ritardando
                over = t16 - (end16 - slow_last)
                t += 0.5 * self.step * over * over / slow_last
            return t

        ev = []
        for s, d, p, v in self.notes:
            jitter = self.rnd.gauss(0, 7)
            on = max(0.0, ms(s) + jitter)
            off = ms(s + d) - 0.12 * self.step * min(d, 2) + jitter
            vel = max(20, min(118, int(v + self.rnd.gauss(0, 4))))
            ev.append((on, 1, bytes([0x90, p, vel])))
            ev.append((max(on + 30, off), 0, bytes([0x80, p, 0])))
        for down, up in self.pedal:
            ev.append((ms(down) + 25, 2, bytes([0xB0, 64, 127])))
            ev.append((ms(up) - 15, -1, bytes([0xB0, 64, 0])))
        ev.sort(key=lambda e: (e[0], e[1]))
        return [(int(round(t)), b) for t, _, b in ev]


def smf(events):
    """Format 0, 1000 ticks per quarter at 60 bpm: one tick is one millisecond."""
    def varlen(n):
        out = [n & 0x7F]
        n >>= 7
        while n:
            out.insert(0, 0x80 | (n & 0x7F))
            n >>= 7
        return bytes(out)

    track = bytearray(b"\x00\xff\x51\x03" + (1_000_000).to_bytes(3, "big"))
    last = 0
    for t, b in events:
        track += varlen(t - last) + b
        last = t
    track += b"\x00\xff\x2f\x00"
    return b"MThd" + struct.pack(">IHHH", 6, 0, 1, 1000) + b"MTrk" + struct.pack(">I", len(track)) + track


# ---- Beethoven, Fur Elise (WoO 59), opening ----------------------------------------------
def fur_elise():
    p = Piece(sixteenth_ms=190, seed=1810)
    B = 6                                               # 3/8: six sixteenths a bar

    def lh(chord, v=46):
        for i, nm in enumerate(chord.split()):
            p.add(i, nm, 1, v - 2 * i)

    def a1(v=66):                                       # E D# E B D C
        for i, nm in enumerate("E5 D#5 E5 B4 D5 C5".split()):
            p.add(i, nm, 1, v - (6 if i in (1, 3) else 0))

    def phrase(ending, v=66):
        a1(v); p.next(B)
        p.ped(0, 5.7); p.add(0, "A4", 2, v + 2); lh("A2 E3 A3"); p.add(3, "C4", 1, v - 12); p.add(4, "E4", 1, v - 10); p.add(5, "A4", 1, v - 6); p.next(B)
        p.ped(0, 5.7); p.add(0, "B4", 2, v + 2); lh("E2 E3 G#3"); p.add(3, "E4", 1, v - 12); p.add(4, "G#4", 1, v - 10); p.add(5, "B4", 1, v - 6); p.next(B)
        p.ped(0, 5.7); p.add(0, "C5", 2, v + 4); lh("A2 E3 A3"); p.add(3, "E4", 1, v - 12); p.add(4, "E5", 1, v - 2); p.add(5, "D#5", 1, v - 8); p.next(B)
        a1(v); p.next(B)
        p.ped(0, 5.7); p.add(0, "A4", 2, v + 2); lh("A2 E3 A3"); p.add(3, "C4", 1, v - 12); p.add(4, "E4", 1, v - 10); p.add(5, "A4", 1, v - 6); p.next(B)
        p.ped(0, 5.7); p.add(0, "B4", 2, v + 2); lh("E2 E3 G#3"); p.add(3, "E4", 1, v - 12); p.add(4, "C5", 1, v - 6); p.add(5, "B4", 1, v - 8); p.next(B)
        p.ped(0, 5.7); lh("A2 E3 A3", 44)
        if ending == "again":
            p.add(0, "A4", 3, v - 4); p.add(4, "E5", 1, v - 4); p.add(5, "D#5", 1, v - 10)
        elif ending == "on":
            p.add(0, "A4", 2, v - 4); p.add(3, "B4", 1, v - 8); p.add(4, "C5", 1, v - 6); p.add(5, "D5", 1, v - 4)
        else:
            p.add(0, "A4", 6, v - 10); p.add(0, "A3 C4 E4", 6, 40)
        p.next(B)

    p.bar = -2
    p.add(0, "E5", 1, 62); p.add(1, "D#5", 1, 56); p.next(2)
    phrase("again")
    phrase("on")
    # the brighter middle part, in C major
    for top, start, run, chord in (("E5", "G4", "F5 E5", "C3 G3 C4"), ("D5", "F4", "E5 D5", "G2 G3 B3"),
                                   ("C5", "E4", "D5 C5", "A2 E3 A3")):
        p.ped(0, 5.7); p.add(0, top, 3, 70); lh(chord, 48)
        p.add(3, start, 1, 54); p.add(4, run.split()[0], 1, 64); p.add(5, run.split()[1], 1, 60); p.next(B)
    p.ped(0, 5.7); p.add(0, "B4", 2, 64); lh("E2 E3 G#3", 46); p.add(3, "E4", 1, 52); p.add(4, "E5", 1, 62); p.add(5, "D#5", 1, 56); p.next(B)
    phrase("end", v=62)
    return "Fur-Elise", p.render(slow_last=12)


# ---- Mozart, Rondo alla turca (K. 331), first part ----------------------------------------
def alla_turca():
    p = Piece(sixteenth_ms=128, seed=1783)
    B = 8                                               # 2/4

    def lh(bass, chord, v=50):                          # bass, then three short chords
        p.add(0, bass, 1, v + 4)
        for at in (2, 4, 6):
            p.add(at, chord, 1, v - 6)

    def turn(at, notes, v=70):                          # four sixteenths
        for i, nm in enumerate(notes.split()):
            p.add(at + i, nm, 1, v - (5 if i % 2 else 0))

    def opening():
        p.add(0, "C5", 2, 74); turn(4, "D5 C5 B4 C5"); lh("A3", "C4 E4"); p.next(B)
        p.add(0, "E5", 2, 76); turn(4, "F5 E5 D#5 E5"); lh("A3", "C4 E4"); p.next(B)
        turn(0, "B5 A5 G#5 A5", 74); turn(4, "B5 A5 G#5 A5", 76); lh("A3", "C4 E4"); p.next(B)
        p.add(0, "C6", 4, 84); p.add(4, "A5", 2, 72); p.add(6, "C6", 2, 76); lh("A3", "C4 E4", 54); p.next(B)

    def first_half():
        opening()
        for top in ("G5 A5", "G5 A5", "G5 F#5"):
            g, last = top.split()
            p.add(0, "B5", 2, 74); p.add(2, "A5", 2, 66); p.add(4, g, 2, 68); p.add(6, last, 2, 66)
            if last == "F#5":
                lh("B2", "D#3 A3")
            else:
                lh("E3", "G3 B3")
            p.next(B)
        p.add(0, "E5", 4, 70); p.add(0, "E3 B3", 3, 52); turn(4, "B4 A4 G#4 A4", 66); p.next(B)

    def second_half(last):
        opening()
        p.add(0, "C6", 4, 82); p.add(4, "A5", 2, 70); p.add(6, "B5", 2, 72); lh("A3", "C4 E4", 54); p.next(B)
        for i, nm in enumerate("C6 B5 A5 G#5".split()):
            p.add(2 * i, nm, 2, 76 - 3 * i)
        lh("E3", "G#3 B3"); p.next(B)
        for i, nm in enumerate("A5 E5 F5 D5".split()):
            p.add(2 * i, nm, 2, 70 - 2 * i)
        lh("D3", "F3 A3"); p.next(B)
        p.add(0, "C5", 4, 68); p.add(4, "B4", 3, 64); lh("E3", "G#3 B3"); p.next(B)
        p.add(0, "A4", 4 if not last else 8, 66); p.add(0, "A2 A3", 4, 56)
        if not last:
            turn(4, "B4 A4 G#4 A4", 64)
        p.next(B)

    p.bar = -4
    turn(0, "B4 A4 G#4 A4", 64); p.next(4)
    first_half(); second_half(last=False)
    first_half(); second_half(last=True)
    return "Rondo-alla-Turca", p.render(slow_last=10)


# ---- Bach, Prelude in C major (BWV 846), first bars ---------------------------------------
def prelude_in_c():
    p = Piece(sixteenth_ms=205, seed=1722)
    chords = [
        "C3 E3 G3 C4 E4", "C3 D3 A3 D4 F4", "B2 D3 G3 D4 F4", "C3 E3 G3 C4 E4",
        "C3 E3 A3 E4 A4", "C3 D3 F#3 A3 D4", "B2 D3 G3 D4 G4", "B2 C3 E3 G3 C4",
        "A2 C3 E3 G3 C4", "D2 A2 D3 F#3 C4", "G2 B2 D3 G3 B3", "G2 Bb2 E3 G3 C#4",
        "F2 A2 D3 A3 D4", "F2 Ab2 D3 F3 B3", "E2 G2 C3 G3 C4", "E2 F2 A2 C3 F3",
        "D2 F2 A2 C3 F3", "G1 D2 G2 B2 F3", "C2 E2 G2 C3 E3",
    ]
    for b, ch in enumerate(chords):
        n = ch.split()
        arc = 8 * (1 - abs(b - 11) / 11)                 # swell towards the middle
        for half in (0, 8):
            p.ped(half, half + 7.8)
            p.add(half, n[0], 8, 50 + arc)
            p.add(half + 1, n[1], 7, 44 + arc)
            for i, nm in enumerate((n[2], n[3], n[4], n[2], n[3], n[4])):
                p.add(half + 2 + i, nm, 1, 50 + arc + (6 if i in (2, 5) else 0) - (4 if half else 0))
        p.next(16)
    # a closing chord, spread from the bottom
    p.ped(0, 15.5)
    for i, nm in enumerate("C2 C3 G3 C4 E4".split()):
        p.add(i * 0.35, nm, 16, 52 - 2 * i)
    return "Prelude-in-C", p.render(slow_last=40)


def main():
    out = os.path.join(os.path.dirname(__file__), "..", "main", "examples")
    os.makedirs(out, exist_ok=True)
    for old in os.listdir(out):
        if old.endswith(".mid"):
            os.remove(os.path.join(out, old))
    # examples.bin is what the firmware embeds: per file, a NUL-terminated name, a 4-byte
    # little-endian length and the bytes; a zero byte where the next name would start ends it.
    blob = bytearray()
    for make in (fur_elise, alla_turca, prelude_in_c):
        name, ev = make()
        notes = sum(1 for _, b in ev if b[0] == 0x90)
        secs = round(ev[-1][0] / 1000)
        fn = f"{name}_{secs:04d}s_{notes:04d}n.mid"
        data = smf(ev)
        with open(os.path.join(out, fn), "wb") as f:
            f.write(data)
        blob += fn.encode() + b"\0" + struct.pack("<I", len(data)) + data
        print(fn)
    blob += b"\0"
    with open(os.path.join(out, "examples.bin"), "wb") as f:
        f.write(blob)


if __name__ == "__main__":
    main()
