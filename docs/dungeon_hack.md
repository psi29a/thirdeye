Dungeon Hack — extraction notes

Summary
-------
This document records the steps taken to obtain and extract the Dungeon Hack (USA) distribution from the Internet Archive item "DungeonHackUSA", and the final contents obtained after extracting ARJ archives found inside the ISO.

What was downloaded
-------------------
- Source: https://archive.org/download/DungeonHackUSA
- File downloaded: "Dungeon Hack (USA).zip" (saved locally as /tmp/dungeonhack.zip)
- The ZIP contained two files of interest:
  - "Dungeon Hack (USA).cue"
  - "Dungeon Hack (USA).bin"

Steps performed
---------------
1. Downloaded the ZIP from Archive.org and extracted the .cue and .bin into the repository workspace under extracted/DungeonHackUSA/.
   - CUE contents: referenced the BIN (MODE1/2352, INDEX 01 00:00:00).
   - BIN SHA-1: 510dfc5038bb17671be5722b9e676de4044d604b

2. Copied the ZIP, the .cue and the .bin to ~/Downloads for convenience:
   - ~/Downloads/dungeonhack.zip
   - ~/Downloads/Dungeon Hack (USA).cue
   - ~/Downloads/Dungeon Hack (USA).bin

3. Extracted the MODE1 payload from the BIN (each 2352-byte sector contains 2048 bytes of user data starting at offset 16). The 2048-byte payloads were concatenated to form an ISO at:
   - ~/Downloads/DungeonHackUSA.iso
   - ISO SHA-1: 56b2ff66f4326f789653616ea38116eb146ef6f4
   - Sectors written: 9226

4. Extracted the ISO contents into:
   - ~/Downloads/HACK_ISO
   Method used: 7z x DungeonHackUSA.iso -o~/Downloads/HACK_ISO
   (script tries 7z first, falls back to mounting the ISO on macOS if 7z isn't available)

5. Located ARJ archives inside the ISO and extracted them into:
   - ~/Downloads/HACK
   ARJ files discovered in ISO extraction:
   - DATA1.ARJ
   - DEMO1.ARJ
   - DEMO2.ARJ

Tools used
----------
- curl (download)
- unzip (to inspect ZIP contents and extract the CUE/BIN)
- Python (to split 2352-byte sectors and write 2048-byte payloads into an ISO)
- 7z (p7zip) when available for ISO and ARJ extraction; fallback: hdiutil (macOS) + rsync, or unar/arj if available
- xxd / hexdump for previews

Files created on disk (key paths)
---------------------------------
- /tmp/dungeonhack.zip (downloaded ZIP)
- extracted/DungeonHackUSA/Dungeon Hack (USA).cue
- extracted/DungeonHackUSA/Dungeon Hack (USA).bin
- ~/Downloads/dungeonhack.zip
- ~/Downloads/Dungeon Hack (USA).cue
- ~/Downloads/Dungeon Hack (USA).bin
- ~/Downloads/DungeonHackUSA.iso
- ~/Downloads/HACK_ISO/  (ISO contents)
- ~/Downloads/HACK/      (ARJ-extracted final files)

ISO top-level files (from ISO extraction)
-----------------------------------------
1. DEARJ.EXE
2. DATA1.ARJ
3. DEMO1.ARJ
4. INSTALL.EXE
5. DEMO2.ARJ
6. DATA1.NFO
7. DATA2.NFO
8. DATA3.NFO

ARJ archives found (absolute paths)
-----------------------------------
1. /Users/bret.curtis/Downloads/HACK_ISO/DATA1.ARJ
2. /Users/bret.curtis/Downloads/HACK_ISO/DEMO1.ARJ
3. /Users/bret.curtis/Downloads/HACK_ISO/DEMO2.ARJ

Contents of the final extraction from the ARJ files
---------------------------------------------------
(These files were extracted into ~/Downloads/HACK — paths are relative to that directory.)

--- extracted files (first 200 entries) ---
DRV/A32PASDG.DLL
DRV/A32ARXM.DLL
DRV/A32ADLIB.DLL
DRV/A32PASFM.DLL
DRV/A32TANDY.DLL
DRV/A32MT32.DLL
DRV/A32ARDG.DLL
DRV/A32PASOP.DLL
DRV/A32SP1FM.DLL
DRV/A32SBDG.DLL
DRV/VFXSCAN.DLL
DRV/A32SBPDG.DLL
DRV/A32SBFM.DLL
DRV/A32SPKR.DLL
DRV/A32SP2FM.DLL
DRV/A32ALGDG.DLL
DRV/A32ALGFM.DLL
DRV/VESA480.DLL
INIT
HACK.BAT
OAK/README.DOC
OAK/67VESA.COM
OAK/OAK.ZIP
OAK/37VESA.COM
STDPATCH.AD
AESOP.EXE
DOS4GW.EXE
MAZE.EXE
PARADISE/VESA.EXE
PARADISE/READ.ME
SBLASTER.COM
LEVELS.DAT
ART/SKY-5.PCX
ART/TITLE.LBM
ART/SKY-4.PCX
ART/OUTSIDE.LBM
ART/SKY-6.PCX
ART/SKY-7.PCX
ART/SKY-3.PCX
ART/SKY-2.PCX
ART/SKY-1.PCX
ART/DEMO0003.LBM
ART/DEMO0002.LBM
ART/DEMO0000.LBM
ART/DEMO0001.LBM
ART/DEMO0005.LBM
ART/DEMO0004.LBM
ART/DEMO0006.LBM
G.BAT
CODE.1
README.BAT
EVEREX/VESA.COM
EVEREX/VESAOFF.COM
EVEREX/README.DOC
EVEREX/VESA.DOC
EVEREX/EVRXVESA.COM
EVEREX/VESAON.COM
EVEREX/EVRXVESA.OUT
CHARS
DEMOGNBG.EXE
GRAPH.INI
C&T/SETVESA.EXE
C&T/V452.BAT
C&T/VESA452.ASM
C&T/VESA452.INC
C&T/VESA451.COM
C&T/VESA452.COM
C&T/VTEST.EXE
C&T/README.TXT
C&T/VESA.INC
RES4
RES3
T
RES2
STB/READ.ME
STB/STB-VESA.COM
RES5
VIDEO7/VESALIST.EXE
VIDEO7/V7VESA.COM
SAMPLE.AD
SIGMA/READ.ME
SIGMA/SIGVESA.COM
ADLIB.ADV
AUDIO/STDPATCH.AD
AUDIO/S_CLOCK.XMI
AUDIO/ST1_GR1.XMI
AUDIO/ST1_GR0.XMI
AUDIO/ST1_GR2.XMI
AUDIO/ST1_GR3.XMI
AUDIO/ERROR.MSG
CHECKSYS.EXE
COLORS
TRIDENT/VESA.EXE
TRIDENT/TVGAVESA.DOC
OPEN.RES
A32SBDG.DLL
SBDIG.ADV
MOUSE.DAT
HACK.TBL
PCSPKR.ADV
TECMAR/VGAVESA.COM
TECMAR/VESATEST.EXE
TECMAR/VESATEST.C
A32SBFM.DLL
GENOA/VESAMODE.EXE
GENOA/VESA.COM
GENOA/READ.ME
GENOA/VESABOX.EXE
GENOA/GENBOX.PAS
GENOA/GENBOX.EXE
GENOA/VESABOX.PAS
README.TXT
CODE.2
APPIAN/APVESA.EXE
RLOFTSR.EXE
NEWCHAR
MT32MPU.ADV
ATI/VVESA.COM
CODE.3
OPEN.TBL
SOUND.EXE
HACK.RES
SAVEGAME/SETTINGS.DAT
SAVEGAME/NEWSCORE.EXE
SAVEGAME/VISIBLE.DAT
SAVEGAME/PC.DAT
SAVEGAME/HISCORE.DEF
SAVEGAME/HISCORE.DAT
SAVEGAME/SAVEGAME.DIR
SAVEGAME/SETSAVE.DAT
ORCHID/ORCHDVSA.COM
ORCHID/ORCHDVSA.DOC
SAVE/TEST2.VCR
SAVE/TEST1.VCR
DATA/3DSPRITE.SHP
DATA/3DSHIPS.SHP
RES0
CIRRUS/CRUSVESA.COM
CIRRUS/README.TXT
RES7
RES9
RES10
SINTAB
RES8
RES6
RES1

(If you need the complete tree instead of the first 200 entries, run: find ~/Downloads/HACK -type f | sed 's|^~/Downloads/HACK/||')

Notes & next actions
--------------------
- I used 7z for ISO and ARJ extraction; if you'd prefer mounting the ISO and inspecting it interactively, hdiutil attach ~/Downloads/DungeonHackUSA.iso will mount the image on macOS.
- If you want a CUE pointing to the generated ISO instead of the BIN, add a small cue file like:

  FILE "DungeonHackUSA.iso" BINARY
    TRACK 01 MODE1/2048
      INDEX 01 00:00:00

- The final extracted directory (~/Downloads/HACK) contains the game executables, data files, drivers, and art assets needed to run the DOS game (under DOSBox or similar).

If you want, next can:
- Produce a tar.gz of ~/Downloads/HACK for transport
- Create a mountable .img with the CUE referencing the ISO
- Run a quick DOSBox test script to verify the demo/installation runs

Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>
