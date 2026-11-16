; vdp.asm -- the VDP writers (adam.h). Arguments are variables, not stack.
; Each locks the NMI out (vdp_busy) and unlocks at the end, taking a frame
; the NMI held back (vdp_catch_up). A zero count does nothing (a count that
; counts down from zero would spray 64K of RAM over the VRAM).

        SECTION code_user

        PUBLIC  _va_addr, _va_len, _va_src, _va_val
        PUBLIC  _va_write, _va_writef, _va_fill
        PUBLIC  _map_next
        EXTERN  _vdp_busy, _vdp_pend, _vdp_catch_up

; set the write address va_addr, lock; DE the count, HL the source
setup:
        ld      a,1
        ld      (_vdp_busy),a
        ld      hl,(_va_addr)
        ld      a,l
        out     ($A1),a
        ld      a,h
        or      $40
        out     ($A1),a
        ld      de,(_va_len)
        ld      hl,(_va_src)
        ret

unlock:
        xor     a
        ld      (_vdp_busy),a
        ld      a,(_vdp_pend)
        or      a
        ret     z
        jp      _vdp_catch_up

; 37 T-states a byte up to 256 (the TMS9918A wants 29 or more between
; writes while it draws), 50 for longer
_va_write:
        ld      de,(_va_len)
        ld      a,d
        or      e
        ret     z
        call    setup
        ld      a,d
        or      a
        jr      nz,wl
        ld      b,e             ; e = 0 can't be: d = 0 and a count of 0 returned
wb:     ld      a,(hl)
        out     ($A0),a
        inc     hl
        djnz    wb
        jr      unlock
wl:     ld      a,(hl)
        out     ($A0),a
        inc     hl
        dec     de
        ld      a,d
        or      e
        jr      nz,wl
        jr      unlock

; OTIR, 21 T-states a byte
_va_writef:
        ld      de,(_va_len)
        ld      a,d
        or      e
        ret     z
        call    setup
fl:     ld      a,d
        or      a
        jr      z,ftail
        ld      bc,$00A0        ; b = 0 is 256
        otir
        dec     d
        jr      fl
ftail:  ld      a,e
        or      a
        jr      z,unlock
        ld      b,a
        ld      c,$A0
        otir
        jr      unlock

; 41 T-states a byte
_va_fill:
        ld      de,(_va_len)
        ld      a,d
        or      e
        ret     z
        call    setup
        ld      a,(_va_val)
        ld      h,a
fi:     ld      a,h
        out     ($A0),a
        dec     de
        ld      a,d
        or      e
        jr      nz,fi
        jr      unlock

; uint8_t map_next(const uint8_t *map, const uint8_t *drawn, uint8_t from)
; (play.c, PLAT_MAP_NEXT) the first tile from `from` on where the map and
; what is drawn differ, or TILES: about 50 T-states a tile against some 400
; as C (the H89's, src/h89/h19cell.asm). zsdcc's arguments on the stack.
TILES   equ     143             ; ARENA_W * ARENA_H

_map_next:
        ld      hl,2
        add     hl,sp
        ld      e,(hl)
        inc     hl
        ld      d,(hl)                  ; DE = map
        inc     hl
        ld      c,(hl)
        inc     hl
        ld      b,(hl)                  ; BC = drawn
        inc     hl
        ld      a,(hl)                  ; from
        cp      TILES
        jr      nc,mn_none
        ld      l,a
        ld      h,0
        add     hl,de
        ex      de,hl                   ; DE = map + from
        ld      l,a
        ld      h,0
        add     hl,bc                   ; HL = drawn + from
        neg
        add     a,TILES
        ld      b,a                     ; B = tiles left
mn_loop:
        ld      a,(de)
        cp      (hl)
        jr      nz,mn_found
        inc     de
        inc     hl
        djnz    mn_loop
mn_none:
        ld      l,TILES
        ret
mn_found:
        ld      a,TILES
        sub     b
        ld      l,a
        ret

        SECTION bss_user
_va_addr:       defs    2
_va_len:        defs    2
_va_src:        defs    2
_va_val:        defs    1
