// ==WindhawkMod==
// @id              start-menu-touch-gesture-bridge
// @name            Start Menu Touch Gesture Bridge
// @description     Publishes the live Win10 Start edge-gesture handle and state from explorer for the StartMenuExperienceHost side
// @version         0.16.0
// @author          Zhanyuanium
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -I"C:\Users\doubao\source\repos\swipe-for-10start\mods" -Wall -Wextra -luser32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Start Menu Touch Gesture Bridge (explorer side)

Explorer-side half of the touch-swipe bridge for the ExplorerPatcher legacy
Windows 10 Start menu.

The mod calls no hide of its own and presses no key. The Start side owns both
closes: a keyboard close is the user's Windows tap, a gesture close is a Windows
tap the Start mod sends at the end of its retract. This mod only decides how
long the shell's key handler must wait for the panel.

- A gesture close publishes a plan at its release, and a fresh record marked
  `tapped` immediately before it sends the Windows key. Only the tapped record
  names the Hide that needs no wait, because only it proves the tap is already
  on its way. When that Hide arrives on a tapped record this hook has not
  consumed, the panel is already gone; the hook logs
  `hide passed (gesture)` and calls the original at once. A Hide that arrives
  while only the plan is current is the user's own Windows press during the
  settle, a keyboard close, and gets the lead below.
- A gesture close that a new edge pull cancels before its tap publishes the
  cancelled kind at once, so no Hide in between is ever mistaken for the tap's.
- The close record names no menu, so a tapped record is accepted only when it is
  newer than the sequence this process seeded at attach and was stamped at or
  after the newest open generation it has seen. That alone keeps a tap whose Hide
  never came from passing an unrelated Hide under the next menu; the channel also
  asks the Start side to cancel its stamp when it publishes a new open. Both
  ticks come from one clock, so only a tap and an open inside the same ~16 ms
  tick cannot be ordered: an unconsumed fresh stamp is evidence of correlation,
  not proof of cause, and the plan-versus-tapped distinction is what carries the
  correctness.
- Any other Hide is a keyboard close: the mod publishes the `retract` request on
  the channel and waits for the Start mod's answer with the settle it
  just planned, then holds the shell for exactly that duration plus a 30 ms
  margin. The request carries the menu-open generation the hook saw and a
  deadline, so a request that reaches the Start side late cannot start a settle
  under the menu the user opened next; the hook marks the request done when the
  Hide it held completes. A menu with no published generation sends 0, which is
  never a valid generation and which the Start side refuses. A Start mod that
  cannot answer costs a 100 ms wait and then a fixed 560 ms lead. A request that
  cannot be published skips the wait, because a delay without a retract would
  only slow the close down.

Measured on this build, `Hide` is not the whole close: the shell keeps its Start
toggle in the key handler. Both closes now go through that handler, so the
toggle is always in step and the next Windows press after the close opens the
menu. The old programmatic hide plus a 120 ms re-arm tap is gone; that tap raced
a user press inside its window, which is the bug this protocol removes.

The mod also observes the taskbar-edge gesture path in `twinui.pcshell.dll`,
then publishes the live `GestureAnimationData` handle (source/provider id), the
edge, the start point and the gesture state to shared memory:

- mapping: `Local\WindhawkStartGestureShared` (`StartGestureShared`)
- event:   `Local\WindhawkStartGestureEvent` (auto-reset, signalled on write)

A StartMenuExperienceHost-side consumer could read the mapping and bind a
Composition animation with `InputAnimationTarget::GetInputAnimationObject`; the
shipping Start mod does not read it. Its live couplings are the close stamp and
the retract request/answer pair described below, both in
`Local\SwipeFor10Start_v2_*` (see `swipe_ipc_shared.h`).

Hooked functions are forwarded unchanged except for the keyboard wait described
below. The mod creates no windows, threads or COM objects. It refuses to
initialize unless its own image can be pinned, and on unload it waits, with no
deadline, until every Hide that is inside the hook has left, and closes the
channel only then. A Hide that does not return therefore holds the whole Windhawk
reload until it does; that is the price of never handing the host a trampoline a
live frame can still call.

Notes:
- Windhawk compiles the mod from stdin, so the repository `mods` folder is
  passed via `@compilerOptions -I...` to resolve `start_gesture_shared.h`.
  Update that path if the repository is moved.
- RVAs are specific to `twinui.pcshell.dll` 26100.9949 (x86-64). Before any
  hook is installed, the loaded module must report that exact file version
  (and, when readable, the PE timestamp of the loaded image must match the file
  on disk). Otherwise the mod logs one mismatch line and installs nothing.
- Hooks are installed only after `twinui.pcshell.dll` is loaded, via a
  `LoadLibraryExW` hook plus the module check in `Wh_ModAfterInit`.
- The sequence field is a seqlock: a reader must retry when it is odd or when
  it changed between two reads. The IPC channel uses the same rule per record; a
  single section-wide counter would not work because the two sides write
  different records.
- `edge` is written verbatim from the shell. In the Phase 0 log, a swipe that
  started at (45, 900) on a 1440x900 screen (taskbar on the left) reported
  edge 0, matching `StartGestureEdge::Left`.
- The point is published once per gesture from `ReportEdgeGesture`, in the same
  write as the edge and the handle. There is no per-frame position sampling.
- Each gesture state transition appends one line to `bridge.log` in the mod
  storage folder: `state=... edge=... src=... prov=... <reason>`. The channel to
  the Start mod logs one `ipc open created=... close=...` at init, one
  `activation state reset` when a re-enable re-initialises the pinned image (a
  refused one logs `mod init refused: ...` and installs nothing instead), and
  one `hide open generation unavailable err=...` for a Hide that found no
  published open generation (0 is never a generation).
- The two channels have two lifetimes. The v2 channel (the section mapping and
  view, the mutex and the three events) is closed when the mod is disabled, and
  only then: the disable flag keeps new Hides out of it, and the unload waits for
  every Hide that already entered to leave before it closes anything. That wait
  has no deadline, so its outcome is one of two. `drained`: the count reached
  zero, the channel is closed, the unload returns. `stalled`: the count is still
  nonzero and nothing else happens — no close, no return, no handle left behind.
  The price of `stalled` is exact: a Hide that does not return, because the shell
  call under it never came back or a debugger suspended its thread, holds the
  Windhawk reload until it resumes. A re-enable of the pinned image reuses a
  channel an earlier activation left open instead of opening a second one over
  it.
  The gesture mapping and its event stay for the rest of the explorer process
  lifetime instead: a consumer maps them independently, and an in-flight gesture
  callback may still hold the view, so unmapping them is unsafe. The image is
  pinned, so a re-enable re-initialises this same mapping instead of opening a
  new one; a different installed version maps its own handles and leaves the
  older image's pair to process exit. That is two handles per pinned image,
  deliberate; process exit reclaims them.
- The in-flight count buys the unload's close, and one more thing. The lease
  spans the whole hook, the `Hide_Original` call included, so a count of zero
  also says that no frame of this hook can still need the MinHook trampoline.
  That zero is the only condition on which this mod hands trampoline removal back
  to the host: a frame that is still about to call the original holds a lease, so
  no timeout can stand in for the count, and a timeout that expired under a live
  frame would return to the host a frame whose trampoline it is about to free.
  An ordinary unload is quiet at once, because every wait inside the hook ends
  within one 10 ms step of the disable flag; a wait long enough to be noticed
  means a frame that cannot run at all. 500 ms is where the wait first reports one
  (`ipc unload drain over 500 ms hides=...`), and from then on it reports every
  10 s (`ipc unload stalled hides=... waited=... ms`) until the count reaches
  zero. Both lines are diagnostics: neither ends the wait and neither changes
  what happens next.
- The count answers "is a Hide still inside", never "this Hide runs after that
  one": two Hides on different threads hold it at once, and a re-entrant Hide on
  one thread holds one lease per frame, so it balances by itself. The lease is
  taken before the hook reads anything and released on every exit, so a path
  that returns early and an exception out of `FileLog` both release it exactly
  once. A call that has entered the detour but has not yet taken its lease is
  invisible to the count; nothing inside the mod can close that window.
- Two limits survive, and they are not the same limit. For an uncounted entry
  the pin keeps this image's code and the lease destructor mapped, so its return
  address and its release stay valid. For the trampoline under a frame, the
  allocation is the host's: Windhawk does not promise a drained hook at
  `Wh_ModUninit`. The shipped engine reports 2.0.0-alpha.8 and its `windhawk.dll`
  carries the same `LoadedMod::BeforeUninit` / `_Z12Wh_ModUninitv` lifecycle, and
  at that tag `ModsManager::ReloadModsAndSettings` calls `mod.BeforeUninit()`
  (which queues the disable), applies it with `MH_ApplyQueuedEx`, then calls
  `mod.Uninitialize()` — `Wh_ModUninit` — and only after that waits for threads
  inside the mod image with `ThreadsCallStackWaitForRegions(..., 200, 400)`
  before `m_mods.erase(it)` frees the module; `~CustomizationSession` reaches the
  same order by destroying its `MinHookScopeApply` (which disables every hook)
  before its `ModsManager`. `LoadedMod::BeforeUninit` says what it waits for
  itself: "Waits for in-flight hook creations, then locks out new ones." That
  wait is bounded and its result is discarded, so "the scan found the frame" and
  "the frame exited before removal" are two claims: the first is what the host
  gives when the scan can see the stack, the second is what this mod's drain
  gives for every counted frame. `MH_RemoveHookEx` frees a trampoline only in
  `~LoadedMod`, after that wait, and every caller of the original has this hook
  below it on the stack, so a scan that sees the frame is enough for that frame;
  a scan blind to it leaves a return into freed code, and that is Windhawk's
  unload to fix, not a mod's. Neither claim reaches a trampoline, because a
  trampoline is not in this image: the pin is the cover for this module's code,
  and the drain is the cover for every counted frame's trampoline, because it
  returns only at zero. What is left is the entry window the count cannot see, and
  only the host's scan can cover that.
- The pin is a prerequisite. `Wh_ModInit` returns `FALSE` when it fails and logs
  `mod init refused: the image could not be pinned; no hook, no channel and no
  thread installed`; a shell-owned `Hide` thread needs valid return code as much
  as a worker would, so installing a hook over an image that may go away is not
  an option. Success is
  `GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
  GET_MODULE_HANDLE_EX_FLAG_PIN)` on an address in this module, logged once as
  `mod pin image=... pinned=1` (or `mod pin failed err=...`). The price is an
  image that stays mapped for the rest of the explorer process lifetime: a
  re-enable re-initialises the same pinned DLL, and a version change leaves the
  old image mapped as well. Against a return into unmapped code, that mapping is
  the cheaper of the two risks.
- A re-enable refuses while the previous activation still has a Hide inside:
  `mod init refused: a Hide from the previous activation is still inside; no
  hook, no channel and no thread installed`. Clearing `g_disabled` and
  re-pointing the globals under that frame would hand it a half-reset module, so
  the old state stays exactly as the unload left it. A drained unload returns at
  zero, so this refusal is reachable only through the entry window the count
  cannot see; it is kept because that window is real. When the count is zero,
  `ResetReinitState` clears the lifecycle flags, retries a failed gesture
  mapping, and the init logs `activation state reset`; a ready mapping and a
  channel an earlier activation left open are reused rather than rebuilt.

The delayed Hide:

- Both closes are Windows taps, so every Hide this hook sees was decided by the
  shell's own key handler. No manager pointer is cached, no worker exists, and no
  call is made on the shell's behalf. Every Show and Hide logs one line
  (`show a=... b=... c=...`, `hide reason=...`) before the delay decision.
- A gesture close publishes its plan at its release and a tapped record the
  moment its Windows tap goes down; only the tapped one names the tap's own Hide,
  so the hook logs `hide passed (gesture)` and calls the original at once for it.
  A tapped Hide never waits: no publisher in this repository writes a nonzero
  close delay, and the lead that remains is the keyboard close's alone.
- A plan or a cancel never passes a Hide: the tap is the record that carries
  `tapped`, and the plan stays current for the whole settle. A user's Windows
  press during that settle is therefore a keyboard close and gets its own lead,
  and a close a new pull cancels publishes the cancelled kind at once, not at the
  tap's deadline.
- A keyboard close publishes the `retract` request on the same channel and wakes
  the Start mod through its request event. The request names the menu-open
  generation this hook read and a deadline; that mod plans its settle from the
  panel's current `Progress` only while that generation is current and the
  deadline has not passed, then answers with the duration (or the no-settle
  sentinel) under the request's sequence number. When this hook's Hide completes
  it marks the request done, so a request whose close is over cannot start a
  settle under the next menu. The hook waits for that answer, logs
  `hide delayed reason=... retract=... lead=...` and
  holds the original for the answered duration plus 30 ms, so the shell hides
  after the slide has landed instead of cutting it. The wait for the answer is
  bounded at 100 ms; a missing answer or the sentinel falls back to the fixed
  560 ms lead. Both waits step in 10 ms so a mod being disabled ends them at
  once. When the request cannot be published the hide runs immediately: a delay
  without a retract would only slow the close down.
- The original always runs. An earlier version returned `S_OK` without it;
  measured, the shell's Start toggle then stayed one close ahead and the Windows
  key and the taskbar Start button stopped working until
  `StartMenuExperienceHost.exe` restarted.
- `Hide` is `HRESULT (void* this, uint32_t reason)` on
  twinui.pcshell.dll 26100.9949. The prologue reads `rcx` (this) and `edx`
  (reason), compares `edx` with 7 and returns 0 on success or the negative
  HRESULT of the failing inner call. It never reads `r8` or `r9`.
*/
// ==/WindhawkModReadme==

/*
 * [INPUT]: depends on twinui.pcshell.dll 10.0.26100.9949 in explorer.exe for
 *   TouchEdgeGestureProcessor::InputGestureOperationStarted/Ended,
 *   EdgeAnimationRelationshipManager::ReportEdgeGesture,
 *   CStartExperienceManager::SetAnimationData/Show/Hide; on version.dll for the
 *   build gate; on LoadLibraryExW from kernelbase.dll for the late module load;
 *   on GetModuleHandleExW from kernelbase.dll, which must succeed before this
 *   mod installs anything, and which pins this image for the process lifetime;
 *   on the shared-memory contract in start_gesture_shared.h; on the channel in
 *   swipe_ipc_shared.h, whose close record carries a tapped gesture stamp (the
 *   tap's own Hide) and whose retract request/answer pair carries the settle
 *   duration a keyboard close needs, bound to the menu-open generation the
 *   request was written for. A generation of 0 means the Start side published no
 *   open, so no request may name it and no close stamp can be bound; both then
 *   fall back to the fixed lead.
 * [OUTPUT]: the `Local\WindhawkStartGestureShared` mapping and its auto-reset
 *   event, holding the edge, the gesture handle and the state under a seqlock;
 *   a `bridge.log` line per state transition; one `retract` request on the Start
 *   mod's channel, marked done when its Hide completes, and a wait in
 *   CStartExperienceManager::Hide for the settle duration the Start mod answers
 *   with plus a 30 ms margin, so a Windows-key close slides the panel out before
 *   the window is hidden; a `hide delayed reason=... retract=... lead=...` line
 *   per keyboard close; a `hide passed (gesture)` line and the original
 *   call at once for a Hide on a tapped stamp that is newer than the attach
 *   watermark and not older than the newest open generation, because the Start
 *   side taps before the shell's key handler hides. The v2 channel is closed on
 *   disable, and only at quiescence: the disable flag keeps new Hides out and
 *   the unload waits, with no deadline, until every Hide already inside has
 *   left, then closes the channel (`ipc close`) and returns. Because the lease
 *   spans the whole hook, the original call included, that same zero is the only
 *   condition under which this mod hands trampoline removal back to the host: a
 *   frame still about to call the original holds a lease, so no timeout can
 *   stand in for the count. The two outcomes are `drained` (zero, channel
 *   closed, returned) and `stalled` (still nonzero, and the unload keeps
 *   waiting; nothing else happens). 500 ms is where the wait first says a call
 *   is late (`ipc unload drain over 500 ms hides=...`), and every 10 s after
 *   that it reports `ipc unload stalled hides=... waited=... ms`. The price is
 *   paid in the host: a Hide that does not return, because the shell call under
 *   it never came back or a debugger suspended its thread, blocks the Windhawk
 *   reload until it resumes. The count is a lifetime guard for that channel, not
 *   a barrier between two Hides: it counts the Hides inside `Hide_Hook` through
 *   an RAII lease that every exit path releases, so a re-entrant call on one
 *   thread balances per frame, and a call that has entered the detour but not
 *   yet taken its lease is invisible to it. Windhawk does not drain the hook
 *   before `Wh_ModUninit`: at 2.0.0-alpha.8 `ModsManager` calls `Wh_ModUninit`
 *   and only then waits for threads inside the mod image
 *   (`ThreadsCallStackWaitForRegions(..., 200, 400)`) before freeing the module,
 *   and that wait is bounded and unchecked. This mod therefore refuses to
 *   initialize unless its own image is pinned
 *   (`GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
 *   GET_MODULE_HANDLE_EX_FLAG_PIN)`, `mod pin image=... pinned=1`), which keeps
 *   the return address and the lease destructor of an uncounted frame valid; the
 *   host's wait covers an uncounted frame's trampoline when it can see the
 *   frame, and a scan blind to it covers nothing. A re-enable re-initialises the
 *   same image and refuses while the previous activation still has a Hide inside
 *   — reachable only through that uncounted entry window, because a drained
 *   unload returns at zero; otherwise the lifecycle flags are reset (logged
 *   `activation state reset`), the gesture mapping is reused, and a v2 channel an
 *   earlier activation left open is handed on rather than opened again. The
 *   gesture mapping keeps the process-lifetime behaviour below. Every path calls
 *   the original Hide; nothing is pressed and no hide is requested. No exported
 *   symbols.
 * [POS]: explorer-side half of the legacy Start gesture work. It observes the
 *   shell's own hide decision and decides whether that Hide still needs the
 *   retract lead. Both closes are Windows taps performed by the Start side, and
 *   a gesture close is tapped only after its retract; the shell's Start toggle is
 *   always updated by its key handler, so this mod needs no manager pointer,
 *   worker thread, timer or re-arm tap. The shipping Start mod does not read the
 *   shared gesture memory; the close record, the open generation and the
 *   retract request/answer pair in the swipe_ipc_shared.h channel are the
 *   couplings that are live today. That channel's handles are released when the
 *   mod can prove no Hide is inside it; the older gesture mapping is kept for
 *   the process lifetime because its consumer maps it independently. An
 *   unconsumed fresh stamp is evidence of correlation, not proof of cause: the
 *   plan-versus-tapped distinction is what makes the gesture path correct, and
 *   the binding only keeps a stamp of a menu that is gone from passing a later
 *   Hide. The drain runs inside `Wh_ModUninit` and returns only at quiescence, so
 *   the reload blocks instead of running under a live frame: the image is pinned
 *   as a prerequisite because a hook-only mod cannot wait for its own caller, the
 *   pin covers an entry the count cannot see, and the drain covers the trampoline
 *   of every counted frame.
 * [PROTOCOL]: Update this header on change, then check AGENTS.md
 */

#include <windows.h>
#include <winver.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "start_gesture_shared.h"
#include "swipe_ipc_shared.h"

namespace {

constexpr uintptr_t kTwinuiPreferredBase = 0x180000000;

// twinui.pcshell.dll 26100.9949 (x86-64), verified by the Phase 0 probe.
constexpr uintptr_t kTouchEdgeGestureOperationStartedVa = 0x1800CAD80;
constexpr uintptr_t kTouchEdgeGestureOperationEndedVa = 0x1805F8840;
constexpr uintptr_t kReportEdgeGestureVa = 0x1806F63BC;
constexpr uintptr_t kSetAnimationDataVa = 0x1803C7740;
constexpr uintptr_t kShowVa = 0x180254C90;
constexpr uintptr_t kHideVa = 0x1803C5960;

constexpr wchar_t kTwinuiModuleName[] = L"twinui.pcshell.dll";

// The build the RVAs above were derived from.
constexpr unsigned kTwinuiVersion[4] = {10, 0, 26100, 9949};

// A keyboard close is a Windows tap by the user. Hide is where the shell takes
// the Start window away; the panel needs time to retract, so the hide waits for
// it. The wait is normally the duration the Start side answers with; this fixed
// lead is what remains when no answer arrives: a Start mod that is not running,
// a broken channel, a settle that did not start. A full 900+ DIP panel needs
// 409 ms at the 2.2 DIP/ms the Start mod plans with.
constexpr uint64_t kHideLeadMs = 560;

// How long the answer may take. The request wakes the Start mod's input thread
// through an event, so an answer normally arrives in a few milliseconds; this
// bound only covers a Start side that is busy or gone.
constexpr uint64_t kRetractPlanWaitMs = 100;
// Added to the answered duration: the answer is published a moment after the
// animation starts, and the wait's 10 ms slicing can add one step.
constexpr uint64_t kHideLeadMarginMs = 30;
// The Start side's "no settle started" answer, and the corrupt-value cap.
constexpr int32_t kRetractPlanNoSettleMs = 1;
constexpr uint64_t kRetractPlanMaxMs = 2000;

// How long a published retract request stays valid for the Start side. The
// bridge's own hold after publishing is at most kRetractPlanWaitMs plus the
// fixed lead (650 ms), and it marks the request done when that Hide completes,
// so this deadline is the backstop for a lost marker, not the usual end: a
// request can never reach a settle after the close it was written for is over.
constexpr uint64_t kRetractRequestWindowMs = 700;

// A tapped close is consumed by exactly one Hide. The Start side publishes the
// plan at the release, marks the same record `tapped` with a fresh sequence
// immediately before it sends the Windows key, so the Hide that follows is the
// tap's own and needs no wait. A tapped record older than this window belongs to
// a close that is over, and a tap whose Hide never came leaves its record valid
// for this window and no longer; the plan alone never passes a Hide.
constexpr uint64_t kCloseStampFreshMs = 800;

// The hide waits in 10 ms steps, so the wait can end early when the mod is being
// disabled under it.
constexpr DWORD kHidePollMs = 10;

// Where an unload that is still waiting first says so. Diagnostic only: reaching
// this mark grants nothing, and no mark ends the wait.
constexpr DWORD kIpcUnloadMarkMs = 500;

// How often a stalled unload repeats itself while it keeps waiting. Nothing else
// happens on that path: no close, no return, no handle left behind.
constexpr DWORD kIpcUnloadStallReportMs = 10000;

// The cross-process channel (swipe_ipc_shared.h). Opened once at init. Its
// handles are closed by Wh_ModUninit once no Hide can still be inside them; see
// the two lifetimes there. The gesture mapping and its event, unlike this
// channel, stay for the whole process lifetime.
SwipeIpcChannel g_ipc;
// The highest tapped close this hook has consumed. Seeded from the channel at
// attach, so a tap written before this process belongs to someone else's close.
LONG64 g_handledCloseSeq = 0;

struct Point {
    float x;
    float y;
};

struct GestureAnimationData {
    uint64_t sourceId;
    uint64_t providerId;
};

static_assert(sizeof(GestureAnimationData) == 16,
              "GestureAnimationData layout must match Phase 0");

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

std::atomic<bool> g_explorerHooked{false};
std::atomic<bool> g_afterInit{false};
std::atomic<bool> g_disabled{false};
// Set only after Wh_SetFunctionHook accepted the Hide hook, so a hide is never
// delayed on a build where the hook was refused.
std::atomic<bool> g_hideDelayEnabled{false};
// Hides currently inside Hide_Hook, entry to exit. Wh_ModUninit closes the v2
// channel only when this is zero, and Wh_ModInit refuses to re-activate this
// image while it is not. It is a lifetime guard for that channel, not a barrier
// between two Hides: it answers "is a Hide still inside", never "this Hide runs
// after that one".
std::atomic<int> g_hideCalls{0};

// Set when Wh_ModInit refused to install anything, so the later lifecycle
// callbacks leave the live activation's channel and count alone. Wh_ModInit's
// FALSE already tells the host not to call them; this makes a host that calls
// them anyway harmless.
std::atomic<bool> g_initRefused{false};

// Counts one Hide_Hook call for as long as that call is inside the hook, the
// original call included. The lease is taken before the hook reads anything and
// released on every exit, including unwinding, so no path can leave the count
// stranded.
//
// Two Hides on different threads hold a lease at the same time, and a
// re-entrant Hide on one thread holds one lease per frame, so it balances by
// itself. The count reaching zero is `drained`, and it is the only condition on
// which Wh_ModUninit closes the channel or returns; because the lease spans the
// `Hide_Original` call it also settles the trampoline question for every counted
// frame: a frame that is still about to call the original holds a lease, so the
// unload does not return under it and no timeout may stand in for the count. The
// one window the count cannot see is an entry that has not yet run the
// constructor below; the pinned image keeps that frame's own code and the
// destructor mapped, and its trampoline use is the host's unload to cover.
struct HideCallLease {
    HideCallLease() { g_hideCalls.fetch_add(1, std::memory_order_acq_rel); }
    ~HideCallLease() { g_hideCalls.fetch_sub(1, std::memory_order_acq_rel); }

    HideCallLease(const HideCallLease&) = delete;
    HideCallLease& operator=(const HideCallLease&) = delete;
};

enum : int {
    kSharedNotStarted = 0,
    kSharedInitializing = 1,
    kSharedReady = 2,
    kSharedFailed = 3,
};

std::atomic<int> g_sharedInitState{kSharedNotStarted};
StartGestureShared* g_shared = nullptr;  // Published before kSharedReady
HANDLE g_sharedMapping = nullptr;
HANDLE g_sharedEvent = nullptr;
std::atomic<bool> g_sharedErrorLogged{false};

// The start point belongs to the gesture identified by gestureStartTick, so a
// later ReportEdgeGesture call for the same gesture never replaces it.
std::atomic<uint64_t> g_startPointTick{0};

// ---------------------------------------------------------------------------
// Shared memory
// ---------------------------------------------------------------------------

void LogSharedErrorOnce(const wchar_t* what, DWORD error) {
    if (!g_sharedErrorLogged.exchange(true)) {
        Wh_Log(L"%s failed (error %u); gesture data may be incomplete", what,
               static_cast<unsigned int>(error));
    }
}

void ResetSharedFields(StartGestureShared* shared) {
    // Unpublish first and publish magic last, so a consumer that mapped the
    // section concurrently never sees a half-initialized struct.
    shared->magic = 0;
    shared->version = 0;
    shared->sequence = 0;
    shared->state = static_cast<uint32_t>(StartGestureState::None);
    shared->edge = -1;
    shared->sourceId = 0;
    shared->providerId = 0;
    // Negative coordinates are not valid screen positions; the consumer can
    // use them to detect "no point reported yet".
    shared->startX = -1.0f;
    shared->startY = -1.0f;
    shared->currentX = -1.0f;
    shared->currentY = -1.0f;
    shared->gestureStartTick = 0;
    shared->lastUpdateTick = 0;
    shared->version = kWindhawkStartGestureVersion;
    shared->magic = kWindhawkStartGestureMagic;
}

StartGestureShared* CreateOrOpenShared() {
    HANDLE mapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
        static_cast<DWORD>(sizeof(StartGestureShared)),
        kWindhawkStartGestureMappingName);
    if (!mapping) {
        LogSharedErrorOnce(L"CreateFileMappingW", GetLastError());
        return nullptr;
    }

    const bool fresh = GetLastError() != ERROR_ALREADY_EXISTS;

    void* view = MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
                               sizeof(StartGestureShared));
    if (!view) {
        LogSharedErrorOnce(L"MapViewOfFile", GetLastError());
        CloseHandle(mapping);
        return nullptr;
    }
    g_sharedMapping = mapping;

    auto* shared = static_cast<StartGestureShared*>(view);
    const bool valid = !fresh &&
                       shared->magic == kWindhawkStartGestureMagic &&
                       shared->version == kWindhawkStartGestureVersion;
    // An odd sequence on an otherwise valid section means a previous mod
    // instance was unloaded in the middle of a write; reset it too.
    if (!valid || (shared->sequence & 1u) != 0u) {
        ResetSharedFields(shared);
    }

    g_sharedEvent =
        CreateEventW(nullptr, FALSE, FALSE, kWindhawkStartGestureEventName);
    if (!g_sharedEvent) {
        // Publishing still works; the consumer can poll the sequence.
        LogSharedErrorOnce(L"CreateEventW", GetLastError());
    }

    return shared;
}

StartGestureShared* EnsureShared() {
    const int state = g_sharedInitState.load(std::memory_order_acquire);
    if (state == kSharedReady) {
        return g_shared;
    }
    if (state == kSharedFailed) {
        return nullptr;
    }

    int expected = kSharedNotStarted;
    if (g_sharedInitState.compare_exchange_strong(expected,
                                                  kSharedInitializing,
                                                  std::memory_order_acq_rel)) {
        StartGestureShared* shared = CreateOrOpenShared();
        g_shared = shared;
        g_sharedInitState.store(shared ? kSharedReady : kSharedFailed,
                                std::memory_order_release);
        return shared;
    }

    // Another thread is initializing. Don't wait inside a hook; this write is
    // skipped and the next one will see the ready state.
    return g_sharedInitState.load(std::memory_order_acquire) == kSharedReady
               ? g_shared
               : nullptr;
}

struct SharedUpdate {
    bool written;
    uint32_t previousState;
    uint32_t newState;
    int32_t edge;
    int64_t sourceId;
    int64_t providerId;
};

// Serializes the seqlock writers. The gesture callbacks run on the explorer UI
// thread, but the unload path publishes from the Windhawk thread.
SRWLOCK g_sharedWriteLock = SRWLOCK_INIT;

// Bumps the seqlock around the write, updates the timestamp and signals the
// consumer.
template <typename Writer>
SharedUpdate UpdateShared(Writer&& writer) {
    SharedUpdate result{};

    if (g_disabled.load(std::memory_order_relaxed)) {
        return result;
    }

    StartGestureShared* shared = EnsureShared();
    if (!shared) {
        return result;
    }

    AcquireSRWLockExclusive(&g_sharedWriteLock);
    InterlockedIncrement(
        reinterpret_cast<volatile LONG*>(&shared->sequence));
    result.previousState = shared->state;
    writer(shared);
    shared->lastUpdateTick = GetTickCount64();
    result.newState = shared->state;
    result.edge = shared->edge;
    result.sourceId = shared->sourceId;
    result.providerId = shared->providerId;
    InterlockedIncrement(
        reinterpret_cast<volatile LONG*>(&shared->sequence));

    if (g_sharedEvent) {
        SetEvent(g_sharedEvent);
    }

    ReleaseSRWLockExclusive(&g_sharedWriteLock);

    result.written = true;
    return result;
}

// Used while the mod is being disabled, when normal writes are already
// blocked. Hooks are no longer applied after Wh_ModBeforeUninit returns.
void PublishInactiveOnUnload() {
    if (g_sharedInitState.load(std::memory_order_acquire) != kSharedReady) {
        return;
    }
    StartGestureShared* shared = g_shared;
    if (!shared) {
        return;
    }

    AcquireSRWLockExclusive(&g_sharedWriteLock);
    InterlockedIncrement(
        reinterpret_cast<volatile LONG*>(&shared->sequence));
    shared->state = static_cast<uint32_t>(StartGestureState::None);
    shared->edge = -1;
    shared->sourceId = 0;
    shared->providerId = 0;
    shared->startX = -1.0f;
    shared->startY = -1.0f;
    shared->currentX = -1.0f;
    shared->currentY = -1.0f;
    shared->gestureStartTick = 0;
    shared->lastUpdateTick = GetTickCount64();
    InterlockedIncrement(
        reinterpret_cast<volatile LONG*>(&shared->sequence));
    ReleaseSRWLockExclusive(&g_sharedWriteLock);

    if (g_sharedEvent) {
        SetEvent(g_sharedEvent);
    }
}

// ---------------------------------------------------------------------------
// Logging (state transitions only, never per frame)
// ---------------------------------------------------------------------------

// Appends to <mod storage>\bridge.log. Called once per gesture state
// transition.
void FileLog(const wchar_t* format, ...) {
    static SRWLOCK lock = SRWLOCK_INIT;

    WCHAR storage[MAX_PATH] = {};
    if (!Wh_GetModStoragePath(storage, ARRAYSIZE(storage))) {
        return;
    }

    WCHAR path[MAX_PATH] = {};
    swprintf_s(path, L"%s\\bridge.log", storage);

    WCHAR line[1024] = {};
    va_list args;
    va_start(args, format);
    vswprintf_s(line, ARRAYSIZE(line), format, args);
    va_end(args);

    char utf8[2048] = {};
    int bytes = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8,
                                    sizeof(utf8), nullptr, nullptr);
    if (bytes <= 1) {
        return;
    }

    AcquireSRWLockExclusive(&lock);
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, utf8, static_cast<DWORD>(bytes - 1), &written, nullptr);
        WriteFile(file, "\r\n", 2, &written, nullptr);
        CloseHandle(file);
    }
    ReleaseSRWLockExclusive(&lock);
}

void LogStateChange(const SharedUpdate& update, const wchar_t* reason) {
    if (!update.written || update.previousState == update.newState) {
        return;
    }
    FileLog(L"state=%u edge=%d src=%lld prov=%lld %s",
            static_cast<unsigned int>(update.newState),
            static_cast<int>(update.edge),
            static_cast<long long>(update.sourceId),
            static_cast<long long>(update.providerId), reason);
}

// ---------------------------------------------------------------------------
// Build gate
//
// The hardcoded RVAs are only valid for the build they were taken from. The
// gate checks the file version of the loaded module and, when the file can be
// read, that its PE timestamp is the one in memory. A mismatch installs
// nothing.
// ---------------------------------------------------------------------------

using GetFileVersionInfoSizeW_t = decltype(&GetFileVersionInfoSizeW);
using GetFileVersionInfoW_t = decltype(&GetFileVersionInfoW);
using VerQueryValueW_t = decltype(&VerQueryValueW);

struct VersionApi {
    HMODULE module = nullptr;
    bool ownsReference = false;
    GetFileVersionInfoSizeW_t size = nullptr;
    GetFileVersionInfoW_t info = nullptr;
    VerQueryValueW_t query = nullptr;
};

void UnloadVersionApi(VersionApi& api) {
    if (api.ownsReference && api.module) {
        FreeLibrary(api.module);
    }
    api = {};
}

bool LoadVersionApi(VersionApi* api) {
    HMODULE module = GetModuleHandleW(L"version.dll");
    if (!module) {
        module = LoadLibraryW(L"version.dll");
        if (!module) {
            return false;
        }
        api->ownsReference = true;
    }
    api->module = module;

    api->size = reinterpret_cast<GetFileVersionInfoSizeW_t>(
        GetProcAddress(module, "GetFileVersionInfoSizeW"));
    api->info = reinterpret_cast<GetFileVersionInfoW_t>(
        GetProcAddress(module, "GetFileVersionInfoW"));
    api->query = reinterpret_cast<VerQueryValueW_t>(
        GetProcAddress(module, "VerQueryValueW"));

    if (!api->size || !api->info || !api->query) {
        UnloadVersionApi(*api);
        return false;
    }
    return true;
}

// Reads FileVersion from the first StringFileInfo translation. The fixed
// version resource can lag the string on incremental Windows updates, so the
// string is what the gate compares.
bool QueryFileVersionString(const VersionApi& api, const wchar_t* path,
                            wchar_t* out, size_t outCount) {
    DWORD handle = 0;
    const DWORD size = api.size(path, &handle);
    if (size == 0 || size > 8192) {
        return false;
    }

    BYTE buffer[8192];
    if (!api.info(path, 0, size, buffer)) {
        return false;
    }

    struct Translation {
        WORD language;
        WORD codePage;
    };

    Translation* translations = nullptr;
    UINT translationBytes = 0;
    if (!api.query(buffer, L"\\VarFileInfo\\Translation",
                   reinterpret_cast<void**>(&translations),
                   &translationBytes) ||
        !translations) {
        return false;
    }

    const size_t translationCount = translationBytes / sizeof(Translation);
    for (size_t i = 0; i < translationCount; i++) {
        wchar_t key[64] = {};
        swprintf_s(key, ARRAYSIZE(key),
                   L"\\StringFileInfo\\%04x%04x\\FileVersion",
                   translations[i].language, translations[i].codePage);

        wchar_t* value = nullptr;
        UINT valueChars = 0;
        if (!api.query(buffer, key, reinterpret_cast<void**>(&value),
                       &valueChars) ||
            !value || valueChars == 0) {
            continue;
        }

        size_t length = 0;
        while (length + 1 < outCount && value[length] != L'\0') {
            out[length] = value[length];
            length++;
        }
        out[length] = L'\0';
        return length != 0;
    }

    return false;
}

// Parses the leading "a.b.c.d" of a file version string; a suffix such as
// " (WinBuild...)" is ignored.
bool ParseVersion(const wchar_t* text, unsigned* parts) {
    const wchar_t* cursor = text;
    for (int i = 0; i < 4; i++) {
        while (*cursor == L' ' || *cursor == L'\t') {
            cursor++;
        }
        if (*cursor < L'0' || *cursor > L'9') {
            return false;
        }

        unsigned value = 0;
        while (*cursor >= L'0' && *cursor <= L'9') {
            value = value * 10 + static_cast<unsigned>(*cursor - L'0');
            if (value > 0xFFFF) {
                return false;
            }
            cursor++;
        }
        parts[i] = value;

        if (i < 3) {
            if (*cursor != L'.') {
                return false;
            }
            cursor++;
        }
    }
    return true;
}

bool ReadLoadedPeTimestamp(HMODULE module, DWORD* timestamp) {
    const auto* base = reinterpret_cast<const BYTE*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
        dos->e_lfanew > 0x1000) {
        return false;
    }

    const auto* nt =
        reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }
    *timestamp = nt->FileHeader.TimeDateStamp;
    return true;
}

bool ReadFilePeTimestamp(const wchar_t* path, DWORD* timestamp) {
    HANDLE file = CreateFileW(path, GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE |
                                  FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    bool ok = false;
    IMAGE_DOS_HEADER dos{};
    DWORD read = 0;
    if (ReadFile(file, &dos, sizeof(dos), &read, nullptr) &&
        read == static_cast<DWORD>(sizeof(dos)) &&
        dos.e_magic == IMAGE_DOS_SIGNATURE && dos.e_lfanew > 0 &&
        dos.e_lfanew <= 0x1000) {
        LARGE_INTEGER offset{};
        offset.QuadPart = dos.e_lfanew;
        if (SetFilePointerEx(file, offset, nullptr, FILE_BEGIN)) {
            IMAGE_NT_HEADERS nt{};
            if (ReadFile(file, &nt, sizeof(nt), &read, nullptr) &&
                read == static_cast<DWORD>(sizeof(nt)) &&
                nt.Signature == IMAGE_NT_SIGNATURE) {
                *timestamp = nt.FileHeader.TimeDateStamp;
                ok = true;
            }
        }
    }

    CloseHandle(file);
    return ok;
}

bool TwinuiBuildMatches(HMODULE module) {
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(module, path, ARRAYSIZE(path))) {
        Wh_Log(L"twinui build mismatch unknown; refusing to install hooks");
        return false;
    }

    wchar_t rawVersion[64] = {};
    wchar_t displayVersion[64] = L"unknown";
    unsigned parts[4] = {};
    bool versionMatches = false;

    VersionApi api;
    if (LoadVersionApi(&api)) {
        if (QueryFileVersionString(api, path, rawVersion,
                                   ARRAYSIZE(rawVersion)) &&
            ParseVersion(rawVersion, parts)) {
            swprintf_s(displayVersion, ARRAYSIZE(displayVersion),
                       L"%u.%u.%u.%u", parts[0], parts[1], parts[2],
                       parts[3]);
            versionMatches = true;
            for (int i = 0; i < 4; i++) {
                if (parts[i] != kTwinuiVersion[i]) {
                    versionMatches = false;
                    break;
                }
            }
        }
        UnloadVersionApi(api);
    }

    if (!versionMatches) {
        Wh_Log(L"twinui build mismatch %s; refusing to install hooks",
               displayVersion);
        return false;
    }

    // A version match is enough to proceed; the timestamp check is an extra
    // guard against a file that was replaced while the module is loaded.
    DWORD loadedTimestamp = 0;
    DWORD fileTimestamp = 0;
    if (ReadLoadedPeTimestamp(module, &loadedTimestamp) &&
        ReadFilePeTimestamp(path, &fileTimestamp) &&
        loadedTimestamp != fileTimestamp) {
        Wh_Log(L"twinui build mismatch %s (timestamp 0x%08X != 0x%08X); "
               L"refusing to install hooks",
               displayVersion, loadedTimestamp, fileTimestamp);
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// twinui.pcshell.dll hooks
// ---------------------------------------------------------------------------

using InputGestureOperationStarted_t = void (*)(void*, const void*, const void*);
InputGestureOperationStarted_t InputGestureOperationStarted_Original = nullptr;

void InputGestureOperationStarted_Hook(void* self, const void* operation,
                                       const void* args) {
    const uint64_t now = GetTickCount64();
    const SharedUpdate update = UpdateShared([now](StartGestureShared* shared) {
        // The complete start record: no field of the previous gesture stays
        // visible next to the new state and timestamp.
        shared->state = static_cast<uint32_t>(StartGestureState::Active);
        shared->edge = -1;
        shared->sourceId = 0;
        shared->providerId = 0;
        shared->startX = -1.0f;
        shared->startY = -1.0f;
        shared->currentX = -1.0f;
        shared->currentY = -1.0f;
        shared->gestureStartTick = now;
    });
    LogStateChange(update, L"InputGestureOperationStarted");

    if (InputGestureOperationStarted_Original) {
        InputGestureOperationStarted_Original(self, operation, args);
    }
}

using InputGestureOperationEnded_t = void (*)(void*, const void*, const void*);
InputGestureOperationEnded_t InputGestureOperationEnded_Original = nullptr;

void InputGestureOperationEnded_Hook(void* self, const void* operation,
                                     const void* args) {
    // Committed means only "the gesture finished while its handle was live",
    // which is the open signal the Start side consumes. It is not a claim
    // about what the shell did with the window.
    const SharedUpdate update = UpdateShared([](StartGestureShared* shared) {
        if (shared->state ==
                static_cast<uint32_t>(StartGestureState::Active) &&
            (shared->sourceId != 0 || shared->providerId != 0)) {
            shared->state =
                static_cast<uint32_t>(StartGestureState::Committed);
        }
    });
    LogStateChange(update,
                   L"InputGestureOperationEnded (ended with a live handle)");

    // The handle is deliberately kept: the subsequent Hide clears it.
    if (InputGestureOperationEnded_Original) {
        InputGestureOperationEnded_Original(self, operation, args);
    }
}

using ReportEdgeGesture_t = void (*)(void*, int, const GestureAnimationData*,
                                     Point);
ReportEdgeGesture_t ReportEdgeGesture_Original = nullptr;

void ReportEdgeGesture_Hook(void* self, int edge,
                            const GestureAnimationData* data, Point point) {
    // Called once per gesture right after InputGestureOperationStarted. This
    // is the first call that carries the edge, the handle and a real point, so
    // it republishes the whole start record in one seqlock write.
    const SharedUpdate update =
        UpdateShared([edge, data, point](StartGestureShared* shared) {
            const uint64_t now = GetTickCount64();
            shared->state = static_cast<uint32_t>(StartGestureState::Active);
            shared->edge = edge;

            uint64_t startTick = shared->gestureStartTick;
            if (startTick == 0) {
                startTick = now;
            }
            // Keep the first point of this gesture if the shell reports the
            // edge more than once.
            if (g_startPointTick.load(std::memory_order_relaxed) !=
                startTick) {
                shared->startX = point.x;
                shared->startY = point.y;
                g_startPointTick.store(startTick, std::memory_order_relaxed);
            }
            shared->currentX = point.x;
            shared->currentY = point.y;
            shared->gestureStartTick = startTick;

            if (data) {
                shared->sourceId = static_cast<int64_t>(data->sourceId);
                shared->providerId = static_cast<int64_t>(data->providerId);
            }
        });
    LogStateChange(update, L"ReportEdgeGesture");

    if (ReportEdgeGesture_Original) {
        ReportEdgeGesture_Original(self, edge, data, point);
    }
}

using SetAnimationData_t = HRESULT (*)(void*, uint64_t, uint64_t, uint64_t);
SetAnimationData_t SetAnimationData_Original = nullptr;

HRESULT SetAnimationData_Hook(void* self, uint64_t a, uint64_t b, uint64_t c) {
    // Phase 0: a=0, b=SourceId, c=ProviderId at gesture start.
    const uint64_t now = GetTickCount64();
    const SharedUpdate update =
        UpdateShared([b, c, now](StartGestureShared* shared) {
            if (shared->state !=
                static_cast<uint32_t>(StartGestureState::Active)) {
                // A new gesture outside the Started path starts with a clean
                // record rather than the previous gesture's edge/point.
                shared->gestureStartTick = now;
                shared->edge = -1;
                shared->startX = -1.0f;
                shared->startY = -1.0f;
                shared->currentX = -1.0f;
                shared->currentY = -1.0f;
            }
            shared->sourceId = static_cast<int64_t>(b);
            shared->providerId = static_cast<int64_t>(c);
            shared->state = static_cast<uint32_t>(StartGestureState::Active);
        });
    LogStateChange(update, L"SetAnimationData");

    return SetAnimationData_Original ? SetAnimationData_Original(self, a, b, c)
                                     : S_OK;
}

using Show_t = HRESULT (*)(void*, uint8_t, uint8_t, uint32_t);
Show_t Show_Original = nullptr;

HRESULT Show_Hook(void* self, uint8_t a, uint8_t b, uint32_t c) {
    FileLog(L"show a=%u b=%u c=%u", static_cast<unsigned int>(a),
            static_cast<unsigned int>(b), static_cast<unsigned int>(c));

    // Only a Show that follows a real gesture handle publication is a
    // gesture-driven open. A normal mouse/keyboard open must not look like a
    // gesture to the Start side.
    StartGestureShared* sharedState = EnsureShared();
    if (sharedState &&
        (sharedState->sourceId != 0 || sharedState->providerId != 0)) {
        const SharedUpdate update = UpdateShared([](StartGestureShared* shared) {
            if (shared->sourceId != 0 || shared->providerId != 0) {
                shared->state =
                    static_cast<uint32_t>(StartGestureState::Active);
            }
        });
        LogStateChange(update, L"Show (live handle)");
    }

    return Show_Original ? Show_Original(self, a, b, c) : S_OK;
}

using Hide_t = HRESULT (*)(void*, uint32_t);
Hide_t Hide_Original = nullptr;

// Clears the published gesture record when the shell hides the Start window.
void ClearSharedGestureState(const wchar_t* reason) {
    const SharedUpdate update = UpdateShared([](StartGestureShared* shared) {
        shared->state = static_cast<uint32_t>(StartGestureState::None);
        // Drop the handle together with the Start window, so a later
        // non-gesture Show cannot expose an expired handle.
        shared->sourceId = 0;
        shared->providerId = 0;
        shared->gestureStartTick = 0;
        shared->edge = -1;
        shared->startX = -1.0f;
        shared->startY = -1.0f;
        shared->currentX = -1.0f;
        shared->currentY = -1.0f;
    });
    LogStateChange(update, reason);
    g_startPointTick.store(0, std::memory_order_relaxed);
}

// Gives the Start side the time its retract needs. The steps stay at 10 ms so a
// mod that is being disabled mid-wait ends the delay at once instead of holding
// the hide for a retract nothing will draw.
void WaitForRetractLead(uint64_t leadMs) {
    const ULONGLONG deadline = GetTickCount64() + leadMs;
    while (!g_disabled.load(std::memory_order_relaxed)) {
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) {
            break;
        }
        Sleep(static_cast<DWORD>(
            std::min<ULONGLONG>(kHidePollMs, deadline - now)));
    }
}

// Consumes the Start side's tapped record when it belongs to this Hide. The
// sequence number is the whole protocol: a tapped record this hook has not seen
// is a gesture close whose Windows tap is going down right now, so this Hide is
// that tap's and needs no wait; a tapped record it has seen is a later keyboard
// close. The plan alone never passes: it is published at the release, up to a
// full settle before the tap exists, so a Hide during that settle is the user's
// own Windows press and gets its own lead. A cancelled record never taps at all.
//
// The record names no menu, so two more tests bind it to one. It must be newer
// than the sequence this process seeded at attach, and it must not be older than
// the newest open generation this hook has seen: a stamp written before that
// open belongs to a menu that is gone, and would otherwise pass an unrelated
// Hide under the menu the user has since opened. The channel also asks the Start
// side to cancel its stamp when it publishes a new open; the test above does not
// depend on it. With no known open generation nothing can be bound, so the stamp
// is refused and the Hide takes the keyboard path.
bool ConsumeGestureStamp(bool openKnown, uint64_t openTick) {
    if (!g_ipc.state || !openKnown) {
        return false;
    }

    // One snapshot, so the kind, the tapped flag and the tick can never come
    // from two different publishes.
    SwipeIpcCloseSnapshot close{};
    if (!SwipeIpcReadClose(&g_ipc, &close)) {
        return false;
    }
    if (close.seq == 0 || close.seq == g_handledCloseSeq) {
        return false;
    }
    if (close.tapped == 0 || close.kind != kSwipeCloseKindGesture) {
        return false;
    }

    const uint64_t now = GetTickCount64();
    if (now > close.tick && now - close.tick > kCloseStampFreshMs) {
        // The tap that wrote this is long over; the Hide in hand is a keyboard
        // close and gets its own lead.
        return false;
    }
    if (close.tick < openTick) {
        // Stamped before the menu now on screen was published: a tap whose Hide
        // never came. The current menu's own tap is always newer than its open.
        return false;
    }

    g_handledCloseSeq = close.seq;
    return true;
}

// Waits for the Start side's answer to this request. Only an answer whose
// sequence number echoes the request counts, so an answer to an earlier close
// can never satisfy this one, and no timestamp is involved. Returns false when
// no answer arrived inside kRetractPlanWaitMs; `settleMs` is left at the
// no-settle sentinel when the Start side answered that nothing started.
bool WaitForRetractPlan(LONG64 request, int32_t* settleMs) {
    if (!g_ipc.state || !g_ipc.retractPlanEvent) {
        return false;
    }

    const ULONGLONG deadline = GetTickCount64() + kRetractPlanWaitMs;
    for (;;) {
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) {
            return false;
        }

        // Slices keep the disabled check: a mod that is being unloaded must not
        // hold the shell's UI thread.
        const DWORD wait = WaitForSingleObject(
            g_ipc.retractPlanEvent,
            static_cast<DWORD>(
                std::min<ULONGLONG>(kHidePollMs, deadline - now)));
        if (wait == WAIT_OBJECT_0) {
            // The plan is copied as one seqlock record and accepted only when it
            // echoes this request, so an answer to an earlier close can never
            // satisfy this one and no half-written plan is read.
            SwipeIpcRetractPlan plan{};
            if (!SwipeIpcReadRetractPlan(&g_ipc, &plan)) {
                continue;
            }
            if (plan.request != request) {
                continue;  // an answer to an earlier request
            }
            *settleMs =
                plan.settleMs > 0 &&
                        static_cast<uint64_t>(plan.settleMs) <= kRetractPlanMaxMs
                    ? plan.settleMs
                    : kRetractPlanNoSettleMs;
            return true;
        }
        if (g_disabled.load(std::memory_order_relaxed)) {
            return false;
        }
        if (wait != WAIT_TIMEOUT) {
            return false;
        }
    }
}
// ---------------------------------------------------------------------------
// Delayed Hide
//
// A gesture close taps the Windows key at the end of its retract, so the Hide
// that tap causes needs no wait: the panel is already gone. The tapped record
// names that case, and only the tap publishes it. A keyboard close publishes the
// `retract` request and then waits for the Start mod's answer: that mod plans the
// settle from the panel's travel and publishes its duration, so the shell is held
// for exactly as long as the slide takes. Without an answer the fixed lead
// applies. No worker, manager pointer, timer or re-arm tap is involved: every
// Hide is the shell's own call, on its own thread.
// ---------------------------------------------------------------------------

// CStartExperienceManager::Hide(void* self, uint32_t reason) -> HRESULT
// twinui.pcshell.dll 26100.9949, RVA 0x3C5960, vtable slot after Show.
//
// The prologue stores rcx (this) and edx (reason), zeroes dil and compares
// `edx` with 7; r8 and r9 are written before they are read, so the function
// takes two arguments. Both return paths set eax: 0 on success, the negative
// HRESULT of a failing inner call otherwise, so the return type is HRESULT.
HRESULT Hide_Hook(void* self, uint32_t reason) {
    // The lease covers the whole call: the channel reads, the waits and the
    // original call. It is released on every exit, so an early return or an
    // exception out of FileLog cannot strand the count that Wh_ModUninit drains.
    const HideCallLease hideCall;
    FileLog(L"hide reason=%u", reason);
    ClearSharedGestureState(L"Hide");

    // The shell's key handler decided this hide from a Windows tap. A gesture
    // close publishes its plan at the release and marks the same record tapped
    // immediately before its own Windows tap, so the Hide that tap causes is
    // already too late to cut the animation and runs at once. A keyboard close
    // (no record, a plan or a cancel, a record this hook already consumed, one
    // older than the window, one stamped before the menu now open) requests the
    // retract through the channel and waits for the Start mod's answer. Only that
    // mod knows the panel's travel, so the wait is its planned settle plus one
    // margin; no answer at all falls back to the fixed lead, which covers the
    // full 409 ms slide.
    //
    // This hook always calls the original. An earlier version returned S_OK to
    // cancel a hide when the user pulled the menu open again; measured, the
    // shell's Start toggle then stayed one close ahead and the Windows key and
    // the taskbar Start button stopped working until the host restarted. A close
    // is cancelled on the Start side instead, by not sending its tap.
    if (g_hideDelayEnabled.load(std::memory_order_relaxed) &&
        !g_disabled.load(std::memory_order_relaxed)) {
        // The newest menu open this hook has seen, read once and used by both
        // paths: the stamped Hide is bound to it, and the keyboard path sends it
        // with its request. No published generation (ERROR_NOT_READY), no stable
        // copy, or no channel at all leaves both at 0: the request then carries
        // 0, which the Start side refuses, and no close stamp can pass either,
        // so this Hide takes the fixed lead.
        uint64_t openGeneration = 0;
        uint64_t openTick = 0;
        DWORD openError = 0;
        const bool openKnown =
            SwipeIpcReadOpen(&g_ipc, &openGeneration, &openTick, &openError);
        if (!openKnown) {
            openGeneration = 0;
            openTick = 0;
            if (g_ipc.state) {
                // Only a channel that exists can have a record problem worth
                // naming; without one, `hide retract request failed` already
                // says why this Hide takes the fixed lead.
                FileLog(L"hide open generation unavailable err=%u",
                        static_cast<unsigned int>(openError));
            }
        }

        if (ConsumeGestureStamp(openKnown, openTick)) {
            // The Start side tapped after its retract, so the panel is already
            // gone: no wait belongs to this Hide.
            FileLog(L"hide passed (gesture)");
        } else {
            const LONG64 request = SwipeIpcRequestRetract(
                &g_ipc, openGeneration, kRetractRequestWindowMs);
            if (request != 0) {
                int32_t settleMs = kRetractPlanNoSettleMs;
                const bool answered = WaitForRetractPlan(request, &settleMs);
                const bool planned = answered &&
                                     settleMs > kRetractPlanNoSettleMs;
                const uint64_t lead =
                    planned ? static_cast<uint64_t>(settleMs) +
                                  kHideLeadMarginMs
                            : kHideLeadMs;

                if (planned) {
                    FileLog(L"hide delayed reason=%u retract=%d lead=%llu",
                            reason, settleMs,
                            static_cast<unsigned long long>(lead));
                } else {
                    FileLog(L"hide delayed reason=%u retract=%s lead=%llu",
                            reason, answered ? L"none" : L"unanswered",
                            static_cast<unsigned long long>(lead));
                }
                WaitForRetractLead(lead);
                // This Hide is over. A request the Start side reads from now on
                // belongs to no close of this menu and is marked done here, so it
                // cannot start a settle later.
                SwipeIpcClearRetractRequest(&g_ipc, request);
            } else {
                FileLog(L"hide retract request failed");
            }
        }
    }

    const HRESULT result = Hide_Original ? Hide_Original(self, reason) : S_OK;
    return result;
}

// ---------------------------------------------------------------------------
// Hook installation
// ---------------------------------------------------------------------------

bool InstallHook(void* target, void* hook, void** original,
                 const wchar_t* name) {
    if (!target) {
        Wh_Log(L"Cannot hook %s: null target", name);
        return false;
    }
    if (!Wh_SetFunctionHook(target, hook, original)) {
        Wh_Log(L"Failed to hook %s", name);
        return false;
    }
    Wh_Log(L"Hooked %s", name);
    return true;
}

void InstallExplorerHooks(HMODULE module) {
    if (!module) {
        return;
    }
    if (g_disabled.load(std::memory_order_relaxed)) {
        return;
    }
    // One attempt per process: a mismatch is not retried on every module load.
    if (g_explorerHooked.exchange(true)) {
        return;
    }
    if (!TwinuiBuildMatches(module)) {
        // The gate logged the mismatch; no hook is installed at all.
        return;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(module);
    const auto at = [base](uintptr_t preferredVa) -> void* {
        return reinterpret_cast<void*>(base +
                                       (preferredVa - kTwinuiPreferredBase));
    };

    InstallHook(
        at(kTouchEdgeGestureOperationStartedVa),
        reinterpret_cast<void*>(InputGestureOperationStarted_Hook),
        reinterpret_cast<void**>(&InputGestureOperationStarted_Original),
        L"TouchEdgeGestureProcessor::InputGestureOperationStarted");

    InstallHook(
        at(kTouchEdgeGestureOperationEndedVa),
        reinterpret_cast<void*>(InputGestureOperationEnded_Hook),
        reinterpret_cast<void**>(&InputGestureOperationEnded_Original),
        L"TouchEdgeGestureProcessor::InputGestureOperationEnded");

    InstallHook(
        at(kReportEdgeGestureVa),
        reinterpret_cast<void*>(ReportEdgeGesture_Hook),
        reinterpret_cast<void**>(&ReportEdgeGesture_Original),
        L"EdgeAnimationRelationshipManager::ReportEdgeGesture");

    InstallHook(at(kSetAnimationDataVa),
                reinterpret_cast<void*>(SetAnimationData_Hook),
                reinterpret_cast<void**>(&SetAnimationData_Original),
                L"CStartExperienceManager::SetAnimationData");

    InstallHook(at(kShowVa), reinterpret_cast<void*>(Show_Hook),
                reinterpret_cast<void**>(&Show_Original),
                L"CStartExperienceManager::Show");

    if (InstallHook(at(kHideVa), reinterpret_cast<void*>(Hide_Hook),
                    reinterpret_cast<void**>(&Hide_Original),
                    L"CStartExperienceManager::Hide")) {
        g_hideDelayEnabled.store(true, std::memory_order_relaxed);
    }
}

void TryInstallFromLoadedModules(bool apply) {
    if (g_disabled.load(std::memory_order_relaxed)) {
        return;
    }

    bool installed = false;
    if (!g_explorerHooked.load(std::memory_order_relaxed)) {
        HMODULE twinui = GetModuleHandleW(kTwinuiModuleName);
        if (twinui) {
            InstallExplorerHooks(twinui);
            installed = true;
        }
    }

    if (installed && apply && g_afterInit.load(std::memory_order_relaxed)) {
        Wh_ApplyHookOperations();
    }
}

// ---------------------------------------------------------------------------
// Late load of twinui.pcshell.dll
// ---------------------------------------------------------------------------

using LoadLibraryExW_t = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);
LoadLibraryExW_t LoadLibraryExW_Original = nullptr;

HMODULE WINAPI LoadLibraryExW_Hook(LPCWSTR fileName, HANDLE file,
                                   DWORD flags) {
    HMODULE module = LoadLibraryExW_Original
                         ? LoadLibraryExW_Original(fileName, file, flags)
                         : nullptr;

    if (module && !g_explorerHooked.load(std::memory_order_relaxed)) {
        if (module == GetModuleHandleW(kTwinuiModuleName)) {
            TryInstallFromLoadedModules(true);
        }
    }

    return module;
}

// ---------------------------------------------------------------------------
// Module lifetime
// ---------------------------------------------------------------------------

// Any address in this module identifies the image that must stay mapped. The
// function is never called; only its address is taken.
void ModulePinAnchor() {}

// The pinned image, once per process. A re-init of the same image keeps it.
HMODULE g_modulePin = nullptr;

// Pins this image until the process terminates. A Hide frame that returns after
// Wh_ModUninit still needs its own return address and the HideCallLease
// destructor to be valid, and a hook-only mod cannot wait for its caller the way
// a threaded mod can. The pin covers exactly that: this image's code, its
// globals and the lease destructor.
//
// It does not cover the MinHook trampoline. That allocation belongs to the host,
// which removes hooks and frees trampolines only after Wh_ModUninit, following
// its ThreadsCallStackWaitForRegions(..., 200, 400) scan; the scan is bounded and
// its result is discarded, so a frame it sees is a frame it waited on, and a
// frame it does not see is one nothing covered. "The scan saw the frame" and
// "the frame exited before removal" are two claims, and only the first follows
// from that scan. The counted frames are covered instead by the drain, which
// returns only when the count has reached zero.
//
// A failure is a refusal, not a warning: Wh_ModInit returns FALSE and installs
// nothing. A shell-owned thread that resumes in an unmapped image is the crash
// this pin exists to prevent, and "the mod owns no thread" says nothing about a
// thread the shell owns. The success is logged once; a re-enable of the same
// image runs Wh_ModInit again and finds g_modulePin already set.
bool PinThisModule() {
    if (g_modulePin) {
        return true;  // a re-init of the same pinned image
    }
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&ModulePinAnchor),
                           &module)) {
        g_modulePin = module;
        FileLog(L"mod pin image=%p pinned=1", reinterpret_cast<void*>(module));
        return true;
    }
    FileLog(L"mod pin failed err=%u", static_cast<unsigned>(GetLastError()));
    return false;
}

// A pinned image is initialised a second time in the same process: after a
// clean unload the module is still mapped, so the next enable runs Wh_ModInit
// on that same image and the loader initialises nothing. Every lifecycle flag
// the previous activation left behind is cleared here, so the second init
// installs its hooks again instead of believing they are still there.
//
// Wh_ModInit calls this only when g_hideCalls is zero: a Hide from the previous
// activation that is still inside would otherwise see g_disabled cleared and the
// globals re-pointed under it. A refused activation never reaches this function.
// A drained unload returns at zero, so that refusal is reachable only through the
// entry window the count cannot see; g_hideCalls itself is still deliberately not
// touched, because such a frame owns its lease and that count must say so.
void ResetReinitState() {
    g_initRefused.store(false, std::memory_order_relaxed);
    g_disabled.store(false, std::memory_order_relaxed);
    g_afterInit.store(false, std::memory_order_relaxed);
    g_explorerHooked.store(false, std::memory_order_relaxed);
    g_hideDelayEnabled.store(false, std::memory_order_relaxed);
    g_startPointTick.store(0, std::memory_order_relaxed);
    // A failed gesture mapping is retried by the next activation, as it was
    // when every activation loaded a fresh image. A ready one is kept: the
    // section and this process's view of it are still valid.
    if (g_sharedInitState.load(std::memory_order_relaxed) == kSharedFailed) {
        g_sharedInitState.store(kSharedNotStarted, std::memory_order_relaxed);
        g_sharedErrorLogged.store(false, std::memory_order_relaxed);
    }
}

}  // namespace

BOOL Wh_ModInit() {
    Wh_Log(L"Start Menu Touch Gesture Bridge 0.16.0 initializing");

    // A disable/enable cycle re-initialises this same image, because the pin
    // keeps it mapped. Nothing in it was re-initialised by the loader, so the
    // previous activation's lifecycle flags must be cleared explicitly — but
    // only once nothing of that activation is still running. A drained unload
    // returns at zero, so a Hide can still be inside only through the entry
    // window the count cannot see; clearing g_disabled and re-pointing the
    // globals under that frame would hand it a half-reset module, so this
    // activation refuses and leaves the previous state exactly as the unload
    // left it. The next enable tries again.
    const bool reinit = g_modulePin != nullptr;
    if (reinit && g_hideCalls.load(std::memory_order_acquire) != 0) {
        g_initRefused.store(true, std::memory_order_relaxed);
        FileLog(L"mod init refused: a Hide from the previous activation is "
                L"still inside; no hook, no channel and no thread installed");
        return FALSE;
    }

    ResetReinitState();
    if (reinit) {
        FileLog(L"activation state reset");
    }

    // A prerequisite, before the channel and the hooks: a Hide frame that
    // outlives the unload must return into mapped code, and a shell-owned thread
    // needs valid return code as much as a worker would. Nothing is installed
    // when this fails; see PinThisModule.
    if (!PinThisModule()) {
        g_initRefused.store(true, std::memory_order_relaxed);
        FileLog(L"mod init refused: the image could not be pinned; no hook, no "
                L"channel and no thread installed");
        return FALSE;
    }

    // Create the objects up front so the first gesture never pays for it.
    EnsureShared();

    // The channel to the Start side. Opening it here means the section and its
    // events exist before the first Hide, and a Start mod that attaches later
    // joins the same ones. This is the only call: SwipeIpcOpen may sleep for its
    // init budget while the other side finishes a claim, so it must not run on
    // a hook. A failure here means every Hide takes the fixed lead for the life
    // of this attachment, which the budget makes reachable only for a section
    // that cannot be initialised at all.
    //
    // A re-enable of the pinned image starts with no channel, because a drained
    // unload closes it before Wh_ModUninit returns; a refused activation never
    // opened one. Either way, do not map over a channel this process already
    // holds: SwipeIpcOpen would leak the first and swap the globals under it.
    bool created = false;
    DWORD ipcError = 0;
    const bool channelOpen = g_ipc.state != nullptr;
    if (!channelOpen && !SwipeIpcOpen(&g_ipc, &created, &ipcError)) {
        Wh_Log(L"Swipe IPC channel unavailable (%u); a keyboard close takes the "
               L"fixed lead",
               static_cast<unsigned>(ipcError));
    } else {
        // A tap published before this process attached belongs to a close this
        // hook never saw: seed the watermark so only a later tap can pass.
        SwipeIpcCloseSnapshot close{};
        g_handledCloseSeq = SwipeIpcReadClose(&g_ipc, &close)
                                ? close.seq
                                : SwipeIpcLoad64(&g_ipc.state->closeSeq);
        FileLog(L"ipc open created=%d close=%lld", created ? 1 : 0,
                static_cast<long long>(g_handledCloseSeq));
    }

    HMODULE kernelBase = GetModuleHandleW(L"kernelbase.dll");
    auto loadLibraryExW = kernelBase
                              ? reinterpret_cast<LoadLibraryExW_t>(
                                    GetProcAddress(kernelBase,
                                                   "LoadLibraryExW"))
                              : nullptr;
    if (!loadLibraryExW) {
        Wh_Log(L"LoadLibraryExW not found; twinui.pcshell.dll hooks may not "
               L"be installed");
    } else if (!Wh_SetFunctionHook(
                   reinterpret_cast<void*>(loadLibraryExW),
                   reinterpret_cast<void*>(LoadLibraryExW_Hook),
                   reinterpret_cast<void**>(&LoadLibraryExW_Original))) {
        Wh_Log(L"Failed to hook LoadLibraryExW; twinui.pcshell.dll hooks may "
               L"not be installed");
    } else {
        Wh_Log(L"Hooked LoadLibraryExW");
    }

    TryInstallFromLoadedModules(false);

    return TRUE;
}

void Wh_ModAfterInit() {
    if (g_initRefused.load(std::memory_order_relaxed)) {
        return;
    }
    g_afterInit = true;
    TryInstallFromLoadedModules(false);
    Wh_ApplyHookOperations();
}

void Wh_ModBeforeUninit() {
    // A refused activation installed nothing; the flag and the reset below
    // belong to the activation that is still running.
    if (g_initRefused.load(std::memory_order_relaxed)) {
        return;
    }
    // This must be set before Windhawk removes the hooks. The Hide hook reads it
    // in its wait loop, so a hide that is being held ends its wait at once. A
    // Hide that enters after this point skips the channel; one that is already
    // inside holds its lease until it returns. The v2 channel is closed in
    // Wh_ModUninit, after that flag and after the drain.
    g_disabled = true;
    g_startPointTick.store(0, std::memory_order_relaxed);

    PublishInactiveOnUnload();
}

void Wh_ModUninit() {
    // A refused activation installed nothing, so the channel and the count
    // below belong to the activation that is still running.
    if (g_initRefused.load(std::memory_order_relaxed)) {
        return;
    }

    // Two channels, two lifetimes, for two reasons.
    //
    // The gesture mapping (Local\WindhawkStartGestureShared) and its event are
    // kept for the rest of the explorer process lifetime, on purpose. A consumer
    // maps the section independently and may poll it at any time, and an
    // in-flight gesture callback can still hold the view pointer that
    // EnsureShared cached; unmapping it here would turn either into an access to
    // a freed view. A re-enable re-initialises this same pinned image and
    // reuses the pair it already mapped; a different installed version maps its
    // own handles and leaves the previous image's pair to process exit. The
    // reset published by PublishInactiveOnUnload is what tells a reader that
    // this instance is gone, not the handle lifetime. Process exit reclaims
    // every pair.
    //
    // The v2 channel is closed, because its objects are named and its records
    // belong to the other process: g_ipc is unmapped and its mapping, mutex and
    // three events are closed once no Hide hook can still be inside them.
    // g_disabled was set in Wh_ModBeforeUninit, so no new Hide decision reaches
    // the channel; g_hideCalls names the Hides that are already inside. Their
    // waits end within one kHidePollMs step of that flag, so an ordinary unload
    // is drained at once.
    //
    // The wait below has no deadline, and that is the whole point. The lease
    // spans the whole call, the original Hide included, so a count of zero also
    // proves that no frame of this hook can still need the MinHook trampoline.
    // Windhawk removes the hooks and frees their trampolines as soon as this
    // function returns, so returning with a frame still about to call
    // Hide_Original would hand the host a frame whose trampoline it is about to
    // free. A timeout cannot stand in for the count: it would expire under
    // exactly that frame. The two outcomes are `drained` — the count reached
    // zero, the channel is closed, this call returns — and `stalled` — the count
    // is still nonzero and nothing else happens: no close, no return, no handle
    // left behind. The price is paid where it is owed: a Hide that does not
    // return, because the shell call under it never came back or a debugger
    // suspended its thread, blocks the Windhawk reload until it resumes.
    //
    // kIpcUnloadMarkMs and kIpcUnloadStallReportMs are diagnostics only. Neither
    // grants anything and neither ends the wait. The reports go through FileLog,
    // so a Hide wedged inside FileLog keeps them out of the file; that changes no
    // outcome, because this call is blocked either way.
    //
    // The pin and the count are not the same cover. The pin keeps an entry that
    // has not yet taken its lease from returning into unmapped code; the drain
    // covers the trampoline use of every counted frame. What remains is the
    // host's unload: Windhawk calls this function before it waits for threads
    // inside the mod image (ModsManager::ReloadModsAndSettings calls
    // Wh_ModUninit, then ThreadsCallStackWaitForRegions(..., 200, 400), then
    // frees the module), and that wait is bounded and its result discarded. So
    // "the scan found the frame" and "the frame exited before removal" are two
    // claims: the first is what that scan gives when it can see the stack, the
    // second is what this drain gives for every counted frame, and neither one
    // is proof for an uncounted entry. The pin does not reach the trampoline; it
    // is the host's allocation, not this image.
    const ULONGLONG drainStart = GetTickCount64();
    bool markLogged = false;
    ULONGLONG nextStallReport = 0;
    for (;;) {
        const int hides = g_hideCalls.load(std::memory_order_acquire);
        if (hides == 0) {
            break;
        }
        const ULONGLONG now = GetTickCount64();
        if (!markLogged &&
            now - drainStart >= static_cast<ULONGLONG>(kIpcUnloadMarkMs)) {
            markLogged = true;
            nextStallReport = now + kIpcUnloadStallReportMs;
            FileLog(L"ipc unload drain over %u ms hides=%d",
                    static_cast<unsigned>(kIpcUnloadMarkMs), hides);
        }
        if (markLogged && now >= nextStallReport) {
            nextStallReport = now + kIpcUnloadStallReportMs;
            FileLog(L"ipc unload stalled hides=%d waited=%llu ms", hides,
                    static_cast<unsigned long long>(now - drainStart));
        }
        Sleep(kHidePollMs);
    }

    // Drained: the count reached zero, so this is the one path that closes the
    // channel and the one point where the host may take the trampolines back.
    FileLog(L"ipc unload drained waited=%llu ms",
            static_cast<unsigned long long>(GetTickCount64() - drainStart));
    if (g_ipc.state) {
        SwipeIpcClose(&g_ipc);
        FileLog(L"ipc close");
    }
}
