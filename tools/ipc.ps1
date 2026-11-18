# IPC tool for the Start mod: sends a debug command, or reads the channel.
#
# [INPUT]: .NET MemoryMappedFile / EventWaitHandle / Mutex, against the section,
#          events and mutex `mods/swipe_ipc_shared.h` defines.
# [OUTPUT]: `pwsh -File tools/ipc.ps1 p=0.5` publishes one command; -State
#           prints one snapshot of the channel (each record with the sequence it
#           was copied under) and refuses a section whose initState is not ready
#           or whose magic, version or layoutBytes is not this header's.
# [POS]: replaces writing into the mod's debug.txt from a shell. The offsets
#        below are the contract in swipe_ipc_shared.h; the mod asserts the same
#        numbers at compile time, and this tool compares the section's initState,
#        magic, version and layoutBytes before it reads or writes, so a foreign
#        or uninitialised section is refused instead of misread. Every record is
#        a seqlock: the counter is bumped to odd before a write and to even
#        after, and a reader retries while it is odd or changed, returning the
#        accepted counter with the copy so no display can mix two publishes. The
#        command record needs the channel's writer mutex as well, because two
#        tools publishing at once would interleave their text. An interrupted
#        publish leaves cmdSeq odd on purpose: -State then prints that record as
#        `unstable`, no reader accepts the half-written line, and the next
#        publisher steps the odd value forward before it writes a complete
#        record. The command event is set only after the even publish, never by
#        a failed one.
#
# Examples:
#   pwsh -NoProfile -File tools\ipc.ps1 -State
#   pwsh -NoProfile -File tools\ipc.ps1 p=0.5
#   pwsh -NoProfile -File tools\ipc.ps1 anim=0

[CmdletBinding()]
param(
    [Parameter(Position = 0)][string] $Command,
    [switch] $State
)

$ErrorActionPreference = 'Stop'

$StateName = 'Local\SwipeFor10Start_v2_State'
$CommandEventName = 'Local\SwipeFor10Start_v2_Cmd'
$CommandMutexName = 'Local\SwipeFor10Start_v2_CmdWrite'

# Offsets from SwipeIpcState, in bytes. Keep in step with the asserts there.
$OffsetMagic = 0
$OffsetVersion = 4
$OffsetCmdSeq = 8
$OffsetCmdText = 16
$CmdTextBytes = 512
$OffsetCmdTick = 528
$OffsetCloseSeq = 536
$OffsetCloseTick = 544
$OffsetCloseKind = 552
$OffsetCloseDelayMs = 556
$OffsetCloseTapped = 560
$OffsetClosePull = 564
$OffsetRetractRequestSeq = 568
$OffsetRetractRequestTick = 576
$OffsetRetractPlanMs = 584
$OffsetRetractRequestOpen = 592
$OffsetRetractRequestDeadline = 600
$OffsetRetractPlanSeq = 608
$OffsetRetractPlanRequest = 616
$OffsetRetractCancelSeq = 624
$OffsetOpenSeq = 632
$OffsetOpenTick = 640
$OffsetInitState = 648
$OffsetLayoutBytes = 652
$StateBytes = 672

function Open-State {
    try {
        return [System.IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting(
            $StateName)
    } catch {
        throw ("The Start mod's IPC section '$StateName' is not open. " +
               'Start the menu once so StartMenuExperienceHost loads the mod.')
    }
}

# Reads a seqlock record: retries while the counter is odd or changed. Returns
# the sequence the copy was taken under together with the copy, so a caller can
# print both from one snapshot instead of re-reading the counter and hoping it
# still describes the fields it just printed.
function Read-Record {
    param(
        [System.IO.MemoryMappedFiles.MemoryMappedViewAccessor] $View,
        [int] $Offset,
        [scriptblock] $Copy
    )

    for ($attempt = 0; $attempt -lt 64; $attempt++) {
        $before = $View.ReadInt64($Offset)
        if (($before -band 1) -ne 0) { continue }
        $value = & $Copy
        $after = $View.ReadInt64($Offset)
        if ($before -eq $after -and ($after -band 1) -eq 0) {
            return [pscustomobject]@{ Seq = $before; Value = $value }
        }
    }
    return $null
}

if ($State) {
    $mapping = Open-State
    try {
        $view = $mapping.CreateViewAccessor(
            0, $StateBytes,
            [System.IO.MemoryMappedFiles.MemoryMappedFileAccess]::Read)

        # Validate the layout before printing any number from it. The offsets
        # below are this header's; a section of another version would be read at
        # the wrong places, and its numbers would look plausible. magic, version
        # and layoutBytes are written once by the claimer and initState is its
        # publication barrier, so a section still initialising fails here too.
        $magic = $view.ReadUInt32($OffsetMagic)
        $version = $view.ReadUInt32($OffsetVersion)
        $layoutBytes = $view.ReadInt32($OffsetLayoutBytes)
        $initState = $view.ReadInt32($OffsetInitState)
        if ($initState -ne 2 -or $magic -ne 0x53313053 -or $version -ne 2 -or
            $layoutBytes -ne $StateBytes) {
            $detail = ('magic=0x{0:X8} version={1} layoutBytes={2} ' +
                       'initState={3}') -f $magic, $version, $layoutBytes,
                       $initState
            throw ("The section '$StateName' is not a ready v2 layout this " +
                   "tool reads: $detail. Nothing was printed.")
        }

        $cmd = Read-Record $view $OffsetCmdSeq {
            # One byte array, decoded up to the first NUL: a shorter command must
            # not show the tail of a longer one written earlier in the same
            # record.
            $raw = [byte[]]::new($CmdTextBytes)
            $view.ReadArray($OffsetCmdText, $raw, 0, $CmdTextBytes) | Out-Null
            $length = [Array]::IndexOf($raw, [byte]0)
            if ($length -lt 0) { $length = $CmdTextBytes }
            [pscustomobject]@{
                text = [System.Text.Encoding]::UTF8.GetString($raw, 0, $length)
                tick = $view.ReadInt64($OffsetCmdTick)
            }
        }
        $close = Read-Record $view $OffsetCloseSeq {
            [pscustomobject]@{
                tick   = $view.ReadInt64($OffsetCloseTick)
                kind   = $view.ReadInt32($OffsetCloseKind)
                delay  = $view.ReadInt32($OffsetCloseDelayMs)
                tapped = $view.ReadInt32($OffsetCloseTapped)
                pull   = $view.ReadInt32($OffsetClosePull)
            }
        }
        $retractRequest = Read-Record $view $OffsetRetractRequestSeq {
            [pscustomobject]@{
                tick     = $view.ReadInt64($OffsetRetractRequestTick)
                open     = $view.ReadInt64($OffsetRetractRequestOpen)
                deadline = $view.ReadInt64($OffsetRetractRequestDeadline)
            }
        }
        $plan = Read-Record $view $OffsetRetractPlanSeq {
            [pscustomobject]@{
                ms      = $view.ReadInt32($OffsetRetractPlanMs)
                request = $view.ReadInt64($OffsetRetractPlanRequest)
            }
        }
        $open = Read-Record $view $OffsetOpenSeq {
            $view.ReadInt64($OffsetOpenTick)
        }

        [pscustomobject]@{
            magic      = '0x{0:X8}' -f $magic
            version    = $version
            initState  = $initState
            layoutBytes = $layoutBytes
            cmdSeq     = $(if ($cmd) { $cmd.Seq } else { 'unstable' })
            cmdText    = $(if ($cmd) { $cmd.Value.text } else { 'unstable' })
            cmdTick    = $(if ($cmd) { $cmd.Value.tick } else { 'unstable' })
            closeSeq   = $(if ($close) { $close.Seq } else { 'unstable' })
            close      = $(if ($close) { "kind=$($close.Value.kind) tapped=$($close.Value.tapped) pull=$($close.Value.pull) delay=$($close.Value.delay) tick=$($close.Value.tick)" } else { 'unstable' })
            retractRequestSeq      = $(if ($retractRequest) { $retractRequest.Seq } else { 'unstable' })
            retractRequest         = $(if ($retractRequest) { "open=$($retractRequest.Value.open) tick=$($retractRequest.Value.tick) deadline=$($retractRequest.Value.deadline)" } else { 'unstable' })
            retractPlanSeq         = $(if ($plan) { $plan.Seq } else { 'unstable' })
            retractPlan            = $(if ($plan) { "ms=$($plan.Value.ms) request=$($plan.Value.request)" } else { 'unstable' })
            retractCancelSeq     = $view.ReadInt64($OffsetRetractCancelSeq)
            openSeq    = $(if ($open) { $open.Seq } else { 'unstable' })
            openTick   = $(if ($open) { $open.Value } else { 'unstable' })
        } | Format-List
        $view.Dispose()
    } finally {
        $mapping.Dispose()
    }
    return
}

if (-not $Command) {
    throw 'Usage: ipc.ps1 <command> | ipc.ps1 -State  (commands: p=, t=, e=, anim=)'
}

$text = $Command.Trim()
if ($text.Length -eq 0 -or $text.Length -ge $CmdTextBytes) {
    throw "Command must be 1..$($CmdTextBytes - 1) characters."
}

$bytes = [System.Text.Encoding]::UTF8.GetBytes($text)
# The record is a byte field, so the limit is bytes, not characters: a longer
# multibyte line would otherwise write past cmdText into the record's next field.
if ($bytes.Length -ge $CmdTextBytes) {
    throw ("Command is $($bytes.Length) bytes; the record holds at most " +
           "$($CmdTextBytes - 1).")
}

$mutex = $null
try {
    $mutex = [System.Threading.Mutex]::OpenExisting($CommandMutexName)
} catch {
    throw ("The Start mod's command mutex '$CommandMutexName' is not open; " +
           'nothing would serialise this write against another tool.')
}

$taken = $false
try {
    try {
        $taken = $mutex.WaitOne(200)
    } catch [System.Threading.AbandonedMutexException] {
        # The previous holder died mid-write; the record is republished below.
        $taken = $true
    }
    if (-not $taken) {
        throw 'Another ipc.ps1 holds the command mutex; nothing was sent.'
    }

    $mapping = Open-State
    try {
        $view = $mapping.CreateViewAccessor(
            0, $StateBytes,
            [System.IO.MemoryMappedFiles.MemoryMappedFileAccess]::ReadWrite)

        # The same layout check as -State, before a byte is written: the offsets
        # below are this header's, a section of another version would take this
        # text and this counter into fields that mean something else, and a
        # section that is not ready may still be initialising.
        $magic = $view.ReadUInt32($OffsetMagic)
        $version = $view.ReadUInt32($OffsetVersion)
        $layoutBytes = $view.ReadInt32($OffsetLayoutBytes)
        $initState = $view.ReadInt32($OffsetInitState)
        if ($initState -ne 2) {
            throw ("The section '$StateName' is not ready " +
                   "(initState=$initState); nothing was sent.")
        }
        if ($magic -ne 0x53313053 -or $version -ne 2 -or
            $layoutBytes -ne $StateBytes) {
            $detail = ('magic=0x{0:X8} version={1} layoutBytes={2} ' +
                       'initState={3}') -f $magic, $version, $layoutBytes,
                       $initState
            throw ("The section '$StateName' is not the v2 layout this tool " +
                   "writes: $detail. Nothing was sent.")
        }

        # Seqlock: odd marks a write in progress, even publishes it. There is no
        # restore on failure: if a write below throws, the record stays odd, so
        # no reader accepts the half-written line and a command event that was
        # signalled earlier cannot wake the Start side with it. The next
        # publisher steps the odd value forward (the same rule as
        # SwipeIpcBeginWrite in the header) and writes the whole record again;
        # -State reports the odd record as `unstable` until then.
        $sequence = $view.ReadInt64($OffsetCmdSeq)
        $odd = if (($sequence -band 1) -eq 0) { $sequence + 1 } else { $sequence + 2 }
        $view.Write($OffsetCmdSeq, $odd)
        $view.WriteArray($OffsetCmdText, $bytes, 0, $bytes.Length)
        $view.Write($OffsetCmdText + $bytes.Length, [byte]0)
        $view.Write($OffsetCmdTick, [Environment]::TickCount64)
        $view.Write($OffsetCmdSeq, $odd + 1)
        $sequence = $odd + 1
        $view.Dispose()
    } finally {
        $mapping.Dispose()
    }
} finally {
    if ($taken) { $mutex.ReleaseMutex() | Out-Null }
    $mutex.Dispose()
}

try {
    $event = [System.Threading.EventWaitHandle]::OpenExisting(
        $CommandEventName)
    $event.Set() | Out-Null
    $event.Dispose()
} catch {
    throw ("Command '$text' was written, but the Start mod's command event " +
           "'$CommandEventName' is not open, so nothing woke to read it.")
}

"sent seq=$sequence '$text'"
