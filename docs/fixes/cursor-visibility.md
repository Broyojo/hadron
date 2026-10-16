# Native cursor visibility

This backports only the cursor patch from Elvin Hayatov's Wine MR 11799 v4:
https://list.winehq.org/hyperkitty/list/wine-gitlab@list.winehq.org/message/747TVCL5XYAMFH3MFVYM3FICMNZJSCBL/
The original author is retained. The unrelated GCMouse/raw-input changes are excluded.

The player reports that Ready or Not's native pointer reappears over the crosshair
when the pointer reaches the bottom screen edge, even though the Dock stays hidden.
Tabbing out and back hides it again. A standalone Windows cursor/clip test reproduced
native visibility changing from hidden at the center to visible at the bottom, remaining
visible after returning to the center. Wine's requested hidden state did not change
in that run. Other traces also show legitimate native unhide when the pointer leaves
Wine windows; those transitions must not be confused with the edge reproduction.

The backport represents a Windows hidden cursor with a transparent NSCursor, restores
it when AppKit changes the current cursor and disables cursor-location assistance for
the Wine application. It avoids polling deprecated global cursor-visibility APIs and
does not change system preferences or add per-game behavior. SDK availability for the
presentation option begins at macOS 10.11.2.

## Verification and limits

- Official patch-queue verification passes: Wine 34 patches, DXMT 2 and NotProton 6.
  Patch applies unchanged to Hadron's current Wine queue; compiled both in the local
  investigation source and independently against the current upstream queue.
- Native cursor-object construction succeeds. An empty-image TIFF export does not
  produce pixels and is not used as evidence that rendering is transparent.
- Automated post-fix edge measurement was blocked by an overlapping macOS capture UI;
  no passing visual regression is claimed from that blocked test.
- Installed atomically in the local runtime, preserving backups and the signed loader.
  A subsequent Ready or Not run uses the regular Steam runner and its real prefix.
  Final player acceptance of the cursor edge behavior is still pending.

The standalone Windows probe in tests/win/cursor-visibility.c hides and clips the
cursor in a painted fullscreen window for twenty seconds, then restores it on exit.
Move to the bottom edge and back; repeat with app focus changes. Confirm that the
pointer remains invisible within the probe and returns normally outside it/on exit.
Build with a Windows C compiler and user32/gdi32, for example llvm-mingw with
-municode -luser32 -lgdi32. Start it through scripts/play, as with every test executable.

The real-prefix Steam-runner check initialized D3D11 without renderer command-line
arguments after saving the game's supported DefaultGraphicsRHI preference. It exited
with status 0 and no watchdog stop. This does not prove a Steam Play click or visual
cursor acceptance: Steam can explicitly pass -dx12 from its own launch selection.
