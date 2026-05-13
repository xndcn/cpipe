# Pixel 8 Pro DNG Fixture Source

`pixel8pro.dng` is a 1920x1080 even-aligned crop of the raw.pixls.us Pixel 8 Pro
sample.

- Source: `https://raw.pixls.us/getfile.php/7751/nice/Google - Pixel 8 Pro - 16bit (4:3).dng`
- Source mirror path: `https://raw.pixls.us/data/Google/Pixel 8 Pro/PXL_20240415_103400204.RAW-02.ORIGINAL.dng`
- Source SHA-256: `45420c595401547cca950ae58d552bc82b32d31ce1d58c082df33004016197f5`
- Source repository row: Google / Pixel 8 Pro / 16bit (4:3), dated 2025-04-28, license CC0.
- Derived crop: x=8, y=8, width=1920, height=1080 from the 8160x6144 2x2 Bayer raw image.
- Derived SHA-256: `67a966d399b36cbefd9148641e5b154c47af386ac0738f260139b77e1b79a286`
- License: CC0 1.0 Universal, matching the raw.pixls.us repository entry.

The crop preserves the Pixel 8 Pro raw samples and P1-required calibration tags.
The source DNG does not carry `AsShotNeutral`; the fixture stores the neutral
derived from LibRaw's `pre_mul` values so `wb.dual_illuminant` can exercise the
real sample through the P1 pipeline.
