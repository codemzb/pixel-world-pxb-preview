// pxb_reg_contract.h
//
// SINGLE SOURCE OF TRUTH for the .pxb Windows file-association contract.
// Shared by pxb-preview.exe (src/app/regutil.cpp) and the thumbnail handler
// DLL (integrations/windows/PxbThumbnailHandler/PxbThumbnailHandler.cpp).
//
// These identifiers are a PUBLIC contract with the OS once registered:
// changing any of them silently breaks existing associations, so every side
// MUST keep using exactly these values. Change only in sync across projects.
//
#pragma once

// CLSID of the thumbnail provider:
//   {A1B2C3D4-0001-4E5F-8A9B-112233445566}
// Canonical GUID bytes (used by the DLL's INITGUID definition).
#define PXB_CLSID_BYTES \
    0xA1B2C3D4, 0x0001, 0x4E5F, {0x8A, 0x9B, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66}
// Canonical string form — NOTE the braces: registry CLSID keys are written
// with StringFromGUID2 output, which includes them ({...}).
#define PXB_CLSID_STR_W L"{A1B2C3D4-0001-4E5F-8A9B-112233445566}"

// ProgID that owns the "Open" verb for .pxb files.
#define PXB_PROGID_W L"PxbPreview.pxbfile"

// Explorer's thumbnail-handler subkey (IShellItemImageFactory contract).
#define PXB_SHELLEX_KEY_W L"{E357FCCD-A995-4576-B01F-234630154E96}"

// Extension handled.
#define PXB_EXT_W L".pxb"
