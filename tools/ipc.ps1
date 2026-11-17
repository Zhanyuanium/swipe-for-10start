# IPC tool for the Start mod: sends a debug command, or reads the channel.
#
# [INPUT]: .NET MemoryMappedFile / EventWaitHandle, against the section and
#          events `mods/swipe_ipc_shared.h` defines.
# [OUTPUT]: `pwsh -File tools/ipc.ps1 p=0.5` publishes one command; -State
#           prints the channel's counters and last values.
# [POS]: replaces writing into the mod's debug.txt from a shell. The offsets
#        below are the contract in swipe_ipc_shared.h; the mod asserts the same
#        numbers, so a mismatch fails at compile time there, not at run time
#        here.
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

$StateName = 'Local\SwipeFor10Start_v1_State'
$CommandEventName = 'Local\SwipeFor10Start_v1_Cmd'

# Offsets from SwipeIpcState, in bytes.
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
$OffsetRetractRequestSeq = 560
$OffsetRetractRequestTick = 568
$OffsetRetractPlanSeq = 576
$OffsetRetractPlanMs = 584
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

if ($State) {
    $mapping = Open-State
    try {
        $view = $mapping.CreateViewAccessor(
            0, $StateBytes,
            [System.IO.MemoryMappedFiles.MemoryMappedFileAccess]::Read)

        $magic = $view.ReadUInt32($OffsetMagic)
        $version = $view.ReadUInt32($OffsetVersion)
        $text = [System.Text.Encoding]::UTF8.GetString(
            [byte[]]@(0..($CmdTextBytes - 1) | ForEach-Object {
                $view.ReadByte($OffsetCmdText + $_)
            })).TrimEnd([char]0)

        [pscustomobject]@{
            magic     = '0x{0:X8}' -f $magic
            version   = $version
            cmdSeq    = $view.ReadInt64($OffsetCmdSeq)
            cmdText   = $text
            closeSeq  = $view.ReadInt64($OffsetCloseSeq)
            closeKind = $view.ReadInt32($OffsetCloseKind)
            closeTick = $view.ReadInt64($OffsetCloseTick)
            retractRequestSeq = $view.ReadInt64($OffsetRetractRequestSeq)
            retractPlanSeq    = $view.ReadInt64($OffsetRetractPlanSeq)
            retractPlanMs     = $view.ReadInt32($OffsetRetractPlanMs)
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

$mapping = Open-State
try {
    $view = $mapping.CreateViewAccessor(
        0, $StateBytes,
        [System.IO.MemoryMappedFiles.MemoryMappedFileAccess]::ReadWrite)

    # The Start mod reads the text before it looks at the sequence number, so
    # the text goes in first and the number is bumped last. The event is what
    # wakes the reader; it is manual-reset and the reader resets it.
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($text)
    $view.WriteArray($OffsetCmdText, $bytes, 0, $bytes.Length)
    $view.Write($OffsetCmdText + $bytes.Length, [byte]0)
    $view.Write($OffsetCmdTick, [Environment]::TickCount64)
    $sequence = $view.ReadInt64($OffsetCmdSeq) + 1
    $view.Write($OffsetCmdSeq, $sequence)
    $view.Dispose()
} finally {
    $mapping.Dispose()
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
