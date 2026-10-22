"""Writes the engine's placeholder sounds into content/sounds/.

Every sound is synthesised here from sine waves and seeded noise, so the repository holds no
third-party audio. Games replace any of them by putting a file of the same name in their own
content/sounds/. Run from anywhere: python scripts/make_placeholder_sounds.py
"""

import math
import random
import struct
import wave
from pathlib import Path

RATE = 22050
OUT = Path(__file__).resolve().parent.parent / 'content' / 'sounds'


def write(name, samples):
    OUT.mkdir(parents=True, exist_ok=True)
    peak = max(1e-9, max(abs(s) for s in samples))
    scale = 0.9 / peak if peak > 0.9 else 1.0
    with wave.open(str(OUT / f'{name}.wav'), 'wb') as file:
        file.setnchannels(1)
        file.setsampwidth(2)
        file.setframerate(RATE)
        file.writeframes(b''.join(
            struct.pack('<h', int(max(-1.0, min(1.0, s * scale)) * 32767)) for s in samples))


def frames(seconds):
    return int(seconds * RATE)


def noise(seed):
    generator = random.Random(seed)
    return lambda: generator.uniform(-1.0, 1.0)


def lowpass(samples, amount):
    out, value = [], 0.0
    for s in samples:
        value += (s - value) * amount
        out.append(value)
    return out


def sweep(start_hz, end_hz, seconds, decay, harmonics=(1.0,)):
    """A tone gliding from start_hz to end_hz, fading exponentially."""
    out, phase = [], 0.0
    count = frames(seconds)
    for i in range(count):
        t = i / count
        frequency = start_hz + (end_hz - start_hz) * t
        phase += 2.0 * math.pi * frequency / RATE
        tone = sum(weight * math.sin(phase * (n + 1)) for n, weight in enumerate(harmonics))
        out.append(tone * math.exp(-decay * t) * min(1.0, i / 40.0))
    return out


def mix(*parts):
    length = max(len(p) for p in parts)
    return [sum(p[i] if i < len(p) else 0.0 for p in parts) for i in range(length)]


def scaled(samples, gain):
    return [s * gain for s in samples]


def burst(seconds, decay, smoothing, seed):
    sample = noise(seed)
    count = frames(seconds)
    return lowpass([sample() * math.exp(-decay * i / count) for i in range(count)], smoothing)


def footstep():
    return mix(scaled(burst(0.07, 9.0, 0.25, 1), 0.8), scaled(sweep(140, 90, 0.07, 8.0), 0.5))


def jump():
    return mix(scaled(sweep(220, 480, 0.18, 4.0, (1.0, 0.3)), 0.6), scaled(burst(0.12, 6.0, 0.15, 2), 0.3))


def land():
    return mix(scaled(sweep(95, 45, 0.16, 6.0), 1.0), scaled(burst(0.06, 10.0, 0.3, 3), 0.5))


def pain():
    return scaled(sweep(340, 190, 0.3, 3.0, (1.0, 0.5, 0.33, 0.25)), 0.8)


def death():
    out, phase = [], 0.0
    count = frames(0.85)
    for i in range(count):
        t = i / count
        frequency = (280 - 210 * t) * (1.0 + 0.04 * math.sin(2 * math.pi * 7 * i / RATE))
        phase += 2.0 * math.pi * frequency / RATE
        tone = math.sin(phase) + 0.5 * math.sin(2 * phase) + 0.3 * math.sin(3 * phase)
        out.append(tone * (1.0 - t) ** 1.5 * min(1.0, i / 60.0))
    return out


def respawn():
    out = []
    count = frames(0.6)
    for i in range(count):
        t = i / count
        envelope = math.sin(math.pi * t) * (0.7 + 0.3 * math.sin(2 * math.pi * 18 * i / RATE))
        frequency = 400 + 800 * t
        tone = sum(math.sin(2 * math.pi * frequency * k * i / RATE) / k for k in (1, 2, 3))
        out.append(tone * envelope)
    return out


def bell(hz, seconds):
    count = frames(seconds)
    return [(math.sin(2 * math.pi * hz * i / RATE) + 0.4 * math.sin(2 * math.pi * hz * 2.76 * i / RATE))
            * math.exp(-5.0 * i / count) * min(1.0, i / 30.0) for i in range(count)]


def checkpoint():
    gap = [0.0] * frames(0.12)
    return mix(bell(660, 0.5), gap + bell(990, 0.55))


def mover_start():
    return mix(scaled(burst(0.25, 7.0, 0.08, 4), 0.9), scaled(sweep(70, 55, 0.25, 5.0), 0.8))


def mover_stop():
    return mix(scaled(burst(0.3, 6.0, 0.06, 5), 1.0), scaled(sweep(60, 40, 0.3, 4.0), 1.0))


def mover_move():
    # Every part repeats a whole number of times a second, so the sound loops without a seam.
    count = frames(1.0)
    return [(0.6 * math.sin(2 * math.pi * 55 * i / RATE) + 0.35 * math.sin(2 * math.pi * 110 * i / RATE)
             + 0.15 * math.sin(2 * math.pi * 165 * i / RATE))
            * (0.85 + 0.15 * math.sin(2 * math.pi * 4 * i / RATE)) for i in range(count)]


def explosion():
    rumble = burst(1.3, 4.5, 0.035, 6)
    crack = burst(0.12, 14.0, 0.6, 7)
    boom = sweep(85, 28, 1.1, 4.0, (1.0, 0.4))
    return mix(scaled(rumble, 1.0), scaled(crack, 0.5), scaled(boom, 0.9))


def machinegun_fire():
    return mix(scaled(burst(0.09, 9.0, 0.45, 8), 1.0), scaled(sweep(180, 90, 0.08, 7.0), 0.6))


def shotgun_fire():
    return mix(scaled(burst(0.45, 6.0, 0.2, 9), 1.0), scaled(sweep(120, 45, 0.35, 5.0), 0.9),
               scaled(burst(0.05, 20.0, 0.8, 10), 0.6))


def grenade_launcher_fire():
    return mix(scaled(sweep(260, 110, 0.18, 6.0, (1.0, 0.4)), 0.8),
               scaled(burst(0.12, 8.0, 0.2, 11), 0.6))


def rocket_launcher_fire():
    hiss = [s * min(1.0, i / 400.0) for i, s in enumerate(burst(0.6, 3.5, 0.5, 12))]
    return mix(scaled(sweep(150, 60, 0.3, 5.0), 0.8), scaled(hiss, 0.7))


def impact():
    return mix(scaled(burst(0.06, 12.0, 0.7, 13), 0.9), scaled(sweep(900, 500, 0.05, 9.0), 0.25))


def hit():
    return scaled(sweep(1500, 1300, 0.06, 6.0), 0.6)


def weapon_switch():
    gap = [0.0] * frames(0.07)
    return mix(scaled(burst(0.03, 15.0, 0.9, 14), 0.6), gap + scaled(burst(0.04, 12.0, 0.7, 15), 0.7))


def chime(notes, step):
    out = []
    for n, hz in enumerate(notes):
        out = mix(out or [0.0], [0.0] * frames(step * n) + bell(hz, 0.35))
    return out


def pickup_health():
    return scaled(chime([520, 660, 780], 0.06), 0.7)


def pickup_armour():
    return mix(scaled(sweep(300, 600, 0.25, 3.0, (1.0, 0.5, 0.3)), 0.6),
               scaled(burst(0.08, 10.0, 0.3, 16), 0.3))


def pickup_ammo():
    gap = [0.0] * frames(0.05)
    return mix(scaled(burst(0.04, 14.0, 0.6, 17), 0.7), gap + scaled(burst(0.05, 12.0, 0.5, 18), 0.6))


def pickup_weapon():
    return mix(scaled(burst(0.06, 12.0, 0.7, 19), 0.7), scaled(chime([440, 660], 0.08), 0.5))


def pickup_respawn():
    return scaled(sweep(700, 1100, 0.3, 4.0, (1.0, 0.3)), 0.5)


def growl(start_hz, end_hz, seconds, wobble_hz, seed):
    """A rough voice: a buzzy tone that wobbles, with breath under it, swelling then fading."""
    out, phase = [], 0.0
    breath = noise(seed)
    count = frames(seconds)
    for i in range(count):
        t = i / count
        frequency = (start_hz + (end_hz - start_hz) * t) * (1.0 + 0.06 * math.sin(2 * math.pi * wobble_hz * i / RATE))
        phase += 2.0 * math.pi * frequency / RATE
        tone = math.sin(phase) + 0.6 * math.sin(2 * phase) + 0.4 * math.sin(3 * phase) + 0.25 * math.sin(5 * phase)
        out.append((tone + 0.5 * breath()) * math.sin(math.pi * min(1.0, t * 1.4)) ** 0.7 * (1.0 - t) ** 0.5)
    return lowpass(out, 0.3)


def beeps(notes, seconds, gap):
    """Square-ish tones one after another, as a machine would call out."""
    out = []
    for n, hz in enumerate(notes):
        count = frames(seconds)
        tone = [(1.0 if math.sin(2 * math.pi * hz * i / RATE) >= 0.0 else -1.0) * 0.5
                * min(1.0, i / 40.0) * min(1.0, (count - i) / 200.0) for i in range(count)]
        out = mix(out or [0.0], [0.0] * frames((seconds + gap) * n) + lowpass(tone, 0.35))
    return out


def alert():
    return scaled(growl(200, 360, 0.3, 9, 20), 0.8)


def swing():
    # Noise that swells and fades, brighter in the middle, as something swept through the air.
    sample = noise(21)
    count = frames(0.28)
    out, value = [], 0.0
    for i in range(count):
        t = i / count
        amount = 0.04 + 0.3 * math.sin(math.pi * t)
        value += (sample() - value) * amount
        out.append(value * math.sin(math.pi * t) ** 2)
    return scaled(out, 1.0)


def charger_alert():
    return mix(scaled(growl(95, 150, 0.6, 6, 22), 1.0), scaled(burst(0.4, 3.0, 0.06, 23), 0.4))


def charger_pain():
    return scaled(growl(170, 110, 0.25, 11, 24), 0.9)


def charger_death():
    return mix(scaled(growl(150, 45, 1.0, 5, 25), 1.0), scaled(burst(0.6, 4.0, 0.05, 26), 0.3))


def charger_footstep():
    return mix(scaled(burst(0.09, 8.0, 0.12, 27), 0.9), scaled(sweep(85, 45, 0.12, 6.0), 0.9))


def gunner_alert():
    return beeps([880, 660, 990], 0.08, 0.03)


def gunner_pain():
    return mix(scaled(sweep(620, 380, 0.2, 4.0, (1.0, 0.0, 0.5, 0.0, 0.3)), 0.7),
               scaled(burst(0.12, 8.0, 0.6, 28), 0.3))


def gunner_death():
    falling = sweep(700, 90, 0.8, 2.5, (1.0, 0.0, 0.45, 0.0, 0.25))
    static = [s * min(1.0, i / frames(0.3)) for i, s in enumerate(burst(0.9, 3.0, 0.7, 29))]
    return mix(scaled(falling, 0.7), scaled(static, 0.35))


for name, make in [('footstep', footstep), ('jump', jump), ('land', land), ('pain', pain),
                   ('death', death), ('respawn', respawn), ('checkpoint', checkpoint),
                   ('mover_start', mover_start), ('mover_stop', mover_stop),
                   ('mover_move', mover_move), ('explosion', explosion),
                   ('machinegun_fire', machinegun_fire), ('shotgun_fire', shotgun_fire),
                   ('grenade_launcher_fire', grenade_launcher_fire),
                   ('rocket_launcher_fire', rocket_launcher_fire), ('impact', impact), ('hit', hit),
                   ('weapon_switch', weapon_switch), ('pickup_health', pickup_health),
                   ('pickup_armour', pickup_armour), ('pickup_ammo', pickup_ammo),
                   ('pickup_weapon', pickup_weapon), ('pickup_respawn', pickup_respawn),
                   ('alert', alert), ('swing', swing), ('charger_alert', charger_alert),
                   ('charger_pain', charger_pain), ('charger_death', charger_death),
                   ('charger_footstep', charger_footstep), ('gunner_alert', gunner_alert),
                   ('gunner_pain', gunner_pain), ('gunner_death', gunner_death)]:
    write(name, make())
    print(f'wrote {name}.wav')
