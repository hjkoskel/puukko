; PUUKKO – loading screen + loader.
;
; First file on the disk ("PUUKKO"). Shows the PETSCII loading picture
; (loadscreen/*.seq), then loads "PUUKKO GAME" with the KERNAL from a small
; routine in the cassette buffer ($0334), so the picture stays up while the
; ~50 KB game streams in. An IRQ hook turns the blue bars beside "LOADING"
; into a progress bar driven by the KERNAL's load pointer ($AE/$AF).
; Then it jumps into the game's cc65 startup ($080D).

        .import __RES_LOAD__, __RES_RUN__, __RES_SIZE__

SCREEN  = $0400
COLRAM  = $D800
GAMESTART = $080D               ; after the game's BASIC "SYS" stub
LOAD_LO = $08                   ; game spans $0801 .. $CFFF
LOAD_HI = $D0

SETMSG  = $FF90
SETLFS  = $FFBA
SETNAM  = $FFBD
LOAD    = $FFD5

        .segment "LOADADDR"
        .word $0801

        .segment "MAIN"
; 10 SYS2061
        .word basend, 10
        .byte $9E, "2061", 0
basend: .word 0

start:  sei
        lda #0
        sta $D020
        lda bgcol
        sta $D021
        lda #$15                ; upper case / graphics PETSCII, screen $0400
        sta $D018
        lda #$1B
        sta $D011
        ; picture: 1000 screen codes + 1000 colours, 4 pages each
        ldx #0
@cp:    lda scr,x
        sta SCREEN,x
        lda scr+250,x
        sta SCREEN+250,x
        lda scr+500,x
        sta SCREEN+500,x
        lda scr+750,x
        sta SCREEN+750,x
        lda col,x
        sta COLRAM,x
        lda col+250,x
        sta COLRAM+250,x
        lda col+500,x
        sta COLRAM+500,x
        lda col+750,x
        sta COLRAM+750,x
        inx
        cpx #250
        bne @cp
        ; resident loader -> cassette buffer
        ldx #0
@rc:    lda __RES_LOAD__,x
        sta __RES_RUN__,x
        inx
        cpx #<__RES_SIZE__
        bne @rc
        jmp resident

bgcol:  .incbin "../loadscreen/Puukko-Alley-Loading-Screen_bgcolor.seq"
scr:    .incbin "../loadscreen/Puukko-Alley-Loading-Screen_screen.seq"
col:    .incbin "../loadscreen/Puukko-Alley-Loading-Screen_color.seq"

; ------------------------------------------------------------------ resident
        .segment "RES"
resident:
        lda #<irqhook
        sta $0314
        lda #>irqhook
        sta $0315
        cli
        lda #0
        jsr SETMSG              ; no "SEARCHING/LOADING" messages
        lda #namelen
        ldx #<name
        ldy #>name
        jsr SETNAM
        lda #1
        ldx $BA                 ; the drive we were loaded from
        bne @dev
        ldx #8
@dev:   ldy #1                  ; load to the file's own address
        jsr SETLFS
        lda #0
        jsr LOAD
        bcs fail
        sei
        lda #$31
        sta $0314
        lda #$EA
        sta $0315
        lda #8
        jsr paint               ; bars full
        lda #$0B                ; blank the screen for the hand-over (the game's
        sta $D011               ; startup switches charsets before it draws)
        jmp GAMESTART
fail:   sei                     ; disk error: red border, wait for a reset
        lda #$31
        sta $0314
        lda #$EA
        sta $0315
        cli
        lda #2
        sta $D020
@w:     jmp @w

; progress: cells = (load pointer hi - $08) / 25  -> 0..8 per bar
irqhook:
        lda $AF
        sec
        sbc #LOAD_LO
        bcc @done
        ldx #0
@div:   cmp #25
        bcc @n
        sbc #25
        inx
        bne @div
@n:     txa
        jsr paint
@done:  jmp $EA31

; paint A cells (0..8) of both bars white, from the outer ends inwards
paint:  sta cells
        ldx #0
@lp:    lda #6                  ; blue = still to load
        cpx cells
        bcs @set
        lda #1                  ; white = loaded
@set:   sta COLRAM+22*40+1,x    ; left bar, rows 22-23, cols 1..8
        sta COLRAM+23*40+1,x
        stx tmp
        pha
        lda #38
        sec
        sbc tmp
        tay
        pla
        sta COLRAM+22*40,y      ; right bar, cols 38..31 (mirror)
        sta COLRAM+23*40,y
        ldx tmp
        inx
        cpx #8
        bne @lp
        rts

name:   .byte "puukko game"     ; (ca65 maps lower case to PETSCII upper case)
namelen = * - name
cells:  .byte 0
tmp:    .byte 0
