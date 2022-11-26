Write-Host "PS Script to force winmm.dll wrapper loading"
Write-Host "--------------------------------------------`n"
Write-Host "This PowerShell script will patch a file that the user specifies 
to update any references to winmm.dll into winm2.dll thereby
enabling the winmm wrapper to be force loaded. This may be necessary if
the operating system settings or compatibility options do not allow the
standard winmm.dll wrapper file to be loaded. Usually you should input
the game executable e.g. game.exe as a target.`n"

#####
# y/n Prompt:
$confirmation = Read-Host "Continue? [y/n]"
while ($confirmation -ne "y")
{
    if ($confirmation -eq 'n') {exit}
    $confirmation = Read-Host "Continue? [y/n]"
}

#####

$ThisFile = Read-Host -Prompt "Enter file name to be edited"

#####

if (-Not (Test-Path -Path $ThisFile))
{
    Write-Host "ERROR! $ThisFile not found." -BackgroundColor Red -ForegroundColor Black
    $confirmation = Read-Host "Press Enter to exit"
    exit
}

#####

$SEL = Select-String -Path $ThisFile "winmm.dll"

if ($SEL -ne $null)
{
    Write-Host "winmm.dll string found. Renaming... Please wait..."
}
else
{
    Write-Host "ERROR! winmm.dll string not found." -BackgroundColor Red -ForegroundColor Black

    if (Test-Path -Path "$ThisFile.winmm.backup")
    {
        Write-Host "$ThisFile.winmm.backup file found. Is $ThisFile already patched?"
    }

    $confirmation = Read-Host "Press Enter to exit"
    exit
}

#####

# Create a backup:
if (-Not (Test-Path -Path "$ThisFile.winmm.backup"))
{
    Copy-Item -Path "$ThisFile" -Destination "$ThisFile.winmm.backup"
}

#####
# source:
# https://stackoverflow.com/questions/73790902/replace-string-in-a-binary-clipboard-dump-from-onenote

# To compensate for a difference between Windows PowerShell and PowerShell (Core) 7+
# with respect to how byte processing is requested: -Encoding Byte vs. -AsByteStream
$byteEncParam = 
  if ($IsCoreCLR) { @{ AsByteStream = $true } } 
  else            { @{ Encoding = 'Byte' } }

# Read the file *as a byte array*.
$data = Get-Content @byteEncParam -ReadCount 0  $ThisFile

# Convert the array to a "hex string" in the form "nn-nn-nn-...",
# where nn represents a two-digit hex representation of each byte,
# e.g. '41-42' for 0x41, 0x42, which, if interpreted as a
# single-byte encoding (ASCII), is 'AB'.
$dataAsHexString = [BitConverter]::ToString($data)

# Define the search and replace strings, and convert them into
# "hex strings" too, using their UTF-8 byte representation.
$search = 'WINMM.dll'
$replacement = 'winm2.dll'
$searchAsHexString = [BitConverter]::ToString([Text.Encoding]::UTF8.GetBytes($search))
$replaceAsHexString = [BitConverter]::ToString([Text.Encoding]::UTF8.GetBytes($replacement))

# Perform the replacement.
$dataAsHexString = $dataAsHexString.Replace($searchAsHexString, $replaceAsHexString)

# Convert he modified "hex string" back to a byte[] array.
$modifiedData = [byte[]] ($dataAsHexString -split '-' -replace '^', '0x')

# Save the byte array back to the file.
Set-Content @byteEncParam $ThisFile -Value $modifiedData

#####

$data = Get-Content @byteEncParam -ReadCount 0  $ThisFile
$dataAsHexString = [BitConverter]::ToString($data)
$search = 'WINMM.DLL'
$replacement = 'winm2.dll'
$searchAsHexString = [BitConverter]::ToString([Text.Encoding]::UTF8.GetBytes($search))
$replaceAsHexString = [BitConverter]::ToString([Text.Encoding]::UTF8.GetBytes($replacement))
$dataAsHexString = $dataAsHexString.Replace($searchAsHexString, $replaceAsHexString)
$modifiedData = [byte[]] ($dataAsHexString -split '-' -replace '^', '0x')
Set-Content @byteEncParam $ThisFile -Value $modifiedData

#####

$data = Get-Content @byteEncParam -ReadCount 0  $ThisFile
$dataAsHexString = [BitConverter]::ToString($data)
$search = 'winmm.dll'
$replacement = 'winm2.dll'
$searchAsHexString = [BitConverter]::ToString([Text.Encoding]::UTF8.GetBytes($search))
$replaceAsHexString = [BitConverter]::ToString([Text.Encoding]::UTF8.GetBytes($replacement))
$dataAsHexString = $dataAsHexString.Replace($searchAsHexString, $replaceAsHexString)
$modifiedData = [byte[]] ($dataAsHexString -split '-' -replace '^', '0x')
Set-Content @byteEncParam $ThisFile -Value $modifiedData

#####

if (-Not (Test-Path -Path "winm2.dll"))
{
    Rename-Item -Path "winmm.dll" -NewName "winm2.dll"
}

#####

Start-Sleep -s 2 # Sleep a couple of seconds.
Write-Host "`nAll done!" -BackgroundColor Green -ForegroundColor Black
[console]::beep(500,300) # Plays a beep sound.
$confirmation = Read-Host "Press Enter to exit"
