#!/usr/bin/env python3
"""ELF32 ARM fixture with real Thumb code, vectors and symbols.

No cross compiler required. This fixture is not a bootable UF2/Pico SDK firmware.
"""
import pathlib
import struct
import sys


def make_elf():
    base = 0x10000100
    # Le segment commence par les vecteurs. Le code commence à l'offset 0x100.
    payload = bytearray(0x160)
    # Entrées de 32 bits, little-endian. Les offsets 0 et 4 donnent MSP et PC.
    # Les offsets 12, 44 et 60 routent HardFault, SVC et SysTick vers Fault_Handler.
    # Le bit 0 des adresses de code sélectionne Thumb.
    for offset, value in [(0, 0x20041000), (4, 0x10000201),
                          (12, 0x10000241), (44, 0x10000241), (60, 0x10000241)]:
        struct.pack_into("<I", payload, offset, value)
    # Cœur 0 : ajouter 1 jusqu'à 5. Cœur 1 : ajouter 2 jusqu'à 10.
    for offset, increment, target, bkpt in [(0x100, 1, 5, 0), (0x120, 2, 10, 1)]:
        code = [
            0x2000,              # MOVS r0, #0       Initialise le compteur.
            0x4903,              # LDR r1, [pc, #12] Charge l'adresse du compteur en SRAM.
            0x3000 | increment,  # ADDS r0, #n       Ajoute le pas du cœur.
            0x6008,              # STR r0, [r1]      Rend la valeur observable en mémoire.
            0x2800 | target,     # CMP r0, #cible    Met à jour le drapeau Z.
            0xD1FB,              # BNE boucle       Repart à ADDS si Z vaut zéro.
            0xBE00 | bkpt,       # BKPT #n          Arrête le débogueur avec le numéro du cœur.
            0xE7FE,              # B .              Reste ici si l'exécution reprend après BKPT.
        ]
        struct.pack_into("<8H", payload, offset, *code)
        # Mot de données visé par LDR. Chaque cœur écrit à une adresse distincte.
        struct.pack_into("<I", payload, offset + 16, 0x20000000 + bkpt * 4)
    # Fault_Handler exécute BKPT #2. BX LR utilise l'adresse de retour d'exception.
    struct.pack_into("<2H", payload, 0x140, 0xBE02, 0x4770)

    # ELF réserve un nom vide et une entrée de symbole nulle à l'index zéro.
    strings = bytearray(b"\0")
    symbols = bytearray(16)
    # 0x12 désigne une fonction globale. 0x11 désigne un objet global.
    # Les compteurs utilisent SHN_ABS. Leurs adresses SRAM sont fixées ici.
    for name, value, info, section in [
        ("Reset_Handler", 0x10000201, 0x12, 2),
        ("core1_entry", 0x10000221, 0x12, 2),
        ("Fault_Handler", 0x10000241, 0x12, 2),
        ("counter", 0x20000000, 0x11, 0xFFF1),
        ("counter1", 0x20000004, 0x11, 0xFFF1),
        ("__vectors", base, 0x11, 1),
    ]:
        symbols.extend(struct.pack("<IIIBBH", len(strings), value, 0, info, 0, section))
        strings.extend(name.encode("ascii") + b"\0")

    # La table des noms de sections est distincte de la table des noms de symboles.
    section_names = bytearray(b"\0")
    name_offsets = {}
    for name in [".vectors", ".text", ".symtab", ".strtab", ".shstrtab"]:
        name_offsets[name] = len(section_names)
        section_names.extend(name.encode("ascii") + b"\0")
    # Garder 0x100 octets devant les données pour les en-têtes ELF et PT_LOAD.
    image = bytearray(0x100) + payload
    sym_offset = len(image)
    image.extend(symbols)
    str_offset = len(image)
    image.extend(strings)
    names_offset = len(image)
    image.extend(section_names)
    image.extend(b"\0" * (-len(image) % 4))  # Aligner les en-têtes de section sur 4 octets.
    sh_offset = len(image)
    image.extend(bytes(40))  # Première entrée de section réservée et nulle.

    def section(name, kind, flags, address, offset, size, link=0, info=0, alignment=4, entry_size=0):
        image.extend(struct.pack("<10I", name_offsets[name], kind, flags, address,
                                 offset, size, link, info, alignment, entry_size))

    # .vectors et .text occupent le segment chargé. Les tables servent aux outils ELF.
    section(".vectors", 1, 2, base, 0x100, 0x100)
    section(".text", 1, 6, base + 0x100, 0x200, 0x60)
    section(".symtab", 2, 0, 0, sym_offset, len(symbols), 4, 1, 4, 16)
    section(".strtab", 3, 0, 0, str_offset, len(strings), alignment=1)
    section(".shstrtab", 3, 0, 0, names_offset, len(section_names), alignment=1)
    # ELF32 little-endian. ET_EXEC et EM_ARM identifient un exécutable ARM.
    # Le champ d'entrée pointe sur Reset_Handler avec le bit Thumb à 1.
    ident = b"\x7fELF\x01\x01\x01" + bytes(9)
    struct.pack_into("<16sHHIIIIIHHHHHH", image, 0, ident, 2, 40, 1,
                     0x10000201, 52, sh_offset, 0x05000000, 52, 32, 1, 40, 6, 5)
    # Un seul PT_LOAD en lecture/exécution. Taille en mémoire égale à celle du fichier.
    struct.pack_into("<8I", image, 52, 1, 0x100, base, base, len(payload), len(payload), 5, 4)
    return image


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: python3 examples/make_demo.py chemin/demo.elf")
    output = pathlib.Path(sys.argv[1])
    output.write_bytes(make_elf())
    print(f"ELF ARM/Thumb : {output} ({output.stat().st_size} octets)")
