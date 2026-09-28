# say.ps1 -- speak a dialogue file into one WAV per line, with a voice per character.
#
#   powershell -ExecutionPolicy Bypass -File say.ps1 -Script club_night.txt -Out wav
#
# Windows SAPI is already on this machine, so the club needs no TTS dependency and no
# network: reva speaks in Zira (en-US, precise), kaeli in Hazel (en-GB, warmer), which
# is the cheapest way to make two synthetic voices read as two different people.
# Rate carries the rest of the characterisation.
param(
    [string]$Script = "club_night.txt",
    [string]$Out = "wav"
)
Add-Type -AssemblyName System.Speech
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$src = Join-Path $here $Script
$dir = Join-Path $here $Out
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }

$voices = @{ "0" = "Microsoft Zira Desktop"; "1" = "Microsoft Hazel Desktop" }
$rates  = @{ "0" = 2; "1" = 1 }

$i = 0
foreach ($raw in Get-Content -LiteralPath $src) {
    $line = $raw.Trim()
    if ($line -eq "" -or $line.StartsWith("#")) { continue }
    $parts = $line.Split("|", 2)
    if ($parts.Count -lt 2) { continue }
    $spk = $parts[0].Trim()
    $text = $parts[1].Trim()
    $wav = Join-Path $dir ("line_{0:d2}.wav" -f $i)
    $synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
    $synth.SelectVoice($voices[$spk])
    $synth.Rate = $rates[$spk]
    $synth.SetOutputToWaveFile($wav)
    $synth.Speak($text)
    $synth.Dispose()
    $len = (Get-Item $wav).Length
    "{0}`t{1}`t{2}`t{3}" -f $i, $spk, $len, $text
    $i++
}
"lines: $i"
