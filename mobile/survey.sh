#!/bin/bash
# Read-only survey of the RAN EP7 source. Does not modify the source tree.
cd /home/user/ran-ep7/src || exit 1

echo "== D3D9 device method calls (top 40) =="
grep -rhoE '(m_pd3dDevice|pd3dDevice|m_pD3DDevice|pD3DDevice|m_pDevice|pDevice|m_pd3dDevice9)[ ]*(->|[.])[ ]*[A-Za-z_]+[ ]*\(' \
  --include='*.cpp' --include='*.h' . 2>/dev/null | grep -v '^\./=' \
  | sed -E 's/.*(->|[.])[ ]*//; s/[ ]*\($//' | sort | uniq -c | sort -rn | head -40

echo "== Win32 API families (files using them) =="
for api in CreateWindow PeekMessage GetMessage DispatchMessage RegOpenKey RegQueryValue CreateThread WaitForSingleObject CreateMutex QueryPerformanceCounter timeGetTime CreateFile GetModuleFileName SetCursor ShowCursor ClipCursor SetWindowLong GetDC ReleaseDC; do
  n=$(grep -rlE "\b${api}[A-Za-z]*[ ]*\(" --include='*.cpp' --include='*.h' . 2>/dev/null | grep -v '^\./=' | wc -l)
  echo "$n $api"
done

echo "== DirectInput / DirectSound object usage (files) =="
grep -rlE 'LPDIRECTINPUT|IDirectInput|LPDIRECTSOUND|IDirectSound' --include='*.cpp' --include='*.h' . 2>/dev/null | grep -v '^\./=' | wc -l

echo "== files with backslash path literals =="
grep -rlE '"[^"]*\\\\[^"]*"' --include='*.cpp' . 2>/dev/null | grep -v '^\./=' | wc -l
