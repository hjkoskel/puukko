; PUUKKO C64 – low level: IRQ (frame tick, $D021 raster splits, music, SFX)
; and NMI (4-bit $D418 digi playback from CIA2 timer A).
;
; Memory configuration: BASIC + KERNAL banked out ($01 = $35), so the
; hardware vectors at $FFFA/$FFFE live in RAM and point here. The main code
; sometimes banks I/O out too ($01 = $34) to reach the sprite cache under
; $D000, so both handlers bank I/O in themselves and restore $01 on exit.

        .export _sys_init, _frame_cnt, _is_pal
        .export _split_n, _split_line, _split_col, _top_bg
        .export _music_play, _music_stop, _sfx_play, _digi_play, _digi_on, _snd_vol
        .export _music_enabled, _sfx_enabled, _relocate_hi
        .import __HIDATA_LOAD__, __HIDATA_RUN__, __HIDATA_SIZE__

        .import _digi_lo, _digi_hi, _digi_elo, _digi_ehi
        .import _digi_period_pal, _digi_period_ntsc
        .import _mus_tab_lo, _mus_tab_hi, _sfx_tab_lo, _sfx_tab_hi

HITAB   = $0200         ; page aligned nibble tables
LOTAB   = $0300
FRAME_LINE = 251

MZP     = $FB           ; music pointer (IRQ only)
SZP     = $FD           ; sfx pointer (IRQ + sfx_play under SEI)

        .segment "DATA"
_frame_cnt:     .byte 0
_is_pal:        .byte 1
_split_n:       .byte 0         ; number of $D021 splits this screen
_split_line:    .res 8, 0
_split_col:     .res 8, 0
_top_bg:        .byte 0
split_idx:      .byte 0
_digi_on:       .byte 0
_snd_vol:       .byte $0F
_music_enabled: .byte 1
_sfx_enabled:   .byte 1

mus_on:         .byte 0
mtime:          .byte 0,0
mgate:          .byte 0,0
mptrlo:         .byte 0,0
mptrhi:         .byte 0,0
mstartlo:       .byte 0,0
mstarthi:       .byte 0,0
mwave:          .byte $10,$10
vofs:           .byte 0,7
mad:            .byte $09,$0A
msr:            .byte $09,$0A
sfx_on:         .byte 0

        .segment "CODE"

; ------------------------------------------------------------------ relocate
; copy the HIDATA blob (loaded at $C000 with the program) to its run
; address under the KERNAL ROM. Must run before $C000 is used by the VIC.
_relocate_hi:
        lda #<__HIDATA_LOAD__
        sta $FB
        lda #>__HIDATA_LOAD__
        sta $FC
        lda #<__HIDATA_RUN__
        sta $FD
        lda #>__HIDATA_RUN__
        sta $FE
        ldx #>(__HIDATA_SIZE__ + 255)
        beq @done
        ldy #0
@cp:    lda ($FB),y
        sta ($FD),y
        iny
        bne @cp
        inc $FC
        inc $FE
        dex
        bne @cp
@done:  rts

; ------------------------------------------------------------------ init
; void sys_init(void) – called with interrupts off, $01 already = $35
_sys_init:
        sei
        ; PAL/NTSC: find the highest raster line low byte
@w1:    lda $D012
@w2:    cmp $D012
        beq @w2
        bmi @w1
        ldx #1
        cmp #$20
        bcs @pal
        dex
@pal:   stx _is_pal

        ; nibble tables
        ldx #0
@tab:   txa
        and #$0F
        sta LOTAB,x
        txa
        lsr
        lsr
        lsr
        lsr
        sta HITAB,x
        inx
        bne @tab

        ; vectors
        lda #<irq
        sta $FFFE
        lda #>irq
        sta $FFFF
        lda #<nmi
        sta $FFFA
        lda #>nmi
        sta $FFFB

        ; CIA interrupts off
        lda #$7F
        sta $DC0D
        sta $DD0D
        lda $DC0D
        lda $DD0D
        lda #0
        sta $DD0E

        ; SID clear
        ldx #$18
        lda #0
@sid:   sta $D400,x
        dex
        bpl @sid
        lda #$08
        sta $D403
        sta $D40A
        sta $D411
        lda #$0F
        sta $D418

        ; raster IRQ
        lda $D011
        and #$7F
        sta $D011
        lda #FRAME_LINE
        sta $D012
        lda #1
        sta $D01A
        lda #$FF
        sta $D019
        cli
        rts

; ------------------------------------------------------------------ IRQ
irq:    pha
        txa
        pha
        tya
        pha
        lda $01
        pha
        lda #$35
        sta $01
        lda #$FF
        sta $D019
        ldx split_idx
        cpx _split_n
        bcs frame_irq
        ; ---- background split
        lda _split_col,x
        sta $D021
        inx
        stx split_idx
        cpx _split_n
        bcs @last
        lda _split_line,x
        sta $D012
        jmp irq_out
@last:  lda #FRAME_LINE
        sta $D012
        jmp irq_out

frame_irq:
        lda _top_bg
        sta $D021
        lda #0
        sta split_idx
        inc _frame_cnt
        lda _split_n
        beq @nosplit
        lda _split_line
        sta $D012
        bne @snd
@nosplit:
        lda #FRAME_LINE
        sta $D012
@snd:   jsr music_tick
        jsr sfx_tick
irq_out:
        pla
        sta $01
        pla
        tay
        pla
        tax
        pla
nmi_ignore:
        rti

; ------------------------------------------------------------------ NMI digi
; Self-modifying: nmi_ptr = current byte, nmi_tab = HITAB/LOTAB page.
; Registers are saved on the stack: an NMI stretched by badlines/sprite DMA
; can be interrupted by the next one, so no self-modified save slots here.
nmi:    pha
        lda $01
        pha
        lda #$35
        sta $01
        lda $DD0D
        bpl nmi_01              ; not from the CIA (RESTORE key) -> ignore
        txa
        pha
nmi_ptr = *+1
        ldx $FFFF
nmi_tab = *+2
        lda HITAB,x
        sta $D418
        lda nmi_tab
        eor #(>HITAB ^ >LOTAB)
        sta nmi_tab
        cmp #>HITAB
        bne nmi_x               ; just played the high nibble
        inc nmi_ptr
        bne @noc
        inc nmi_ptr+1
@noc:   lda nmi_ptr
nmi_elo = *+1
        cmp #0
        bne nmi_x
        lda nmi_ptr+1
nmi_ehi = *+1
        cmp #0
        bne nmi_x
        ; end of sample
        lda #$7F
        sta $DD0D
        lda #0
        sta $DD0E
        sta $D404               ; DC off, give the voices back to music/sfx
        sta $D40B
        sta $D412
        sta _digi_on
        lda mad
        sta $D405
        lda msr
        sta $D406
        lda mad+1
        sta $D40C
        lda msr+1
        sta $D40D
        lda _snd_vol
        sta $D418
nmi_x:  pla
        tax
nmi_01: pla
        sta $01
        pla
        rti

; void __fastcall__ digi_play(unsigned char id)
_digi_play:
        tax
        lda _sfx_enabled
        bne @go
        rts
@go:    lda #$7F
        sta $DD0D
        lda #0
        sta $DD0E
        lda $DD0D
        lda _digi_lo,x
        sta nmi_ptr
        lda _digi_hi,x
        sta nmi_ptr+1
        lda _digi_elo,x
        sta nmi_elo
        lda _digi_ehi,x
        sta nmi_ehi
        lda #>HITAB
        sta nmi_tab
        lda #1
        sta _digi_on
        sei
        lda #0
        sta sfx_on
        sta $D405               ; all three voices: AD = 0, SR = $F0 ...
        sta $D40C
        sta $D413
        lda #$F0
        sta $D406
        sta $D40D
        sta $D414
        lda #$49                ; ... test bit + pulse + gate = a constant DC level
        sta $D404               ; that the $D418 volume nibbles modulate (loud on
        sta $D40B               ; the 6581, and what makes the 8580 audible at all)
        sta $D412
        cli
        ldx _is_pal
        beq @ntsc
        lda _digi_period_pal
        ldx _digi_period_pal+1
        jmp @per
@ntsc:  lda _digi_period_ntsc
        ldx _digi_period_ntsc+1
@per:   sta $DD04
        stx $DD05
        lda #$81
        sta $DD0D
        lda #$11
        sta $DD0E
        rts

; ------------------------------------------------------------------ music
; void __fastcall__ music_play(unsigned char track)
_music_play:
        asl
        tax                     ; x = track*2 (lead), x+1 = bass
        sei
        lda _mus_tab_lo,x
        sta mstartlo
        sta mptrlo
        lda _mus_tab_hi,x
        sta mstarthi
        sta mptrhi
        lda _mus_tab_lo+1,x
        sta mstartlo+1
        sta mptrlo+1
        lda _mus_tab_hi+1,x
        sta mstarthi+1
        sta mptrhi+1
        lda #1
        sta mtime
        sta mtime+1
        lda #0
        sta mgate
        sta mgate+1
        lda mad
        sta $D405
        lda msr
        sta $D406
        lda mad+1
        sta $D40C
        lda msr+1
        sta $D40D
        lda _music_enabled
        sta mus_on
        cli
        rts

_music_stop:
        lda #0
        sta mus_on
        sta $D404
        sta $D40B
        rts

music_tick:
        lda mus_on
        bne @go
        rts
@go:    ldx #0
@vloop: lda mgate,x
        beq @nog
        dec mgate,x
        bne @nog
        ldy vofs,x
        lda mwave,x
        sta $D404,y             ; gate off -> pluck decays
@nog:   dec mtime,x
        bne @next
        lda mptrlo,x
        sta MZP
        lda mptrhi,x
        sta MZP+1
@fetch: ldy #2
        lda (MZP),y
        bne @note
        ldy #0
        lda (MZP),y
        bne @stop
        lda mstartlo,x
        sta MZP
        sta mptrlo,x
        lda mstarthi,x
        sta MZP+1
        sta mptrhi,x
        jmp @fetch
@stop:  lda #0
        sta mus_on
        sta $D404
        sta $D40B
        rts
@note:  sta mtime,x
        lda MZP
        clc
        adc #3
        sta mptrlo,x
        lda MZP+1
        adc #0
        sta mptrhi,x
        lda _digi_on
        bne @next
        ldy #0
        lda (MZP),y
        iny
        ora (MZP),y
        beq @next               ; rest
        lda (MZP),y
        pha
        dey
        lda (MZP),y
        ldy vofs,x
        sta $D400,y
        pla
        sta $D401,y
        lda mwave,x
        sta $D404,y
        ora #1
        sta $D404,y
        lda #2
        sta mgate,x
@next:  inx
        cpx #2
        beq @done
        jmp @vloop
@done:  rts

; ------------------------------------------------------------------ sfx (voice 3)
; void __fastcall__ sfx_play(unsigned char id)
_sfx_play:
        ldx _sfx_enabled
        bne @go
        rts
@go:    ldx _digi_on
        beq @ok
        rts                     ; the voice owns the SID right now
@ok:    tax
        sei
        lda _sfx_tab_lo,x
        sta SZP
        lda _sfx_tab_hi,x
        sta SZP+1
        ldy #0
        lda #$08
        sta $D412               ; reset osc + envelope gate
        lda (SZP),y
        sta $D413
        iny
        lda (SZP),y
        sta $D414
        lda SZP
        clc
        adc #2
        sta SZP
        bcc @nc
        inc SZP+1
@nc:    lda #1
        sta sfx_on
        cli
        rts

sfx_tick:
        lda sfx_on
        beq @out
        lda _digi_on
        bne @out
        ldy #0
        lda (SZP),y
        cmp #$FF
        beq @end
        pha
        iny
        lda (SZP),y
        sta $D40E
        iny
        lda (SZP),y
        sta $D40F
        pla
        sta $D412
        lda SZP
        clc
        adc #3
        sta SZP
        bcc @out
        inc SZP+1
@out:   rts
@end:   lda #0
        sta sfx_on
        rts

; ------------------------------------------------------------------ fighter swap
; void __fastcall__ fswap(unsigned char size) – exchange the fc/fo structs
        .export _fswap
        .import _fc, _fo
_fswap: tax
        dex
@lp:    lda _fc,x
        ldy _fo,x
        sta _fo,x
        tya
        sta _fc,x
        dex
        bpl @lp
        rts

; ------------------------------------------------------------------ random
; 16-bit xorshift (7, 9, 8) – unsigned char rnd(void)
        .export _rnd, _rndn
        .segment "DATA"
seed:   .word $ACE1
        .segment "CODE"
_rnd:   lda seed+1
        lsr
        lda seed
        ror
        eor seed+1
        sta seed+1              ; x ^= x << 7 (hi part)
        ror
        eor seed
        sta seed                ; ... and the >> 9 part
        eor seed+1
        sta seed+1              ; x ^= x << 8
        ldx #0
        rts

; unsigned char __fastcall__ rndn(unsigned char n) – (rnd() * n) >> 8
_rndn:  sta mul_n
        jsr _rnd
        sta mul_a
        lda #0
        ldx #8
        lsr mul_a
@lp:    bcc @nc
        clc
        adc mul_n
@nc:    ror
        ror mul_a
        dex
        bne @lp
        ldx #0
        rts                     ; A = high byte of the product
        .segment "DATA"
mul_n:  .byte 0
mul_a:  .byte 0
        .segment "CODE"

; ------------------------------------------------------------------ input
; unsigned char kbd_scan(void): joysticks + full keyboard matrix.
; kb[r] = keys held in row r (joystick 1 columns masked), kbp[r] = new presses.
; returns OR of all held keys (0 = keyboard idle)
        .export _kbd_scan, _kb, _kbp, _joy1, _joy2
        .segment "BSS"
_kb:    .res 8
_kbp:   .res 8
_joy1:  .res 1
_joy2:  .res 1
kany:   .res 1
ktmp:   .res 1
        .segment "RODATA"
rowsel: .byte $FE,$FD,$FB,$F7,$EF,$DF,$BF,$7F
        .segment "CODE"
_kbd_scan:
        lda #$FF
        sta $DC02
        sta $DC00
        lda #0
        sta $DC03
        sta kany
        lda $DC01
        eor #$FF
        and #$1F
        sta _joy1
        lda $DC00
        eor #$FF
        and #$1F
        sta _joy2
        lda _joy1
        eor #$FF
        sta kmask
        ldx #7
krow:   lda rowsel,x
        sta $DC00
        lda $DC01
        eor #$FF
kmask = *+1
        and #$FF
        sta ktmp
        eor _kb,x               ; changed bits
        and ktmp                ; ... that are now down
        sta _kbp,x
        lda ktmp
        sta _kb,x
        ora kany
        sta kany
        dex
        bpl krow
        lda #$FF
        sta $DC00
        lda kany
        ldx #0
        rts

; ------------------------------------------------------------------ profiling
; void __fastcall__ prof_mark(unsigned char slot): adds the CIA1 timer A
; cycles elapsed since the previous mark to prof_acc[slot] (debug builds)
.ifdef PROF
        .export _prof_mark, _prof_acc, _prof_init
        .segment "BSS"
_prof_acc: .res 64
plast:  .res 2
pnow:   .res 2
        .segment "CODE"
_prof_init:
        lda #$FF
        sta $DC04
        sta $DC05
        lda #$11
        sta $DC0E               ; start, continuous, force load
        rts
_prof_mark:
        asl
        asl
        tay
@rd:    ldx $DC05
        lda $DC04
        cpx $DC05
        bne @rd
        sta pnow
        stx pnow+1
        lda plast               ; delta = last - now (counts down)
        sec
        sbc pnow
        pha
        lda plast+1
        sbc pnow+1
        tax
        pla
        clc
        adc _prof_acc,y
        sta _prof_acc,y
        txa
        adc _prof_acc+1,y
        sta _prof_acc+1,y
        bcc @nc
        lda _prof_acc+2,y
        adc #0
        sta _prof_acc+2,y
        bcc @nc
        lda _prof_acc+3,y
        adc #0
        sta _prof_acc+3,y
@nc:    lda pnow
        sta plast
        lda pnow+1
        sta plast+1
        rts
.endif

; ------------------------------------------------------------------ sprites
        .export _spr_vsync, _spr_flipcopy
        .import _spr_vis, _spr_next, _spr_nbuf, _spr_dbuf, _spr_hw, _spr_y
        .import _spr_flash, _spr_col1, _spr_col2, _spr_x, _spr_src, _spr_dst
REVTAB  = $F000
REVMC   = $F100
SPRPTRS = $C3F8

        .segment "BSS"
v_en:   .res 1
v_msb:  .res 1
v_mask: .res 1
v_slot: .res 1
v_y:    .res 1
v_layer: .res 1
f_sl:   .res 1
f_sh:   .res 1
f_dl:   .res 1
f_dh:   .res 1
        .segment "RODATA"
hwmask: .byte $07, $38          ; sprite bits for hw base 0 / 3
        .segment "CODE"

; commit both fighters' sprites: pointers, positions, colours, enable, MSB
_spr_vsync:
        lda #0
        sta v_en
        sta v_msb
        ldx #0
@slot:  stx v_slot
        lda _spr_vis,x
        bne @vis
        jmp @next
@vis:   lda _spr_nbuf,x
        sta _spr_dbuf,x
        ldy _spr_hw,x           ; 0 or 3
        lda hwmask
        cpy #0
        beq @m0
        lda hwmask+1
@m0:    sta v_mask
        ora v_en
        sta v_en
        lda _spr_next,x         ; pointers
        sta SPRPTRS,y
        clc
        adc #1
        sta SPRPTRS+1,y
        adc #1
        sta SPRPTRS+2,y
        lda _spr_flash,x        ; colours
        beq @norm
        lda #1
        sta $D027,y
        sta $D028,y
        sta $D029,y
        bne @pos
@norm:  lda #10                 ; light red skin
        sta $D027,y
        lda _spr_col1,x
        sta $D028,y
        lda _spr_col2,x
        sta $D029,y
@pos:   lda _spr_y,x
        sta v_y
        tya
        asl
        tay                     ; y = hw * 2 (VIC position registers)
        txa
        asl
        tax                     ; x = slot * 2 (s16 array)
        lda _spr_x+1,x
        bmi @neg
        beq @lo
        lda v_mask
        ora v_msb
        sta v_msb
@lo:    lda _spr_x,x
        jmp @setx
@neg:   lda #0
@setx:  sta $D000,y
        sta $D002,y
        sta $D004,y
        lda v_y
        sta $D001,y
        sta $D003,y
        sta $D005,y
        ldx v_slot
@next:  inx
        cpx #2
        beq @done
        jmp @slot
@done:  lda v_msb
        sta $D010
        lda v_en
        sta $D015
        rts

; mirror one cached frame (3 x 64 bytes at spr_src) into spr_dst
; d[r] = rev[s[r+2]], d[r+1] = rev[s[r+1]], d[r+2] = rev[s[r]] for every row
setops: lda f_sl
        sta fs0+1
        clc
        adc #1
        sta fs1+1
        adc #1
        sta fs2+1
        lda f_sh
        sta fs0+2
        sta fs1+2
        sta fs2+2
        lda fs1+1               ; propagate carries (rare: only across a page)
        cmp f_sl
        bcs @c1
        inc fs1+2
        inc fs2+2
        bne @c2
@c1:    lda fs2+1
        cmp f_sl
        bcs @c2
        inc fs2+2
@c2:    lda f_dl
        sta fd0+1
        clc
        adc #1
        sta fd1+1
        adc #1
        sta fd2+1
        lda f_dh
        sta fd0+2
        sta fd1+2
        sta fd2+2
        lda fd1+1
        cmp f_dl
        bcs @d1
        inc fd1+2
        inc fd2+2
        rts
@d1:    lda fd2+1
        cmp f_dl
        bcs @d2
        inc fd2+2
@d2:    rts

_spr_flipcopy:
        lda _spr_src
        sta f_sl
        lda _spr_src+1
        sta f_sh
        lda _spr_dst
        sta f_dl
        lda _spr_dst+1
        sta f_dh
        lda #>REVTAB
        sta ft0+2
        sta ft1+2
        sta ft2+2
        lda #3
        sta v_layer
flayer: jsr setops
        ldy #0
frow:
fs0:    ldx $1000,y
ft0:    lda REVTAB,x
fd2:    sta $1000,y
fs1:    ldx $1000,y
ft1:    lda REVTAB,x
fd1:    sta $1000,y
fs2:    ldx $1000,y
ft2:    lda REVTAB,x
fd0:    sta $1000,y
        iny
        iny
        iny
        cpy #63
        bne frow
        dec v_layer
        beq fout
        clc
        lda f_sl
        adc #64
        sta f_sl
        bcc fn1
        inc f_sh
        clc
fn1:    lda f_dl
        adc #64
        sta f_dl
        bcc fn2
        inc f_dh
fn2:    lda v_layer
        cmp #1                  ; last layer = multicolour: pair-mirror table
        bne flayer
        lda #>REVMC
        sta ft0+2
        sta ft1+2
        sta ft2+2
        jmp flayer
fout:   rts
