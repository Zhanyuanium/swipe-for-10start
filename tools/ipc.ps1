# IPC tool for the Start mod: reads the controller/executor channel.
#
# [INPUT]: .NET MemoryMappedFile, against the section `mods/swipe_ipc_shared.h`
#          defines.
# [OUTPUT]: `pwsh -File tools/ipc.ps1 -State` prints one snapshot of the channel
#           (each record with the sequence it was copied under, plus the controller
#           words, the peer liveness pair and the peer activation epoch) and
#           refuses a section whose initState is not ready or whose magic or
#           layoutBytes is not this header's.
# [POS]: the read-only view of the merged mod's channel. There is no command
#        publisher any more: the debug command record, its mutex and its event are
#        gone, so a position argument is refused instead of silently doing
#        nothing. The offsets below are the contract in swipe_ipc_shared.h; the mod
#        asserts the same numbers at compile time, and this tool compares the
#        section's initState, magic and layoutBytes before it reads. The object
#        namespace is the ABI identity, so a section from an older development
#        image cannot be opened at these names at all. Every record is a seqlock:
#        the counter is bumped to odd before a write and to even after, and a
#        reader retries while it is odd or changed, returning the accepted counter
#        with the copy so no display can mix two publishes.
#
# Example:
#   pwsh -NoProfile -File tools\ipc.ps1 -State

[CmdletBinding()]
param(
    [Parameter(Position = 0)][string] $Command,
    [switch] $State
)

$ErrorActionPreference = 'Stop'

$StateName = 'Local\SwipeFor10Start_SingleMod_State'

# Offsets from SwipeIpcState, in bytes. Keep in step with the asserts there.
$OffsetMagic = 0
$OffsetOpenSeq = 8
$OffsetOpenTick = 16
$OffsetInitState = 24
$OffsetLayoutBytes = 28
$OffsetIntentSeq = 40
$OffsetIntentTick = 48
$OffsetIntentKind = 56
$OffsetIntentTrigger = 60
$OffsetIntentMethod = 64
$OffsetIntentFlags = 68
$OffsetIntentOpen = 72
$OffsetIntentAckSeq = 80
$OffsetIntentAckFor = 88
$OffsetIntentAckState = 96
$OffsetIntentAckDuration = 100
$OffsetIntentAckEntry = 104
$OffsetIntentAckHr = 108
$OffsetExecSeq = 112
$OffsetExecTick = 120
$OffsetExecKind = 128
$OffsetExecMethod = 132
$OffsetExecFlags = 136
$OffsetExecExecuteAt = 144
$OffsetExecOpen = 152
$OffsetExecTarget = 160
$OffsetExecAckSeq = 168
$OffsetExecAckFor = 176
$OffsetExecAckState = 184
$OffsetExecAckHr = 188
$OffsetExecAckVisible = 192
$OffsetControllerDirection = 196
$OffsetControllerTouch = 200
$OffsetPeerTick = 204
$OffsetPeerState = 208
$OffsetPeerEpoch = 212
$StateBytes = 216

# The ack outcome and visibility words are shared constants (swipe_ipc_shared.h).
# A missing key prints a blank name, the number is still shown beside it.
$AckNames = @{
    0 = 'pending'
    1 = 'executed'
    2 = 'refused'
    3 = 'expired'
    4 = 'confirmed'
    5 = 'unconfirmed'
    6 = 'unknown'
    7 = 'superseded'
}
$VisibilityNames = @{
    -1 = 'unknown'
    0  = 'hidden'
    1  = 'visible'
}

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

if (-not $State) {
    if ($Command) {
        throw ('The debug command channel is gone; this tool is read-only. ' +
               'Use: ipc.ps1 -State')
    }
    throw 'Usage: ipc.ps1 -State'
}

$mapping = Open-State
try {
    $view = $mapping.CreateViewAccessor(
        0, $StateBytes,
        [System.IO.MemoryMappedFiles.MemoryMappedFileAccess]::Read)

    # Validate the layout before printing any number from it. The offsets below
    # are this header's; a section of another layout would be read at the wrong
    # places, and its numbers would look plausible. magic and layoutBytes are
    # written once by the claimer and initState is its publication barrier, so a
    # section still initialising fails here too.
    $magic = $view.ReadUInt32($OffsetMagic)
    $layoutBytes = $view.ReadInt32($OffsetLayoutBytes)
    $initState = $view.ReadInt32($OffsetInitState)
    if ($initState -ne 2 -or $magic -ne 0x53313053 -or
        $layoutBytes -ne $StateBytes) {
        $detail = ('magic=0x{0:X8} layoutBytes={1} initState={2}') -f
                  $magic, $layoutBytes, $initState
        throw ("The section '$StateName' is not the ready merged-mod layout " +
               "this tool reads: $detail. Nothing was printed.")
    }

    $open = Read-Record $view $OffsetOpenSeq {
        $view.ReadInt64($OffsetOpenTick)
    }
    $intent = Read-Record $view $OffsetIntentSeq {
        [pscustomobject]@{
            kind    = $view.ReadInt32($OffsetIntentKind)
            trigger = $view.ReadInt32($OffsetIntentTrigger)
            method  = $view.ReadInt32($OffsetIntentMethod)
            flags   = $view.ReadInt32($OffsetIntentFlags)
            open    = $view.ReadInt64($OffsetIntentOpen)
        }
    }
    $intentAck = Read-Record $view $OffsetIntentAckSeq {
        [pscustomobject]@{
            for      = $view.ReadInt64($OffsetIntentAckFor)
            state    = $view.ReadInt32($OffsetIntentAckState)
            duration = $view.ReadInt32($OffsetIntentAckDuration)
            entry    = $view.ReadInt32($OffsetIntentAckEntry)
            hr       = $view.ReadInt32($OffsetIntentAckHr)
        }
    }
    $exec = Read-Record $view $OffsetExecSeq {
        [pscustomobject]@{
            kind      = $view.ReadInt32($OffsetExecKind)
            method    = $view.ReadInt32($OffsetExecMethod)
            flags     = $view.ReadInt32($OffsetExecFlags)
            executeAt = $view.ReadInt64($OffsetExecExecuteAt)
            open      = $view.ReadInt64($OffsetExecOpen)
            target    = $view.ReadInt64($OffsetExecTarget)
        }
    }
    $execAck = Read-Record $view $OffsetExecAckSeq {
        [pscustomobject]@{
            command  = $view.ReadInt64($OffsetExecAckFor)
            state    = $view.ReadInt32($OffsetExecAckState)
            stateName = $script:AckNames[$view.ReadInt32($OffsetExecAckState)]
            hr       = $view.ReadInt32($OffsetExecAckHr)
            visible  = $view.ReadInt32($OffsetExecAckVisible)
            visibleName = $script:VisibilityNames[$view.ReadInt32($OffsetExecAckVisible)]
        }
    }

    [pscustomobject]@{
        magic       = '0x{0:X8}' -f $magic
        initState   = $initState
        layoutBytes = $layoutBytes
        openSeq     = $(if ($open) { $open.Seq } else { 'unstable' })
        openTick    = $(if ($open) { $open.Value } else { 'unstable' })
        intentSeq   = $(if ($intent) { $intent.Seq } else { 'unstable' })
        intent      = $(if ($intent) { "kind=$($intent.Value.kind) trigger=$($intent.Value.trigger) method=$($intent.Value.method) flags=$($intent.Value.flags) open=$($intent.Value.open)" } else { 'unstable' })
        intentAckSeq = $(if ($intentAck) { $intentAck.Seq } else { 'unstable' })
        intentAck   = $(if ($intentAck) { "for=$($intentAck.Value.for) state=$($intentAck.Value.state) duration=$($intentAck.Value.duration) entry=$($intentAck.Value.entry) hr=$($intentAck.Value.hr)" } else { 'unstable' })
        execSeq     = $(if ($exec) { $exec.Seq } else { 'unstable' })
        exec        = $(if ($exec) { "kind=$($exec.Value.kind) method=$($exec.Value.method) flags=$($exec.Value.flags) executeAt=$($exec.Value.executeAt) open=$($exec.Value.open) target=$($exec.Value.target)" } else { 'unstable' })
        execAckSeq  = $(if ($execAck) { $execAck.Seq } else { 'unstable' })
        execAck     = $(if ($execAck) { "command=$($execAck.Value.command) state=$($execAck.Value.state)($($execAck.Value.stateName)) hr=$($execAck.Value.hr) visible=$($execAck.Value.visible)($($execAck.Value.visibleName))" } else { 'unstable' })
        controllerDirection = $view.ReadInt32($OffsetControllerDirection)
        controllerTouch     = $view.ReadInt32($OffsetControllerTouch)
        peerTick            = $view.ReadUInt32($OffsetPeerTick)
        peerState           = $view.ReadInt32($OffsetPeerState)
        peerEpoch           = $view.ReadUInt32($OffsetPeerEpoch)
    } | Format-List
    $view.Dispose()
} finally {
    $mapping.Dispose()
}
