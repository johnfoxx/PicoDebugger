#!/usr/bin/env python3
"""Generate an actual Thumb ELF for offline direct startup on RP2040/RP2350.

The program sends OK through UART0, starts PWM0, sets GPIO25 output latches,
polls a timer alarm, saves the time at 0x20000000, then executes BKPT #3.
It is a debugger fixture, not a ROM-bootable Pico SDK/UF2 image.
"""
import argparse
from pathlib import Path
import struct


def make_elf(chip):
    pico2 = chip == "rp2350"
    base = 0x10000000
    payload = bytearray(0x400)
    # Vecteurs de démarrage. MSP est au sommet de la SRAM propre à chaque puce.
    # Le PC pointe vers l'offset 0x100, avec le bit Thumb à 1.
    struct.pack_into("<II", payload, 0, 0x20082000 if pico2 else 0x20042000, base + 0x101)
    code, literals = [], []

    def load(register, value):
        # LDR Rt, [PC, #imm]. Le PC Thumb vaut l'adresse de l'instruction + 4.
        # L'encodage aligne cette base sur 4 et compte la distance en mots.
        # Le littéral de 32 bits reste dans la table qui suit le code.
        pc = (base + 0x100 + len(code) * 2 + 4) & ~3
        target = base + 0x200 + len(literals) * 4
        delta = target - pc
        assert delta >= 0 and delta % 4 == 0 and delta // 4 <= 255
        code.append(0x4800 | register << 8 | delta // 4)
        literals.append(value)

    def store(address, value):
        # Charger l'adresse MMIO dans r0 et la valeur dans r1.
        load(0, address)
        load(1, value)
        code.append(0x6001)  # STR r1, [r0]

    # Les blocs existent sur les deux profils. Leurs adresses de base changent.
    uart = 0x40070000 if pico2 else 0x40034000
    timer = 0x400B0000 if pico2 else 0x40054000
    pwm = 0x400A8000 if pico2 else 0x40050000
    store(uart + 0x2C, 0x70)   # UARTLCR_H : mots de 8 bits, FIFO active.
    store(uart + 0x30, 0x301)  # UARTCR : activer UART, TX et RX.
    store(uart, ord("O"))      # UARTDR : envoyer O puis K dans la FIFO TX.
    store(uart, ord("K"))
    store(pwm + 0x10, 3)      # CH0_TOP : compter de 0 à 3 avant le retour à zéro.
    store(pwm, 1)             # CH0_CSR : démarrer le compteur PWM 0.

    # Les écritures SIO activent la sortie GPIO25 et placent son verrou à 1.
    store(0xD0000030 if pico2 else 0xD0000020, 1 << 25)  # GPIO_OE bas.
    store(0xD0000018 if pico2 else 0xD0000014, 1 << 25)  # GPIO_OUT_SET bas.

    store(timer + 0x10, 80)   # ALARM0 : armer l'alarme pour le tick absolu 80.
    # Lire INTR, l'état brut de l'alarme. Le programme interroge ce registre en boucle.
    load(0, timer + (0x3C if pico2 else 0x34))
    loop = len(code)
    code.extend([
        0x6801,  # LDR r1, [r0]   Lit INTR.
        0x2900,  # CMP r1, #0     Détecte si l'alarme est encore en attente.
    ])
    # BEQ boucle. Le déplacement est signé, en demi-mots, depuis le PC + 4.
    code.append(0xD000 | ((loop - len(code) - 2) & 0xFF))
    load(0, timer + 0x28)     # Adresse de TIMERAWL, les 32 bits bas du temps.
    code.append(0x6801)       # LDR r1, [r0]   Capture le temps après l'alarme.
    load(0, 0x20000000)       # Adresse de destination en SRAM.
    code.extend([
        0x6001,  # STR r1, [r0]   Rend l'instant observable dans le débogueur.
        0xBE03,  # BKPT #3        Signale la fin de l'exemple.
        0xE7FE,  # B .            Reste ici si l'exécution reprend.
    ])

    # Écrire séparément les opcodes Thumb de 16 bits et les littéraux de 32 bits.
    # Les deux zones doivent tenir dans le même segment ELF.
    assert len(code) * 2 <= 0x100 and len(literals) * 4 <= 0x200
    struct.pack_into("<" + "H" * len(code), payload, 0x100, *code)
    struct.pack_into("<" + "I" * len(literals), payload, 0x200, *literals)
    # Laisser de la place pour les en-têtes ELF. Ajouter ensuite les noms de sections.
    image = bytearray(0x100) + payload
    names = b"\0.vectors\0.text\0.shstrtab\0"
    names_offset = len(image)
    image.extend(names)
    image.extend(bytes(-len(image) % 4))
    sections_offset = len(image)
    # Entrées de section : nulle, vecteurs, code et littéraux, noms de sections.
    for section in [(0,) * 10,
                    (1, 1, 2, base, 0x100, 0x100, 0, 0, 256, 0),
                    (10, 1, 6, base + 0x100, 0x200, 0x300, 0, 0, 4, 0),
                    (16, 3, 0, 0, names_offset, len(names), 0, 0, 1, 0)]:
        image.extend(struct.pack("<10I", *section))
    # ELF32 little-endian exécutable ARM. Le point d'entrée vise la première instruction Thumb.
    struct.pack_into("<16sHHIIIIIHHHHHH", image, 0, b"\x7fELF\x01\x01\x01" + bytes(9),
                     2, 40, 1, base + 0x101, 52, sections_offset, 0x05000000, 52, 32, 1, 40, 4, 3)
    # PT_LOAD indique au chargeur où copier les vecteurs, le code et les littéraux.
    struct.pack_into("<8I", image, 52, 1, 0x100, base, base, len(payload), len(payload), 5, 256)
    return image


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--chip", choices=["rp2040", "rp2350"], default="rp2350")
    args = parser.parse_args()
    args.output.write_bytes(make_elf(args.chip))
    print(str(args.output) + " — " + args.chip + ", démarrage ELF direct, sans ROM")
