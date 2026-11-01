| The recorded KBNA demo for the emulator build (src/feed_fake.c reads it): res/fake/*.bin, made by
| `make fake-feed`. In the 68000's part of the ROM; the SH-2s never read the cartridge.
        .section .rodata.fake, "a"
        .balign 4
        .globl  fake_seq_rom
fake_seq_rom:
        .incbin "res/fake/kbna_seq.bin"
        .balign 4
        .globl  fake_detail_rom
fake_detail_rom:
        .incbin "res/fake/kbna_detail.bin"
        .balign 4
        .globl  fake_cockpit_rom
fake_cockpit_rom:
        .incbin "res/fake/kbna_cockpit.bin"
        .balign 4

        .section .note.GNU-stack, "", %progbits
