#requires -Version 5.1
<#
.SYNOPSIS
  Native Windows Forms chat for the CxxRelay JSONL C++ worker.
.DESCRIPTION
  Runs on Windows PowerShell 5.1 without Python, a local web server,
  browsers, or third-party PowerShell modules. The worker is a persistent
  child process communicating via UTF-8 JSON lines over stdin/stdout.
.EXAMPLE
  powershell.exe -NoProfile -STA -File .\prototype\windows\Start-CxxRelayChat.ps1 -Mode mock
.EXAMPLE
  powershell.exe -NoProfile -STA -File .\prototype\windows\Start-CxxRelayChat.ps1 -Mode openai
#>
[CmdletBinding()]
param(
    [ValidateSet('mock', 'openai')]
    [string] $Mode = 'mock',
    [string] $WorkerPath = ''
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

if ($env:OS -ne 'Windows_NT') {
    throw 'Windows Forms is available only on Windows.'
}
if ([System.Threading.Thread]::CurrentThread.GetApartmentState() -ne [System.Threading.ApartmentState]::STA) {
    throw 'Start Windows PowerShell with -STA (e.g. powershell.exe -NoProfile -STA -File ...).'
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

# Real .NET event handlers enqueue output on a thread-safe queue. PowerShell
# scriptblocks are never executed on the process output reader threads.
if (-not ('CxxRelayOutputLine' -as [type])) {
Add-Type -TypeDefinition @'
using System;
using System.Collections.Concurrent;
using System.Diagnostics;
public sealed class CxxRelayOutputLine {
    public readonly string Stream;
    public readonly string Text;
    public CxxRelayOutputLine(string stream, string text) {
        Stream = stream;
        Text = text;
    }
}
public static class CxxRelayOutputBridge {
    public static void Attach(Process process, ConcurrentQueue<CxxRelayOutputLine> queue) {
        process.OutputDataReceived += (sender, args) => {
            if (args.Data != null) queue.Enqueue(new CxxRelayOutputLine("stdout", args.Data));
        };
        process.ErrorDataReceived += (sender, args) => {
            if (args.Data != null) queue.Enqueue(new CxxRelayOutputLine("stderr", args.Data));
        };
    }
}
'@
}

$script:ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$script:Process = $null
$script:Queue = [System.Collections.Concurrent.ConcurrentQueue[CxxRelayOutputLine]]::new()
$script:Busy = $false
$script:ActiveId = ''
$script:PendingApproval = $null
$script:AssistantStarted = $false
$script:AssistantText = ''
$script:Closing = $false
$script:ProcessExited = $false
$script:Mode = $Mode

function Resolve-CxxRelayWorker {
    param([string] $SuppliedPath)
    if (-not [string]::IsNullOrWhiteSpace($SuppliedPath)) {
        $resolved = [System.IO.Path]::GetFullPath($SuppliedPath)
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            throw "Worker executable not found: $resolved"
        }
        return $resolved
    }
    $candidates = @(
        'build-win\Release\prototype.worker.exe',
        'build-win\Debug\prototype.worker.exe',
        'build-win\prototype.worker.exe',
        'build\Release\prototype.worker.exe',
        'build\Debug\prototype.worker.exe',
        'build\prototype.worker.exe'
    )
    foreach ($candidate in $candidates) {
        $path = Join-Path $script:ProjectRoot $candidate
        if (Test-Path -LiteralPath $path -PathType Leaf) { return $path }
    }
    throw "Worker not found."
}

function New-UiButton {
    param([string] $Caption, [int] $Width = 115)
    $button = [System.Windows.Forms.Button]::new()
    $button.Text = $Caption
    $button.Width = $Width
    $button.Height = 31
    $button.Margin = [System.Windows.Forms.Padding]::new(4)
    $button.FlatStyle = [System.Windows.Forms.FlatStyle]::System
    return $button
}

$script:Form = [System.Windows.Forms.Form]::new()
$script:Form.Text = 'CxxRelay | Local C++ Chat Workflow'
$script:Form.StartPosition = [System.Windows.Forms.FormStartPosition]::CenterScreen
$script:Form.MinimumSize = [System.Drawing.Size]::new(790, 570)
$script:Form.Size = [System.Drawing.Size]::new(1180, 760)
$script:Form.Font = [System.Drawing.Font]::new('Segoe UI', 9.0)

$root = [System.Windows.Forms.TableLayoutPanel]::new()
$root.Dock = [System.Windows.Forms.DockStyle]::Fill
$root.ColumnCount = 1
$root.RowCount = 4
[void] $root.ColumnStyles.Add([System.Windows.Forms.ColumnStyle]::new([System.Windows.Forms.SizeType]::Percent, 100.0))
[void] $root.RowStyles.Add([System.Windows.Forms.RowStyle]::new([System.Windows.Forms.SizeType]::Absolute, 57.0))
[void] $root.RowStyles.Add([System.Windows.Forms.RowStyle]::new([System.Windows.Forms.SizeType]::Percent, 100.0))
[void] $root.RowStyles.Add([System.Windows.Forms.RowStyle]::new([System.Windows.Forms.SizeType]::Absolute, 110.0))
[void] $root.RowStyles.Add([System.Windows.Forms.RowStyle]::new([System.Windows.Forms.SizeType]::Absolute, 28.0))
$script:Form.Controls.Add($root)

$header = [System.Windows.Forms.FlowLayoutPanel]::new()
$header.Dock = [System.Windows.Forms.DockStyle]::Fill
$header.FlowDirection = [System.Windows.Forms.FlowDirection]::LeftToRight
$header.WrapContents = $false
$header.Padding = [System.Windows.Forms.Padding]::new(8, 8, 0, 0)
$header.AutoScroll = $true
$root.Controls.Add($header, 0, 0)

$heading = [System.Windows.Forms.Label]::new()
$heading.Text = 'CxxRelay  |  Chat'
$heading.AutoSize = $false
$heading.Width = 205
$heading.Height = 34
$heading.TextAlign = [System.Drawing.ContentAlignment]::MiddleLeft
$heading.Font = [System.Drawing.Font]::new('Segoe UI', 14.0, [System.Drawing.FontStyle]::Bold)
$header.Controls.Add($heading)
$script:ModeLabel = [System.Windows.Forms.Label]::new()
$script:ModeLabel.Text = if ($Mode -eq 'mock') { 'Offline / Mock' } else { 'OpenAI / Live' }
$script:ModeLabel.Width = 116
$script:ModeLabel.Height = 36
$script:ModeLabel.TextAlign = [System.Drawing.ContentAlignment]::MiddleLeft
$header.Controls.Add($script:ModeLabel)
$script:ResetButton = New-UiButton 'Neuer Chat' 104
$script:RefreshButton = New-UiButton 'Aktualisieren' 116
$script:CancelButton = New-UiButton 'Abbrechen' 105
$header.Controls.Add($script:ResetButton)
$header.Controls.Add($script:RefreshButton)
$header.Controls.Add($script:CancelButton)

$split = [System.Windows.Forms.SplitContainer]::new()
$split.Dock = [System.Windows.Forms.DockStyle]::Fill
$split.Orientation = [System.Windows.Forms.Orientation]::Vertical
$split.Panel1MinSize = 330
$split.Panel2MinSize = 230
$root.Controls.Add($split, 0, 1)

$script:Chat = [System.Windows.Forms.RichTextBox]::new()
$script:Chat.Dock = [System.Windows.Forms.DockStyle]::Fill
$script:Chat.ReadOnly = $true
$script:Chat.DetectUrls = $false
$script:Chat.HideSelection = $false
$script:Chat.BorderStyle = [System.Windows.Forms.BorderStyle]::None
$script:Chat.BackColor = [System.Drawing.Color]::FromArgb(250, 251, 253)
$script:Chat.Font = [System.Drawing.Font]::new('Segoe UI', 10.5)
$split.Panel1.Padding = [System.Windows.Forms.Padding]::new(10)
$split.Panel1.Controls.Add($script:Chat)

$sidebar = [System.Windows.Forms.TabControl]::new()
$sidebar.Dock = [System.Windows.Forms.DockStyle]::Fill
$split.Panel2.Controls.Add($sidebar)
$tabDocs = [System.Windows.Forms.TabPage]::new('Dokumente')
$tabRef = [System.Windows.Forms.TabPage]::new('Reflection / Tools')
$tabEvents = [System.Windows.Forms.TabPage]::new('Ereignisse')
$sidebar.TabPages.AddRange([System.Windows.Forms.TabPage[]] @($tabDocs, $tabRef, $tabEvents))

$script:Documents = [System.Windows.Forms.ListView]::new()
$script:Documents.Dock = [System.Windows.Forms.DockStyle]::Fill
$script:Documents.View = [System.Windows.Forms.View]::Details
$script:Documents.FullRowSelect = $true
$script:Documents.GridLines = $true
[void] $script:Documents.Columns.Add('ID', 45)
[void] $script:Documents.Columns.Add('Titel', 190)
[void] $script:Documents.Columns.Add('Kategorie', 92)
$tabDocs.Controls.Add($script:Documents)

$script:ReflectionText = [System.Windows.Forms.RichTextBox]::new()
$script:ReflectionText.Dock = [System.Windows.Forms.DockStyle]::Fill
$script:ReflectionText.ReadOnly = $true
$script:ReflectionText.WordWrap = $true
$script:ReflectionText.Font = [System.Drawing.Font]::new('Consolas', 9.0)
$tabRef.Controls.Add($script:ReflectionText)

$script:Events = [System.Windows.Forms.RichTextBox]::new()
$script:Events.Dock = [System.Windows.Forms.DockStyle]::Fill
$script:Events.ReadOnly = $true
$script:Events.WordWrap = $true
$script:Events.Font = [System.Drawing.Font]::new('Consolas', 8.5)
$tabEvents.Controls.Add($script:Events)

$composer = [System.Windows.Forms.TableLayoutPanel]::new()
$composer.Dock = [System.Windows.Forms.DockStyle]::Fill
$composer.RowCount = 2
$composer.ColumnCount = 2
[void] $composer.ColumnStyles.Add([System.Windows.Forms.ColumnStyle]::new([System.Windows.Forms.SizeType]::Percent, 100.0))
[void] $composer.ColumnStyles.Add([System.Windows.Forms.ColumnStyle]::new([System.Windows.Forms.SizeType]::Absolute, 130.0))
[void] $composer.RowStyles.Add([System.Windows.Forms.RowStyle]::new([System.Windows.Forms.SizeType]::Absolute, 39.0))
[void] $composer.RowStyles.Add([System.Windows.Forms.RowStyle]::new([System.Windows.Forms.SizeType]::Percent, 100.0))
$root.Controls.Add($composer, 0, 2)

$quick = [System.Windows.Forms.FlowLayoutPanel]::new()
$quick.Dock = [System.Windows.Forms.DockStyle]::Fill
$quick.WrapContents = $false
$quick.AutoScroll = $true
$composer.SetColumnSpan($quick, 2)
$composer.Controls.Add($quick, 0, 0)
foreach ($example in @(
    @{ Caption = 'Dokumente anzeigen'; Text = 'Liste alle Dokumente' },
    @{ Caption = 'Berichte suchen'; Text = 'Suche nach Bericht' },
    @{ Caption = 'Umbenennen'; Text = 'Benenne Dokument 1 um in Abschlussbericht' },
    @{ Caption = 'Langsamer Test'; Text = 'Bitte langsam antworten' }
)) {
    $b = New-UiButton $example.Caption 147
    $b.Tag = $example.Text
    $b.Add_Click({ param($sender, $e) if (-not $script:Busy -and $null -eq $script:PendingApproval) {
        $script:Prompt.Text = [string] $sender.Tag
        $script:Prompt.Focus()
    } })
    $quick.Controls.Add($b)
}

$script:Prompt = [System.Windows.Forms.TextBox]::new()
$script:Prompt.Multiline = $true
$script:Prompt.ScrollBars = [System.Windows.Forms.ScrollBars]::Vertical
$script:Prompt.Dock = [System.Windows.Forms.DockStyle]::Fill
$script:Prompt.Font = [System.Drawing.Font]::new('Segoe UI', 10.5)
$script:Prompt.Margin = [System.Windows.Forms.Padding]::new(9, 4, 4, 9)
$composer.Controls.Add($script:Prompt, 0, 1)
$script:SendButton = New-UiButton 'Senden' 110
$script:SendButton.Dock = [System.Windows.Forms.DockStyle]::Fill
$script:SendButton.Margin = [System.Windows.Forms.Padding]::new(4, 4, 9, 9)
$composer.Controls.Add($script:SendButton, 1, 1)

$script:Status = [System.Windows.Forms.Label]::new()
$script:Status.Dock = [System.Windows.Forms.DockStyle]::Fill
$script:Status.TextAlign = [System.Drawing.ContentAlignment]::MiddleLeft
$script:Status.Padding = [System.Windows.Forms.Padding]::new(10, 0, 0, 0)
$script:Status.Text = 'Worker wird gestartet ...'
$script:Status.BackColor = [System.Drawing.Color]::FromArgb(233, 237, 243)
$root.Controls.Add($script:Status, 0, 3)

function Scroll-Chat {
    $script:Chat.SelectionStart = $script:Chat.TextLength
    $script:Chat.ScrollToCaret()
}
function Write-Chat {
    param([string] $Role, [string] $Text)
    $script:Chat.SelectionStart = $script:Chat.TextLength
    $script:Chat.SelectionColor = if ($Role -eq 'Du') {
        [System.Drawing.Color]::FromArgb(35, 86, 148)
    } elseif ($Role -eq 'Tool') {
        [System.Drawing.Color]::FromArgb(96, 95, 103)
    } else {
        [System.Drawing.Color]::FromArgb(31, 34, 41)
    }
    $script:Chat.AppendText("${Role}: $Text`r`n`r`n")
    Scroll-Chat
}
function Write-Delta {
    param([string] $Text)
    if (-not $script:AssistantStarted) {
        $script:Chat.SelectionStart = $script:Chat.TextLength
        $script:Chat.SelectionColor = [System.Drawing.Color]::FromArgb(31, 34, 41)
        $script:Chat.AppendText('Assistent: ')
        $script:AssistantStarted = $true
    }
    $script:AssistantText += $Text
    $script:Chat.AppendText($Text)
    Scroll-Chat
}
function Finish-Assistant {
    param([string] $FinalText)
    if (-not $script:AssistantStarted) {
        if (-not [string]::IsNullOrEmpty($FinalText)) { Write-Chat 'Assistent' $FinalText }
    } else {
        if ($script:AssistantText -ne $FinalText -and -not [string]::IsNullOrEmpty($FinalText)) {
            # A provider may stream partial text then supply a corrected final answer.
            $script:Chat.AppendText("`r`n[Endfassung] $FinalText")
        }
        $script:Chat.AppendText("`r`n`r`n")
    }
    $script:AssistantStarted = $false
    $script:AssistantText = ''
    Scroll-Chat
}
function Log-Event {
    param([string] $Message)
    $script:Events.AppendText(('{0:HH:mm:ss}  {1}' -f [DateTime]::Now, $Message) + "`r`n")
    if ($script:Events.TextLength -gt 60000) {
        $script:Events.Text = $script:Events.Text.Substring($script:Events.TextLength - 30000)
    }
    $script:Events.SelectionStart = $script:Events.TextLength
    $script:Events.ScrollToCaret()
}
function Update-Buttons {
    $interactive = -not $script:Busy -and $null -eq $script:PendingApproval -and -not $script:ProcessExited
    $script:SendButton.Enabled = $interactive
    $script:Prompt.Enabled = $interactive
    $script:RefreshButton.Enabled = $interactive
    $script:ResetButton.Enabled = $interactive
    $script:CancelButton.Enabled = $script:Busy -and -not $script:ProcessExited
    if ($script:ProcessExited) {
        $script:Status.Text = 'Worker beendet – Chat nicht verfügbar'
    } elseif ($null -ne $script:PendingApproval) {
        $script:Status.Text = 'Benutzerbestätigung erforderlich'
    } elseif ($script:Busy) {
        $script:Status.Text = 'Anfrage läuft ...'
    } else {
        $script:Status.Text = "Bereit – $script:Mode"
    }
}
function Write-WorkerCommand {
    param([string] $Action, [hashtable] $Parameters = @{}, [switch] $Independent)
    if ($script:Closing -or $script:ProcessExited -or $null -eq $script:Process -or $script:Process.HasExited) {
        throw 'C++ Worker is not running.'
    }
    $id = [Guid]::NewGuid().ToString('N')
    $message = @{ id = $id; action = $Action }
    foreach ($key in $Parameters.Keys) { $message[$key] = $Parameters[$key] }
    $json = ConvertTo-Json -InputObject $message -Depth 16 -Compress
    # UTF-8 bytes directly on stdin BaseStream: no dependency on console codepage.
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($json + "`n")
    $stream = $script:Process.StandardInput.BaseStream
    $stream.Write($bytes, 0, $bytes.Length)
    $stream.Flush()
    if (-not $Independent) {
        $script:ActiveId = $id
        $script:Busy = $true
        Update-Buttons
    }
    return $id
}
function Request-Worker {
    param([string] $Action, [hashtable] $Parameters = @{}, [switch] $Independent)
    try {
        [void] (Write-WorkerCommand $Action $Parameters -Independent:$Independent)
    } catch {
        Log-Event ('Senden fehlgeschlagen: ' + $_.Exception.Message)
        Write-Chat 'System' ('Worker-Fehler: ' + $_.Exception.Message)
        if (-not $Independent) { $script:Busy = $false; Update-Buttons }
    }
}
function Show-Snapshot {
    param($Snapshot)
    if ($null -eq $Snapshot) { return }
    $script:Documents.BeginUpdate()
    try {
        $script:Documents.Items.Clear()
        foreach ($doc in $Snapshot.documents) {
            $row = [System.Windows.Forms.ListViewItem]::new([string] $doc.id)
            [void] $row.SubItems.Add([string] $doc.title)
            [void] $row.SubItems.Add([string] $doc.category)
            [void] $script:Documents.Items.Add($row)
        }
    } finally { $script:Documents.EndUpdate() }
    $lines = [System.Collections.Generic.List[string]]::new()
    foreach ($type in $Snapshot.types) {
        $lines.Add(('Typ: {0}' -f $type.type))
        $lines.Add(('  Felder: {0}' -f (($type.fields) -join ', ')))
        $lines.Add(('  Methoden: {0}' -f (($type.methods) -join ', ')))
    }
    $lines.Add('')
    $lines.Add('Explizit freigegebene Tools:')
    foreach ($tool in $Snapshot.tools) {
        $access = if ($tool.access -eq 'write') { 'Bestätigung' } else { 'Lesen' }
        $lines.Add(('  {0} ({1})' -f $tool.name, $access))
    }
    $script:ReflectionText.Text = $lines -join "`r`n"
    if ($Snapshot.failed) { Log-Event 'Chat fehlgeschlagen – bitte Neuer Chat wählen.' }
}
function Ask-Approval {
    param($Event)
    # Only the explicit Windows Forms decision generates approved=true.
    $script:PendingApproval = $Event
    Update-Buttons
    $argumentsJson = ConvertTo-Json -InputObject $Event.arguments -Depth 16
    $message = "Der C++ Worker fordert eine schreibende Operation an:`r`n`r`n"
    $message += "Tool: $($Event.tool_name)`r`n`r`nArgumente:`r`n$argumentsJson`r`n`r`n"
    $message += 'Soll dieser genaue Aufruf ausgeführt werden?'
    $choice = [System.Windows.Forms.MessageBox]::Show(
        $script:Form, $message, 'CxxRelay – Schreibzugriff bestätigen',
        [System.Windows.Forms.MessageBoxButtons]::YesNo,
        [System.Windows.Forms.MessageBoxIcon]::Warning,
        [System.Windows.Forms.MessageBoxDefaultButton]::Button2)
    $approved = $choice -eq [System.Windows.Forms.DialogResult]::Yes
    $approvalId = [string] $Event.approval_id
    $script:PendingApproval = $null
    Log-Event ($(if ($approved) { 'Änderung bestätigt' } else { 'Änderung abgelehnt' }))
    Write-Chat 'System' ($(if ($approved) { 'Schreibzugriff bestätigt.' } else { 'Schreibzugriff abgelehnt.' }))
    if (-not $script:Closing) {
        $script:AssistantStarted = $false
        $script:AssistantText = ''
        Request-Worker 'resume' @{ approval_id = $approvalId; approved = $approved }
    }
}
function Handle-WorkerEvent {
    param($Event)
    if ($null -eq $Event) { return }
    $kind = [string] $Event.type
    $id = [string] $Event.id
    if ($null -ne $Event.PSObject.Properties['snapshot']) { Show-Snapshot $Event.snapshot }
    # A cancel request is independent and emits its own 'complete'.
    if ($id -ne $script:ActiveId) {
        if ($kind -eq 'complete' -and $null -ne $Event.PSObject.Properties['status'] -and $Event.status -eq 'cancel_requested') {
            Log-Event 'Abbruch an den Worker übermittelt.'
        }
        return
    }
    switch ($kind) {
        'model_request' { Log-Event ("Modellanfrage; Tools: $($Event.available_tools)") }
        'tool_requested' {
            $description = '{0} {1}' -f $Event.name, $Event.arguments_json
            Write-Chat 'Tool' ('Angefordert: ' + $description)
            Log-Event ('Tool angefordert: ' + $description)
        }
        'tool' {
            Write-Chat 'Tool' ("$($Event.name): $($Event.code)")
            Log-Event ("Tool: $($Event.name) ($($Event.code))")
        }
        'delta' { Write-Delta ([string] $Event.text) }
        'approval' {
            $script:Busy = $false
            Finish-Assistant ''
            Log-Event ('Freigabe angefordert: ' + $Event.tool_name)
            Ask-Approval $Event
        }
        'complete' {
            $script:Busy = $false
            if ($null -ne $Event.PSObject.Properties['answer']) {
                Finish-Assistant ([string] $Event.answer)
            }
            if ($null -ne $Event.PSObject.Properties['status'] -and $Event.status -eq 'reset') {
                $script:Chat.Clear()
                Write-Chat 'System' 'Neuer Chat gestartet. Die C++-Objekte bleiben erhalten.'
                Log-Event 'Chat zurückgesetzt'
            }
            Update-Buttons
        }
        'cancelled' {
            $script:Busy = $false
            Finish-Assistant ''
            Write-Chat 'System' ([string] $Event.message)
            Log-Event 'Anfrage abgebrochen; Chat zurücksetzen.'
            Update-Buttons
        }
        'error' {
            $script:Busy = $false
            Finish-Assistant ''
            Write-Chat 'System' ([string] $Event.message)
            Log-Event ('Worker-Fehler: ' + [string] $Event.message)
            Update-Buttons
        }
        default { Log-Event ('Unbekanntes Event: ' + $kind) }
    }
}
function Send-Prompt {
    if ($script:Busy -or $null -ne $script:PendingApproval) { return }
    $message = $script:Prompt.Text.Trim()
    if ($message.Length -eq 0) { return }
    $script:Prompt.Clear()
    Write-Chat 'Du' $message
    $script:AssistantStarted = $false
    $script:AssistantText = ''
    Request-Worker 'send' @{ text = $message }
}

$script:SendButton.Add_Click({ Send-Prompt })
$script:Prompt.Add_KeyDown({ param($sender, $e)
    if ($e.KeyCode -eq [System.Windows.Forms.Keys]::Enter -and -not $e.Shift) {
        $e.SuppressKeyPress = $true
        Send-Prompt
    }
})
$script:RefreshButton.Add_Click({ if (-not $script:Busy) { Request-Worker 'snapshot' } })
$script:ResetButton.Add_Click({ if (-not $script:Busy) { Request-Worker 'reset' } })
$script:CancelButton.Add_Click({
    if ($script:Busy) {
        Log-Event 'Abbruch angefordert ...'
        Request-Worker 'cancel' @{} -Independent
    }
})

$script:UiTimer = [System.Windows.Forms.Timer]::new()
$script:UiTimer.Interval = 45
$script:UiTimer.Add_Tick({
    $entry = [CxxRelayOutputLine] $null
    $handled = 0
    while ($handled -lt 150 -and $script:Queue.TryDequeue([ref] $entry)) {
        $handled++
        if ($entry.Stream -eq 'stderr') {
            # Internal diagnostics only; do not surface raw worker text as chat content.
            Log-Event ('[stderr] ' + $entry.Text)
        } else {
            try {
                $event = ConvertFrom-Json -InputObject $entry.Text
                Handle-WorkerEvent $event
            } catch {
                Log-Event ('Ungültiges Worker-Ereignis: ' + $_.Exception.Message)
            }
        }
    }
    if (-not $script:ProcessExited -and $null -ne $script:Process -and $script:Process.HasExited) {
        $script:ProcessExited = $true
        Log-Event ('Worker-Prozess beendet (ExitCode ' + $script:Process.ExitCode + ').')
        Update-Buttons
    }
})

$script:Form.Add_Shown({
    if ($split.Width -gt 700) {
        $split.SplitterDistance = [int] ($split.Width * 0.67)
    }
    Write-Chat 'System' 'Willkommen! Teste den C++ Worker im Mock-Modus oder mit einem echten Modell. Enter sendet, Shift+Enter erzeugt einen Zeilenumbruch.'
    $script:UiTimer.Start()
    Request-Worker 'snapshot'
    $script:Prompt.Focus()
})
$script:Form.Add_FormClosing({
    $script:Closing = $true
    $script:UiTimer.Stop()
    if ($null -ne $script:Process) {
        try {
            # The prototype owns this dedicated worker. Terminate rather than
            # blocking the GUI on a potentially long-running provider request.
            if (-not $script:Process.HasExited) { $script:Process.Kill() }
        } catch { }
        try { $script:Process.Dispose() } catch { }
    }
})

try {
    $executable = Resolve-CxxRelayWorker $WorkerPath
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $executable
    $start.Arguments = '--' + $Mode
    $start.WorkingDirectory = $script:ProjectRoot
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [System.Text.UTF8Encoding]::new($false)
    $start.StandardErrorEncoding = [System.Text.UTF8Encoding]::new($false)
    $script:Process = [System.Diagnostics.Process]::new()
    $script:Process.StartInfo = $start
    [CxxRelayOutputBridge]::Attach($script:Process, $script:Queue)
    if (-not $script:Process.Start()) { throw 'Failed to start C++ worker.' }
    $script:Process.BeginOutputReadLine()
    $script:Process.BeginErrorReadLine()
    Update-Buttons
    [System.Windows.Forms.Application]::Run($script:Form)
} catch {
    $message = $_.Exception.Message
    [void] [System.Windows.Forms.MessageBox]::Show($message, 'CxxRelay – Startup Error',
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Error)
    throw
} finally {
    if ($null -ne $script:Process) {
        try { if (-not $script:Process.HasExited) { $script:Process.Kill() } } catch { }
        $script:Process.Dispose()
    }
    if ($null -ne $script:UiTimer) { $script:UiTimer.Dispose() }
    $script:Form.Dispose()
}
