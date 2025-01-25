sc create wp81dbgPrint type= kernel binPath= C:\Data\USERS\Public\Documents\wp81dbgPrint.sys

- only to start a legacy driver:
sc start wp81debuglogger
sc start wp81dbgPrint

sc stop wp81dbgPrint
sc stop wp81debuglogger
sc delete wp81dbgPrint

