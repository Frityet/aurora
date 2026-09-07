# Indexed GX texture matrix loads

Implemented the release SDK GXLoadTexMtxIndx packet: GX_LOAD_INDX_C (0x30), source index in bits 16+, (8 or 12 words -1) in bits12..15 and exact ordinary/postmatrix XF destination. The existing decoder already selects GX_TEX_MTX_ARRAY, source stride and endian convention. Postmatrix copy now accepts both 8 and 12 words, preserving the third row for 2x4 loads.

Primary local evidence: Petari decomp/build/original-j3d-joint-traversal-20260903/retail/asm/RVL_SDK/gx/GXTransform.s GXLoadTexMtxIndx (retail 0x804C07DC,size 80), plus Dolphin OpcodeDecoding.h indexed-XF routing. The release function allows 2x4 postmatrix loads; the stricter GD debug assertion is not the release API contract.

All 258 tests from 6 suites passed in gx_fifo_tests (CMake build exit 0, runtime exit 0, 2026-09-07). New tests cover index/stride/native-endian ordinary matrices and big-endian postmatrices, exact raw FIFO packet bytes, and untouched trailing rows after partial loads. This is CPU FIFO/DL proof, not rendered image-effect proof.

Root evidence: notes/original-water-owner-20260907/gx-fifo-{build,run}.log and gx-fifo-tests.xml.
