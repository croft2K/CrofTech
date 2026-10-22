# vcpkg overlay ports

Ports here replace the ones in the vcpkg registry (`vcpkg.json` lists this folder under
`overlay-ports`). Each is a copy of the registry's port at the pinned baseline with a small change,
noted in its `portfile.cmake`; when the baseline moves, copy the new port and apply the change again.

- `recastnavigation`: Detour built with virtual query filters (`DT_VIRTUAL_QUERYFILTER`) and 64-bit
  polygon references (`DT_POLYREF64`). Both are public compile definitions of the Detour target, so
  everything that includes Detour's headers sees the same layout.
