# wp81debug

Attempt to create an equivalent of  the [Sysinternals debugView](https://learn.microsoft.com/en-us/sysinternals/downloads/debugview) for Windows Phone 8.1  
The OutputDebugString part is largely inspired by the article [Mechanism of OutputDebugString](https://www.codeproject.com/Articles/23776/Mechanism-of-OutputDebugString)  

> [!NOTE]
> WorkInProgress: currently only the messages of the user-mode function [OutputDebugString](https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-outputdebugstringa) are printed.

## Usage

![usage](Capture01.PNG)
Each line has the format H.L P S  
Where H.L are the [high and low DateTime](https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-filetime) at the moment of the reading of message.  
P is the ID of the process.  
And S is the message.

> [!WARNING]
> Some messages can be missing when multiple applications are reading the shared memory used by the OutputDebugString function: a message consumed by a reader won't be available to the other readers.  

## Requirements

### Deployment

- [Install a telnet server on the phone](https://github.com/fredericGette/wp81documentation/tree/main/telnetOverUsb#readme), in order to run the application.  
- Manually copy the executable from the root of this GitHub repository to the shared folder of the phone.
> [!NOTE]
> When you connect your phone with a USB cable, this folder is visible in the Explorer of your computer. And in the phone, this folder is mounted in `C:\Data\USERS\Public\Documents`  

### Compilation

- [Visual Studio 2015 with the Windows Phone SDK](https://github.com/fredericGette/wp81documentation/blob/main/ConsoleApplicationBuilding/README.md).  
