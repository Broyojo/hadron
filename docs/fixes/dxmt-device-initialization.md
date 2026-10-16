# Initialize the encoding device before its command helpers

`ArgumentEncodingContext` declared `device_` after `clear_uav_cmd`. The helper constructor
compiles compute pipelines via `SimpleCommandContext`, which reads `ctx.device_` before its
initialization. C++ initializes members in declaration order. Move both declaration and
initializer before the dependent helpers; no graphics capability or feature level changes.

A Paradox GPU-process diagnostic exposed the invalid receiver 0xbaadf00dbaadf00d during
compute-pipeline creation, Wine's uninitialized-heap fill. The corrected D3D11 DLL builds.
A compute-backed UAV clear and staging readback succeeds, but also passed on the old DLL:
that check does not independently reproduce the allocation pattern. Source initialization
order is the primary evidence for this narrowly scoped correction.

M4, macOS 27.0.1. The player's final Raft/Cities runs used the corrected DLL along with other
changes. No standalone gameplay regression, launcher fix or Khronos CTS claim is made;
CTS covers Vulkan/OpenGL, not this D3D11-to-Metal path. Foreign-window presentation experiments
are deliberately excluded. Patch applies after current main's two DXMT patches.
