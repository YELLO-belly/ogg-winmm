On some systems the running of unsigned PowerShell scripts may be disabled. In this case you can try running the script with the command:
"powershell -noprofile -executionpolicy bypass -file force-winmm-loading.ps1"

Disclaimer:
This PowerShell script edits binary files. Use it with caution and common sense!

Starting with Windows 10 it can often happen that the winmm.dll wrapper in the game folder is ignored by the OS and the real system file is used. This behaviour can be triggered by MS security settings or some ACT compatibility settings like the Win9x layers or the SingleProcAffinity Shim/Fix. One way to work around this issue is to HEX edit the game executable to change the reference to winmm.dll to something else like winm2.dll. This PowerShell script is designed to automate this process as much as possible.

Note that some digital distributions of old games on Steam or GOG already use a similar method of renaming the wrapper files to make sure they are loaded by the game. Although their naming conventions might differ.

Usage:
- Drop the PS script into the game folder where the executable that you wish to edit resides.
- Run the script by right clicking on it and selecting "Run with PowerShell". (note that Windows will likely ask you about the execution policy on first run. This is a safety measure to protect the user from harmful scripts. If you trust the script select "Y" otherwise you can not run it.)
- When asked type in the game executable (e.g. game.exe) that you wish to be edited.
- If the winmm.dll string is found inside the executable it will then be edited to winm2.dll and a backup of the original executable is also created.
- If a winmm.dll wrapper is located in the game folder it will also be renamed to winm2.dll.

Now you should be ready to run the game normally and the renamed winm2.dll wrapper should be used.

Some important notes:
- This is just a script which will look for the presence of "winmm.dll" string inside binary files. Do not go crazy with it. Use it only inside a game folder and only give it an executable name of the game. That said sometimes (but rarely) the game code may run from a non executable file like a DLL but unless you are absolutely certain of what you are doing do not randomly edit files as this may lead to a big mess.
- Although the script creates a backup file it is not fool proof. Do not try to edit files randomly! If you feel like you do not know what you are doing then don't.
